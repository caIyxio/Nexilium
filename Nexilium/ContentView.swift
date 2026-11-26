import SwiftUI
import UIKit
import UniformTypeIdentifiers

struct ContentView: View {
    @State private var selectedTab = 0

    var body: some View {
        TabView(selection: $selectedTab) {
            NavigationStack {
                HomeView(selectedTab: $selectedTab)
            }
            .tabItem {
                Label("Home", systemImage: "house")
            }
            .tag(0)

            NavigationStack {
                Color(uiColor: .systemBackground)
                    .navigationTitle("Library")
                    .navigationBarTitleDisplayMode(.large)
            }
            .tabItem {
                Label("Library", systemImage: "square.grid.2x2")
            }
            .tag(1)

            NavigationStack {
                SettingsView()
            }
            .tabItem {
                Label("Settings", systemImage: "gearshape")
            }
            .tag(2)
        }
    }
}

private struct HomeView: View {
    @Binding var selectedTab: Int
    @AppStorage("system.firmwareImported") private var firmwareImported = false
    @AppStorage("system.prodKeysImported") private var prodKeysImported = false
    @AppStorage("system.titleKeysImported") private var titleKeysImported = false
    @State private var showFirmwareInstaller = false
    @State private var showSystemMenu = false

    private var systemMenuReady: Bool {
        firmwareImported && prodKeysImported && titleKeysImported
    }

    var body: some View {
        Color(uiColor: .systemBackground)
            .navigationTitle("Nexilium")
            .navigationBarTitleDisplayMode(.large)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Menu {
                        Button {
                            selectedTab = 2
                        } label: {
                            Label("Settings", systemImage: "gearshape")
                        }

                        Button {
                            showFirmwareInstaller = true
                        } label: {
                            Label("Install Nintendo Switch firmware", systemImage: "square.and.arrow.down")
                        }

                        Button {
                            showSystemMenu = true
                        } label: {
                            Label("Launch System Menu", systemImage: "play")
                        }
                        .disabled(!systemMenuReady)
                    } label: {
                        Image(systemName: "ellipsis")
                            .font(.system(size: 18, weight: .semibold))
                            .frame(width: 36, height: 36)
                            .contentShape(Rectangle())
                    }
                    .accessibilityLabel("Home menu")
                }
            }
            .sheet(isPresented: $showFirmwareInstaller) {
                NavigationStack {
                    FirmwareInstallerView()
                }
            }
            .fullScreenCover(isPresented: $showSystemMenu) {
                SystemMenuView()
            }
    }
}

private struct FirmwareInstallerView: View {
    @Environment(\.dismiss) private var dismiss
    @AppStorage("system.firmwareImported") private var firmwareImported = false
    @AppStorage("system.prodKeysImported") private var prodKeysImported = false
    @AppStorage("system.titleKeysImported") private var titleKeysImported = false
    @AppStorage("system.firmwareFilename") private var firmwareFilename = ""
    @AppStorage("system.prodKeysFilename") private var prodKeysFilename = ""
    @AppStorage("system.titleKeysFilename") private var titleKeysFilename = ""
    @State private var importingFirmware = false
    @State private var importingKeys = false
    @State private var importingTitleKeys = false
    @State private var errorMessage: String?

    private var storageDirectory: URL {
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        return documents.appendingPathComponent("Nexilium", isDirectory: true)
    }

    var body: some View {
        Form {
            Section("Nintendo Switch firmware") {
                LabeledContent("Firmware package", value: firmwareImported ? "Imported: " + firmwareFilename : "Not imported")
                Button {
                    importingFirmware = true
                } label: {
                    Label(firmwareImported ? "Replace firmware package" : "Choose firmware package", systemImage: "folder")
                }
            }

            Section("Keys") {
                LabeledContent("prod.keys", value: prodKeysImported ? "Imported" : "Not imported")
                Button {
                    importingKeys = true
                } label: {
                    Label(prodKeysImported ? "Replace prod.keys" : "Choose prod.keys", systemImage: "key")
                }

                LabeledContent("title.keys", value: titleKeysImported ? "Imported" : "Not imported")
                Button {
                    importingTitleKeys = true
                } label: {
                    Label(titleKeysImported ? "Replace title.keys" : "Choose title.keys", systemImage: "key.fill")
                }
            }

            Section {
                Text("Import a firmware .zip archive and both key files. Nexilium currently stores these files locally; firmware parsing, validation and system emulation are not implemented yet.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }

            if let errorMessage {
                Section {
                    Text(errorMessage)
                        .foregroundStyle(.red)
                        .font(.footnote)
                }
            }
        }
        .navigationTitle("Install firmware")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItem(placement: .confirmationAction) {
                Button("Done") { dismiss() }
            }
        }
        .fileImporter(
            isPresented: $importingFirmware,
            allowedContentTypes: [.zip],
            allowsMultipleSelection: false
        ) { result in
            handleImport(result, expectedExtension: "zip", destinationName: "firmware-package.zip") { name in
                firmwareFilename = name
                firmwareImported = true
            }
        }
        .fileImporter(
            isPresented: $importingKeys,
            allowedContentTypes: [.plainText, .data],
            allowsMultipleSelection: false
        ) { result in
            handleImport(result, expectedExtension: "keys", destinationName: "prod.keys") { name in
                prodKeysFilename = name
                prodKeysImported = true
            }
        }
        .fileImporter(
            isPresented: $importingTitleKeys,
            allowedContentTypes: [.plainText, .data],
            allowsMultipleSelection: false
        ) { result in
            handleImport(result, expectedExtension: "keys", destinationName: "title.keys") { name in
                titleKeysFilename = name
                titleKeysImported = true
            }
        }
    }

    private func handleImport(
        _ result: Result<[URL], Error>,
        expectedExtension: String?,
        destinationName: String,
        onSuccess: (String) -> Void
    ) {
        do {
            guard let sourceURL = try result.get().first else { return }
            if let expectedExtension, sourceURL.pathExtension.lowercased() != expectedExtension {
                errorMessage = "Please select a file named \\(destinationName)."
                return
            }

            let hasAccess = sourceURL.startAccessingSecurityScopedResource()
            defer {
                if hasAccess {
                    sourceURL.stopAccessingSecurityScopedResource()
                }
            }

            try FileManager.default.createDirectory(
                at: storageDirectory,
                withIntermediateDirectories: true
            )
            let destination = storageDirectory.appendingPathComponent(destinationName)
            if FileManager.default.fileExists(atPath: destination.path) {
                try FileManager.default.removeItem(at: destination)
            }
            try FileManager.default.copyItem(at: sourceURL, to: destination)
            errorMessage = nil
            onSuccess(sourceURL.lastPathComponent)
        } catch {
            errorMessage = "Could not import the selected file: \(error.localizedDescription)"
        }
    }
}

private struct SystemMenuView: View {
    @Environment(\.dismiss) private var dismiss
    private let orientationController = LandscapeOrientationController()

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
            VStack(spacing: 18) {
                Image(systemName: "gamecontroller.fill")
                    .font(.system(size: 42))
                    .foregroundStyle(.white)

                Text("System Menu")
                    .font(.largeTitle.weight(.semibold))
                    .foregroundStyle(.white)

                Text("Firmware files are imported. The Nexilium system-menu runtime is not connected yet.")
                    .font(.body)
                    .multilineTextAlignment(.center)
                    .foregroundStyle(.white.opacity(0.7))
                    .frame(maxWidth: 420)

                Button("Return to Nexilium") {
                    dismiss()
                }
                .buttonStyle(.borderedProminent)
                .tint(.white)
                .foregroundStyle(.black)
                .padding(.top, 8)
            }
            .padding(32)
        }
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
