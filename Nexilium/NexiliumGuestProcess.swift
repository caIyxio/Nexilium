import Foundation

enum NexiliumGuestProcessError: LocalizedError {
    case engineInitializationFailed
    case invalidProgramImage

    var errorDescription: String? {
        switch self {
        case .engineInitializationFailed:
            return "The guest CPU and memory machine could not be initialized."
        case .invalidProgramImage:
            return "The NPDM or main NSO could not be prepared for execution."
        }
    }
}

/// Owns one JIT-free guest machine and prepares a validated NPDM + NSO image.
/// The current Horizon implementation is incomplete, so this does not imply
/// that qlaunch or a complete Switch process can run yet.
final class NexiliumGuestProcess {
    private var engine = NXEngine()
    private var initialized = false
    private(set) var preparedProcess: NXPreparedProcess?

    init() throws {
        guard NXEngineInit(&engine) else {
            throw NexiliumGuestProcessError.engineInitializationFailed
        }
        initialized = true
    }

    deinit {
        if initialized {
            NXEngineDestroy(&engine)
        }
    }

    func load(_ image: NCAProgramImage, at loadBase: UInt64) throws {
        var prepared = NXPreparedProcess()
        let loaded = image.npdm.withUnsafeBytes { npdmBuffer -> Bool in
            guard let npdmBytes = npdmBuffer.bindMemory(to: UInt8.self).baseAddress else {
                return false
            }
            return image.mainNSO.withUnsafeBytes { nsoBuffer -> Bool in
                guard let nsoBytes = nsoBuffer.bindMemory(to: UInt8.self).baseAddress else {
                    return false
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

    @discardableResult
    func run(instructionBudget: UInt64) -> NXCPUResult {
        guard initialized else { return NX_CPU_INVALID_ARGUMENT }
        return NXEngineRun(&engine, instructionBudget)
    }
}
