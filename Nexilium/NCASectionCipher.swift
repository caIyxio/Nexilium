import Foundation
import CommonCrypto

enum NCASectionCipherError: LocalizedError {
    case invalidRange
    case unreadableSection
    case invalidKeyFile
    case missingKey(String)
    case unsupportedKeyAreaIndex(UInt8)
    case titleKeyRequired
    case unsupportedEncryption(UInt8)
    case cryptographicFailure(Int32)

    var errorDescription: String? {
        switch self {
        case .invalidRange:
            return "The requested read extends beyond the NCA section."
        case .unreadableSection:
            return "The requested NCA section data could not be read completely."
        case .invalidKeyFile:
            return "The production-key file could not be parsed."
        case .missingKey(let name):
            return "The production-key file is missing \(name)."
        case .unsupportedKeyAreaIndex(let index):
            return "The NCA uses an unsupported key-area index (\(index))."
        case .titleKeyRequired:
            return "This NCA uses a title key; title-key section decryption is not implemented yet."
        case .unsupportedEncryption(let type):
            return "NCA section encryption type \(type) is not supported by this reader yet."
        case .cryptographicFailure(let status):
            return "NCA section decryption failed (CommonCrypto status \(status))."
        }
    }
}

/// Random-access reader for NCA sections using no encryption or standard AES-CTR.
/// AES-CTR-Ex, AES-XTS sections, BKTR, and title-key NCAs remain unsupported.
enum NCASectionCipher {
    static func readRange(
        from fileURL: URL,
        section: NCASectionInfo,
        header: NCAHeaderInfo,
        prodKeysURL: URL,
        offset: UInt64,
        count: Int
    ) throws -> Data {
        guard count >= 0,
              offset <= section.byteSize,
              UInt64(count) <= section.byteSize - offset,
              section.byteOffset <= UInt64.max - offset else {
            throw NCASectionCipherError.invalidRange
        }
        if count == 0 { return Data() }

        let absoluteOffset = section.byteOffset + offset
        let file = try FileHandle(forReadingFrom: fileURL)
        defer { try? file.close() }
        try file.seek(toOffset: absoluteOffset)
        let encrypted = try file.read(upToCount: count) ?? Data()
        guard encrypted.count == count else {
            throw NCASectionCipherError.unreadableSection
        }

        switch section.encryptionType {
        case 1:
            return encrypted
        case 3:
            guard header.rightsID == String(repeating: "0", count: 32) else {
                throw NCASectionCipherError.titleKeyRequired
            }
            let keys = try loadKeys(at: prodKeysURL)
            let sectionKey = try decryptSectionKey(header: header, keys: keys)
            return try decryptCTR(
                encrypted,
                key: sectionKey,
                rawUpperIV: section.ctrUpperIV,
                absoluteOffset: absoluteOffset
            )
        default:
            throw NCASectionCipherError.unsupportedEncryption(section.encryptionType)
        }
    }

    private static func loadKeys(at url: URL) throws -> [String: Data] {
        let text: String
        do {
            text = try String(contentsOf: url, encoding: .utf8)
        } catch {
            throw NCASectionCipherError.invalidKeyFile
        }

        var keys: [String: Data] = [:]
        for rawLine in text.components(separatedBy: .newlines) {
            let line = rawLine.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !line.isEmpty, !line.hasPrefix("#"),
                  !line.hasPrefix(";"),
                  let separator = line.firstIndex(of: "=") else { continue }

            let name = line[..<separator]
                .trimmingCharacters(in: .whitespacesAndNewlines)
                .lowercased()
            let value = line[line.index(after: separator)...]
                .trimmingCharacters(in: .whitespacesAndNewlines)
            guard !name.isEmpty, value.count.isMultiple(of: 2),
                  value.unicodeScalars.allSatisfy({
                      CharacterSet(charactersIn: "0123456789abcdefABCDEF").contains($0)
                  }) else { continue }

            var bytes = Data()
            var index = value.startIndex
            while index < value.endIndex {
                let next = value.index(index, offsetBy: 2)
                guard let byte = UInt8(value[index..<next], radix: 16) else {
                    bytes.removeAll()
                    break
                }
                bytes.append(byte)
                index = next
            }
            if !bytes.isEmpty { keys[name] = bytes }
        }
        return keys
    }

    private static func decryptSectionKey(
        header: NCAHeaderInfo,
        keys: [String: Data]
    ) throws -> Data {
        let categories = ["application", "ocean", "system"]
        let keyIndex = Int(header.keyAreaEncryptionKeyIndex)
        guard categories.indices.contains(keyIndex) else {
            throw NCASectionCipherError.unsupportedKeyAreaIndex(
                header.keyAreaEncryptionKeyIndex
            )
        }

        // NCA crypto_type and crypto_type2 are generation selectors. Use the
        // lower value and map the one-based values to the prod.keys suffix.
        let generation = min(header.keyGeneration, header.keyGeneration2)
        let revision = generation == 0 ? 0 : Int(generation) - 1
        let keyName = "key_area_key_\(categories[keyIndex])_\(String(format: "%02x", revision))"
        guard let keyAreaKey = keys[keyName], keyAreaKey.count == 16 else {
            throw NCASectionCipherError.missingKey(keyName)
        }
        guard header.encryptedKeyArea.count == 0x40 else {
            throw NCASectionCipherError.invalidKeyFile
        }

        let clearKeyArea = try aesECB(
            Array(header.encryptedKeyArea),
            key: keyAreaKey,
            operation: CCOperation(kCCDecrypt)
        )
        // The reference NCA implementation selects decrypted_keys[2] for CTR.
        return Data(clearKeyArea[0x20..<0x30])
    }

    private static func decryptCTR(
        _ input: Data,
        key: Data,
        rawUpperIV: [UInt8],
        absoluteOffset: UInt64
    ) throws -> Data {
        guard key.count == 16, rawUpperIV.count == 8 else {
            throw NCASectionCipherError.invalidKeyFile
        }

        let skip = Int(absoluteOffset & 0xF)
        let firstCounter = absoluteOffset >> 4
        let requiredBytes = skip + input.count
        let blockCount = (requiredBytes + 15) / 16
        guard blockCount > 0,
              firstCounter <= UInt64.max - UInt64(blockCount - 1) else {
            throw NCASectionCipherError.invalidRange
        }

        var counters = [UInt8](repeating: 0, count: blockCount * 16)
        for block in 0..<blockCount {
            let base = block * 16
            // The NCA section's 64-bit upper IV is serialized in reverse byte order.
            for byte in 0..<8 {
                counters[base + byte] = rawUpperIV[7 - byte]
            }

            // AES-CTR's lower 64-bit counter is the big-endian absolute
            // NCA block number (absolute file offset divided by 16).
            let counter = firstCounter + UInt64(block)
            for byte in 0..<8 {
                counters[base + 15 - byte] =
                    UInt8((counter >> (byte * 8)) & 0xFF)
            }
        }

        let keyStream = try aesECBEncrypt(counters, key: key)
        let source = [UInt8](input)
        var result = [UInt8](repeating: 0, count: source.count)
        for index in source.indices {
            result[index] = source[index] ^ keyStream[skip + index]
        }
        return Data(result)
    }

    private static func aesECBEncrypt(_ input: [UInt8], key: Data) throws -> [UInt8] {
        try aesECB(input, key: key, operation: CCOperation(kCCEncrypt))
    }

    private static func aesECB(
        _ input: [UInt8],
        key: Data,
        operation: CCOperation
    ) throws -> [UInt8] {
        guard key.count == 16, !input.isEmpty,
              input.count.isMultiple(of: 16) else {
            throw NCASectionCipherError.invalidKeyFile
        }

        var output = [UInt8](repeating: 0, count: input.count)
        let outputCapacity = output.count
        var outputLength = 0
        let status: CCCryptorStatus = key.withUnsafeBytes { keyBytes in
            input.withUnsafeBytes { inputBytes in
                output.withUnsafeMutableBytes { outputBytes in
                    CCCrypt(
                        operation,
                        CCAlgorithm(kCCAlgorithmAES),
                        CCOptions(kCCOptionECBMode),
                        keyBytes.baseAddress,
                        key.count,
                        nil,
                        inputBytes.baseAddress,
                        input.count,
                        outputBytes.baseAddress,
                        outputCapacity,
                        &outputLength
                    )
                }
            }
        }

        guard status == kCCSuccess, outputLength == input.count else {
            throw NCASectionCipherError.cryptographicFailure(status)
        }
        return output
    }
}
