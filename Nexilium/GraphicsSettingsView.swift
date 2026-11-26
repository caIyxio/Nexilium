import SwiftUI
import AppEntitlements

struct GraphicsSettingsView: View {
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
