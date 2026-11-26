import SwiftUI

struct SettingsView: View {
    var body: some View {
        List {
            Section {
                NavigationLink {
                    GraphicsSettingsView()
                } label: {
                    SettingsCategoryRow(
                        title: "Graphics",
                        subtitle: "Rendering, resolution and performance",
                        symbol: "gpu"
                    )
                }

                NavigationLink {
                    ControllerSettingsView()
                } label: {
                    SettingsCategoryRow(
                        title: "Controllers",
                        subtitle: "Input devices and button layout",
                        symbol: "gamecontroller"
                    )
                }

                NavigationLink {
                    SystemSettingsView()
                } label: {
                    SettingsCategoryRow(
                        title: "System",
                        subtitle: "Region, language and audio",
                        symbol: "cpu"
                    )
                }

                NavigationLink {
                    MiscSettingsView()
                } label: {
                    SettingsCategoryRow(
                        title: "Miscellaneous",
                        subtitle: "Overlays and save preferences",
                        symbol: "slider.horizontal.3"
                    )
                }
            }

            Section {
                NavigationLink {
                    AboutView()
                } label: {
                    SettingsCategoryRow(
                        title: "About Nexilium",
                        subtitle: "Version, project and acknowledgements",
                        symbol: "info.circle"
                    )
                }
            }
        }
        .navigationTitle("Settings")
        .navigationBarTitleDisplayMode(.large)
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
    var body: some View {
        Form {
            Section {
                HStack(spacing: 14) {
                    Image(systemName: "gamecontroller.fill")
                        .font(.system(size: 30))
                        .frame(width: 54, height: 54)
                        .background(.quaternary, in: RoundedRectangle(cornerRadius: 13))

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
                LabeledContent("App version", value: "Early development")
                LabeledContent("Minimum iOS", value: "18.0")
            }

            Section("Project") {
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
