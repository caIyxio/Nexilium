import SwiftUI

struct GraphicsSettingsView: View {
    @AppStorage("graphics.resolutionScale") private var resolutionScale = 1.0
    @AppStorage("graphics.vsync") private var verticalSync = true
    @AppStorage("graphics.shaderCache") private var shaderCache = true
    @AppStorage("graphics.showPerformanceOverlay") private var showPerformanceOverlay = false
    @AppStorage("graphics.ramLimitGB") private var ramLimitGB = 2.0

    var body: some View {
        NXPage {
            NXInfoRow(
                title: "Graphics API",
                value: "Metal",
                detail: "Metal is the planned graphics backend for Nexilium."
            )

            NXPickerRow(
                title: "Resolution Scale",
                selection: $resolutionScale,
                options: [0.5, 0.75, 1.0, 1.5, 2.0]
            ) { String(format: "%g×", $0) }

            NXPickerRow(
                title: "RAM Limit",
                selection: $ramLimitGB,
                options: [1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0]
            ) { String(format: "%g GB", $0) }

            MemoryEntitlementStatusView()

            NXToggleRow(title: "Vertical Sync", isOn: $verticalSync)
            NXToggleRow(title: "Shader Cache", isOn: $shaderCache)
            NXToggleRow(title: "Performance Overlay", isOn: $showPerformanceOverlay)
        }
    }
}
