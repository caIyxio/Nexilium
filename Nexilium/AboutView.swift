import SwiftUI

struct AboutView: View {
    private var appVersion: String {
        Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "1.0.0"
    }

    var body: some View {
        NXPage {
            HStack(spacing: 14) {
                Image("NexiliumIcon")
                    .resizable()
                    .scaledToFit()
                    .frame(width: 54, height: 54)
                    .clipShape(RoundedRectangle(cornerRadius: 13))

                VStack(alignment: .leading, spacing: 4) {
                    Text("Nexilium")
                        .font(.system(size: 22, weight: .semibold))
                        .foregroundStyle(NXTheme.text)
                    Text("Nintendo Switch emulator for iOS")
                        .font(.system(size: 14))
                        .foregroundStyle(NXTheme.secondary)
                }
                Spacer()
            }
            .padding(.horizontal, 18)
            .padding(.vertical, 10)

            NXInfoRow(title: "App Version", value: appVersion)
            NXInfoRow(title: "Minimum iOS", value: "18.0")
            NXInfoRow(title: "Developer", value: "Silicon Team")

            if let url = URL(string: "https://github.com/caIyxio/Nexilium") {
                NXLinkRow(title: "Source Code on GitHub", url: url)
            }

            NXNote("Nexilium is an independent open-source project. Nintendo and Nintendo Switch are trademarks of their respective owners.")
        }
    }
}
