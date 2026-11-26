#include "NXNSO.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define NX_NSO_HEADER_SIZE 0x100u
#define NX_NSO_SIGNATURE UINT32_C(0x304F534E)
#define NX_NSO_MAX_IMAGE_SIZE (256u * 1024u * 1024u)

typedef struct {
    uint32_t fileOffset;
    uint32_t memoryOffset;
    uint32_t memorySize;
    uint32_t fileSize;
    bool compressed;
} NXNSOSegment;

static uint32_t readU32LE(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static bool addOverflowU64(uint64_t a, uint64_t b, uint64_t *result) {
    if (a > UINT64_MAX - b) return true;
    *result = a + b;
    return false;
}

/* Raw LZ4 block decoder used by NSO segments; no frame wrapper is expected. */
static bool decodeLZ4Block(const uint8_t *source, size_t sourceSize,
                           uint8_t *destination, size_t destinationSize) {
    if ((!source && sourceSize) || (!destination && destinationSize)) return false;
    size_t input = 0;
    size_t output = 0;

    while (input < sourceSize) {
        uint8_t token = source[input++];
        size_t literalLength = (size_t)(token >> 4);
        if (literalLength == 15) {
            uint8_t extension;
            do {
                if (input >= sourceSize) return false;
                extension = source[input++];
                if (literalLength > SIZE_MAX - extension) return false;
                literalLength += extension;
            } while (extension == 255);
        }

        if (literalLength > sourceSize - input ||
            literalLength > destinationSize - output) return false;
        memcpy(destination + output, source + input, literalLength);
        input += literalLength;
        output += literalLength;

        if (input == sourceSize) break;
        if (sourceSize - input < 2) return false;

        size_t distance = (size_t)source[input] |
                          ((size_t)source[input + 1] << 8);
        input += 2;
        if (distance == 0 || distance > output) return false;

        size_t matchLength = (size_t)(token & 0x0F) + 4;
        if ((token & 0x0F) == 15) {
            uint8_t extension;
            do {
                if (input >= sourceSize) return false;
                extension = source[input++];
                if (matchLength > SIZE_MAX - extension) return false;
                matchLength += extension;
            } while (extension == 255);
        }

        if (matchLength > destinationSize - output) return false;
        for (size_t i = 0; i < matchLength; ++i) {
            destination[output + i] = destination[output + i - distance];
        }
        output += matchLength;
    }

    return input == sourceSize && output == destinationSize;
}

static uint16_t readU16LE(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint64_t readU64LE(const uint8_t *bytes) {
    return (uint64_t)readU32LE(bytes) |
           ((uint64_t)readU32LE(bytes + 4) << 32);
}

static bool addSignedOffset(uint64_t base, int64_t offset,
                            uint64_t *result) {
    if (!result) return false;
    if (offset >= 0)
        return !addOverflowU64(base, (uint64_t)offset, result);
    const uint64_t magnitude = (uint64_t)(-(offset + 1)) + 1u;
    if (base < magnitude) return false;
    *result = base - magnitude;
    return true;
}

static bool addressInImage(const NXLoadedNSO *image,
                           uint64_t address, size_t size) {
    if (!image || address < image->loadAddress ||
        address > UINT64_MAX - size)
        return false;
    const uint64_t offset = address - image->loadAddress;
    return offset <= image->imageSize &&
           (uint64_t)size <= image->imageSize - offset;
}

static bool readGuestU64(NXEngine *engine, uint64_t address,
                         uint64_t *value) {
    if (!engine || !value) return false;
    uint8_t bytes[sizeof(uint64_t)];
    if (!NXEngineReadBytes(engine, address, bytes, sizeof(bytes)))
        return false;
    *value = readU64LE(bytes);
    return true;
}

static bool writeGuestU64(NXEngine *engine, uint64_t address,
                          uint64_t value) {
    if (!engine) return false;
    uint8_t bytes[sizeof(uint64_t)];
    for (size_t i = 0; i < sizeof(bytes); ++i)
        bytes[i] = (uint8_t)(value >> (i * 8));
    return NXEngineWriteBytes(engine, address, bytes, sizeof(bytes));
}

static bool readSymbolName(NXEngine *engine,
                            const NXLoadedNSO *module,
                            const uint8_t symbol[24],
                            char *name, size_t capacity) {
    if (!engine || !module || !symbol || !name || capacity < 2 ||
        module->stringTableAddress == 0)
        return false;
    const uint32_t nameOffset = readU32LE(symbol);
    if (nameOffset >= module->stringTableSize) return false;

    const uint64_t remaining = module->stringTableSize - nameOffset;
    const size_t amount = remaining < capacity ? (size_t)remaining : capacity;
    uint64_t address = 0;
    if (addOverflowU64(module->stringTableAddress, nameOffset, &address) ||
        !NXEngineReadBytes(engine, address, name, amount))
        return false;

    for (size_t i = 0; i < amount; ++i) {
        if (name[i] == '\0') return true;
    }
    return false;
}

static bool resolveDynamicSymbol(NXEngine *engine, uint64_t loadBase,
                                 const NXLoadedNSO *current,
                                 const NXNSORegistry *registry,
                                 uint64_t symbolTableAddress,
                                 uint64_t symbolCount,
                                 uint32_t symbolIndex,
                                 uint64_t *symbolAddress) {
    if (!symbolAddress || !engine || !current) return false;
    if (symbolIndex == 0) {
        *symbolAddress = 0;
        return true;
    }
    if (symbolTableAddress == 0 || symbolIndex >= symbolCount)
        return false;

    const uint64_t entryOffset = (uint64_t)symbolIndex * 24u;
    uint64_t entryAddress = 0;
    if (addOverflowU64(symbolTableAddress, entryOffset, &entryAddress))
        return false;

    uint8_t symbol[24];
    if (!NXEngineReadBytes(engine, entryAddress, symbol, sizeof(symbol)))
        return false;
    const uint16_t sectionIndex = readU16LE(symbol + 6);
    const uint64_t value = readU64LE(symbol + 8);
    if (sectionIndex != 0) {
        if (sectionIndex == UINT16_C(0xFFF1)) {
            *symbolAddress = value;
            return true;
        }
        return !addOverflowU64(loadBase, value, symbolAddress);
    }

    /* Undefined weak symbols resolve to zero when no provider is loaded. */
    const uint8_t binding = symbol[4] >> 4;
    if (binding == 2u && !registry)
        return false; /* Defer weak resolution until all modules are loaded. */

    char requestedName[512];
    if (!readSymbolName(engine, current, symbol,
                        requestedName, sizeof(requestedName))) {
        if (binding == 2u && registry) {
            *symbolAddress = 0;
            return true;
        }
        return false;
    }

    uint64_t weakAddress = 0;
    bool foundWeak = false;
    if (registry) {
        const size_t moduleCount = registry->count < NX_NSO_MAX_MODULES
            ? registry->count : NX_NSO_MAX_MODULES;
        for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex) {
            const NXLoadedNSO *module = &registry->modules[moduleIndex];
            if (module->loadBase == loadBase ||
                module->symbolTableAddress == 0 ||
                module->stringTableAddress == 0)
                continue;

            for (uint64_t index = 1; index < module->symbolCount; ++index) {
                uint64_t providerEntryAddress = 0;
                if (addOverflowU64(module->symbolTableAddress,
                                   index * 24u, &providerEntryAddress))
                    break;
                uint8_t providerSymbol[24];
                if (!NXEngineReadBytes(engine, providerEntryAddress,
                                       providerSymbol, sizeof(providerSymbol)))
                    break;

                const uint16_t providerSection = readU16LE(providerSymbol + 6);
                const uint8_t providerBinding = providerSymbol[4] >> 4;
                if (providerSection == 0 ||
                    (providerBinding != 1u && providerBinding != 2u))
                    continue;

                char providerName[512];
                if (!readSymbolName(engine, module, providerSymbol,
                                    providerName, sizeof(providerName)) ||
                    strcmp(requestedName, providerName) != 0)
                    continue;

                const uint64_t providerValue = readU64LE(providerSymbol + 8);
                uint64_t resolved = providerValue;
                if (providerSection != UINT16_C(0xFFF1) &&
                    addOverflowU64(module->loadBase, providerValue, &resolved))
                    continue;

                if (providerBinding == 1u) {
                    *symbolAddress = resolved;
                    return true;
                }
                if (!foundWeak) {
                    weakAddress = resolved;
                    foundWeak = true;
                }
            }
        }
    }

    if (foundWeak) {
        *symbolAddress = weakAddress;
        return true;
    }
    if (binding == 2u) {
        *symbolAddress = 0;
        return true;
    }
    return false;
}

static bool applyAArch64AbsoluteRelocation(
    NXEngine *engine, uint64_t loadBase, const NXLoadedNSO *image,
    uint64_t targetOffset, uint32_t type, uint32_t symbolIndex,
    int64_t addend, uint64_t symbolTableAddress, uint64_t symbolCount,
    const NXNSORegistry *registry, NXLoadedNSO *stats) {
    uint64_t targetAddress = 0, symbolAddress = 0, value = 0;
    if (addOverflowU64(loadBase, targetOffset, &targetAddress))
        return false;

    if (type == 257u) { /* R_AARCH64_ABS64 */
        if (!addressInImage(image, targetAddress, 8)) return false;
        if (!resolveDynamicSymbol(engine, loadBase, image, registry,
                                  symbolTableAddress, symbolCount,
                                  symbolIndex, &symbolAddress)) {
            ++stats->unsupportedRelocationCount;
            return true; /* Undefined imports still require rtld. */
        }
        if (!addSignedOffset(symbolAddress, addend, &value) ||
            !writeGuestU64(engine, targetAddress, value))
            return false;
        ++stats->symbolRelocationCount;
        return true;
    }

    if (type == 258u) { /* R_AARCH64_ABS32 */
        uint8_t encoded[4];
        if (!addressInImage(image, targetAddress, 4)) return false;
        if (!resolveDynamicSymbol(engine, loadBase, image, registry,
                                  symbolTableAddress, symbolCount,
                                  symbolIndex, &symbolAddress)) {
            ++stats->unsupportedRelocationCount;
            return true;
        }
        if (!addSignedOffset(symbolAddress, addend, &value) ||
            value > UINT32_MAX)
            return false;
        const uint32_t value32 = (uint32_t)value;
        encoded[0] = (uint8_t)value32;
        encoded[1] = (uint8_t)(value32 >> 8);
        encoded[2] = (uint8_t)(value32 >> 16);
        encoded[3] = (uint8_t)(value32 >> 24);
        if (!NXEngineWriteBytes(engine, targetAddress, encoded, sizeof(encoded)))
            return false;
        ++stats->symbolRelocationCount;
        return true;
    }
    return false;
}

static bool applyRelaTable(NXEngine *engine, uint64_t loadBase,
                           const NXLoadedNSO *image,
                           uint64_t tableAddress, uint64_t tableSize,
                           uint64_t symbolTableAddress,
                           uint64_t symbolCount,
                           const NXNSORegistry *registry,
                           NXLoadedNSO *stats) {
    if (tableSize == 0) return true;
    if (tableSize % 24u != 0 || tableSize > SIZE_MAX ||
        !addressInImage(image, tableAddress, (size_t)tableSize))
        return false;

    const uint64_t count = tableSize / 24u;
    for (uint64_t i = 0; i < count; ++i) {
        const uint64_t entryAddress = tableAddress + i * 24u;
        uint8_t entry[24];
        if (!NXEngineReadBytes(engine, entryAddress, entry, sizeof(entry)))
            return false;

        const uint64_t targetOffset = readU64LE(entry);
        const uint64_t info = readU64LE(entry + 8);
        const int64_t addend = (int64_t)readU64LE(entry + 16);
        const uint32_t type = (uint32_t)info;
        const uint32_t symbolIndex = (uint32_t)(info >> 32);

        if (type == 0) continue; /* R_AARCH64_NONE */
        if (type == 1027u) { /* R_AARCH64_RELATIVE */
            if (symbolIndex != 0) {
                ++stats->unsupportedRelocationCount;
                continue;
            }
            uint64_t targetAddress = 0, relocatedValue = 0;
            if (addOverflowU64(loadBase, targetOffset, &targetAddress) ||
                !addressInImage(image, targetAddress, sizeof(uint64_t)) ||
                !addSignedOffset(loadBase, addend, &relocatedValue) ||
                !writeGuestU64(engine, targetAddress, relocatedValue))
                return false;
            ++stats->relativeRelocationCount;
        } else if (type == 257u || type == 258u) {
            if (!applyAArch64AbsoluteRelocation(
                    engine, loadBase, image, targetOffset, type, symbolIndex,
                    addend, symbolTableAddress, symbolCount, registry, stats))
                return false;
        } else {
            ++stats->unsupportedRelocationCount;
        }
    }
    return true;
}

static bool applyRelTable(NXEngine *engine, uint64_t loadBase,
                          const NXLoadedNSO *image,
                          uint64_t tableAddress, uint64_t tableSize,
                          uint64_t symbolTableAddress,
                          uint64_t symbolCount,
                          const NXNSORegistry *registry,
                          NXLoadedNSO *stats) {
    if (tableSize == 0) return true;
    if (tableSize % 16u != 0 || tableSize > SIZE_MAX ||
        !addressInImage(image, tableAddress, (size_t)tableSize))
        return false;

    const uint64_t count = tableSize / 16u;
    for (uint64_t i = 0; i < count; ++i) {
        const uint64_t entryAddress = tableAddress + i * 16u;
        uint8_t entry[16];
        if (!NXEngineReadBytes(engine, entryAddress, entry, sizeof(entry)))
            return false;

        const uint64_t targetOffset = readU64LE(entry);
        const uint64_t info = readU64LE(entry + 8);
        const uint32_t type = (uint32_t)info;
        const uint32_t symbolIndex = (uint32_t)(info >> 32);
        if (type == 0) continue;
        uint64_t targetAddress = 0;
        if (addOverflowU64(loadBase, targetOffset, &targetAddress))
            return false;

        if (type == 1027u) {
            uint64_t implicitAddend = 0, relocatedValue = 0;
            if (symbolIndex != 0) {
                ++stats->unsupportedRelocationCount;
                continue;
            }
            if (!addressInImage(image, targetAddress, 8) ||
                !readGuestU64(engine, targetAddress, &implicitAddend) ||
                addOverflowU64(loadBase, implicitAddend, &relocatedValue) ||
                !writeGuestU64(engine, targetAddress, relocatedValue))
                return false;
            ++stats->relativeRelocationCount;
        } else if (type == 257u) {
            uint64_t implicitAddend = 0;
            if (!addressInImage(image, targetAddress, 8) ||
                !readGuestU64(engine, targetAddress, &implicitAddend))
                return false;
            if (!applyAArch64AbsoluteRelocation(
                    engine, loadBase, image, targetOffset, type, symbolIndex,
                    (int64_t)implicitAddend, symbolTableAddress,
                    symbolCount, registry, stats))
                return false;
        } else if (type == 258u) {
            uint8_t encoded[4];
            if (!addressInImage(image, targetAddress, 4) ||
                !NXEngineReadBytes(engine, targetAddress, encoded, sizeof(encoded)))
                return false;
            const uint32_t implicitAddend =
                (uint32_t)encoded[0] | ((uint32_t)encoded[1] << 8) |
                ((uint32_t)encoded[2] << 16) | ((uint32_t)encoded[3] << 24);
            if (!applyAArch64AbsoluteRelocation(
                    engine, loadBase, image, targetOffset, type, symbolIndex,
                    (int64_t)implicitAddend, symbolTableAddress,
                    symbolCount, registry, stats))
                return false;
        } else {
            ++stats->unsupportedRelocationCount;
        }
    }
    return true;
}

static bool applyRelrTarget(NXEngine *engine, uint64_t loadBase,
                               const NXLoadedNSO *image,
                               uint64_t targetOffset,
                               NXLoadedNSO *stats) {
    uint64_t targetAddress = 0;
    uint64_t implicitAddend = 0;
    uint64_t relocatedValue = 0;
    if (addOverflowU64(loadBase, targetOffset, &targetAddress) ||
        !addressInImage(image, targetAddress, sizeof(uint64_t)) ||
        !readGuestU64(engine, targetAddress, &implicitAddend) ||
        addOverflowU64(loadBase, implicitAddend, &relocatedValue) ||
        !writeGuestU64(engine, targetAddress, relocatedValue))
        return false;
    ++stats->relativeRelocationCount;
    return true;
}

static bool applyRelrTable(NXEngine *engine, uint64_t loadBase,
                           const NXLoadedNSO *image,
                           uint64_t tableAddress, uint64_t tableSize,
                           NXLoadedNSO *stats) {
    if (tableSize == 0) return true;
    if (tableSize % 8u != 0 || tableSize > SIZE_MAX ||
        !addressInImage(image, tableAddress, (size_t)tableSize))
        return false;

    uint64_t where = 0;
    const uint64_t count = tableSize / 8u;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t entryAddress = 0;
        if (addOverflowU64(tableAddress, i * 8u, &entryAddress))
            return false;
        uint64_t entry = 0;
        if (!readGuestU64(engine, entryAddress, &entry)) return false;

        if ((entry & 1u) == 0) {
            where = entry;
            if (!applyRelrTarget(engine, loadBase, image, where, stats))
                return false;
            if (addOverflowU64(where, 8u, &where)) return false;
        } else {
            const uint64_t bitmap = entry >> 1;
            for (uint32_t bit = 0; bit < 63u; ++bit) {
                if ((bitmap & (UINT64_C(1) << bit)) == 0) continue;
                uint64_t targetOffset = 0;
                if (addOverflowU64(where, (uint64_t)bit * 8u, &targetOffset) ||
                    !applyRelrTarget(engine, loadBase, image,
                                     targetOffset, stats))
                    return false;
            }
            if (addOverflowU64(where, 63u * 8u, &where)) return false;
        }
    }
    return true;
}

/*
 * The MOD0 pointer is stored at +4 in the loaded text image. This pass applies
 * only symbol-free R_AARCH64_RELATIVE relocations; rtld-style symbol lookup
 * (ABS64/GLOB_DAT/JUMP_SLOT) remains a separate, unimplemented stage.
 */
static bool applyMOD0RelativeRelocations(NXEngine *engine,
                                         uint64_t loadBase,
                                         uint64_t textAddress,
                                         uint32_t textSize,
                                         const NXNSORegistry *registry,
                                         NXLoadedNSO *image) {
    if (!engine || !image || textSize < 8u) return true;

    uint64_t pointerAddress = 0;
    if (addOverflowU64(textAddress, 4u, &pointerAddress)) return false;
    uint8_t pointerBytes[4];
    if (!NXEngineReadBytes(engine, pointerAddress,
                           pointerBytes, sizeof(pointerBytes)))
        return false;

    const uint32_t mod0Offset = readU32LE(pointerBytes);
    if (mod0Offset == 0 || mod0Offset > textSize ||
        textSize - mod0Offset < 0x1Cu)
        return true;

    uint64_t mod0Address = 0;
    if (addOverflowU64(textAddress, mod0Offset, &mod0Address))
        return false;
    uint8_t mod0[0x1C];
    if (!NXEngineReadBytes(engine, mod0Address, mod0, sizeof(mod0)) ||
        readU32LE(mod0) != UINT32_C(0x30444F4D))
        return true;

    image->hasModuleHeader = true;
    image->moduleHeaderAddress = mod0Address;
    const int32_t dynamicOffset = (int32_t)readU32LE(mod0 + 4);
    uint64_t dynamicAddress = 0;
    if (!addSignedOffset(mod0Address, dynamicOffset, &dynamicAddress) ||
        !addressInImage(image, dynamicAddress, 16))
        return false;
    image->dynamicAddress = dynamicAddress;

    uint64_t relaOffset = 0, relaSize = 0, relaEntrySize = 0;
    uint64_t relOffset = 0, relSize = 0, relEntrySize = 0;
    uint64_t relrOffset = 0, relrSize = 0, relrEntrySize = 0;
    uint64_t hashOffset = 0, symtabOffset = 0, symEntrySize = 0;
    uint64_t strtabOffset = 0, strtabSize = 0;
    bool hasHash = false, hasSymtab = false, hasSymEntrySize = false;
    bool hasStrtab = false, hasStrtabSize = false;
    const uint64_t maxEntries = image->imageSize / 16u;
    bool terminated = false;

    for (uint64_t i = 0; i < maxEntries; ++i) {
        uint64_t entryAddress = 0;
        if (addOverflowU64(dynamicAddress, i * 16u, &entryAddress) ||
            !addressInImage(image, entryAddress, 16))
            return false;
        uint8_t entry[16];
        if (!NXEngineReadBytes(engine, entryAddress, entry, sizeof(entry)))
            return false;

        const int64_t tag = (int64_t)readU64LE(entry);
        const uint64_t value = readU64LE(entry + 8);
        if (tag == 0) {
            terminated = true;
            break;
        }
        switch (tag) {
            case 7:  relaOffset = value; break; /* DT_RELA */
            case 8:  relaSize = value; break;   /* DT_RELASZ */
            case 9:  relaEntrySize = value; break; /* DT_RELAENT */
            case 17: relOffset = value; break;  /* DT_REL */
            case 18: relSize = value; break;    /* DT_RELSZ */
            case 19: relEntrySize = value; break; /* DT_RELENT */
            case 4:  hashOffset = value; hasHash = true; break; /* DT_HASH */
            case 6:  symtabOffset = value; hasSymtab = true; break; /* DT_SYMTAB */
            case 11: symEntrySize = value; hasSymEntrySize = true; break; /* DT_SYMENT */
            case 5:  strtabOffset = value; hasStrtab = true; break; /* DT_STRTAB */
            case 10: strtabSize = value; hasStrtabSize = true; break; /* DT_STRSZ */
            case 35: relrSize = value; break;     /* DT_RELRSZ */
            case 36: relrOffset = value; break;   /* DT_RELR */
            case 37: relrEntrySize = value; break; /* DT_RELRENT */
            default: break;
        }
    }
    if (!terminated) return false;

    uint64_t symbolTableAddress = 0;
    uint64_t symbolCount = 0;
    if (hasHash && hasSymtab && hasSymEntrySize) {
        uint64_t hashAddress = 0;
        uint8_t hashHeader[8];
        if (symEntrySize != 24u ||
            addOverflowU64(loadBase, hashOffset, &hashAddress) ||
            !addressInImage(image, hashAddress, sizeof(hashHeader)) ||
            !NXEngineReadBytes(engine, hashAddress,
                               hashHeader, sizeof(hashHeader)) ||
            addOverflowU64(loadBase, symtabOffset, &symbolTableAddress))
            return false;
        symbolCount = readU32LE(hashHeader + 4);
        if (symbolCount > SIZE_MAX / 24u ||
            !addressInImage(image, symbolTableAddress,
                            (size_t)(symbolCount * 24u)))
            return false;
    }

    uint64_t stringTableAddress = 0;
    if (hasStrtab && hasStrtabSize) {
        if (strtabSize > SIZE_MAX ||
            addOverflowU64(loadBase, strtabOffset, &stringTableAddress) ||
            !addressInImage(image, stringTableAddress, (size_t)strtabSize))
            return false;
        image->stringTableAddress = stringTableAddress;
        image->stringTableSize = strtabSize;
    }
    image->symbolTableAddress = symbolTableAddress;
    image->symbolCount = symbolCount;

    image->relaTableAddress = 0;
    image->relaTableSize = relaSize;
    image->relTableAddress = 0;
    image->relTableSize = relSize;
    if (relaSize != 0 &&
        addOverflowU64(loadBase, relaOffset, &image->relaTableAddress))
        return false;
    if (relSize != 0 &&
        addOverflowU64(loadBase, relOffset, &image->relTableAddress))
        return false;

    if (relaSize != 0) {
        uint64_t tableAddress = 0;
        if (relaEntrySize != 24u ||
            addOverflowU64(loadBase, relaOffset, &tableAddress) ||
            !applyRelaTable(engine, loadBase, image, tableAddress,
                            relaSize, symbolTableAddress, symbolCount,
                            registry, image))
            return false;
    }
    if (relSize != 0) {
        uint64_t tableAddress = 0;
        if (relEntrySize != 16u ||
            addOverflowU64(loadBase, relOffset, &tableAddress) ||
            !applyRelTable(engine, loadBase, image, tableAddress,
                           relSize, symbolTableAddress, symbolCount,
                           registry, image))
            return false;
    }
    if (relrSize != 0) {
        uint64_t tableAddress = 0;
        if (relrEntrySize != 8u ||
            addOverflowU64(loadBase, relrOffset, &tableAddress) ||
            !applyRelrTable(engine, loadBase, image,
                            tableAddress, relrSize, image))
            return false;
    }
    return true;
}

static bool loadSegment(NXEngine *engine, const uint8_t *fileBytes,
                        size_t fileSize, const NXNSOSegment *segment,
                        uint64_t loadBase) {
    if (segment->memorySize == 0) return true;
    if (segment->fileSize == 0 ||
        segment->fileOffset > fileSize ||
        segment->fileSize > fileSize - segment->fileOffset) return false;

    uint64_t destinationAddress = 0;
    if (addOverflowU64(loadBase, segment->memoryOffset, &destinationAddress))
        return false;

    uint8_t *output = (uint8_t *)calloc(1, segment->memorySize);
    if (!output) return false;

    const uint8_t *input = fileBytes + segment->fileOffset;
    bool ok;
    if (segment->compressed) {
        ok = decodeLZ4Block(input, segment->fileSize,
                            output, segment->memorySize);
    } else {
        ok = segment->fileSize == segment->memorySize;
        if (ok) memcpy(output, input, segment->memorySize);
    }

    if (ok) {
        ok = NXEngineLoadBytes(engine, destinationAddress,
                               output, segment->memorySize);
    }
    free(output);
    return ok;
}

bool NXEngineLoadNSOWithRegistry(NXEngine *engine, const uint8_t *fileBytes,
                                 size_t fileSize, uint64_t loadBase,
                                 NXNSORegistry *registry,
                                 NXLoadedNSO *loadedImage) {
    if (!engine || !engine->initialized || !fileBytes ||
        fileSize < NX_NSO_HEADER_SIZE || !loadedImage) return false;
    if (readU32LE(fileBytes) != NX_NSO_SIGNATURE) return false;

    const uint32_t flags = readU32LE(fileBytes + 0x0C);
    NXNSOSegment segments[3] = {
        {
            .fileOffset = readU32LE(fileBytes + 0x10),
            .memoryOffset = readU32LE(fileBytes + 0x14),
            .memorySize = readU32LE(fileBytes + 0x18),
            .fileSize = (flags & 1u) ? readU32LE(fileBytes + 0x60) : readU32LE(fileBytes + 0x18),
            .compressed = (flags & 1u) != 0
        },
        {
            .fileOffset = readU32LE(fileBytes + 0x20),
            .memoryOffset = readU32LE(fileBytes + 0x24),
            .memorySize = readU32LE(fileBytes + 0x28),
            .fileSize = (flags & 2u) ? readU32LE(fileBytes + 0x64) : readU32LE(fileBytes + 0x28),
            .compressed = (flags & 2u) != 0
        },
        {
            .fileOffset = readU32LE(fileBytes + 0x30),
            .memoryOffset = readU32LE(fileBytes + 0x34),
            .memorySize = readU32LE(fileBytes + 0x38),
            .fileSize = (flags & 4u) ? readU32LE(fileBytes + 0x68) : readU32LE(fileBytes + 0x38),
            .compressed = (flags & 4u) != 0
        }
    };
    const uint32_t bssSize = readU32LE(fileBytes + 0x3C);

    if (segments[0].memorySize == 0) return false;
    uint64_t minimumOffset = UINT64_MAX;
    uint64_t maximumOffset = 0;

    for (size_t i = 0; i < 3; ++i) {
        const NXNSOSegment *segment = &segments[i];
        if (!segment->memorySize) continue;

        uint64_t end = 0;
        if (addOverflowU64(segment->memoryOffset, segment->memorySize, &end))
            return false;
        if (segment->fileOffset > fileSize ||
            segment->fileSize > fileSize - segment->fileOffset) return false;
        if (segment->memoryOffset < minimumOffset) minimumOffset = segment->memoryOffset;
        if (end > maximumOffset) maximumOffset = end;
    }

    uint64_t bssEnd = 0;
    if (addOverflowU64(maximumOffset, bssSize, &bssEnd)) return false;
    if (bssEnd > NX_NSO_MAX_IMAGE_SIZE || minimumOffset >= bssEnd) return false;

    uint64_t mappedSize64 = bssEnd - minimumOffset;
    if (mappedSize64 > NX_NSO_MAX_IMAGE_SIZE || mappedSize64 > SIZE_MAX)
        return false;

    uint64_t mappedAddress = 0;
    if (addOverflowU64(loadBase, minimumOffset, &mappedAddress)) return false;

    if (!NXEngineMapMemory(engine, mappedAddress, (size_t)mappedSize64,
                           NX_MEMORY_READ | NX_MEMORY_WRITE | NX_MEMORY_EXECUTE)) {
        return false;
    }

    for (size_t i = 0; i < 3; ++i) {
        if (!loadSegment(engine, fileBytes, fileSize, &segments[i], loadBase))
            return false;
    }

    uint64_t entryPoint = 0;
    if (addOverflowU64(loadBase, segments[0].memoryOffset, &entryPoint))
        return false;

    memset(loadedImage, 0, sizeof(*loadedImage));
    loadedImage->loadAddress = mappedAddress;
    loadedImage->loadBase = loadBase;
    loadedImage->entryPoint = entryPoint;
    loadedImage->imageSize = mappedSize64;
    loadedImage->textSize = segments[0].memorySize;
    loadedImage->rodataSize = segments[1].memorySize;
    loadedImage->dataSize = segments[2].memorySize;
    loadedImage->bssSize = bssSize;

    if (!applyMOD0RelativeRelocations(
            engine, loadBase, entryPoint, segments[0].memorySize,
            registry, loadedImage))
        return false;

    if (registry && !NXNSORegistryRegister(registry, loadedImage))
        return false;

    engine->cpu.pc = entryPoint;
    return true;
}

void NXNSORegistryInit(NXNSORegistry *registry) {
    if (registry) memset(registry, 0, sizeof(*registry));
}

bool NXNSORegistryRegister(NXNSORegistry *registry,
                           const NXLoadedNSO *module) {
    if (!registry || !module || registry->pendingRelocationsResolved ||
        registry->count >= NX_NSO_MAX_MODULES)
        return false;
    for (size_t i = 0; i < registry->count; ++i) {
        if (registry->modules[i].loadBase == module->loadBase)
            return false;
    }
    registry->modules[registry->count++] = *module;
    return true;
}

bool NXEngineLoadNSO(NXEngine *engine, const uint8_t *fileBytes,
                     size_t fileSize, uint64_t loadBase,
                     NXLoadedNSO *loadedImage) {
    return NXEngineLoadNSOWithRegistry(
        engine, fileBytes, fileSize, loadBase, NULL, loadedImage
    );
}

static bool moduleSymbolIsUndefined(NXEngine *engine,
                                    const NXLoadedNSO *module,
                                    uint32_t symbolIndex) {
    if (!engine || !module || symbolIndex == 0 ||
        symbolIndex >= module->symbolCount ||
        module->symbolTableAddress == 0)
        return false;
    uint64_t address = 0;
    uint8_t symbol[24];
    if (addOverflowU64(module->symbolTableAddress,
                       (uint64_t)symbolIndex * 24u, &address) ||
        !NXEngineReadBytes(engine, address, symbol, sizeof(symbol)))
        return false;
    return readU16LE(symbol + 6) == 0;
}

static bool resolvePendingTable(NXEngine *engine,
                                NXNSORegistry *registry,
                                NXLoadedNSO *module,
                                uint64_t tableAddress,
                                uint64_t tableSize,
                                bool isRela) {
    const size_t entrySize = isRela ? 24u : 16u;
    if (tableSize == 0) return true;
    if (tableSize > SIZE_MAX || tableSize % entrySize != 0 ||
        !addressInImage(module, tableAddress, (size_t)tableSize))
        return false;

    const uint64_t count = tableSize / entrySize;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t entryAddress = 0;
        if (addOverflowU64(tableAddress, i * entrySize, &entryAddress))
            return false;
        uint8_t entry[24] = {0};
        if (!NXEngineReadBytes(engine, entryAddress, entry, entrySize))
            return false;

        const uint64_t targetOffset = readU64LE(entry);
        const uint64_t info = readU64LE(entry + 8);
        const uint32_t type = (uint32_t)info;
        const uint32_t symbolIndex = (uint32_t)(info >> 32);
        if ((type != 257u && type != 258u) ||
            !moduleSymbolIsUndefined(engine, module, symbolIndex))
            continue;

        int64_t addend = 0;
        if (isRela) {
            addend = (int64_t)readU64LE(entry + 16);
        } else if (type == 257u) {
            uint64_t targetAddress = 0, implicitAddend = 0;
            if (addOverflowU64(module->loadBase, targetOffset, &targetAddress) ||
                !addressInImage(module, targetAddress, sizeof(uint64_t)) ||
                !readGuestU64(engine, targetAddress, &implicitAddend))
                return false;
            addend = (int64_t)implicitAddend;
        } else {
            uint64_t targetAddress = 0;
            uint8_t encoded[4];
            if (addOverflowU64(module->loadBase, targetOffset, &targetAddress) ||
                !addressInImage(module, targetAddress, sizeof(encoded)) ||
                !NXEngineReadBytes(engine, targetAddress, encoded, sizeof(encoded)))
                return false;
            const uint32_t implicitAddend =
                (uint32_t)encoded[0] | ((uint32_t)encoded[1] << 8) |
                ((uint32_t)encoded[2] << 16) | ((uint32_t)encoded[3] << 24);
            addend = (int64_t)implicitAddend;
        }

        uint64_t resolvedAddress = 0;
        if (!resolveDynamicSymbol(
                engine, module->loadBase, module, registry,
                module->symbolTableAddress, module->symbolCount,
                symbolIndex, &resolvedAddress))
            continue;

        const uint32_t before = module->symbolRelocationCount;
        if (!applyAArch64AbsoluteRelocation(
                engine, module->loadBase, module, targetOffset, type,
                symbolIndex, addend, module->symbolTableAddress,
                module->symbolCount, registry, module))
            return false;
        if (module->symbolRelocationCount > before &&
            module->unsupportedRelocationCount > 0)
            --module->unsupportedRelocationCount;
    }
    return true;
}

bool NXNSORegistryResolvePending(NXEngine *engine,
                                 NXNSORegistry *registry) {
    if (!engine || !engine->initialized || !registry ||
        registry->count > NX_NSO_MAX_MODULES ||
        registry->pendingRelocationsResolved)
        return false;

    for (size_t i = 0; i < registry->count; ++i) {
        NXLoadedNSO *module = &registry->modules[i];
        if (!resolvePendingTable(engine, registry, module,
                                 module->relaTableAddress,
                                 module->relaTableSize, true) ||
            !resolvePendingTable(engine, registry, module,
                                 module->relTableAddress,
                                 module->relTableSize, false))
            return false;
    }
    registry->pendingRelocationsResolved = true;
    return true;
}
