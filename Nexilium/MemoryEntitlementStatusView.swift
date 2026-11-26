import SwiftUI
import AppEntitlements

struct MemoryEntitlementStatusView: View {
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
