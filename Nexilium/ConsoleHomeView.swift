import SwiftUI
import UIKit

struct ConsoleHomeView: View {
    var onFirmware: (() -> Void)? = nil
    var onExit: (() -> Void)? = nil
    @State private var showingMiiCreator = false
    @State private var showingSettings = false
    @State private var settingsCategory: SettingsCategory = .graphics
    @State private var selectedMenu: HomeMenuItem = .mii
    @AppStorage("miis.savedProfiles") private var savedProfilesData = "[]"

    private let background = Color(red: 0.925, green: 0.933, blue: 0.949)
    private let ink = Color(red: 0.31, green: 0.40, blue: 0.54)

    private var savedProfiles: [MiiProfile] {
        (try? JSONDecoder().decode([MiiProfile].self, from: Data(savedProfilesData.utf8))) ?? []
    }

    var body: some View {
        GeometryReader { proxy in
            ZStack {
                background.ignoresSafeArea()

                VStack(spacing: 0) {
                    topBar
                        .padding(.horizontal, proxy.size.width * 0.04)
                        .padding(.top, max(proxy.safeAreaInsets.top, 12))

                    Spacer(minLength: 12)

                    gamesArea
                        .padding(.horizontal, proxy.size.width * 0.08)
                        .frame(height: proxy.size.height * 0.39)

                    Spacer(minLength: 12)

                    bottomDock
                        .padding(.horizontal, proxy.size.width * 0.16)
                        .padding(.bottom, max(proxy.safeAreaInsets.bottom + 14, 22))
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .ignoresSafeArea()
        .preferredColorScheme(.light)
        .statusBarHidden(false)
        .onAppear { LandscapeOrientationController().setLandscape() }
        .fullScreenCover(isPresented: $showingMiiCreator) {
            NavigationStack { MiiCreatorView() }
        }
        .fullScreenCover(isPresented: $showingSettings) {
            SettingsView(initialSelection: settingsCategory)
        }
    }

    private var topBar: some View {
        HStack(spacing: 10) {
            ForEach(Array(savedProfiles.prefix(3))) { profile in
                MiiAvatarPreview(
                    face: profile.face, skin: profile.skin, hair: profile.hair,
                    hairColor: profile.hairColor, eyes: profile.eyes, mouth: profile.mouth
                )
                .frame(width: 34, height: 38)
                .clipShape(Circle())
                .overlay(Circle().stroke(Color.white, lineWidth: 2))
                .accessibilityLabel(profile.name)
            }
            Spacer()
            // iOS owns the status indicators; Nexilium does not draw pretend Wi-Fi or battery values.
        }
        .frame(height: 42)
    }

    private var gamesArea: some View {
        HStack(spacing: 12) {
            VStack(alignment: .leading, spacing: 8) {
                Text("Games")
                    .font(.system(size: 25, weight: .semibold, design: .rounded))
                    .foregroundStyle(Color.primary.opacity(0.82))
                Text("No games installed")
                    .font(.system(size: 19, weight: .regular, design: .rounded))
                    .foregroundStyle(Color.secondary)
            }
            Spacer(minLength: 0)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .leading)
    }

    private var bottomDock: some View {
        VStack(spacing: 7) {
            HStack(spacing: 0) {
                dockButton(.mii, symbol: "face.smiling", tint: Color(red: 0.35, green: 0.43, blue: 0.57))
                dockButton(.firmware, symbol: "folder.badge.plus", tint: Color(red: 0.91, green: 0.57, blue: 0.18))
                dockButton(.controllers, symbol: "gamecontroller", tint: Color(red: 0.31, green: 0.62, blue: 0.39))
                dockButton(.settings, symbol: "gearshape", tint: Color(red: 0.35, green: 0.48, blue: 0.68))
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 7)
            .background(Color.white.opacity(0.78), in: Capsule())
            .overlay(Capsule().stroke(Color.white.opacity(0.95), lineWidth: 1.5))

            Text(selectedMenu.label)
                .font(.system(size: 13, weight: .medium, design: .rounded))
                .foregroundStyle(ink)
                .frame(height: 18)
        }
    }

    private func dockButton(_ item: HomeMenuItem, symbol: String, tint: Color) -> some View {
        Button {
            selectedMenu = item
            switch item {
            case .mii:
                showingMiiCreator = true
            case .firmware:
                onFirmware?()
            case .controllers:
                settingsCategory = .controllers
                showingSettings = true
            case .settings:
                settingsCategory = .graphics
                showingSettings = true
            }
        } label: {
            ZStack {
                if selectedMenu == item {
                    Circle()
                        .fill(Color.white)
                        .frame(width: 55, height: 55)
                        .overlay(Circle().stroke(Color(red: 0.73, green: 0.79, blue: 0.87), lineWidth: 2))
                        .shadow(color: .black.opacity(0.08), radius: 3, y: 1)
                }
                Image(systemName: symbol)
                    .font(.system(size: 23, weight: .regular))
                    .foregroundStyle(tint)
                    .frame(maxWidth: .infinity)
                    .frame(height: 55)
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(item.label)
    }
}

private enum HomeMenuItem {
    case mii, firmware, controllers, settings

    var label: String {
        switch self {
        case .mii: "Mii Creator"
        case .firmware: "Firmware"
        case .controllers: "Controllers"
        case .settings: "Settings"
        }
    }
}

@MainActor
private final class LandscapeOrientationController {
    func setLandscape() {
        guard let scene = UIApplication.shared.connectedScenes
            .compactMap({ $0 as? UIWindowScene })
            .first(where: { $0.activationState == .foregroundActive }) else { return }
        scene.requestGeometryUpdate(.iOS(interfaceOrientations: .landscape)) { error in
            NSLog("Nexilium: landscape orientation request failed: %@", error.localizedDescription)
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
