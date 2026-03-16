import SwiftUI

struct AboutView: View {
    var body: some View {
        VStack(spacing: 20) {
            Spacer()

            // App icon
            Image(systemName: "speaker.wave.3.fill")
                .font(.system(size: 64))
                .foregroundStyle(
                    LinearGradient(
                        colors: [.blue, .purple],
                        startPoint: .topLeading,
                        endPoint: .bottomTrailing
                    )
                )
                .shadow(color: .purple.opacity(0.3), radius: 10)

            // App name
            Text("Why Is My Audio Low on Mac?")
                .font(.title)
                .fontWeight(.bold)

            // Version
            Text("Version \(appVersion)")
                .font(.subheadline)
                .foregroundColor(.secondary)

            // Description
            Text("System-wide audio boost, EQ, and per-app volume control for macOS")
                .font(.body)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 400)

            // Badge
            HStack(spacing: 6) {
                Image(systemName: "lock.open.fill")
                    .font(.caption)
                Text("Free & Open Source")
                    .font(.callout.weight(.medium))
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 8)
            .background(Color.accentColor.opacity(0.1))
            .cornerRadius(20)

            // Links
            HStack(spacing: 16) {
                Button {
                    if let url = URL(string: "https://github.com/kylekumar/why-is-my-audio-low-on-mac") {
                        NSWorkspace.shared.open(url)
                    }
                } label: {
                    HStack(spacing: 4) {
                        Image(systemName: "link")
                        Text("GitHub")
                    }
                }
                .buttonStyle(.link)

                Button {
                    if let url = URL(string: "https://github.com/kylekumar/why-is-my-audio-low-on-mac/issues") {
                        NSWorkspace.shared.open(url)
                    }
                } label: {
                    HStack(spacing: 4) {
                        Image(systemName: "exclamationmark.bubble")
                        Text("Report Issue")
                    }
                }
                .buttonStyle(.link)
            }

            Spacer()

            // Footer
            Text("Made with frustration and Claude Code")
                .font(.caption)
                .foregroundColor(.secondary.opacity(0.7))
                .padding(.bottom, 8)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding()
    }

    private var appVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0.1.0"
    }
}

#Preview {
    AboutView()
        .frame(width: 580, height: 420)
}
