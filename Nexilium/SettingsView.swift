import SwiftUI
import AppEntitlements

enum SettingsCategory: String, CaseIterable, Identifiable {
    case graphics = "Graphics"
    case controllers = "Controllers"
    case system = "System"
    case miscellaneous = "Miscellaneous"
    case about = "About Nexilium"
    var id: String { rawValue }
}

struct SettingsView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var selection: SettingsCategory?

    init(initialSelection: SettingsCategory = .graphics) {
        _selection = State(initialValue: initialSelection)
    }

    var body: some View {
        GeometryReader { proxy in
            VStack(spacing: 0) {
                HStack(spacing: 10) {
                    Image(systemName: "gearshape")
                        .font(.system(size: 23, weight: .light))
                    Text("System Settings")
                        .font(.system(size: 23, weight: .regular, design: .rounded))
                    Spacer()
                    Button("Done") { dismiss() }
                        .font(.system(size: 16, weight: .medium))
                }
                .padding(.horizontal, 28)
                .frame(height: 58)

                Rectangle()
                    .fill(Color.primary.opacity(0.14))
                    .frame(height: 1)
                    .padding(.horizontal, 28)

                HStack(spacing: 0) {
                    ScrollView {
                        VStack(spacing: 0) {
                            ForEach(SettingsCategory.allCases) { category in
                                Button {
                                    selection = category
                                } label: {
                                    HStack(spacing: 12) {
                                        Rectangle()
                                            .fill((selection ?? .graphics) == category ? Color.blue : Color.clear)
                                            .frame(width: 3)
                                        Text(category.rawValue)
                                            .font(.system(size: 16, weight: (selection ?? .graphics) == category ? .medium : .regular))
                                            .foregroundStyle((selection ?? .graphics) == category ? Color.blue : Color.primary)
                                        Spacer(minLength: 0)
                                    }
                                    .frame(height: 48)
                                    .contentShape(Rectangle())
                                }
                                .buttonStyle(.plain)
                            }
                        }
                        .padding(.top, 8)
                    }
                    .frame(width: proxy.size.width * 0.29)

                    Rectangle()
                        .fill(Color.primary.opacity(0.12))
                        .frame(width: 1)

                    Group {
                        switch selection ?? .graphics {
                        case .graphics: GraphicsSettingsView()
                        case .controllers: ControllerSettingsView()
                        case .system: SystemSettingsView()
                        case .miscellaneous: MiscSettingsView()
                        case .about: AboutView()
                        }
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
            .background(Color(uiColor: .systemBackground))
        }
        .preferredColorScheme(.light)
    }
}

private struct SettingsCategoryRow: View {
    let title: String
    let subtitle: String
    let symbol: String

    var body: some View {
        HStack(spacing: 14) {
            Image(systemName: symbol)
                .font(.system(size: 19, weight: .medium))
                .foregroundStyle(.primary)
                .frame(width: 30)

            VStack(alignment: .leading, spacing: 3) {
                Text(title)
                    .font(.body)
                Text(subtitle)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
            .padding(.vertical, 3)
        }
    }
}

private struct GraphicsSettingsView: View {
    @AppStorage("graphics.resolutionScale") private var resolutionScale = 1.0
    @AppStorage("graphics.vsync") private var verticalSync = true
    @AppStorage("graphics.shaderCache") private var shaderCache = true
    @AppStorage("graphics.showPerformanceOverlay") private var showPerformanceOverlay = false
    @AppStorage("graphics.ramLimitGB") private var ramLimitGB = 2.0

    private var ramLimitLabel: String {
        ramLimitGB.formatted(.number.precision(.fractionLength(0...1))) + " GB"
    }

    var body: some View {
        Form {
            Section("Rendering") {
                LabeledContent("Graphics API", value: "Metal")
                Text("Metal is the planned graphics backend for Nexilium.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }

            Section("Resolution") {
                Picker("Resolution scale", selection: $resolutionScale) {
                    Text("0.5×").tag(0.5)
                    Text("0.75×").tag(0.75)
                    Text("1×").tag(1.0)
                    Text("1.5×").tag(1.5)
                    Text("2×").tag(2.0)
                }
            }

            Section("Memory") {
                VStack(alignment: .leading, spacing: 8) {
                    HStack {
                        Text("RAM limit")
                        Spacer()
                        Text(ramLimitLabel)
                            .foregroundStyle(.secondary)
                            .monospacedDigit()
                    }

                    Slider(value: $ramLimitGB, in: 1...4, step: 0.5)
                }

                MemoryEntitlementStatusView()
            }

            Section("Performance") {
                Toggle("Vertical sync", isOn: $verticalSync)
                Toggle("Shader cache", isOn: $shaderCache)
                Toggle("Performance overlay", isOn: $showPerformanceOverlay)
            }
        }
        .navigationTitle("Graphics")
        .navigationBarTitleDisplayMode(.inline)
    }
}

private struct MemoryEntitlementStatusView: View {
    private let entitlementKey = "com.apple.developer.kernel.increased-memory-limit"

    private var status: String {
        do {
            guard let enabled = try AppEntitlements.getValue(
                for: entitlementKey,
                as: Bool.self
            ) else {
                return "Not present"
            }
            return enabled ? "Enabled" : "Disabled"
        } catch {
            return "Unavailable"
        }
    }

    var body: some View {
        LabeledContent("Increased memory entitlement", value: status)
    }
}

private struct ControllerSettingsView: View {
    @AppStorage("controllers.enabled") private var controllerSupport = true
    @AppStorage("controllers.haptics") private var haptics = true
    @AppStorage("controllers.layout") private var buttonLayout = "Nintendo"

    var body: some View {
        Form {
            Section("Input") {
                Toggle("Controller support", isOn: $controllerSupport)
                Toggle("Controller haptics", isOn: $haptics)
                    .disabled(!controllerSupport)
            }

            Section("Button layout") {
                Picker("Layout", selection: $buttonLayout) {
                    Text("Nintendo").tag("Nintendo")
                    Text("Xbox").tag("Xbox")
                    Text("PlayStation").tag("PlayStation")
                }
                .disabled(!controllerSupport)
            }

            Section {
                Text("Controller configuration will become active when input support is implemented.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
        .navigationTitle("Controllers")
        .navigationBarTitleDisplayMode(.inline)
    }
}

private struct SystemSettingsView: View {
    @AppStorage("system.region") private var region = "Auto"
    @AppStorage("system.language") private var language = "System Default"
    @AppStorage("system.dockedMode") private var dockedMode = false
    @AppStorage("system.audioEnabled") private var audioEnabled = true

    var body: some View {
        Form {
            Section("Console") {
                Picker("Region", selection: $region) {
                    Text("Automatic").tag("Auto")
                    Text("Japan").tag("Japan")
                    Text("North America").tag("North America")
                    Text("Europe").tag("Europe")
                }

                Picker("Language", selection: $language) {
                    Text("System Default").tag("System Default")
                    Text("English").tag("English")
                    Text("Español").tag("Español")
                }

                Toggle("Docked mode", isOn: $dockedMode)
            }

            Section("Audio") {
                Toggle("Enable audio", isOn: $audioEnabled)
            }

            Section {
                Text("These preferences are stored locally. System emulation settings will take effect as the emulator is developed.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
        .navigationTitle("System")
        .navigationBarTitleDisplayMode(.inline)
    }
}

private struct MiscSettingsView: View {
    @AppStorage("misc.autoSave") private var autoSave = true
    @AppStorage("misc.confirmBeforeReset") private var confirmBeforeReset = true
    @AppStorage("misc.showNotifications") private var showNotifications = true

    var body: some View {
        Form {
            Section("General") {
                Toggle("Automatic save data", isOn: $autoSave)
                Toggle("Confirm before clearing data", isOn: $confirmBeforeReset)
                Toggle("In-app notifications", isOn: $showNotifications)
            }

            Section {
                Text("Additional options will appear here as Nexilium gains more features.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
        .navigationTitle("Miscellaneous")
        .navigationBarTitleDisplayMode(.inline)
    }
}

private struct AboutView: View {
    private var appVersion: String {
        Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "1.0.0"
    }

    var body: some View {
        Form {
            Section {
                HStack(spacing: 14) {
                    Image("NexiliumIcon")
                        .resizable()
                        .scaledToFit()
                        .frame(width: 54, height: 54)
                        .clipShape(RoundedRectangle(cornerRadius: 13))

                    VStack(alignment: .leading, spacing: 4) {
                        Text("Nexilium")
                            .font(.title3.weight(.semibold))
                        Text("Nintendo Switch emulator for iOS")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                }
                .padding(.vertical, 6)
            }

            Section("Version") {
                LabeledContent("App version", value: appVersion)
                LabeledContent("Minimum iOS", value: "18.0")
            }

            Section("Project") {
                LabeledContent("Developer", value: "Silicon Team")
                Link(destination: URL(string: "https://github.com/caIyxio/Nexilium")!) {
                    Label("Source code on GitHub", systemImage: "chevron.left.forwardslash.chevron.right")
                }
            }

            Section {
                Text("Nexilium is an independent open-source project. Nintendo and Nintendo Switch are trademarks of their respective owners.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
        .navigationTitle("About")
        .navigationBarTitleDisplayMode(.inline)
    }
}
