import SwiftUI

struct AboutView: View {
    private var appVersion: String {
        Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "1.0.0"
    }

    var body: some View {
        Form {
            Section {
                HStack(spacing: 14) {
                    Image("NexiliumIcon")
                        .resizable()
                        .scaledToFit()
                        .frame(width: 54, height: 54)
                        .clipShape(RoundedRectangle(cornerRadius: 13))

                    VStack(alignment: .leading, spacing: 4) {
                        Text("Nexilium")
                            .font(.title3.weight(.semibold))
                        Text("Nintendo Switch emulator for iOS")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                }
                .padding(.vertical, 6)
            }

            Section("Version") {
                LabeledContent("App version", value: appVersion)
                LabeledContent("Minimum iOS", value: "18.0")
            }

            Section("Project") {
                LabeledContent("Developer", value: "Silicon Team")
                Link(destination: URL(string: "https://github.com/caIyxio/Nexilium")!) {
                    Label("Source code on GitHub", systemImage: "chevron.left.forwardslash.chevron.right")
                }
            }

            Section {
                Text("Nexilium is an independent open-source project. Nintendo and Nintendo Switch are trademarks of their respective owners.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
        .navigationTitle("About")
        .navigationBarTitleDisplayMode(.inline)
    }
}
