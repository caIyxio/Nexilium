import SwiftUI
import AppEntitlements

enum SettingsCategory: String, CaseIterable, Identifiable {
    case graphics = "Graphics"
    case controllers = "Controllers"
    case system = "System"
    case miscellaneous = "Miscellaneous"
    case about = "About Nexilium"
    var id: String { rawValue }
}

struct SettingsView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var selection: SettingsCategory?

    init(initialSelection: SettingsCategory = .graphics) {
        _selection = State(initialValue: initialSelection)
    }

    var body: some View {
        GeometryReader { proxy in
            VStack(spacing: 0) {
                HStack(spacing: 10) {
                    Image(systemName: "gearshape")
                        .font(.system(size: 23, weight: .light))
                    Text("System Settings")
                        .font(.system(size: 23, weight: .regular, design: .rounded))
                    Spacer()
                    Button("Done") { dismiss() }
                        .font(.system(size: 16, weight: .medium))
                }
                .padding(.horizontal, 28)
                .frame(height: 58)

                Rectangle()
                    .fill(Color.primary.opacity(0.14))
                    .frame(height: 1)
                    .padding(.horizontal, 28)

                HStack(spacing: 0) {
                    ScrollView {
                        VStack(spacing: 0) {
                            ForEach(SettingsCategory.allCases) { category in
                                Button {
                                    selection = category
                                } label: {
                                    HStack(spacing: 12) {
                                        Rectangle()
                                            .fill((selection ?? .graphics) == category ? Color.blue : Color.clear)
                                            .frame(width: 3)
                                        Text(category.rawValue)
                                            .font(.system(size: 16, weight: (selection ?? .graphics) == category ? .medium : .regular))
                                            .foregroundStyle((selection ?? .graphics) == category ? Color.blue : Color.primary)
                                        Spacer(minLength: 0)
                                    }
                                    .frame(height: 48)
                                    .contentShape(Rectangle())
                                }
                                .buttonStyle(.plain)
                            }
                        }
                        .padding(.top, 8)
                    }
                    .frame(width: proxy.size.width * 0.29)

                    Rectangle()
                        .fill(Color.primary.opacity(0.12))
                        .frame(width: 1)

                    Group {
                        switch selection ?? .graphics {
                        case .graphics: GraphicsSettingsView()
                        case .controllers: ControllerSettingsView()
                        case .system: SystemSettingsView()
                        case .miscellaneous: MiscSettingsView()
                        case .about: AboutView()
                        }
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
            .background(Color(uiColor: .systemBackground))
        }
        .preferredColorScheme(.light)
    }
}
