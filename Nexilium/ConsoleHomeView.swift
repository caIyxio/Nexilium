import SwiftUI
import UIKit
import Network

struct ConsoleHomeView: View {
    var onFirmware: (() -> Void)? = nil
    var onExit: (() -> Void)? = nil
    @State private var showingSettings = false
    @State private var settingsCategory: SettingsCategory = .graphics
    @State private var selectedMenu: HomeMenuItem = .firmware

    var body: some View {
        GeometryReader { proxy in
            let tile = min(proxy.size.height * 0.36, 170)

            VStack(spacing: 0) {
                topBar
                    .padding(.horizontal, 32)
                    .padding(.top, 14)

                Spacer(minLength: 8)

                gamesArea(tile: tile, width: proxy.size.width)

                Spacer(minLength: 8)

                bottomDock
                    .padding(.bottom, 44)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .overlay(alignment: .bottom) { bottomBar }
            .background(NXTheme.background.ignoresSafeArea())
        }
        .preferredColorScheme(.light)
        .onAppear { LandscapeOrientationController().setLandscape() }
        .fullScreenCover(isPresented: $showingSettings) {
            SettingsView(initialSelection: settingsCategory)
        }
    }

    // MARK: Top bar

    private var topBar: some View {
        HStack {
            Button {
                open(.system)
            } label: {
                Image(systemName: "person.fill")
                    .font(.system(size: 20))
                    .foregroundStyle(Color.white)
                    .frame(width: 42, height: 42)
                    .background(Circle().fill(NXTheme.secondary.opacity(0.75)))
                    .overlay(Circle().stroke(Color.white, lineWidth: 2))
            }
            .buttonStyle(.plain)
            .accessibilityLabel("System settings")

            Spacer()

            StatusCluster()
        }
        .frame(height: 44)
    }

    // MARK: Games

    private func gamesArea(tile: CGFloat, width: CGFloat) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 14) {
                    ForEach(0..<6, id: \.self) { _ in
                        RoundedRectangle(cornerRadius: 8)
                            .fill(Color.white.opacity(0.85))
                            .frame(width: tile, height: tile)
                            .shadow(color: .black.opacity(0.05), radius: 2, y: 1)
                    }
                }
                .padding(.horizontal, width * 0.08)
                .padding(.vertical, 6)
            }

            Text("No games installed")
                .font(.system(size: 15))
                .foregroundStyle(NXTheme.secondary)
                .padding(.horizontal, width * 0.08)
        }
    }

    // MARK: Dock

    private var bottomDock: some View {
        HStack(spacing: 18) {
            ForEach(HomeMenuItem.allCases, id: \.self) { item in
                dockButton(item)
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 8)
        .background(Capsule().fill(Color(red: 0.89, green: 0.89, blue: 0.90)))
    }

    private func dockButton(_ item: HomeMenuItem) -> some View {
        let isSelected = selectedMenu == item
        return Button {
            selectedMenu = item
            activate(item)
        } label: {
            ZStack {
                if isSelected {
                    Circle()
                        .fill(Color.white)
                        .overlay(Circle().strokeBorder(NXTheme.focusGradient, lineWidth: 3))
                        .shadow(color: .black.opacity(0.15), radius: 5, y: 2)
                }
                Image(systemName: item.symbol)
                    .font(.system(size: 24))
                    .foregroundStyle(item.tint)
            }
            .frame(width: 64, height: 64)
            .overlay(alignment: .topLeading) {
                if isSelected {
                    Text(item.label)
                        .font(.system(size: 16))
                        .foregroundStyle(NXTheme.accent)
                        .fixedSize()
                        .offset(y: 74)
                }
            }
            .contentShape(Circle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(item.label)
    }

    // MARK: Bottom bar

    private var bottomBar: some View {
        HStack {
            Image(systemName: "gamecontroller")
                .font(.system(size: 22))
                .foregroundStyle(NXTheme.secondary)
            Spacer()
            Button { activate(selectedMenu) } label: { NXButtonHint("A", "OK") }
                .buttonStyle(.plain)
        }
        .padding(.horizontal, 32)
        .padding(.bottom, 10)
    }

    // MARK: Actions

    private func activate(_ item: HomeMenuItem) {
        switch item {
        case .firmware: onFirmware?()
        case .controllers: open(.controllers)
        case .settings: open(.graphics)
        case .about: open(.about)
        }
    }

    private func open(_ category: SettingsCategory) {
        settingsCategory = category
        showingSettings = true
    }
}

private enum HomeMenuItem: CaseIterable, Hashable {
    case firmware, controllers, settings, about

    var label: String {
        switch self {
        case .firmware: "Firmware"
        case .controllers: "Controllers"
        case .settings: "Settings"
        case .about: "About"
        }
    }

    var symbol: String {
        switch self {
        case .firmware: "folder.badge.plus"
        case .controllers: "gamecontroller"
        case .settings: "gearshape"
        case .about: "info.circle"
        }
    }

    var tint: Color {
        switch self {
        case .firmware: Color(red: 0.95, green: 0.58, blue: 0.15)
        case .controllers: Color(red: 0.30, green: 0.68, blue: 0.35)
        case .settings: Color(red: 0.42, green: 0.42, blue: 0.45)
        case .about: Color(red: 0.25, green: 0.45, blue: 0.80)
        }
    }
}

// MARK: - Real device status (clock, Wi-Fi, battery)

@MainActor
private final class DeviceStatusMonitor: ObservableObject {
    @Published var isOnline = true
    private let monitor = NWPathMonitor()

    init() {
        UIDevice.current.isBatteryMonitoringEnabled = true
        monitor.pathUpdateHandler = { [weak self] path in
            let online = path.status == .satisfied
            Task { @MainActor in self?.isOnline = online }
        }
        monitor.start(queue: DispatchQueue(label: "nexilium.network-monitor"))
    }

    deinit {
        monitor.cancel()
    }

    var batteryPercent: Int? {
        let level = UIDevice.current.batteryLevel
        return level < 0 ? nil : Int((level * 100).rounded())
    }

    var isCharging: Bool {
        let state = UIDevice.current.batteryState
        return state == .charging || state == .full
    }
}

private struct StatusCluster: View {
    @StateObject private var status = DeviceStatusMonitor()

    var body: some View {
        TimelineView(.periodic(from: .now, by: 20)) { context in
            HStack(spacing: 14) {
                Text(context.date, format: .dateTime.hour().minute())
                Image(systemName: status.isOnline ? "wifi" : "wifi.slash")
                HStack(spacing: 4) {
                    if let percent = status.batteryPercent {
                        Text("\(percent)%")
                    }
                    Image(systemName: status.isCharging ? "battery.100percent.bolt" : "battery.100percent")
                }
            }
            .font(.system(size: 18, weight: .semibold))
            .foregroundStyle(NXTheme.text)
        }
    }
}

// MARK: - Orientation

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
