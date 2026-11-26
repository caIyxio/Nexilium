import Foundation
import CommonCrypto

struct NCAHeaderInfo: Codable {
    let magic: String
    let contentType: UInt8
    let keyGeneration: UInt8
    let keyAreaEncryptionKeyIndex: UInt8
    let contentSize: UInt64
    let programID: UInt64
    let rightsID: String
}

enum NCAHeaderError: LocalizedError {
    case fileTooSmall
    case invalidHeaderKey
    case decryptionFailed(Int32)
    case unsupportedMagic(String)
    case invalidContentSize

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
        }
    }
}

/// Decodes the first 0xC00 bytes of a user-supplied NCA file.
/// This validates the cryptographic container header; it does not decrypt NCA sections.
enum NCAHeaderDecoder {
    private static let sectorSize = 0x200
    private static let encryptedHeaderSize = 0xC00

    static func decode(fileURL: URL, headerKey: Data) throws -> NCAHeaderInfo {
        guard headerKey.count == 32 else { throw NCAHeaderError.invalidHeaderKey }

        let file = try FileHandle(forReadingFrom: fileURL)
        defer { try? file.close() }

        let encrypted = try file.read(upToCount: encryptedHeaderSize) ?? Data()
        guard encrypted.count == encryptedHeaderSize else {
            throw NCAHeaderError.fileTooSmall
        }

        let key1 = Array(headerKey.prefix(16))
        let key2 = Array(headerKey.suffix(16))
        var decrypted = [UInt8](encrypted)
        let sectorCount = encryptedHeaderSize / sectorSize

        for sector in 0..<sectorCount {
            // NCA2 section headers are encrypted as sector zero independently.
            let sectorNumber = sector < 2 ? sector : sector
            try decryptSector(
                &decrypted,
                offset: sector * sectorSize,
                sectorNumber: UInt64(sectorNumber),
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

        let contentType = decrypted[0x205]
        let keyGeneration = decrypted[0x220]
        let keyAreaIndex = decrypted[0x207]
        let contentSize = readUInt64LE(decrypted, at: 0x208)
        let programID = readUInt64LE(decrypted, at: 0x210)
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
            keyAreaEncryptionKeyIndex: keyAreaIndex,
            contentSize: contentSize,
            programID: programID,
            rightsID: rightsID
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

        // Nintendo's NCA header tweak serializes the sector number big-endian.
        for index in 0..<8 {
            sectorTweak[15 - index] = UInt8((sectorNumber >> (index * 8)) & 0xFF)
        }
        var tweak = try aesBlock(sectorTweak, key: tweakKey, operation: CCOperation(kCCEncrypt))

        for block in 0..<(sectorSize / 16) {
            let blockOffset = offset + block * 16
            var mixed = [UInt8](repeating: 0, count: 16)
            for index in 0..<16 {
                mixed[index] = bytes[blockOffset + index] ^ tweak[index]
            }

            let decrypted = try aesBlock(mixed, key: dataKey, operation: CCOperation(kCCDecrypt))
            for index in 0..<16 {
                bytes[blockOffset + index] = decrypted[index] ^ tweak[index]
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
                        output.count,
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
            // XTS reduction polynomial: x^128 + x^7 + x^2 + x + 1.
            tweak[0] ^= 0x87
        }
    }
}
