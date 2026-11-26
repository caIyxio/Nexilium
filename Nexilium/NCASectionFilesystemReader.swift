import Foundation

enum NCASectionFilesystemError: LocalizedError {
    case missingPFS0
    case pfs0TooLarge
    case malformedPFS0
    case fileNotFound(String)
    case fileTooLarge(String)

    var errorDescription: String? {
        switch self {
        case .missingPFS0:
            return "This NCA section does not contain a supported PFS0 filesystem."
        case .pfs0TooLarge:
            return "The PFS0 image exceeds Nexilium's current in-memory extraction limit."
        case .malformedPFS0:
            return "The decrypted PFS0 image is malformed."
        case .fileNotFound(let name):
            return "The PFS0 filesystem does not contain \(name)."
        case .fileTooLarge(let name):
            return "The file \(name) exceeds Nexilium's current extraction limit."
        }
    }
}

/// Reads selected files from a PFS0-backed NCA section without copying the
/// whole NCA. The PFS0 image is currently materialized in memory.
enum NCASectionFilesystemReader {
    private static let maximumPFS0Size: UInt64 = 128 * 1024 * 1024
    private static let maximumExtractedFileSize: UInt64 = 128 * 1024 * 1024

    static func extractPFS0File(
        named name: String,
        from fileURL: URL,
        section: NCASectionInfo,
        header: NCAHeaderInfo,
        prodKeysURL: URL
    ) throws -> Data {
        guard let pfsOffset = section.pfs0DataOffset,
              let pfsSize = section.pfs0DataSize else {
            throw NCASectionFilesystemError.missingPFS0
        }
        guard pfsSize > 0, pfsSize <= maximumPFS0Size,
              pfsSize <= UInt64(Int.max) else {
            throw NCASectionFilesystemError.pfs0TooLarge
        }

        let image = try NCASectionCipher.readRange(
            from: fileURL,
            section: section,
            header: header,
            prodKeysURL: prodKeysURL,
            offset: pfsOffset,
            count: Int(pfsSize)
        )

        var extracted: Data?
        let parsed = image.withUnsafeBytes { rawBuffer -> Bool in
            guard let baseAddress = rawBuffer.bindMemory(to: UInt8.self).baseAddress else {
                return false
            }

            var view = NXPFS0View()
            guard NXEngineOpenPFS0(baseAddress, image.count, &view) else {
                return false
            }

            var file = NXPFS0File()
            let found = name.withCString {
                NXEnginePFS0GetFile(&view, $0, &file)
            }
            guard found, let fileBytes = file.bytes else {
                return false
            }
            guard file.size <= Int(maximumExtractedFileSize) else {
                return false
            }

            extracted = Data(bytes: fileBytes, count: file.size)
            return true
        }

        guard parsed else {
            // Distinguish a malformed image from a well-formed image with no
            // requested entry before returning a failure to the caller.
            let structurallyValid = image.withUnsafeBytes { rawBuffer -> Bool in
                guard let baseAddress = rawBuffer.bindMemory(to: UInt8.self).baseAddress else {
                    return false
                }
                var view = NXPFS0View()
                return NXEngineOpenPFS0(baseAddress, image.count, &view)
            }
            if structurallyValid {
                throw NCASectionFilesystemError.fileNotFound(name)
            }
            throw NCASectionFilesystemError.malformedPFS0
        }

        guard let extracted else {
            throw NCASectionFilesystemError.fileTooLarge(name)
        }
        return extracted
    }
}
