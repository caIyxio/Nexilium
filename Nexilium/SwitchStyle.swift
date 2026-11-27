import SwiftUI

// MARK: - Theme

enum NXTheme {
    static let background = Color(red: 0.925, green: 0.925, blue: 0.929)
    static let accent = Color(red: 0.12, green: 0.38, blue: 0.82)
    static let text = Color(red: 0.14, green: 0.14, blue: 0.15)
    static let secondary = Color(red: 0.45, green: 0.45, blue: 0.47)
    static let divider = Color.black.opacity(0.11)

    static var focusGradient: LinearGradient {
        LinearGradient(
            colors: [
                Color(red: 0.93, green: 0.45, blue: 0.85),
                Color(red: 0.45, green: 0.55, blue: 0.98),
                Color(red: 0.25, green: 0.85, blue: 0.95)
            ],
            startPoint: .topLeading,
            endPoint: .bottomTrailing
        )
    }
}

// MARK: - Focus handling (highlights the row you last tapped)

private struct NXFocusKey: EnvironmentKey {
    static let defaultValue: Binding<String?> = .constant(nil)
}

extension EnvironmentValues {
    var nxFocus: Binding<String?> {
        get { self[NXFocusKey.self] }
        set { self[NXFocusKey.self] = newValue }
    }
}

// MARK: - Page container

struct NXPage<Content: View>: View {
    @State private var focus: String?
    private let content: Content

    init(@ViewBuilder content: () -> Content) {
        self.content = content()
    }

    var body: some View {
        ScrollView {
            VStack(spacing: 0) {
                content
            }
            .padding(.horizontal, 24)
            .padding(.vertical, 8)
        }
        .environment(\.nxFocus, $focus)
    }
}

// MARK: - Row building blocks

struct NXRow<Content: View>: View {
    let id: String
    let detail: String?
    let isEnabled: Bool
    private let content: Content
    @Environment(\.nxFocus) private var focus

    init(id: String, detail: String? = nil, isEnabled: Bool = true, @ViewBuilder content: () -> Content) {
        self.id = id
        self.detail = detail
        self.isEnabled = isEnabled
        self.content = content()
    }

    var body: some View {
        let isFocused = focus.wrappedValue == id
        VStack(alignment: .leading, spacing: 0) {
            content
                .background(RoundedRectangle(cornerRadius: 7).fill(isFocused ? Color.white : Color.clear))
                .overlay(
                    RoundedRectangle(cornerRadius: 7)
                        .strokeBorder(NXTheme.focusGradient, lineWidth: 3)
                        .opacity(isFocused ? 1 : 0)
                )
                .shadow(color: Color.black.opacity(isFocused ? 0.12 : 0), radius: 4, y: 2)
                .simultaneousGesture(TapGesture().onEnded {
                    if isEnabled { focus.wrappedValue = id }
                })

            if let detail {
                Text(detail)
                    .font(.system(size: 14))
                    .foregroundStyle(NXTheme.secondary)
                    .padding(.horizontal, 18)
                    .padding(.vertical, 8)
            }

            Rectangle()
                .fill(NXTheme.divider)
                .frame(height: 1)
                .opacity(isFocused ? 0 : 1)
        }
        .opacity(isEnabled ? 1 : 0.45)
        .animation(.easeOut(duration: 0.12), value: isFocused)
    }
}

struct NXRowLabel<Trailing: View>: View {
    let title: String
    private let trailing: Trailing

    init(_ title: String, @ViewBuilder trailing: () -> Trailing) {
        self.title = title
        self.trailing = trailing()
    }

    var body: some View {
        HStack(spacing: 12) {
            Text(title)
                .font(.system(size: 18))
                .foregroundStyle(NXTheme.text)
            Spacer(minLength: 12)
            trailing
        }
        .padding(.horizontal, 18)
        .frame(maxWidth: .infinity, minHeight: 54, alignment: .leading)
        .contentShape(Rectangle())
    }
}

struct NXSwitch: View {
    let isOn: Bool

    var body: some View {
        ZStack(alignment: isOn ? .trailing : .leading) {
            Capsule().fill(isOn ? NXTheme.accent : Color(white: 0.80))
            Circle()
                .fill(Color.white)
                .frame(width: 18, height: 18)
                .padding(.horizontal, 2)
                .shadow(color: .black.opacity(0.2), radius: 1, y: 1)
        }
        .frame(width: 42, height: 22)
        .animation(.easeInOut(duration: 0.15), value: isOn)
    }
}

// MARK: - Ready-made rows

struct NXToggleRow: View {
    let title: String
    var detail: String? = nil
    var isEnabled: Bool = true
    @Binding var isOn: Bool

    var body: some View {
        NXRow(id: title, detail: detail, isEnabled: isEnabled) {
            Button {
                isOn.toggle()
            } label: {
                NXRowLabel(title) { NXSwitch(isOn: isOn) }
            }
            .buttonStyle(.plain)
            .disabled(!isEnabled)
            .accessibilityValue(isOn ? "On" : "Off")
        }
    }
}

struct NXPickerRow<Value: Hashable>: View {
    let title: String
    let detail: String?
    let isEnabled: Bool
    @Binding var selection: Value
    let options: [Value]
    let label: (Value) -> String

    init(
        title: String,
        detail: String? = nil,
        isEnabled: Bool = true,
        selection: Binding<Value>,
        options: [Value],
        label: @escaping (Value) -> String
    ) {
        self.title = title
        self.detail = detail
        self.isEnabled = isEnabled
        self._selection = selection
        self.options = options
        self.label = label
    }

    var body: some View {
        NXRow(id: title, detail: detail, isEnabled: isEnabled) {
            Menu {
                Picker(title, selection: $selection) {
                    ForEach(options, id: \.self) { option in
                        Text(label(option)).tag(option)
                    }
                }
            } label: {
                NXRowLabel(title) {
                    Text(label(selection))
                        .font(.system(size: 18))
                        .foregroundStyle(NXTheme.accent)
                }
            }
            .disabled(!isEnabled)
        }
    }
}

struct NXInfoRow: View {
    let title: String
    let value: String
    var detail: String? = nil

    var body: some View {
        NXRow(id: title, detail: detail) {
            NXRowLabel(title) {
                Text(value)
                    .font(.system(size: 18))
                    .foregroundStyle(NXTheme.secondary)
            }
        }
    }
}

struct NXLinkRow: View {
    let title: String
    let url: URL

    var body: some View {
        NXRow(id: title) {
            Link(destination: url) {
                NXRowLabel(title) {
                    Image(systemName: "arrow.up.right")
                        .font(.system(size: 16, weight: .semibold))
                        .foregroundStyle(NXTheme.accent)
                }
            }
        }
    }
}

struct NXNote: View {
    let text: String

    init(_ text: String) {
        self.text = text
    }

    var body: some View {
        Text(text)
            .font(.system(size: 14))
            .foregroundStyle(NXTheme.secondary)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, 18)
            .padding(.vertical, 12)
    }
}

// MARK: - Bottom bar hint (B Back / A OK)

struct NXButtonHint: View {
    let letter: String
    let label: String

    init(_ letter: String, _ label: String) {
        self.letter = letter
        self.label = label
    }

    var body: some View {
        HStack(spacing: 7) {
            Text(letter)
                .font(.system(size: 13, weight: .heavy))
                .foregroundStyle(Color.white)
                .frame(width: 24, height: 24)
                .background(Circle().fill(Color.black))
            Text(label)
                .font(.system(size: 20))
                .foregroundStyle(NXTheme.text)
        }
        .contentShape(Rectangle())
    }
}
