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
    @State private var selection: SettingsCategory

    init(initialSelection: SettingsCategory = .graphics) {
        _selection = State(initialValue: initialSelection)
    }

    var body: some View {
        GeometryReader { proxy in
            VStack(spacing: 0) {
                header

                Rectangle()
                    .fill(NXTheme.divider)
                    .frame(height: 1)
                    .padding(.horizontal, 32)

                HStack(spacing: 0) {
                    sidebar
                        .frame(width: proxy.size.width * 0.29)

                    Rectangle()
                        .fill(NXTheme.divider)
                        .frame(width: 1)
                        .padding(.vertical, 8)

                    detail
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                }

                bottomBar
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(NXTheme.background.ignoresSafeArea())
        }
        .preferredColorScheme(.light)
    }

    private var header: some View {
        HStack(spacing: 12) {
            Image(systemName: "gearshape")
                .font(.system(size: 26, weight: .light))
            Text("System Settings")
                .font(.system(size: 26))
            Spacer()
        }
        .foregroundStyle(NXTheme.text)
        .padding(.horizontal, 32)
        .frame(height: 62)
    }

    private var sidebar: some View {
        ScrollView {
            VStack(spacing: 0) {
                ForEach(SettingsCategory.allCases) { category in
                    sidebarRow(category)
                    Rectangle()
                        .fill(NXTheme.divider)
                        .frame(height: 1)
                }
            }
            .padding(.leading, 32)
            .padding(.trailing, 8)
            .padding(.top, 6)
        }
    }

    private func sidebarRow(_ category: SettingsCategory) -> some View {
        let isSelected = category == selection
        return Button {
            selection = category
        } label: {
            HStack(spacing: 0) {
                Rectangle()
                    .fill(isSelected ? NXTheme.accent : Color.clear)
                    .frame(width: 3, height: 30)
                Text(category.rawValue)
                    .font(.system(size: 18))
                    .foregroundStyle(isSelected ? NXTheme.accent : NXTheme.text)
                    .padding(.leading, 14)
                Spacer(minLength: 0)
            }
            .frame(height: 50)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }

    @ViewBuilder
    private var detail: some View {
        Group {
            switch selection {
            case .graphics: GraphicsSettingsView()
            case .controllers: ControllerSettingsView()
            case .system: SystemSettingsView()
            case .miscellaneous: MiscSettingsView()
            case .about: AboutView()
            }
        }
        .id(selection)
    }

    private var bottomBar: some View {
        VStack(spacing: 0) {
            Rectangle()
                .fill(NXTheme.divider)
                .frame(height: 1)
                .padding(.horizontal, 32)

            HStack(spacing: 28) {
                Image(systemName: "gamecontroller")
                    .font(.system(size: 22))
                    .foregroundStyle(NXTheme.secondary)
                Spacer()
                Button { dismiss() } label: { NXButtonHint("B", "Back") }
                    .buttonStyle(.plain)
                Button { dismiss() } label: { NXButtonHint("A", "OK") }
                    .buttonStyle(.plain)
            }
            .padding(.horizontal, 32)
            .frame(height: 54)
        }
    }
}
