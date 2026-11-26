import SwiftUI
import UIKit
import UniformTypeIdentifiers

struct ContentView: View {
    @State private var showFirmwareInstaller = false

    var body: some View {
        ConsoleHomeView(onFirmware: {
            showFirmwareInstaller = true
        })
        .sheet(isPresented: $showFirmwareInstaller) {
            NavigationStack { FirmwareInstallerView() }
        }
    }
}

private struct SystemMenuView: View {
    @Environment(\.dismiss) private var dismiss
    private let orientationController = LandscapeOrientationController()

    var body: some View {
        ConsoleHomeView(onExit: { dismiss() })
            .statusBarHidden()
            .onAppear {
                orientationController.setLandscape()
            }
            .onDisappear {
                orientationController.setPortrait()
            }
    }
}

@MainActor
private final class LandscapeOrientationController {
    func setLandscape() {
        setOrientation(.landscape)
    }

    func setPortrait() {
        setOrientation(.portrait)
    }

    private func setOrientation(_ orientation: UIInterfaceOrientationMask) {
        guard let scene = UIApplication.shared.connectedScenes
            .compactMap({ $0 as? UIWindowScene })
            .first(where: { $0.activationState == .foregroundActive }) else {
            return
        }

        scene.requestGeometryUpdate(.iOS(interfaceOrientations: orientation)) { _ in }
    }
}

#Preview {
    ContentView()
}
