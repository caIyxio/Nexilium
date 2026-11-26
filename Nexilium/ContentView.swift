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
    @State private var importTarget: ImportTarget = .firmware
    @State private var isImporterPresented = false
    @State private var errorMessage: String?

    private enum ImportTarget {
        case firmware
        case prodKeys
        case titleKeys

        var destinationName: String {
            switch self {
            case .firmware: return "firmware-package.zip"
            case .prodKeys: return "prod.keys"
            case .titleKeys: return "title.keys"
            }
        }

        var allowedContentTypes: [UTType] {
            switch self {
            case .firmware:
                return [.zip, .folder]
            case .prodKeys, .titleKeys:
                let keysType = UTType(filenameExtension: "keys", conformingTo: .data) ?? .data
                return [keysType, .folder]
            }
        }

        var displayName: String {
            switch self {
            case .firmware: return "firmware ZIP"
            case .prodKeys: return "prod.keys"
            case .titleKeys: return "title.keys"
            }
        }
    }

    private var storageDirectory: URL {
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        return documents.appendingPathComponent("Nexilium", isDirectory: true)
    }

    var body: some View {
        Form {
            Section("Nintendo Switch firmware") {
                LabeledContent("Firmware package", value: firmwareImported ? "Imported: " + firmwareFilename : "Not imported")
                Button {
                    presentImporter(for: .firmware)
                } label: {
                    Label(firmwareImported ? "Replace firmware package" : "Choose firmware package", systemImage: "folder")
                }
            }

            Section("Keys") {
                LabeledContent("prod.keys", value: prodKeysImported ? "Imported" : "Not imported")
                Button {
                    presentImporter(for: .prodKeys)
                } label: {
                    Label(prodKeysImported ? "Replace prod.keys" : "Choose prod.keys", systemImage: "key")
                }

                LabeledContent("title.keys", value: titleKeysImported ? "Imported" : "Not imported")
                Button {
                    presentImporter(for: .titleKeys)
                } label: {
                    Label(titleKeysImported ? "Replace title.keys" : "Choose title.keys", systemImage: "key.fill")
                }
            }

            Section {
                Text("Choose an individual file or a folder containing it. Firmware must be a .zip file; key files must be named prod.keys and title.keys. Nexilium currently stores the selected files locally; firmware parsing, validation and system emulation are not implemented yet.")
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
            isPresented: $isImporterPresented,
            allowedContentTypes: importTarget.allowedContentTypes,
            allowsMultipleSelection: false
        ) { result in
            handleImport(result)
        }
        .alert("Import failed", isPresented: Binding(
            get: { errorMessage != nil },
            set: { if !$0 { errorMessage = nil } }
        )) {
            Button("OK", role: .cancel) { errorMessage = nil }
        } message: {
            Text(errorMessage ?? "The selected file couldn't be imported.")
        }
    }

    private func presentImporter(for target: ImportTarget) {
        importTarget = target
        errorMessage = nil
        isImporterPresented = true
    }

    private func handleImport(_ result: Result<[URL], Error>) {
        do {
            guard let selectedURL = try result.get().first else { return }

            let hasAccess = selectedURL.startAccessingSecurityScopedResource()
            defer {
                if hasAccess {
                    selectedURL.stopAccessingSecurityScopedResource()
                }
            }

            let sourceURL = try resolveFile(from: selectedURL)
            try FileManager.default.createDirectory(
                at: storageDirectory,
                withIntermediateDirectories: true
            )

            let destination = storageDirectory.appendingPathComponent(importTarget.destinationName)
            if FileManager.default.fileExists(atPath: destination.path) {
                try FileManager.default.removeItem(at: destination)
            }
            try FileManager.default.copyItem(at: sourceURL, to: destination)

            switch importTarget {
            case .firmware:
                firmwareFilename = sourceURL.lastPathComponent
                firmwareImported = true
            case .prodKeys:
                prodKeysFilename = sourceURL.lastPathComponent
                prodKeysImported = true
            case .titleKeys:
                titleKeysFilename = sourceURL.lastPathComponent
                titleKeysImported = true
            }
            errorMessage = nil
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func resolveFile(from selectedURL: URL) throws -> URL {
        let values = try selectedURL.resourceValues(forKeys: [.isDirectoryKey])
        let candidates: [URL]

        if values.isDirectory == true {
            guard let enumerator = FileManager.default.enumerator(
                at: selectedURL,
                includingPropertiesForKeys: [.isDirectoryKey],
                options: [.skipsHiddenFiles]
            ) else {
                throw ImportError.cannotReadFolder
            }

            var matches: [URL] = []
            for case let fileURL as URL in enumerator {
                let isDirectory = (try? fileURL.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) ?? false
                guard !isDirectory else { continue }

                if matchesExpectedFile(fileURL) {
                    matches.append(fileURL)
                }
            }
            candidates = matches
        } else {
            candidates = matchesExpectedFile(selectedURL) ? [selectedURL] : []
        }

        guard !candidates.isEmpty else {
            throw ImportError.fileNotFound(importTarget.displayName)
        }
        guard candidates.count == 1, let match = candidates.first else {
            throw ImportError.multipleFiles(importTarget.displayName)
        }
        return match
    }

    private func matchesExpectedFile(_ url: URL) -> Bool {
        switch importTarget {
        case .firmware:
            return url.pathExtension.lowercased() == "zip"
        case .prodKeys:
            return url.lastPathComponent.lowercased() == "prod.keys"
        case .titleKeys:
            return url.lastPathComponent.lowercased() == "title.keys"
        }
    }

    private enum ImportError: LocalizedError {
        case cannotReadFolder
        case fileNotFound(String)
        case multipleFiles(String)

        var errorDescription: String? {
            switch self {
            case .cannotReadFolder:
                return "Nexilium couldn't read that folder. Try selecting the file directly."
            case .fileNotFound(let name):
                return "No \(name) was found in the selected location."
            case .multipleFiles(let name):
                return "More than one \(name) was found. Select the exact file instead of its parent folder."
            }
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
