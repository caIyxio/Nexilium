import SwiftUI
import UIKit

struct ConsoleHomeView: View {
    var onFirmware: (() -> Void)? = nil
    var onExit: (() -> Void)? = nil
    @State private var showingMiiCreator = false
    @State private var showingSettings = false
    @State private var settingsCategory: SettingsCategory = .graphics
    @State private var selectedMenu: HomeMenuItem = .firmware

    private let ink = Color(red: 0.29, green: 0.39, blue: 0.54)

    var body: some View {
        GeometryReader { geometry in
            ZStack {
                Color(red: 0.91, green: 0.92, blue: 0.94).ignoresSafeArea()

                VStack(spacing: 0) {
                    header
                        .padding(.horizontal, max(geometry.size.width * 0.045, 28))
                        .padding(.top, 18)

                    Spacer(minLength: 20)

                    gamesArea
                        .padding(.horizontal, max(geometry.size.width * 0.075, 42))

                    Spacer(minLength: 24)

                    bottomMenu
                        .padding(.horizontal, max(geometry.size.width * 0.09, 48))
                        .padding(.bottom, max(geometry.safeAreaInsets.bottom + 12, 24))
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .ignoresSafeArea()
        .preferredColorScheme(.light)
        .statusBarHidden()
        .onAppear {
            LandscapeOrientationController().setLandscape()
        }
        .fullScreenCover(isPresented: $showingMiiCreator) {
            NavigationStack { MiiCreatorView() }
        }
        .fullScreenCover(isPresented: $showingSettings) {
            SettingsView(initialSelection: settingsCategory)
        }
    }

    private var header: some View {
        HStack(alignment: .center) {
            Text("NEXILIUM")
                .font(.system(size: 17, weight: .bold, design: .rounded))
                .tracking(5)
                .foregroundStyle(ink)
            Spacer()
            Text("HOME")
                .font(.system(size: 14, weight: .semibold, design: .rounded))
                .tracking(3)
                .foregroundStyle(Color.gray.opacity(0.85))
        }
    }

    private var gamesArea: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Games")
                .font(.system(size: 23, weight: .semibold, design: .rounded))
                .foregroundStyle(Color.primary.opacity(0.82))
            Text("No games installed")
                .font(.system(size: 17, weight: .regular, design: .rounded))
                .foregroundStyle(Color.gray)
        }
        .frame(maxWidth: .infinity, minHeight: 150, alignment: .leading)
    }

    private var bottomMenu: some View {
        VStack(spacing: 12) {
            HStack(spacing: 0) {
                menuItem(.mii, icon: "face.smiling", tint: Color(red: 0.28, green: 0.40, blue: 0.59))
                menuItem(.firmware, icon: "folder.badge.plus", tint: Color(red: 0.86, green: 0.56, blue: 0.20))
                menuItem(.controllers, icon: "gamecontroller", tint: Color(red: 0.28, green: 0.59, blue: 0.39))
                menuItem(.settings, icon: "gearshape", tint: Color(red: 0.38, green: 0.48, blue: 0.64))
                if onExit != nil {
                    menuItem(.exit, icon: "power", tint: Color.gray)
                }
            }
            .padding(.horizontal, 18)
            .padding(.vertical, 12)
            .background(Color.white.opacity(0.68), in: Capsule())
            .overlay(Capsule().stroke(Color.white.opacity(0.95), lineWidth: 1.5))

            Text(selectedMenu.label)
                .font(.system(size: 12, weight: .medium, design: .rounded))
                .foregroundStyle(ink.opacity(0.9))
                .frame(height: 16)
        }
    }

    private func menuItem(_ item: HomeMenuItem, icon: String, tint: Color) -> some View {
        Button {
            selectedMenu = item
            switch item {
            case .mii: showingMiiCreator = true
            case .firmware: onFirmware?()
            case .controllers:
                settingsCategory = .controllers
                showingSettings = true
            case .settings:
                settingsCategory = .graphics
                showingSettings = true
            case .exit: onExit?()
            }
        } label: {
            VStack(spacing: 8) {
                Image(systemName: icon)
                    .font(.system(size: 25, weight: .regular))
                    .frame(width: 34, height: 34)
                    .foregroundStyle(tint)
                Text(item.label)
                    .font(.system(size: 11, weight: .medium, design: .rounded))
                    .foregroundStyle(ink)
                    .lineLimit(1)
            }
            .frame(maxWidth: .infinity)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

private enum HomeMenuItem {
    case mii, firmware, controllers, settings, exit

    var label: String {
        switch self {
        case .mii: "Mii Creator"
        case .firmware: "Firmware"
        case .controllers: "Controllers"
        case .settings: "Settings"
        case .exit: "Exit"
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
