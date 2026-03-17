import SwiftUI

struct MainWindow: View {
    @EnvironmentObject var audioState: AudioState

    var body: some View {
        VStack(spacing: 0) {
            // Status bar showing routing when active
            if audioState.isAudioBoostActive {
                HStack(spacing: 8) {
                    Image(systemName: "arrow.triangle.branch")
                        .font(.caption)
                    Text(audioState.routingStatus)
                        .font(.caption)
                    Spacer()
                    Button {
                        audioState.disableAudioBoost()
                    } label: {
                        Text("Disable")
                            .font(.caption)
                    }
                    .buttonStyle(.borderless)
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 6)
                .background(Color.blue.opacity(0.1))
            } else if audioState.isBlackHoleInstalled {
                HStack(spacing: 8) {
                    Image(systemName: "speaker.slash")
                        .font(.caption)
                        .foregroundColor(.secondary)
                    Text("Audio boost not active")
                        .font(.caption)
                        .foregroundColor(.secondary)
                    Spacer()
                    Button {
                        audioState.enableAudioBoost()
                    } label: {
                        Text("Enable")
                            .font(.caption)
                    }
                    .buttonStyle(.borderless)
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 6)
                .background(Color.secondary.opacity(0.05))
            } else {
                HStack(spacing: 8) {
                    Image(systemName: "exclamationmark.triangle")
                        .font(.caption)
                        .foregroundColor(.orange)
                    Text("BlackHole 2ch not installed — go to General tab to set up")
                        .font(.caption)
                        .foregroundColor(.orange)
                    Spacer()
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 6)
                .background(Color.orange.opacity(0.08))
            }

            // Top bar: enable toggle + volume
            HStack(spacing: 16) {
                Image(systemName: audioState.isEnabled ? "speaker.wave.3.fill" : "speaker.slash.fill")
                    .foregroundColor(audioState.isEnabled ? .accentColor : .secondary)
                    .font(.title2)

                Text("Audio Boost")
                    .font(.title3.weight(.semibold))

                Toggle("", isOn: $audioState.isEnabled)
                    .toggleStyle(.switch)
                    .labelsHidden()

                Spacer()

                Text("\(audioState.masterVolumePercent)%")
                    .font(.title2.monospacedDigit().weight(.medium))
                    .foregroundColor(audioState.masterVolume > 1.0 ? .orange : .primary)
                    .frame(width: 60, alignment: .trailing)
            }
            .padding()

            // Volume slider
            Slider(value: $audioState.masterVolume, in: 0...5.0, step: 0.05)
                .disabled(!audioState.isEnabled)
                .padding(.horizontal)

            Divider()
                .padding(.top, 12)

            // Tabs
            TabView {
                EQView()
                    .tabItem {
                        Label("EQ", systemImage: "slider.vertical.3")
                    }
                    .tag(0)

                AppsVolumeView()
                    .tabItem {
                        Label("Apps", systemImage: "square.grid.2x2")
                    }
                    .tag(1)

                GeneralSettingsView()
                    .tabItem {
                        Label("General", systemImage: "gear")
                    }
                    .tag(2)

                AboutView()
                    .tabItem {
                        Label("About", systemImage: "info.circle")
                    }
                    .tag(3)
            }
            .environmentObject(audioState)
        }
        .frame(minWidth: 550, minHeight: 450)
    }
}

#Preview {
    MainWindow()
        .environmentObject(AudioState())
        .frame(width: 620, height: 500)
}
