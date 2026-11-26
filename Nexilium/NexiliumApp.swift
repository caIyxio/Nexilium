import SwiftUI
import UIKit

@main
struct NexiliumApp: App {
    @UIApplicationDelegateAdaptor(NexiliumAppDelegate.self) private var appDelegate

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
        [.portrait, .landscapeLeft, .landscapeRight]
    }
}
