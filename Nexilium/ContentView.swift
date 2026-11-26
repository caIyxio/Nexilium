import SwiftUI

struct ContentView: View {
    var body: some View {
        TabView {
            NavigationStack {
                Color(uiColor: .systemBackground)
                    .navigationTitle("Nexilium")
                    .navigationBarTitleDisplayMode(.large)
            }
            .tabItem {
                Label("Home", systemImage: "house")
            }

            NavigationStack {
                Color(uiColor: .systemBackground)
                    .navigationTitle("Library")
                    .navigationBarTitleDisplayMode(.large)
            }
            .tabItem {
                Label("Library", systemImage: "square.grid.2x2")
            }

            NavigationStack {
                SettingsView()
            }
            .tabItem {
                Label("Settings", systemImage: "gearshape")
            }
        }
    }
}

#Preview {
    ContentView()
}
