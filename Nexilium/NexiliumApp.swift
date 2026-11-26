import SwiftUI
import UIKit

@main
struct NexiliumApp: App {
    @UIApplicationDelegateAdaptor(NexiliumAppDelegate.self) private var appDelegate

    init() {
        guard NexiliumEngine.cpuCoreSelfTestPassed else {
            NSLog("Nexilium: AArch64 CPU core self-test failed")
            return
        }
        NSLog("Nexilium: AArch64 translation core initialized")
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}

final class NexiliumAppDelegate: NSObject, UIApplicationDelegate {
    func application(
        _ application: UIApplication,
        supportedInterfaceOrientationsFor window: UIWindow?
    ) -> UIInterfaceOrientationMask {
        .landscape
    }
}
