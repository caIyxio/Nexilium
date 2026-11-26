import SwiftUI
import LucideSwift

struct ConsoleHomeView: View {
    var onFirmware: (() -> Void)? = nil
    var onExit: (() -> Void)? = nil
    @State private var showingMiiCreator = false
    @State private var showingSettings = false
    @State private var settingsCategory: SettingsCategory = .graphics

    var body: some View {
        GeometryReader { geometry in
            ZStack {
                Color(red: 0.91, green: 0.92, blue: 0.94).ignoresSafeArea()

                VStack(spacing: 0) {
                    HStack {
                        Text("NEXILIUM")
                            .font(.system(size: 13, weight: .bold, design: .rounded))
                            .tracking(3)
                            .foregroundStyle(Color(red: 0.28, green: 0.38, blue: 0.52))
                        Spacer()
                        Text("HOME")
                            .font(.system(size: 11, weight: .semibold))
                            .tracking(1.8)
                            .foregroundStyle(.secondary)
                    }
                    .padding(.horizontal, 34)
                    .padding(.top, 10)

                    Spacer(minLength: 12)

                    HStack(spacing: 14) {
                        VStack(alignment: .leading, spacing: 9) {
                            Text("Games")
                                .font(.system(size: 15, weight: .semibold))
                                .foregroundStyle(Color.primary.opacity(0.8))
                            Text("No games installed")
                                .font(.system(size: 13))
                                .foregroundStyle(.secondary)
                        }
                        .frame(width: 190, alignment: .leading)

                        Spacer(minLength: 0)
                    }
                    .padding(.horizontal, 34)

                    Spacer(minLength: 16)

                    HStack(spacing: 0) {
                        menuButton("Mii Creator", icon: .smile) { showingMiiCreator = true }
                        menuButton("Firmware", icon: .folderDown) { onFirmware?() }
                        menuButton("Controllers", icon: .gamepad2) { settingsCategory = .controllers; showingSettings = true }
                        menuButton("Settings", icon: .settings) { settingsCategory = .graphics; showingSettings = true }
                        if let onExit {
                            menuButton("Exit", icon: .power) { onExit() }
                        }
                    }
                    .padding(.horizontal, 24)
                    .padding(.vertical, 10)
                    .background(.white.opacity(0.78), in: Capsule())
                    .overlay(Capsule().stroke(.white, lineWidth: 1))
                    .padding(.horizontal, 34)
                    .padding(.bottom, 18)
                }
                .frame(width: geometry.size.width, height: geometry.size.height)
            }
        }
        .preferredColorScheme(.light)
        .fullScreenCover(isPresented: $showingMiiCreator) {
            NavigationStack { MiiCreatorView() }
        }
        .fullScreenCover(isPresented: $showingSettings) {
            SettingsView(initialSelection: settingsCategory)
        }
    }

    private func menuButton(_ title: String, icon: LucideIconName, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(spacing: 7) {
                LucideIcon(icon)
                    .frame(width: 23, height: 23)
                Text(title)
                    .font(.system(size: 10, weight: .medium))
            }
            .foregroundStyle(Color(red: 0.35, green: 0.43, blue: 0.54))
            .frame(maxWidth: .infinity)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

private struct ConsoleTile: View {
    let title: String
    let subtitle: String
    let symbol: String
    let tint: Color
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            VStack(alignment: .leading, spacing: 12) {
                RoundedRectangle(cornerRadius: 16)
                    .fill(tint.gradient)
                    .frame(height: 108)
                    .overlay {
                        Image(systemName: symbol)
                            .font(.system(size: 38, weight: .medium))
                            .foregroundStyle(.white)
                    }

                VStack(alignment: .leading, spacing: 4) {
                    Text(title)
                        .font(.system(size: 15, weight: .semibold))
                        .foregroundStyle(.white)
                    Text(subtitle)
                        .font(.system(size: 11))
                        .foregroundStyle(.white.opacity(0.55))
                        .lineLimit(1)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

private enum ConsoleSection: String, Identifiable {
    case album, controllers, settings
    var id: String { rawValue }
    var title: String {
        switch self {
        case .album: "Album"
        case .controllers: "Controllers"
        case .settings: "Settings"
        }
    }
    var message: String {
        switch self {
        case .album: "The capture gallery will be connected when the emulator runtime supports screenshots."
        case .controllers: "Controller management is not connected yet."
        case .settings: "Console settings will be connected to the system runtime in a later engine milestone."
        }
    }
}

struct MiiProfile: Identifiable, Codable {
    var id = UUID()
    var name: String
    var face: String
    var skin: String
    var hair: String
    var hairColor: String
    var eyes: String
    var mouth: String
}

struct MiiCreatorView: View {
    @Environment(\.dismiss) private var dismiss
    @AppStorage("miis.savedProfiles") private var savedProfilesData = "[]"
    @State private var name = "Mii"
    @State private var face = "Round"
    @State private var skin = "Light"
    @State private var hair = "Short"
    @State private var hairColor = "Brown"
    @State private var eyes = "Default"
    @State private var mouth = "Smile"

    private var savedProfiles: [MiiProfile] {
        (try? JSONDecoder().decode([MiiProfile].self, from: Data(savedProfilesData.utf8))) ?? []
    }

    var body: some View {
        Form {
            Section {
                MiiAvatarPreview(face: face, skin: skin, hair: hair, hairColor: hairColor, eyes: eyes, mouth: mouth)
                    .frame(maxWidth: .infinity)
                    .listRowBackground(Color.clear)
                    .padding(.vertical, 14)
            }

            Section("Profile") {
                TextField("Mii name", text: $name)
                    .textInputAutocapitalization(.words)
                    .autocorrectionDisabled()
            }

            Section("Appearance") {
                Picker("Face shape", selection: $face) {
                    Text("Round").tag("Round")
                    Text("Oval").tag("Oval")
                    Text("Square").tag("Square")
                }
                Picker("Skin tone", selection: $skin) {
                    Text("Light").tag("Light")
                    Text("Medium").tag("Medium")
                    Text("Tan").tag("Tan")
                    Text("Deep").tag("Deep")
                }
                Picker("Hair style", selection: $hair) {
                    Text("Short").tag("Short")
                    Text("Long").tag("Long")
                    Text("Spiky").tag("Spiky")
                    Text("Bald").tag("Bald")
                }
                Picker("Hair color", selection: $hairColor) {
                    Text("Black").tag("Black")
                    Text("Brown").tag("Brown")
                    Text("Blonde").tag("Blonde")
                    Text("Red").tag("Red")
                }
                Picker("Eyes", selection: $eyes) {
                    Text("Default").tag("Default")
                    Text("Round").tag("Round")
                    Text("Narrow").tag("Narrow")
                }
                Picker("Mouth", selection: $mouth) {
                    Text("Smile").tag("Smile")
                    Text("Neutral").tag("Neutral")
                    Text("Surprised").tag("Surprised")
                }
            }

            Section {
                Button {
                    saveMii()
                } label: {
                    Label("Save Mii", systemImage: "checkmark.circle.fill")
                        .frame(maxWidth: .infinity)
                }
                .disabled(name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
            }

            Section("Saved Miis") {
                if savedProfiles.isEmpty {
                    Text("No Miis saved yet.")
                        .foregroundStyle(.secondary)
                } else {
                    ForEach(savedProfiles) { profile in
                        HStack {
                            MiiAvatarPreview(face: profile.face, skin: profile.skin, hair: profile.hair, hairColor: profile.hairColor, eyes: profile.eyes, mouth: profile.mouth)
                                .frame(width: 48, height: 58)
                            Text(profile.name)
                            Spacer()
                        }
                    }
                    .onDelete(perform: deleteMiis)
                }
            }
        }
        .navigationTitle("Mii Creator")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItem(placement: .confirmationAction) {
                Button("Done") { dismiss() }
            }
        }
    }

    private func saveMii() {
        var profiles = savedProfiles
        profiles.append(MiiProfile(
            name: name.trimmingCharacters(in: .whitespacesAndNewlines),
            face: face,
            skin: skin,
            hair: hair,
            hairColor: hairColor,
            eyes: eyes,
            mouth: mouth
        ))
        if let data = try? JSONEncoder().encode(profiles) {
            savedProfilesData = String(decoding: data, as: UTF8.self)
        }
    }

    private func deleteMiis(at offsets: IndexSet) {
        var profiles = savedProfiles
        profiles.remove(atOffsets: offsets)
        if let data = try? JSONEncoder().encode(profiles) {
            savedProfilesData = String(decoding: data, as: UTF8.self)
        }
    }
}

private struct MiiAvatarPreview: View {
    let face: String
    let skin: String
    let hair: String
    let hairColor: String
    let eyes: String
    let mouth: String

    private var skinColor: Color {
        switch skin {
        case "Medium": Color(red: 0.88, green: 0.67, blue: 0.49)
        case "Tan": Color(red: 0.69, green: 0.45, blue: 0.29)
        case "Deep": Color(red: 0.40, green: 0.25, blue: 0.18)
        default: Color(red: 1.0, green: 0.83, blue: 0.68)
        }
    }

    private var hairTint: Color {
        switch hairColor {
        case "Black": .black
        case "Blonde": Color(red: 0.88, green: 0.70, blue: 0.28)
        case "Red": Color(red: 0.55, green: 0.20, blue: 0.13)
        default: Color(red: 0.30, green: 0.18, blue: 0.12)
        }
    }

    var body: some View {
        VStack(spacing: 0) {
            ZStack(alignment: .top) {
                faceShape
                    .fill(skinColor)
                    .overlay {
                        VStack(spacing: 17) {
                            HStack(spacing: 25) {
                                eye
                                eye
                            }
                            mouthShape
                        }
                        .padding(.top, 53)
                    }
                if hair != "Bald" {
                    hairShape
                        .fill(hairTint)
                        .frame(height: hair == "Spiky" ? 34 : 25)
                        .padding(.top, 2)
                }
            }
            .frame(width: 116, height: 142)
            .shadow(color: .black.opacity(0.12), radius: 5, y: 3)
        }
        .frame(maxWidth: .infinity)
        .accessibilityLabel("Mii appearance preview")
    }

    private var faceShape: some Shape {
        switch face {
        case "Oval": AnyShape(Ellipse())
        case "Square": AnyShape(RoundedRectangle(cornerRadius: 25))
        default: AnyShape(Capsule())
        }
    }

    private var eye: some View {
        Capsule()
            .fill(Color.black.opacity(0.78))
            .frame(width: eyes == "Narrow" ? 13 : 9, height: eyes == "Round" ? 13 : 8)
    }

    @ViewBuilder
    private var mouthShape: some View {
        switch mouth {
        case "Neutral":
            Capsule().fill(Color(red: 0.48, green: 0.18, blue: 0.18)).frame(width: 20, height: 3)
        case "Surprised":
            Ellipse().fill(Color(red: 0.48, green: 0.18, blue: 0.18)).frame(width: 10, height: 14)
        default:
            Capsule().fill(Color(red: 0.48, green: 0.18, blue: 0.18)).frame(width: 23, height: 7)
        }
    }

    private var hairShape: some Shape {
        UnevenRoundedRectangle(cornerRadii: RectangleCornerRadii(
            topLeading: hair == "Spiky" ? 2 : 22,
            bottomLeading: 8,
            bottomTrailing: 8,
            topTrailing: hair == "Spiky" ? 2 : 22
        ))
    }
}
