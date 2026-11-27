import SwiftUI

struct MiscSettingsView: View {
    @AppStorage("misc.autoSave") private var autoSave = true
    @AppStorage("misc.confirmBeforeReset") private var confirmBeforeReset = true
    @AppStorage("misc.showNotifications") private var showNotifications = true

    var body: some View {
        NXPage {
            NXToggleRow(title: "Automatic Save Data", isOn: $autoSave)
            NXToggleRow(title: "Confirm Before Clearing Data", isOn: $confirmBeforeReset)
            NXToggleRow(title: "In-App Notifications", isOn: $showNotifications)

            NXNote("Additional options will appear here as Nexilium gains more features.")
        }
    }
}
