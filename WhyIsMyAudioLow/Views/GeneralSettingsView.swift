import SwiftUI
import CoreAudio

struct OutputDevice: Identifiable, Hashable {
    let id: AudioDeviceID
    let name: String
}

struct GeneralSettingsView: View {
    @EnvironmentObject var audioState: AudioState
    @State private var driverInstalled = DriverInstaller.isDriverInstalled
    @State private var showingInstallAlert = false
    @State private var showingUninstallAlert = false
    @State private var statusMessage = ""
    @State private var showingStatus = false
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
                Picker("Default output device", selection: Binding<AudioDeviceID>(
                    get: { selectedID },
                    set: { audioState.outputDeviceID = $0 == 0 ? nil : $0 }
                )) {
                    Text("System Default").tag(AudioDeviceID(0))
                    ForEach(outputDevices) { device in
                        Text(device.name).tag(device.id)
                    }
                }
            } header: {
                Text("Output")
            }

            Section {
                HStack {
                    VStack(alignment: .leading, spacing: 4) {
                        HStack(spacing: 8) {
                            Image(systemName: driverInstalled ? "checkmark.circle.fill" : "xmark.circle.fill")
                                .foregroundColor(driverInstalled ? .green : .red)
                            Text(driverInstalled ? "Audio driver installed" : "Audio driver not installed")
                        }
                        .font(.body)

                        Text("The virtual audio driver is required for system-wide volume boost and EQ.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }

                    Spacer()

                    if driverInstalled {
                        Button("Uninstall") {
                            showingUninstallAlert = true
                        }
                        .alert("Uninstall Driver", isPresented: $showingUninstallAlert) {
                            Button("Uninstall", role: .destructive) { uninstallDriver() }
                            Button("Cancel", role: .cancel) {}
                        } message: {
                            Text("This will remove the virtual audio driver. Volume boost and EQ will stop working.")
                        }
                    }

                    Button(driverInstalled ? "Reinstall" : "Install Driver") {
                        showingInstallAlert = true
                    }
                    .buttonStyle(.borderedProminent)
                    .alert("Install Audio Driver", isPresented: $showingInstallAlert) {
                        Button("Install") { installDriver() }
                        Button("Cancel", role: .cancel) {}
                    } message: {
                        Text("This will install a virtual audio driver to enable system-wide audio processing. You'll be prompted for your admin password.")
                    }
                }
            } header: {
                Text("Audio Driver")
            }
        }
        .formStyle(.grouped)
        .padding()
        .onAppear {
            outputDevices = audioState.audioEngine.listOutputDevices().map { OutputDevice(id: $0.id, name: $0.name) }
            driverInstalled = DriverInstaller.isDriverInstalled
        }
        .alert("Driver Status", isPresented: $showingStatus) {
            Button("OK") {}
        } message: {
            Text(statusMessage)
        }
    }

    private func installDriver() {
        DriverInstaller.installDriver { success, message in
            statusMessage = message
            showingStatus = true
            driverInstalled = DriverInstaller.isDriverInstalled
        }
    }

    private func uninstallDriver() {
        DriverInstaller.uninstallDriver { success, message in
            statusMessage = message
            showingStatus = true
            driverInstalled = DriverInstaller.isDriverInstalled
        }
    }
}

#Preview {
    GeneralSettingsView()
        .environmentObject(AudioState())
        .frame(width: 580, height: 420)
}
