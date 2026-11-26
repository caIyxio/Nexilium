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
    let romFS: Data?
}

/// Reads ExeFS program metadata and the main NSO from a decrypted NCA PFS0
/// section. It does not yet perform NSO relocations or start a Horizon process.
enum NCAProgramImageReader {
    static func read(
        from fileURL: URL,
        sections: [NCASectionInfo],
        header: NCAHeaderInfo,
        prodKeysURL: URL
    ) throws -> NCAProgramImage {
        guard let exefs = sections.first(where: {
            $0.partitionType == 1 && $0.fileSystemType == 2
        }) else {
            throw NCAProgramImageError.missingExeFS
        }

        let npdm = try NCASectionFilesystemReader.extractPFS0File(
            named: "main.npdm",
            from: fileURL,
            section: exefs,
            header: header,
            prodKeysURL: prodKeysURL
        )
        let main = try NCASectionFilesystemReader.extractPFS0File(
            named: "main",
            from: fileURL,
            section: exefs,
            header: header,
            prodKeysURL: prodKeysURL
        )

        let romFSSection = sections.first(where: {
            $0.partitionType == 0 && $0.fileSystemType == 3
        })
        let romFS: Data?
        if let romFSSection,
           let offset = romFSSection.ivfcDataOffset,
           let size = romFSSection.ivfcDataSize,
           size > 0, size <= 128 * 1024 * 1024, size <= UInt64(Int.max) {
            romFS = try? NCASectionCipher.readRange(
                from: fileURL,
                section: romFSSection,
                header: header,
                prodKeysURL: prodKeysURL,
                offset: offset,
                count: Int(size)
            )
        } else {
            romFS = nil
        }

        return try decode(npdm: npdm, mainNSO: main, romFS: romFS)
    }

    static func loadExtracted(
        mainNSOURL: URL,
        npdmURL: URL,
        romFSURL: URL? = nil
    ) throws -> NCAProgramImage {
        let main = try Data(contentsOf: mainNSOURL, options: .mappedIfSafe)
        let npdm = try Data(contentsOf: npdmURL, options: .mappedIfSafe)
        let romFS = try romFSURL.map {
            try Data(contentsOf: $0, options: .mappedIfSafe)
        }
        return try decode(npdm: npdm, mainNSO: main, romFS: romFS)
    }

    static func decode(
        npdm: Data,
        mainNSO: Data,
        romFS: Data? = nil
    ) throws -> NCAProgramImage {
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
        return NCAProgramImage(
            metadata: metadata,
            mainNSO: mainNSO,
            npdm: npdm,
            romFS: romFS
        )
    }
}

enum NCAProgramImageError: LocalizedError {
    case invalidNPDM
    case missingExeFS

    var errorDescription: String? {
        switch self {
        case .invalidNPDM:
            return "The ExeFS main.npdm metadata is malformed or unsupported."
        case .missingExeFS:
            return "The program NCA does not contain a supported ExeFS section."
        }
    }
}
