import Foundation

/// Access to Nexilium's native guest CPU core.
enum NexiliumEngine {
    static var cpuCoreSelfTestPassed: Bool {
        NXCPUSelfTest() == 1
    }
}
