import SwiftUI

struct ControllerSettingsView: View {
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
