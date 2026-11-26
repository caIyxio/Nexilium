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

    // The mock ConsoleHomeView is deliberately not exposed as a Switch System Menu.
    // Enable this action only when the real HOS boot pipeline is connected.
    private var systemMenuReady: Bool {
        firmwareImported && prodKeysImported &&
        NexiliumEngine.systemMenuBootSupported
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

                        Button {} label: {
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

    }
}

@MainActor
private struct FirmwareInstallerView: View {
    @Environment(\.dismiss) private var dismiss
    @AppStorage("system.firmwareImported") private var firmwareImported = false
    @AppStorage("system.prodKeysImported") private var prodKeysImported = false
    @AppStorage("system.titleKeysImported") private var titleKeysImported = false
    @AppStorage("system.firmwareFilename") private var firmwareFilename = ""
    @AppStorage("system.firmwareNcaCount") private var firmwareNcaCount = 0
    @AppStorage("system.firmwareBytes") private var firmwareBytes = 0.0
    @AppStorage("system.prodKeysFilename") private var prodKeysFilename = ""
    @AppStorage("system.titleKeysFilename") private var titleKeysFilename = ""
    @State private var importTarget: ImportTarget = .firmware
    @State private var pickerMode: PickerMode?
    @State private var errorMessage: String?
    @State private var statusMessage: String?
    @State private var isInstallingFirmware = false

    private enum ImportTarget {
        case firmware, prodKeys, titleKeys

        var destinationName: String {
            switch self {
            case .firmware: return "firmware-package.zip"
            case .prodKeys: return "prod.keys"
            case .titleKeys: return "title.keys"
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

    private enum PickerMode: String, Identifiable {
        case file, folder
        var id: String { rawValue }
    }

    private var storageDirectory: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Nexilium", isDirectory: true)
    }

    var body: some View {
        Form {
            Section("Nintendo Switch firmware") {
                LabeledContent(
                    "Prepared firmware",
                    value: firmwareImported
                        ? "\(firmwareNcaCount) NCA files · \(ByteCountFormatter.string(fromByteCount: Int64(firmwareBytes), countStyle: .file))"
                        : "Not installed"
                )
                if isInstallingFirmware {
                    ProgressView("Preparing firmware…")
                } else {
                    importButtons(for: .firmware)
                }
                if firmwareImported {
                    Text(firmwareFilename)
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                        .lineLimit(2)
                }
            }

            Section("Keys") {
                LabeledContent("prod.keys", value: prodKeysImported ? "Imported" : "Not imported")
                importButtons(for: .prodKeys)

                LabeledContent("title.keys", value: titleKeysImported ? "Imported" : "Not imported")
                importButtons(for: .titleKeys)
            }

            Section {
                Text("Nexilium extracts NCA files and validates their encrypted headers. Firmware filesystem sections are not mounted until the section-decryption and filesystem layers are connected. prod.keys is checked before use; title.keys is optional for firmware headers.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }

            if let statusMessage {
                Section {
                    Text(statusMessage)
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
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
        .sheet(item: $pickerMode) { mode in
            NexiliumDocumentPicker(
                contentTypes: mode == .file ? [.item] : [.folder],
                asCopy: mode == .file,
                onPick: { url in
                    pickerMode = nil
                    handleImport(url, isCopiedIntoApp: mode == .file)
                },
                onFailure: { message in
                    pickerMode = nil
                    errorMessage = message
                }
            )
            .ignoresSafeArea()
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

    @ViewBuilder
    private func importButtons(for target: ImportTarget) -> some View {
        Button {
            importTarget = target
            errorMessage = nil
            statusMessage = nil
            pickerMode = .file
        } label: {
            Label("Choose file", systemImage: "doc")
        }
        .disabled(isInstallingFirmware)

        Button {
            importTarget = target
            errorMessage = nil
            statusMessage = nil
            pickerMode = .folder
        } label: {
            Label("Choose folder", systemImage: "folder")
        }
        .disabled(isInstallingFirmware)
    }

    private func handleImport(_ selectedURL: URL, isCopiedIntoApp: Bool) {
        do {
            try FileManager.default.createDirectory(
                at: storageDirectory,
                withIntermediateDirectories: true
            )

            let sourceURL: URL
            if isCopiedIntoApp {
                sourceURL = try resolveFile(from: selectedURL)
                try copyToAppStorage(sourceURL)
            } else {
                let hasAccess = selectedURL.startAccessingSecurityScopedResource()
                guard hasAccess else {
                    throw ImportError.noFolderAccess
                }
                defer { selectedURL.stopAccessingSecurityScopedResource() }

                var coordinationError: NSError?
                var importError: Error?
                NSFileCoordinator().coordinate(
                    readingItemAt: selectedURL,
                    options: [],
                    error: &coordinationError
                ) { coordinatedURL in
                    do {
                        let match = try resolveFile(from: coordinatedURL)
                        let childAccess = match.startAccessingSecurityScopedResource()
                        defer {
                            if childAccess { match.stopAccessingSecurityScopedResource() }
                        }
                        try copyToAppStorage(match)
                    } catch {
                        importError = error
                    }
                }

                if let importError { throw importError }
                if let coordinationError { throw coordinationError }
                sourceURL = selectedURL
            }

            switch importTarget {
            case .firmware:
                let archiveURL = storageDirectory.appendingPathComponent("firmware-package.zip")
                let rootDirectory = storageDirectory
                let keysURL = storageDirectory.appendingPathComponent("prod.keys")
                let selectedFilename = isCopiedIntoApp ? sourceURL.lastPathComponent : "firmware-package.zip"
                firmwareImported = false
                isInstallingFirmware = true
                statusMessage = "Decrypting NCA headers and preparing firmware content…"

                Task {
                    do {
                        let summary = try await Task.detached(priority: .userInitiated) {
                            try FirmwarePackageInstaller.install(
                                archiveURL: archiveURL,
                                prodKeysURL: keysURL,
                                storageDirectory: rootDirectory
                            )
                        }.value
                        firmwareFilename = selectedFilename
                        firmwareNcaCount = summary.ncaCount
                        firmwareBytes = Double(summary.totalBytes)
                        firmwareImported = true
                        statusMessage = "Validated \(summary.ncaCount) NCA headers, including \(summary.metadataNcaCount) metadata archives. Firmware content is prepared for the runtime."
                        isInstallingFirmware = false
                    } catch {
                        firmwareImported = false
                        statusMessage = nil
                        errorMessage = error.localizedDescription
                        isInstallingFirmware = false
                    }
                }

            case .prodKeys:
                let keysURL = storageDirectory.appendingPathComponent("prod.keys")
                let keyCount = try FirmwarePackageInstaller.validateProductionKeys(at: keysURL)
                prodKeysFilename = "prod.keys"
                prodKeysImported = true
                statusMessage = "Validated \(keyCount) production-key entries."

            case .titleKeys:
                let keysURL = storageDirectory.appendingPathComponent("title.keys")
                let keyCount = try FirmwarePackageInstaller.validateTitleKeys(at: keysURL)
                titleKeysFilename = "title.keys"
                titleKeysImported = true
                statusMessage = "Validated \(keyCount) title-key entries."
            }
            errorMessage = nil
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func copyToAppStorage(_ sourceURL: URL) throws {
        let destination = storageDirectory.appendingPathComponent(importTarget.destinationName)
        if FileManager.default.fileExists(atPath: destination.path) {
            try FileManager.default.removeItem(at: destination)
        }
        try FileManager.default.copyItem(at: sourceURL, to: destination)
    }

    private func resolveFile(from selectedURL: URL) throws -> URL {
        let values = try selectedURL.resourceValues(forKeys: [.isDirectoryKey])
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
                if !isDirectory && matchesExpectedFile(fileURL) {
                    matches.append(fileURL)
                }
            }
            guard !matches.isEmpty else { throw ImportError.fileNotFound(importTarget.displayName) }
            guard matches.count == 1, let match = matches.first else {
                throw ImportError.multipleFiles(importTarget.displayName)
            }
            return match
        }

        guard matchesExpectedFile(selectedURL) else {
            throw ImportError.fileNotFound(importTarget.displayName)
        }
        return selectedURL
    }

    private func matchesExpectedFile(_ url: URL) -> Bool {
        switch importTarget {
        case .firmware: return url.pathExtension.lowercased() == "zip"
        case .prodKeys: return url.lastPathComponent.lowercased() == "prod.keys"
        case .titleKeys: return url.lastPathComponent.lowercased() == "title.keys"
        }
    }

    private enum ImportError: LocalizedError {
        case cannotReadFolder, noFolderAccess
        case fileNotFound(String), multipleFiles(String)

        var errorDescription: String? {
            switch self {
            case .cannotReadFolder:
                return "Nexilium couldn't read that folder."
            case .noFolderAccess:
                return "iOS didn't grant access to that folder. Please choose it again."
            case .fileNotFound(let name):
                return "No \(name) was found in the selected location."
            case .multipleFiles(let name):
                return "More than one \(name) was found. Choose the exact file instead."
            }
        }
    }
}

private struct NexiliumDocumentPicker: UIViewControllerRepresentable {
    let contentTypes: [UTType]
    let asCopy: Bool
    let onPick: (URL) -> Void
    let onFailure: (String) -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator(onPick: onPick, onFailure: onFailure)
    }

    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let picker = UIDocumentPickerViewController(
            forOpeningContentTypes: contentTypes,
            asCopy: asCopy
        )
        picker.allowsMultipleSelection = false
        picker.delegate = context.coordinator
        return picker
    }

    func updateUIViewController(_ uiViewController: UIDocumentPickerViewController, context: Context) {}

    final class Coordinator: NSObject, UIDocumentPickerDelegate {
        let onPick: (URL) -> Void
        let onFailure: (String) -> Void

        init(onPick: @escaping (URL) -> Void, onFailure: @escaping (String) -> Void) {
            self.onPick = onPick
            self.onFailure = onFailure
        }

        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
            guard let url = urls.first else {
                onFailure("No file was returned by the Files app.")
                return
            }
            onPick(url)
        }

        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {}
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
