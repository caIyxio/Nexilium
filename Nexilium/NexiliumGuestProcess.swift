import Foundation

enum NexiliumGuestProcessError: LocalizedError {
    case engineInitializationFailed
    case invalidProgramImage
    case invalidSystemMenuAssets

    var errorDescription: String? {
        switch self {
        case .engineInitializationFailed:
            return "The guest CPU and memory machine could not be initialized."
        case .invalidProgramImage:
            return "The NPDM or main NSO could not be prepared for execution."
        case .invalidSystemMenuAssets:
            return "The extracted qlaunch files do not match the expected System Menu title."
        }
    }
}

/// Owns one JIT-free guest machine and prepares a validated NPDM + NSO image.
/// The current Horizon implementation is incomplete, so this does not imply
/// that qlaunch or a complete Switch process can run yet.
final class NexiliumGuestProcess {
    private var engine = NXEngine()
    private var initialized = false
    private var mountedFileSystemImages: [Data] = []
    private var moduleRegistry = NXNSORegistry()
    private(set) var preparedProcess: NXPreparedProcess?

    init() throws {
        guard NXEngineInit(&engine) else {
            throw NexiliumGuestProcessError.engineInitializationFailed
        }
        NXNSORegistryInit(&moduleRegistry)
        initialized = true
    }

    deinit {
        if initialized {
            NXEngineDestroy(&engine)
        }
    }

    func load(_ image: NCAProgramImage, at loadBase: UInt64) throws {
        try prepare(image, at: loadBase, usingRegistry: false)
    }

    private func prepare(
        _ image: NCAProgramImage,
        at loadBase: UInt64,
        usingRegistry: Bool
    ) throws {
        var prepared = NXPreparedProcess()
        let loaded = image.npdm.withUnsafeBytes { npdmBuffer -> Bool in
            guard let npdmBytes = npdmBuffer.bindMemory(to: UInt8.self).baseAddress else {
                return false
            }
            return image.mainNSO.withUnsafeBytes { nsoBuffer -> Bool in
                guard let nsoBytes = nsoBuffer.bindMemory(to: UInt8.self).baseAddress else {
                    return false
                }
                if usingRegistry {
                    return NXEnginePrepareMainThreadWithRegistry(
                        &engine,
                        npdmBytes,
                        image.npdm.count,
                        nsoBytes,
                        image.mainNSO.count,
                        loadBase,
                        &moduleRegistry,
                        &prepared
                    )
                }
                return NXEnginePrepareMainThread(
                    &engine,
                    npdmBytes,
                    image.npdm.count,
                    nsoBytes,
                    image.mainNSO.count,
                    loadBase,
                    &prepared
                )
            }
        }

        guard loaded else {
            throw NexiliumGuestProcessError.invalidProgramImage
        }
        preparedProcess = prepared
    }

    func loadSystemMenu(
        assets: FirmwareSystemMenuAssets,
        storageDirectory: URL,
        loadBase: UInt64 = 0x7100000000
    ) throws {
        let mainURL = storageDirectory.appendingPathComponent(assets.mainNSOPath)
        let npdmURL = storageDirectory.appendingPathComponent(assets.npdmPath)
        let romFSURL = assets.romFSPath.map {
            storageDirectory.appendingPathComponent($0)
        }
        let moduleURLs = (assets.modulePaths ?? []).map {
            storageDirectory.appendingPathComponent($0)
        }
        let image = try NCAProgramImageReader.loadExtracted(
            mainNSOURL: mainURL,
            npdmURL: npdmURL,
            romFSURL: romFSURL,
            moduleURLs: moduleURLs
        )
        guard image.metadata.titleID == assets.titleID,
              image.metadata.titleID == 0x0100000000001000 else {
            throw NexiliumGuestProcessError.invalidSystemMenuAssets
        }
        if let romFS = image.romFS {
            let mounted = romFS.withUnsafeBytes { rawBuffer -> Bool in
                guard let bytes = rawBuffer.bindMemory(to: UInt8.self).baseAddress else {
                    return false
                }
                return NXEngineMountRomFS(
                    &engine,
                    "romfs",
                    bytes,
                    romFS.count
                )
            }
            guard mounted else {
                throw NexiliumGuestProcessError.invalidSystemMenuAssets
            }
            mountedFileSystemImages.append(romFS)
        }

        guard image.modules.count < 32, moduleRegistry.count == 0 else {
            throw NexiliumGuestProcessError.invalidSystemMenuAssets
        }

        /* Load ExeFS dependency NSOs first so the main image can resolve them. */
        for (index, module) in image.modules.enumerated() {
            let moduleBase = UInt64(0x7200000000) +
                             UInt64(index) * UInt64(0x20000000)
            var loadedModule = NXLoadedNSO()
            let moduleLoaded = module.image.withUnsafeBytes { rawBuffer -> Bool in
                guard let bytes = rawBuffer.bindMemory(to: UInt8.self).baseAddress else {
                    return false
                }
                return NXEngineLoadNSOWithRegistry(
                    &engine,
                    bytes,
                    module.image.count,
                    moduleBase,
                    &moduleRegistry,
                    &loadedModule
                )
            }
            guard moduleLoaded else {
                throw NexiliumGuestProcessError.invalidProgramImage
            }
        }

        try prepare(image, at: loadBase, usingRegistry: true)
    }

    @discardableResult
    func run(instructionBudget: UInt64) -> NXCPUResult {
        guard initialized else { return NX_CPU_INVALID_ARGUMENT }
        return NXEngineRun(&engine, instructionBudget)
    }
}
