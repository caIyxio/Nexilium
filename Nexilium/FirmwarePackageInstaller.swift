import Foundation
import ZIPFoundation

struct FirmwareInstallSummary: Codable {
    let archiveName: String
    let installedAt: Date
    let ncaCount: Int
    let metadataNcaCount: Int
    let totalBytes: UInt64
    let files: [FirmwareFileRecord]
}

struct FirmwareFileRecord: Codable {
    let path: String
    let byteCount: UInt64
    let isMetadata: Bool
}

enum FirmwarePackageError: LocalizedError {
    case invalidKeys(String)
    case unreadableArchive
    case unsafeEntry(String)
    case packageTooLarge
    case tooManyFiles
    case noNCAContent
    case missingContentMetadata
    case extractionFailed(String)

    var errorDescription: String? {
        switch self {
        case .invalidKeys(let reason):
            return "prod.keys is not a usable key file: \(reason)"
        case .unreadableArchive:
            return "The selected ZIP archive could not be opened."
        case .unsafeEntry(let path):
            return "The archive contains an unsafe file path: \(path)"
        case .packageTooLarge:
            return "The expanded firmware archive exceeds the 1.5 GB safety limit."
        case .tooManyFiles:
            return "The archive contains too many NCA files."
        case .noNCAContent:
            return "No Nintendo Content Archive (.nca) files were found."
        case .missingContentMetadata:
            return "The package is missing CNMT metadata archives and cannot be installed as a firmware set."
        case .extractionFailed(let reason):
            return "Firmware extraction failed: \(reason)"
        }
    }
}

enum FirmwarePackageInstaller {
    private static let maximumExpandedBytes: UInt64 = 1_500_000_000
    private static let maximumNCAFiles = 4096

    static func validateProductionKeys(at url: URL) throws -> Int {
        let text = try String(contentsOf: url, encoding: .utf8)
        var keys: [String: String] = [:]

        for rawLine in text.components(separatedBy: .newlines) {
            let line = rawLine.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !line.isEmpty,
                  !line.hasPrefix("#"),
                  !line.hasPrefix(";"),
                  let separator = line.firstIndex(of: "=") else {
                continue
            }

            let name = line[..<separator]
                .trimmingCharacters(in: .whitespacesAndNewlines)
                .lowercased()
            let value = line[line.index(after: separator)...]
                .trimmingCharacters(in: .whitespacesAndNewlines)
                .lowercased()

            guard !name.isEmpty, !value.isEmpty,
                  value.count.isMultiple(of: 2),
                  value.unicodeScalars.allSatisfy({
                      CharacterSet(charactersIn: "0123456789abcdef").contains($0)
                  }) else {
                continue
            }
            keys[name] = value
        }

        guard let headerKey = keys["header_key"], headerKey.count == 64 else {
            throw FirmwarePackageError.invalidKeys("header_key is missing or has the wrong length.")
        }

        let masterKeys = keys.filter {
            $0.key.range(of: #"^master_key_[0-9a-f]{2}$"#, options: .regularExpression) != nil &&
            $0.value.count == 32
        }
        guard !masterKeys.isEmpty else {
            throw FirmwarePackageError.invalidKeys("No valid master_key entries were found.")
        }
        return keys.count
    }

    static func validateTitleKeys(at url: URL) throws -> Int {
        let text = try String(contentsOf: url, encoding: .utf8)
        var count = 0
        for rawLine in text.components(separatedBy: .newlines) {
            let line = rawLine.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !line.isEmpty, !line.hasPrefix("#"),
                  let separator = line.firstIndex(of: "=") else { continue }
            let value = line[line.index(after: separator)...]
                .trimmingCharacters(in: .whitespacesAndNewlines)
            guard (value.count == 32 || value.count == 64),
                  value.unicodeScalars.allSatisfy({
                      CharacterSet(charactersIn: "0123456789abcdefABCDEF").contains($0)
                  }) else { continue }
            count += 1
        }
        guard count > 0 else {
            throw FirmwarePackageError.invalidKeys("No valid title-key entries were found.")
        }
        return count
    }

    static func install(archiveURL: URL, storageDirectory: URL) throws -> FirmwareInstallSummary {
        let archive: Archive
        do {
            archive = try Archive(url: archiveURL, accessMode: .read)
        } catch {
            throw FirmwarePackageError.unreadableArchive
        }

        let manager = FileManager.default
        try manager.createDirectory(at: storageDirectory,
                                    withIntermediateDirectories: true)

        let staging = storageDirectory.appendingPathComponent(
            ".firmware-staging-\(UUID().uuidString)",
            isDirectory: true
        )
        let destination = storageDirectory.appendingPathComponent("firmware", isDirectory: true)
        let backup = storageDirectory.appendingPathComponent(
            ".firmware-backup-\(UUID().uuidString)",
            isDirectory: true
        )

        try manager.createDirectory(at: staging, withIntermediateDirectories: true)
        var records: [FirmwareFileRecord] = []
        var totalBytes: UInt64 = 0
        var metadataCount = 0

        do {
            for entry in archive {
                guard entry.type == .file else { continue }
                let lowerPath = entry.path.lowercased()
                guard lowerPath.hasSuffix(".nca") else { continue }

                guard isSafeRelativePath(entry.path) else {
                    throw FirmwarePackageError.unsafeEntry(entry.path)
                }
                guard entry.uncompressedSize > 0 else {
                    throw FirmwarePackageError.extractionFailed("An NCA archive entry is empty.")
                }

                let size = UInt64(entry.uncompressedSize)
                guard size <= maximumExpandedBytes,
                      totalBytes <= maximumExpandedBytes - size else {
                    throw FirmwarePackageError.packageTooLarge
                }
                guard records.count < maximumNCAFiles else {
                    throw FirmwarePackageError.tooManyFiles
                }

                let outputURL = staging.appendingPathComponent(entry.path)
                    .standardizedFileURL
                let stagingPrefix = staging.standardizedFileURL.path + "/"
                guard outputURL.path.hasPrefix(stagingPrefix) else {
                    throw FirmwarePackageError.unsafeEntry(entry.path)
                }

                try manager.createDirectory(
                    at: outputURL.deletingLastPathComponent(),
                    withIntermediateDirectories: true
                )
                try archive.extract(entry, to: outputURL)

                let attributes = try manager.attributesOfItem(atPath: outputURL.path)
                let extractedSize = (attributes[.size] as? NSNumber)?.uint64Value ?? 0
                guard extractedSize == size else {
                    throw FirmwarePackageError.extractionFailed("An extracted NCA had an unexpected size.")
                }

                let isMetadata = lowerPath.hasSuffix(".cnmt.nca")
                if isMetadata { metadataCount += 1 }
                totalBytes += size
                records.append(FirmwareFileRecord(
                    path: entry.path,
                    byteCount: size,
                    isMetadata: isMetadata
                ))
            }

            guard !records.isEmpty else { throw FirmwarePackageError.noNCAContent }
            guard metadataCount > 0 else { throw FirmwarePackageError.missingContentMetadata }

            let summary = FirmwareInstallSummary(
                archiveName: archiveURL.lastPathComponent,
                installedAt: Date(),
                ncaCount: records.count,
                metadataNcaCount: metadataCount,
                totalBytes: totalBytes,
                files: records.sorted { $0.path < $1.path }
            )
            let manifestURL = staging.appendingPathComponent("manifest.json")
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            encoder.dateEncodingStrategy = .iso8601
            try encoder.encode(summary).write(to: manifestURL, options: .atomic)

            var movedOldInstall = false
            if manager.fileExists(atPath: destination.path) {
                try manager.moveItem(at: destination, to: backup)
                movedOldInstall = true
            }

            do {
                try manager.moveItem(at: staging, to: destination)
                if movedOldInstall {
                    try? manager.removeItem(at: backup)
                }
            } catch {
                if movedOldInstall {
                    try? manager.moveItem(at: backup, to: destination)
                }
                throw error
            }
            return summary
        } catch {
            try? manager.removeItem(at: staging)
            if let installerError = error as? FirmwarePackageError {
                throw installerError
            }
            throw FirmwarePackageError.extractionFailed(error.localizedDescription)
        }
    }

    private static func isSafeRelativePath(_ path: String) -> Bool {
        guard !path.isEmpty,
              !path.hasPrefix("/"),
              !path.contains("\\"),
              !path.contains(":"),
              !path.contains("\0") else { return false }

        let components = path.split(separator: "/", omittingEmptySubsequences: false)
        return !components.isEmpty &&
            components.allSatisfy { !$0.isEmpty && $0 != "." && $0 != ".." }
    }
}
