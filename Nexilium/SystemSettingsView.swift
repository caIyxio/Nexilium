import SwiftUI

struct SystemSettingsView: View {
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
