import Foundation
import ZIPFoundation

struct FirmwareInstallSummary: Codable {
    let archiveName: String
    let installedAt: Date
    let ncaCount: Int
    let metadataNcaCount: Int
    let totalBytes: UInt64
    let files: [FirmwareFileRecord]
    let systemMenuAssets: FirmwareSystemMenuAssets?
    let systemMenuExtractionIssue: String?
}

struct FirmwareSystemMenuAssets: Codable {
    let ncaPath: String
    let titleID: UInt64
    let mainNSOPath: String
    let npdmPath: String
    let romFSPath: String?
    let modulePaths: [String]
    let mainNSOBytes: UInt64
    let romFSBytes: UInt64?
}

struct FirmwareFileRecord: Codable {
    let path: String
    let byteCount: UInt64
    let isMetadata: Bool
    let contentType: UInt8
    let keyGeneration: UInt8
    let programID: UInt64
    let rightsID: String
    let sections: [NCASectionInfo]
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

    static func install(
        archiveURL: URL,
        prodKeysURL: URL,
        storageDirectory: URL
    ) throws -> FirmwareInstallSummary {
        let headerKey = try loadHeaderKey(at: prodKeysURL)
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

                let headerInfo: NCAHeaderInfo
                do {
                    headerInfo = try NCAHeaderDecoder.decode(
                        fileURL: outputURL,
                        headerKey: headerKey
                    )
                } catch {
                    throw FirmwarePackageError.extractionFailed(
                        "\(entry.path): \(error.localizedDescription)"
                    )
                }
                guard headerInfo.contentSize == size else {
                    throw FirmwarePackageError.extractionFailed(
                        "\(entry.path): header size does not match archive entry size."
                    )
                }

                // NCA ContentType 1 is Meta. Firmware ZIP filenames do not
                // reliably indicate which NCA contains CNMT metadata.
                let isMetadata = headerInfo.contentType == 1
                if isMetadata { metadataCount += 1 }
                totalBytes += size
                records.append(FirmwareFileRecord(
                    path: entry.path,
                    byteCount: size,
                    isMetadata: isMetadata,
                    contentType: headerInfo.contentType,
                    keyGeneration: headerInfo.keyGeneration,
                    programID: headerInfo.programID,
                    rightsID: headerInfo.rightsID,
                    sections: headerInfo.sections
                ))
            }

            guard !records.isEmpty else { throw FirmwarePackageError.noNCAContent }
            guard metadataCount > 0 else { throw FirmwarePackageError.missingContentMetadata }

            let systemMenuResult = try extractSystemMenuAssets(
                records: records,
                stagingDirectory: staging,
                headerKey: headerKey,
                prodKeysURL: prodKeysURL
            )

            let summary = FirmwareInstallSummary(
                archiveName: archiveURL.lastPathComponent,
                installedAt: Date(),
                ncaCount: records.count,
                metadataNcaCount: metadataCount,
                totalBytes: totalBytes,
                files: records.sorted { $0.path < $1.path },
                systemMenuAssets: systemMenuResult.assets,
                systemMenuExtractionIssue: systemMenuResult.issue
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

    private static let systemMenuTitleID: UInt64 = 0x0100000000001000

    private static func extractSystemMenuAssets(
        records: [FirmwareFileRecord],
        stagingDirectory: URL,
        headerKey: Data,
        prodKeysURL: URL
    ) throws -> (assets: FirmwareSystemMenuAssets?, issue: String?) {
        let candidates = records.filter {
            $0.contentType == 0 && $0.programID == systemMenuTitleID
        }
        guard !candidates.isEmpty else {
            return (nil, "No qlaunch program NCA (title ID 0100000000001000) was found.")
        }

        var lastIssue = "The qlaunch program NCA has no supported PFS0 ExeFS section."
        for candidate in candidates {
            let ncaURL = stagingDirectory.appendingPathComponent(candidate.path)
            let header: NCAHeaderInfo
            do {
                header = try NCAHeaderDecoder.decode(fileURL: ncaURL, headerKey: headerKey)
            } catch {
                lastIssue = error.localizedDescription
                continue
            }

            do {
                    let program = try NCAProgramImageReader.read(
                        from: ncaURL,
                        sections: header.sections,
                        header: header,
                        prodKeysURL: prodKeysURL
                    )
                    let outputDirectory = stagingDirectory
                        .appendingPathComponent("system-menu", isDirectory: true)
                    try FileManager.default.createDirectory(
                        at: outputDirectory,
                        withIntermediateDirectories: true
                    )
                    let mainURL = outputDirectory.appendingPathComponent("main.nso")
                    let npdmURL = outputDirectory.appendingPathComponent("main.npdm")
                    try program.mainNSO.write(to: mainURL, options: .atomic)
                    try program.npdm.write(to: npdmURL, options: .atomic)

                    var romFSPath: String?
                    var romFSBytes: UInt64?
                    if let romFS = program.romFS {
                        let romFSURL = outputDirectory.appendingPathComponent("romfs.bin")
                        try romFS.write(to: romFSURL, options: .atomic)
                        romFSPath = "system-menu/romfs.bin"
                        romFSBytes = UInt64(romFS.count)
                    }

                    let modulesDirectory = outputDirectory
                        .appendingPathComponent("modules", isDirectory: true)
                    try FileManager.default.createDirectory(
                        at: modulesDirectory,
                        withIntermediateDirectories: true
                    )
                    var modulePaths: [String] = []
                    for (index, module) in program.modules.enumerated() {
                        let filename = String(format: "module-%02d.nso", index)
                        let moduleURL = modulesDirectory.appendingPathComponent(filename)
                        try module.image.write(to: moduleURL, options: .atomic)
                        modulePaths.append("system-menu/modules/\(filename)")
                    }

                    return (
                        FirmwareSystemMenuAssets(
                            ncaPath: candidate.path,
                            titleID: program.metadata.titleID,
                            mainNSOPath: "system-menu/main.nso",
                            npdmPath: "system-menu/main.npdm",
                            romFSPath: romFSPath,
                            modulePaths: modulePaths,
                            mainNSOBytes: UInt64(program.mainNSO.count),
                            romFSBytes: romFSBytes
                        ),
                        nil
                    )
            } catch {
                lastIssue = error.localizedDescription
            }
        }
        return (nil, lastIssue)
    }

    private static func loadHeaderKey(at url: URL) throws -> Data {
        let text = try String(contentsOf: url, encoding: .utf8)
        for rawLine in text.components(separatedBy: .newlines) {
            let line = rawLine.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !line.isEmpty, !line.hasPrefix("#"),
                  let separator = line.firstIndex(of: "=") else { continue }
            let name = line[..<separator]
                .trimmingCharacters(in: .whitespacesAndNewlines)
                .lowercased()
            guard name == "header_key" else { continue }
            let value = line[line.index(after: separator)...]
                .trimmingCharacters(in: .whitespacesAndNewlines)
            guard value.count == 64,
                  value.unicodeScalars.allSatisfy({
                      CharacterSet(charactersIn: "0123456789abcdefABCDEF").contains($0)
                  }) else {
                throw FirmwarePackageError.invalidKeys("header_key must be 64 hexadecimal characters.")
            }

            var bytes = Data()
            var index = value.startIndex
            while index < value.endIndex {
                let next = value.index(index, offsetBy: 2)
                guard let byte = UInt8(value[index..<next], radix: 16) else {
                    throw FirmwarePackageError.invalidKeys("header_key contains invalid hexadecimal data.")
                }
                bytes.append(byte)
                index = next
            }
            return bytes
        }
        throw FirmwarePackageError.invalidKeys("header_key is missing from prod.keys.")
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
