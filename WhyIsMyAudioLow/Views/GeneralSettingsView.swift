import SwiftUI
import CoreAudio

struct OutputDevice: Identifiable, Hashable {
    let id: AudioDeviceID
    let name: String
}

struct GeneralSettingsView: View {
    @EnvironmentObject var audioState: AudioState
    @State private var showingInstallStatus = false
    @State private var installStatusMessage = ""
    @State private var isInstalling = false
    @State private var outputDevices: [OutputDevice] = []

    var body: some View {
        Form {
            Section {
                Toggle("Launch at login", isOn: $audioState.launchAtLogin)
                Toggle("Auto-enable audio boost on launch", isOn: $audioState.autoEnableOnLaunch)
            } header: {
                Text("Startup")
            }

            Section {
                let selectedID = audioState.outputDeviceID ?? 0
                Picker("Output device (headphones/speakers)", selection: Binding<AudioDeviceID>(
                    get: { selectedID },
                    set: { newValue in
                        audioState.outputDeviceID = newValue == 0 ? nil : newValue
                        if newValue != 0 {
                            audioState.audioEngine.setOutputDevice(newValue)
                        }
                    }
                )) {
                    Text("Auto-detect").tag(AudioDeviceID(0))
                    ForEach(outputDevices) { device in
                        Text(device.name).tag(device.id)
                    }
                }

                Text("This is where you hear audio. BlackHole captures it first, then routes it here after processing.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            } header: {
                Text("Output")
            }

            Section {
                // Installation status
                HStack(spacing: 8) {
                    Image(systemName: audioState.isBlackHoleInstalled ? "checkmark.circle.fill" : "xmark.circle.fill")
                        .foregroundColor(audioState.isBlackHoleInstalled ? .green : .red)
                        .font(.body)

                    VStack(alignment: .leading, spacing: 2) {
                        Text(audioState.isBlackHoleInstalled ? "BlackHole 2ch installed" : "BlackHole 2ch not installed")
                            .font(.body)
                        Text("Free open-source virtual audio driver (MIT license)")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }

                    Spacer()

                    if !audioState.isBlackHoleInstalled {
                        Button {
                            installBlackHole()
                        } label: {
                            if isInstalling {
                                ProgressView()
                                    .controlSize(.small)
                                    .padding(.trailing, 4)
                            }
                            Text(isInstalling ? "Installing..." : "Install BlackHole")
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(isInstalling)
                    }
                }

                // Routing status
                if audioState.isBlackHoleInstalled {
                    HStack(spacing: 8) {
                        Image(systemName: audioState.isAudioBoostActive ? "arrow.triangle.branch" : "minus.circle")
                            .foregroundColor(audioState.isAudioBoostActive ? .blue : .secondary)
                            .font(.body)

                        VStack(alignment: .leading, spacing: 2) {
                            Text("Audio routing: \(audioState.routingStatus)")
                                .font(.body)

                            if audioState.isAudioBoostActive, let origDevice = audioState.originalOutputDevice {
                                Text("Will restore to \"\(origDevice)\" when disabled")
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                            }
                        }

                        Spacer()

                        if audioState.isAudioBoostActive {
                            Button("Disable Audio Boost") {
                                audioState.disableAudioBoost()
                            }
                            .buttonStyle(.bordered)
                        } else {
                            Button("Enable Audio Boost") {
                                audioState.enableAudioBoost()
                            }
                            .buttonStyle(.borderedProminent)
                        }
                    }
                }
            } header: {
                Text("BlackHole Setup")
            }
        }
        .formStyle(.grouped)
        .padding()
        .onAppear {
            audioState.refreshBlackHoleStatus()
            outputDevices = audioState.audioEngine.listOutputDevices().map {
                OutputDevice(id: $0.id, name: $0.name)
            }
        }
        .alert("BlackHole Setup", isPresented: $showingInstallStatus) {
            Button("OK") {}
        } message: {
            Text(installStatusMessage)
        }
    }

    private func installBlackHole() {
        isInstalling = true
        BlackHoleSetup.install { success, message in
            isInstalling = false
            installStatusMessage = message
            showingInstallStatus = true
            audioState.refreshBlackHoleStatus()
        }
    }
}

#Preview {
    GeneralSettingsView()
        .environmentObject(AudioState())
        .frame(width: 580, height: 420)
}
