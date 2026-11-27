import SwiftUI

struct ControllerSettingsView: View {
    @AppStorage("controllers.enabled") private var controllerSupport = true
    @AppStorage("controllers.haptics") private var haptics = true
    @AppStorage("controllers.layout") private var buttonLayout = "Nintendo"

    var body: some View {
        NXPage {
            NXToggleRow(title: "Controller Support", isOn: $controllerSupport)

            NXToggleRow(
                title: "Controller Haptics",
                isEnabled: controllerSupport,
                isOn: $haptics
            )

            NXPickerRow(
                title: "Button Layout",
                isEnabled: controllerSupport,
                selection: $buttonLayout,
                options: ["Nintendo", "Xbox", "PlayStation"]
            ) { $0 }

            NXNote("Controller configuration will become active when input support is implemented.")
        }
    }
}
