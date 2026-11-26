import Foundation

struct NCAProgramMetadata: Codable {
    let titleID: UInt64
    let titleIDRangeMin: UInt64
    let titleIDRangeMax: UInt64
    let mainStackSize: UInt32
    let version: UInt32
    let mainThreadPriority: UInt8
    let defaultCPU: UInt8
    let isRetail: Bool
    let is64Bit: Bool
    let addressSpaceType: UInt8
}

struct NCAProgramImage {
    let metadata: NCAProgramMetadata
    let mainNSO: Data
    let npdm: Data
}

/// Reads ExeFS program metadata and the main NSO from a decrypted NCA PFS0
/// section. It does not yet perform NSO relocations or start a Horizon process.
enum NCAProgramImageReader {
    static func read(
        from fileURL: URL,
        section: NCASectionInfo,
        header: NCAHeaderInfo,
        prodKeysURL: URL
    ) throws -> NCAProgramImage {
        let npdm = try NCASectionFilesystemReader.extractPFS0File(
            named: "main.npdm",
            from: fileURL,
            section: section,
            header: header,
            prodKeysURL: prodKeysURL
        )
        let main = try NCASectionFilesystemReader.extractPFS0File(
            named: "main",
            from: fileURL,
            section: section,
            header: header,
            prodKeysURL: prodKeysURL
        )

        return try decode(npdm: npdm, mainNSO: main)
    }

    static func loadExtracted(
        mainNSOURL: URL,
        npdmURL: URL
    ) throws -> NCAProgramImage {
        let main = try Data(contentsOf: mainNSOURL, options: .mappedIfSafe)
        let npdm = try Data(contentsOf: npdmURL, options: .mappedIfSafe)
        return try decode(npdm: npdm, mainNSO: main)
    }

    static func decode(npdm: Data, mainNSO: Data) throws -> NCAProgramImage {
        var native = NXNPDMView()
        let valid = npdm.withUnsafeBytes { rawBuffer -> Bool in
            guard let bytes = rawBuffer.bindMemory(to: UInt8.self).baseAddress else {
                return false
            }
            return NXEngineOpenNPDM(bytes, npdm.count, &native)
        }
        guard valid else {
            throw NCAProgramImageError.invalidNPDM
        }

        let metadata = NCAProgramMetadata(
            titleID: native.titleID,
            titleIDRangeMin: native.titleIDRangeMin,
            titleIDRangeMax: native.titleIDRangeMax,
            mainStackSize: native.mainStackSize,
            version: native.version,
            mainThreadPriority: native.mainThreadPriority,
            defaultCPU: native.defaultCPU,
            isRetail: native.isRetail != 0,
            is64Bit: (native.mmuFlags & 1) != 0,
            addressSpaceType: (native.mmuFlags >> 1) & 0x7
        )
        return NCAProgramImage(metadata: metadata, mainNSO: mainNSO, npdm: npdm)
    }
}

enum NCAProgramImageError: LocalizedError {
    case invalidNPDM

    var errorDescription: String? {
        "The ExeFS main.npdm metadata is malformed or unsupported."
    }
}
