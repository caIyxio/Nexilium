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
        let npdmCount = image.npdm.count
        let nsoCount = image.mainNSO.count

        let loaded: Bool = image.npdm.withUnsafeBytes { (npdmBuffer: UnsafeRawBufferPointer) -> Bool in
            guard let npdmBytes = npdmBuffer.bindMemory(to: UInt8.self).baseAddress else {
                return false
            }

            let result: Bool = image.mainNSO.withUnsafeBytes { (nsoBuffer: UnsafeRawBufferPointer) -> Bool in
                guard let nsoBytes = nsoBuffer.bindMemory(to: UInt8.self).baseAddress else {
                    return false
                }

                if usingRegistry {
                    let registryResult: Bool = NXEnginePrepareMainThreadWithRegistry(
                        &engine,
                        npdmBytes,
                        npdmCount,
                        nsoBytes,
                        nsoCount,
                        loadBase,
                        &moduleRegistry,
                        &prepared
                    )
                    return registryResult
                }

                let prepareResult: Bool = NXEnginePrepareMainThread(
                    &engine,
                    npdmBytes,
                    npdmCount,
                    nsoBytes,
                    nsoCount,
                    loadBase,
                    &prepared
                )
                return prepareResult
            }
            return result
        }

        guard loaded else {
            throw NexiliumGuestProcessError.invalidProgramImage
        }
        preparedProcess = prepared
    }

    private func mountRomFS(_ romFS: Data) throws {
        let romFSSize = romFS.count
        let mounted: Bool = romFS.withUnsafeBytes { (rawBuffer: UnsafeRawBufferPointer) -> Bool in
            guard let bytes = rawBuffer.bindMemory(to: UInt8.self).baseAddress else {
                return false
            }
            let result: Bool = NXEngineMountRomFS(
                &engine,
                "romfs",
                bytes,
                romFSSize
            )
            return result
        }

        guard mounted else {
            throw NexiliumGuestProcessError.invalidSystemMenuAssets
        }
        mountedFileSystemImages.append(romFS)
    }

    private func loadDependencyModule(
        _ module: NCAProgramModule,
        at moduleBase: UInt64
    ) throws {
        var loadedModule = NXLoadedNSO()
        let moduleSize = module.image.count

        let moduleLoaded: Bool = module.image.withUnsafeBytes { (rawBuffer: UnsafeRawBufferPointer) -> Bool in
            guard let bytes = rawBuffer.bindMemory(to: UInt8.self).baseAddress else {
                return false
            }
            let result: Bool = NXEngineLoadNSO(
                &engine,
                bytes,
                moduleSize,
                moduleBase,
                &loadedModule
            )
            return result
        }

        guard moduleLoaded else {
            throw NexiliumGuestProcessError.invalidProgramImage
        }
        let registered: Bool = NXNSORegistryRegister(&moduleRegistry, &loadedModule)
        guard registered else {
            throw NexiliumGuestProcessError.invalidProgramImage
        }
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
        let modulePaths = assets.modulePaths ?? []
        let moduleURLs = modulePaths.map {
            storageDirectory.appendingPathComponent($0)
        }

        let image = try NCAProgramImageReader.loadExtracted(
            mainNSOURL: mainURL,
            npdmURL: npdmURL,
            romFSURL: romFSURL,
            moduleURLs: moduleURLs
        )

        let expectedSystemMenuTitleID: UInt64 = 0x0100000000001000
        guard image.metadata.titleID == assets.titleID,
              image.metadata.titleID == expectedSystemMenuTitleID else {
            throw NexiliumGuestProcessError.invalidSystemMenuAssets
        }

        if let romFS = image.romFS {
            try mountRomFS(romFS)
        }

        guard image.modules.count < 32, moduleRegistry.count == 0 else {
            throw NexiliumGuestProcessError.invalidSystemMenuAssets
        }

        /* Load ExeFS dependency NSOs first so the main image can resolve them. */
        for (index, module) in image.modules.enumerated() {
            let indexValue = UInt64(index)
            let moduleStride: UInt64 = 0x20000000
            let moduleBase = UInt64(0x7200000000) + indexValue * moduleStride
            try loadDependencyModule(module, at: moduleBase)
        }

        /* Register every image before resolving imports to support cyclic dependencies. */
        try prepare(image, at: loadBase, usingRegistry: false)
        guard var prepared = self.preparedProcess else {
            throw NexiliumGuestProcessError.invalidProgramImage
        }
        let mainRegistered = NXNSORegistryRegister(&moduleRegistry, &prepared.mainImage)
        guard mainRegistered else {
            throw NexiliumGuestProcessError.invalidProgramImage
        }
        let importsResolved = NXNSORegistryResolvePending(&engine, &moduleRegistry)
        guard importsResolved else {
            throw NexiliumGuestProcessError.invalidProgramImage
        }
        self.preparedProcess = prepared
    }

    @discardableResult
    func run(instructionBudget: UInt64) -> NXCPUResult {
        guard initialized else { return NX_CPU_INVALID_ARGUMENT }
        return NXEngineRun(&engine, instructionBudget)
    }
}
