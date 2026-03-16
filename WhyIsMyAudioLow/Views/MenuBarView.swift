import SwiftUI

struct MenuBarView: View {
    @EnvironmentObject var audioState: AudioState

    var body: some View {
        VStack(spacing: 12) {
            // Header with enable toggle
            HStack {
                Image(systemName: audioState.isEnabled ? "speaker.wave.3.fill" : "speaker.slash.fill")
                    .foregroundColor(audioState.isEnabled ? .accentColor : .secondary)
                    .font(.title3)

                Text("Audio Boost")
                    .font(.headline)

                Spacer()

                Toggle("", isOn: $audioState.isEnabled)
                    .toggleStyle(.switch)
                    .labelsHidden()
            }

            Divider()

            // Master volume slider
            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text("Master Volume")
                        .font(.subheadline)
                        .foregroundColor(.secondary)
                    Spacer()
                    Text("\(audioState.masterVolumePercent)%")
                        .font(.subheadline.monospacedDigit())
                        .foregroundColor(audioState.masterVolume > 1.0 ? .orange : .primary)
                }

                Slider(value: $audioState.masterVolume, in: 0...5.0, step: 0.05)
                    .disabled(!audioState.isEnabled)
            }

            Divider()

            // Preset picker
            HStack {
                Text("Preset")
                    .font(.subheadline)
                    .foregroundColor(.secondary)
                Spacer()
                Picker("Preset", selection: $audioState.selectedPreset) {
                    ForEach(EQPreset.allCases) { preset in
                        Text(preset.displayName).tag(preset)
                    }
                }
                .labelsHidden()
                .frame(width: 140)
                .onChange(of: audioState.selectedPreset) { newPreset in
                    audioState.applyPreset(newPreset)
                }
            }

            // Output device picker
            HStack {
                Text("Output")
                    .font(.subheadline)
                    .foregroundColor(.secondary)
                Spacer()
                Text("System Default")
                    .font(.subheadline)
                    .foregroundColor(.primary)
            }

            Divider()

            // Settings button
            Button {
                SettingsWindowController.shared.showSettings(audioState: audioState)
            } label: {
                HStack {
                    Image(systemName: "gear")
                    Text("Settings...")
                }
            }
            .buttonStyle(.plain)

            Button {
                NSApp.terminate(nil)
            } label: {
                HStack {
                    Image(systemName: "power")
                    Text("Quit")
                }
            }
            .buttonStyle(.plain)
        }
        .padding()
        .frame(width: 280)
    }
}

#Preview {
    MenuBarView()
        .environmentObject(AudioState())
}
