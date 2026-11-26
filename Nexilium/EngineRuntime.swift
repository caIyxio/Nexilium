import Foundation

/// Access to Nexilium's native guest CPU core.
enum NexiliumEngine {
    static var cpuCoreSelfTestPassed: Bool {
        NXCPUSelfTest() == 1
    }

    /// Becomes true only when Horizon boot reaches the real Switch system UI.
    static var systemMenuBootSupported: Bool {
        NXEngineCanBootSystemMenu() == 1
    }

    static var backendName: String {
        String(cString: NXCPUBackendName())
    }
}
