import SwiftUI

struct MiscSettingsView: View {
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
