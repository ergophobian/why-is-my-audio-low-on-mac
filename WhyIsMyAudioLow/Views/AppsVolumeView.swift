import SwiftUI
import AppKit

struct AppsVolumeView: View {
    @EnvironmentObject var audioState: AudioState
    @StateObject private var appManager = AppAudioViewModel()

    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack {
                Text("Per-App Volume Control")
                    .font(.headline)
                Spacer()
                Button {
                    appManager.refresh()
                } label: {
                    Image(systemName: "arrow.clockwise")
                }
                .help("Refresh app list")
            }
            .padding()

            Divider()

            if appManager.runningApps.isEmpty {
                Spacer()
                VStack(spacing: 12) {
                    Image(systemName: "speaker.badge.exclamationmark")
                        .font(.system(size: 40))
                        .foregroundColor(.secondary)
                    Text("No audio apps detected")
                        .font(.title3)
                        .foregroundColor(.secondary)
                    Text("Apps playing audio will appear here.\nThe audio driver must be installed for per-app control.")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .multilineTextAlignment(.center)
                }
                Spacer()
            } else {
                List {
                    ForEach(appManager.runningApps, id: \.bundleID) { app in
                        AppVolumeRow(
                            appName: app.name,
                            appIcon: app.icon,
                            volume: Binding(
                                get: { audioState.appVolumes[app.bundleID] ?? 1.0 },
                                set: { audioState.setAppVolume(bundleID: app.bundleID, volume: $0) }
                            )
                        )
                    }
                }
            }
        }
    }
}

// MARK: - App Volume Row

struct AppVolumeRow: View {
    let appName: String
    let appIcon: NSImage
    @Binding var volume: Double

    var volumePercent: Int {
        Int(volume * 100)
    }

    var body: some View {
        HStack(spacing: 12) {
            Image(nsImage: appIcon)
                .resizable()
                .frame(width: 24, height: 24)

            Text(appName)
                .frame(width: 120, alignment: .leading)
                .lineLimit(1)

            Slider(value: $volume, in: 0...5.0, step: 0.05)

            Text("\(volumePercent)%")
                .font(.subheadline.monospacedDigit())
                .foregroundColor(volume > 1.0 ? .orange : .primary)
                .frame(width: 48, alignment: .trailing)
        }
        .padding(.vertical, 4)
    }
}

// MARK: - ViewModel for running apps

struct AudioApp: Identifiable {
    let id = UUID()
    let bundleID: String
    let name: String
    let icon: NSImage
}

class AppAudioViewModel: ObservableObject {
    @Published var runningApps: [AudioApp] = []

    init() {
        refresh()
    }

    func refresh() {
        let workspace = NSWorkspace.shared
        let apps = workspace.runningApplications
            .filter { $0.activationPolicy == .regular }
            .compactMap { app -> AudioApp? in
                guard let bundleID = app.bundleIdentifier,
                      let name = app.localizedName else { return nil }
                let icon = app.icon ?? NSImage(systemSymbolName: "app", accessibilityDescription: nil) ?? NSImage()
                return AudioApp(bundleID: bundleID, name: name, icon: icon)
            }
            .sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }

        DispatchQueue.main.async {
            self.runningApps = apps
        }
    }
}

#Preview {
    AppsVolumeView()
        .environmentObject(AudioState())
        .frame(width: 580, height: 400)
}
