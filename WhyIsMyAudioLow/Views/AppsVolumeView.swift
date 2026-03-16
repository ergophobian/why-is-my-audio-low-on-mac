import SwiftUI
import AppKit

struct AppsVolumeView: View {
    @EnvironmentObject var audioState: AudioState
    @StateObject private var appManager = AppAudioManager()
    @State private var searchText: String = ""

    private var filteredApps: [AudioApp] {
        if searchText.isEmpty {
            return appManager.runningApps
        }
        return appManager.runningApps.filter {
            $0.name.localizedCaseInsensitiveContains(searchText)
        }
    }

    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack {
                Text("Per-App Volume Control")
                    .font(.headline)
                Spacer()
                Button {
                    resetAllVolumes()
                } label: {
                    Text("Reset All")
                        .font(.caption)
                }
                .help("Reset all app volumes to 100%")

                Button {
                    appManager.refresh()
                } label: {
                    Image(systemName: "arrow.clockwise")
                }
                .help("Refresh app list")
            }
            .padding()

            // Search field
            HStack {
                Image(systemName: "magnifyingglass")
                    .foregroundColor(.secondary)
                TextField("Filter apps...", text: $searchText)
                    .textFieldStyle(.plain)
                if !searchText.isEmpty {
                    Button {
                        searchText = ""
                    } label: {
                        Image(systemName: "xmark.circle.fill")
                            .foregroundColor(.secondary)
                    }
                    .buttonStyle(.plain)
                }
            }
            .padding(8)
            .background(Color(nsColor: .controlBackgroundColor))
            .cornerRadius(8)
            .padding(.horizontal)
            .padding(.bottom, 8)

            Divider()

            if filteredApps.isEmpty {
                Spacer()
                VStack(spacing: 12) {
                    Image(systemName: searchText.isEmpty
                        ? "speaker.badge.exclamationmark"
                        : "magnifyingglass")
                        .font(.system(size: 40))
                        .foregroundColor(.secondary)
                    Text(searchText.isEmpty
                        ? "No audio apps detected"
                        : "No matching apps")
                        .font(.title3)
                        .foregroundColor(.secondary)
                    Text(searchText.isEmpty
                        ? "Apps playing audio will appear here.\nThe audio driver must be installed for per-app control."
                        : "Try a different search term.")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .multilineTextAlignment(.center)
                }
                Spacer()
            } else {
                List {
                    ForEach(filteredApps) { app in
                        AppVolumeRow(
                            appName: app.name,
                            appIcon: app.icon,
                            volume: Binding(
                                get: { audioState.appVolumes[app.id] ?? 1.0 },
                                set: { audioState.setAppVolume(bundleID: app.id, volume: $0) }
                            )
                        )
                    }
                }
            }
        }
        .onAppear {
            appManager.startRefreshTimer()
        }
        .onDisappear {
            appManager.stopRefreshTimer()
        }
    }

    private func resetAllVolumes() {
        for app in appManager.runningApps {
            audioState.setAppVolume(bundleID: app.id, volume: 1.0)
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

#Preview {
    AppsVolumeView()
        .environmentObject(AudioState())
        .frame(width: 580, height: 400)
}
