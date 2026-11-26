import Foundation
import CommonCrypto

struct NCASectionInfo: Codable {
    let index: Int
    let startBlock: UInt32
    let endBlock: UInt32
    let byteOffset: UInt64
    let byteSize: UInt64
    let fileSystemType: UInt8
    let hashType: UInt8
    let encryptionType: UInt8
    let metadataHashType: UInt8
    let ctrUpperIV: [UInt8]
    let ivfcDataOffset: UInt64?
    let ivfcDataSize: UInt64?
}

struct NCAHeaderInfo: Codable {
    let magic: String
    let contentType: UInt8
    let keyGeneration: UInt8
    let keyGeneration2: UInt8
    let keyAreaEncryptionKeyIndex: UInt8
    let contentSize: UInt64
    let programID: UInt64
    let rightsID: String
    let encryptedKeyArea: Data
    let sections: [NCASectionInfo]
}

enum NCAHeaderError: LocalizedError {
    case fileTooSmall
    case invalidHeaderKey
    case decryptionFailed(Int32)
    case unsupportedMagic(String)
    case invalidContentSize
    case invalidSectionTable
    case invalidIVFC

    var errorDescription: String? {
        switch self {
        case .fileTooSmall:
            return "The NCA is too small to contain an encrypted header."
        case .invalidHeaderKey:
            return "The header_key must contain exactly 32 bytes."
        case .decryptionFailed(let status):
            return "NCA header decryption failed (CommonCrypto status \(status))."
        case .unsupportedMagic(let magic):
            return "The decrypted NCA header has an unsupported signature: \(magic)."
        case .invalidContentSize:
            return "The NCA header reports an invalid content size."
        case .invalidSectionTable:
            return "The NCA header contains an invalid or unsupported section table."
        case .invalidIVFC:
            return "An NCA RomFS section has invalid IVFC metadata or level bounds."
        }
    }
}

/// Decodes the encrypted NCA header, not its filesystem sections.
enum NCAHeaderDecoder {
    private static let sectorSize = 0x200
    private static let encryptedHeaderSize = 0xC00

    static func decode(fileURL: URL, headerKey: Data) throws -> NCAHeaderInfo {
        guard headerKey.count == 32 else { throw NCAHeaderError.invalidHeaderKey }

        let file = try FileHandle(forReadingFrom: fileURL)
        defer { try? file.close() }

        let fileSize = try file.seekToEnd()
        try file.seek(toOffset: 0)
        let encrypted = try file.read(upToCount: encryptedHeaderSize) ?? Data()
        guard encrypted.count == encryptedHeaderSize else {
            throw NCAHeaderError.fileTooSmall
        }

        let key1 = Array(headerKey.prefix(16))
        let key2 = Array(headerKey.suffix(16))
        var decrypted = [UInt8](encrypted)

        // The main header is two 0x200-byte AES-XTS sectors.
        for sector in 0..<2 {
            try decryptSector(
                &decrypted,
                offset: sector * sectorSize,
                sectorNumber: UInt64(sector),
                dataKey: key1,
                tweakKey: key2
            )
        }

        let magicBytes = Array(decrypted[0x200..<0x204])
        guard let magic = String(bytes: magicBytes, encoding: .ascii),
              ["NCA2", "NCA3"].contains(magic) else {
            let display = String(bytes: magicBytes, encoding: .ascii) ?? "????"
            throw NCAHeaderError.unsupportedMagic(display)
        }

        // NCA2 section-header sectors each use tweak sector zero; NCA3 uses
        // their position in the complete 0xC00-byte header.
        for sector in 2..<6 {
            let sectorNumber: UInt64 = magic == "NCA2" ? 0 : UInt64(sector)
            try decryptSector(
                &decrypted,
                offset: sector * sectorSize,
                sectorNumber: sectorNumber,
                dataKey: key1,
                tweakKey: key2
            )
        }

        var sectionLayout = NXNCASectionLayout()
        let sectionTableValid = decrypted.withUnsafeBufferPointer { bytes in
            guard let baseAddress = bytes.baseAddress else { return false }
            return NXNCAParseSectionTable(
                baseAddress,
                bytes.count,
                fileSize,
                &sectionLayout
            )
        }
        guard sectionTableValid else { throw NCAHeaderError.invalidSectionTable }

        var sections: [NCASectionInfo] = []
        for index in 0..<4 {
            var nativeSection = NXNCASectionInfo()
            guard NXNCAGetSectionInfo(
                &sectionLayout,
                UInt32(index),
                &nativeSection
            ) else {
                throw NCAHeaderError.invalidSectionTable
            }
            guard nativeSection.present else { continue }

            let fsHeaderOffset = 0x400 + index * 0x200
            let ctrUpperIV = Array(
                decrypted[(fsHeaderOffset + 0x140)..<(fsHeaderOffset + 0x148)]
            )

            var ivfcDataOffset: UInt64?
            var ivfcDataSize: UInt64?
            if nativeSection.fsType == 0 && nativeSection.hashType == 3 {
                var ivfcView = NXIVFCView()
                var dataOffset = 0
                var dataSize = 0
                let ivfcValid = decrypted.withUnsafeBufferPointer { bytes in
                    guard let baseAddress = bytes.baseAddress else { return false }
                    let superblock = baseAddress.advanced(by: fsHeaderOffset + 0x8)
                    return NXEngineOpenIVFC(
                        superblock,
                        NX_IVFC_HEADER_SIZE,
                        nativeSection.byteSize,
                        &ivfcView
                    ) && NXEngineIVFCGetDataLevel(
                        &ivfcView,
                        &dataOffset,
                        &dataSize
                    )
                }
                guard ivfcValid else { throw NCAHeaderError.invalidIVFC }
                ivfcDataOffset = UInt64(dataOffset)
                ivfcDataSize = UInt64(dataSize)
            }

            sections.append(NCASectionInfo(
                index: Int(nativeSection.index),
                startBlock: nativeSection.startBlock,
                endBlock: nativeSection.endBlock,
                byteOffset: nativeSection.byteOffset,
                byteSize: nativeSection.byteSize,
                fileSystemType: nativeSection.fsType,
                hashType: nativeSection.hashType,
                encryptionType: nativeSection.encryptionType,
                metadataHashType: nativeSection.metadataHashType,
                ctrUpperIV: ctrUpperIV,
                ivfcDataOffset: ivfcDataOffset,
                ivfcDataSize: ivfcDataSize
            ))
        }

        let contentType = decrypted[0x205]
        let keyGeneration = decrypted[0x206]
        let keyAreaIndex = decrypted[0x207]
        let contentSize = readUInt64LE(decrypted, at: 0x208)
        let programID = readUInt64LE(decrypted, at: 0x210)
        let keyGeneration2 = decrypted[0x220]
        let rightsID = decrypted[0x230..<0x240]
            .map { String(format: "%02x", $0) }
            .joined()

        guard contentSize >= UInt64(encryptedHeaderSize) else {
            throw NCAHeaderError.invalidContentSize
        }

        return NCAHeaderInfo(
            magic: magic,
            contentType: contentType,
            keyGeneration: keyGeneration,
            keyGeneration2: keyGeneration2,
            keyAreaEncryptionKeyIndex: keyAreaIndex,
            contentSize: contentSize,
            programID: programID,
            rightsID: rightsID,
            encryptedKeyArea: Data(decrypted[0x300..<0x340]),
            sections: sections
        )
    }

    private static func readUInt64LE(_ bytes: [UInt8], at offset: Int) -> UInt64 {
        var value: UInt64 = 0
        for index in 0..<8 {
            value |= UInt64(bytes[offset + index]) << (index * 8)
        }
        return value
    }

    private static func decryptSector(
        _ bytes: inout [UInt8],
        offset: Int,
        sectorNumber: UInt64,
        dataKey: [UInt8],
        tweakKey: [UInt8]
    ) throws {
        var sectorTweak = [UInt8](repeating: 0, count: 16)

        for index in 0..<8 {
            sectorTweak[15 - index] = UInt8((sectorNumber >> (index * 8)) & 0xFF)
        }
        var tweak = try aesBlock(
            sectorTweak,
            key: tweakKey,
            operation: CCOperation(kCCEncrypt)
        )

        for block in 0..<(sectorSize / 16) {
            let blockOffset = offset + block * 16
            var mixed = [UInt8](repeating: 0, count: 16)
            for index in 0..<16 {
                mixed[index] = bytes[blockOffset + index] ^ tweak[index]
            }

            let blockResult = try aesBlock(
                mixed,
                key: dataKey,
                operation: CCOperation(kCCDecrypt)
            )
            for index in 0..<16 {
                bytes[blockOffset + index] = blockResult[index] ^ tweak[index]
            }
            multiplyTweakByX(&tweak)
        }
    }

    private static func aesBlock(
        _ input: [UInt8],
        key: [UInt8],
        operation: CCOperation
    ) throws -> [UInt8] {
        guard input.count == 16, key.count == 16 else {
            throw NCAHeaderError.invalidHeaderKey
        }

        var output = [UInt8](repeating: 0, count: 16)
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

        guard status == kCCSuccess, outputLength == 16 else {
            throw NCAHeaderError.decryptionFailed(status)
        }
        return output
    }

    private static func multiplyTweakByX(_ tweak: inout [UInt8]) {
        var carry: UInt8 = 0
        for index in 0..<16 {
            let nextCarry = tweak[index] >> 7
            tweak[index] = (tweak[index] << 1) | carry
            carry = nextCarry
        }
        if carry != 0 {
            tweak[0] ^= 0x87
        }
    }
}
