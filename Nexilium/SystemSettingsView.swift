import SwiftUI

struct SystemSettingsView: View {
    @AppStorage("system.region") private var region = "Auto"
    @AppStorage("system.language") private var language = "System Default"
    @AppStorage("system.dockedMode") private var dockedMode = false
    @AppStorage("system.audioEnabled") private var audioEnabled = true

    var body: some View {
        NXPage {
            NXPickerRow(
                title: "Region",
                selection: $region,
                options: ["Auto", "Japan", "North America", "Europe"]
            ) { $0 == "Auto" ? "Automatic" : $0 }

            NXPickerRow(
                title: "Language",
                selection: $language,
                options: ["System Default", "English", "Español"]
            ) { $0 }

            NXToggleRow(title: "Docked Mode", isOn: $dockedMode)
            NXToggleRow(title: "Audio", isOn: $audioEnabled)

            NXNote("These preferences are stored locally. System emulation settings will take effect as the emulator is developed.")
        }
    }
}
