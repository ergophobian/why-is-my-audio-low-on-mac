import SwiftUI

struct GeneralSettingsView: View {
    @EnvironmentObject var audioState: AudioState
    @State private var driverInstalled: Bool = false
    @State private var showingDriverAlert: Bool = false

    var body: some View {
        Form {
            Section {
                Toggle("Launch at login", isOn: $audioState.launchAtLogin)
                Toggle("Auto-enable audio boost on launch", isOn: $audioState.autoEnableOnLaunch)
            } header: {
                Text("Startup")
            }

            Section {
                Picker("Default output device", selection: .constant("System Default")) {
                    Text("System Default").tag("System Default")
                    Text("Built-in Output").tag("Built-in Output")
                    Text("Audio Boost (Virtual)").tag("Audio Boost (Virtual)")
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

                        Text("The virtual audio driver is required for volume boost beyond 100% and per-app volume control.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }

                    Spacer()

                    Button(driverInstalled ? "Reinstall Driver" : "Install Driver") {
                        showingDriverAlert = true
                    }
                    .alert("Install Audio Driver", isPresented: $showingDriverAlert) {
                        Button("Install", role: .destructive) {
                            installDriver()
                        }
                        Button("Cancel", role: .cancel) {}
                    } message: {
                        Text("This will install a virtual audio driver as a System Extension. You may be prompted for administrator access.")
                    }
                }
            } header: {
                Text("Audio Driver")
            }

            Section {
                HStack {
                    Text("Processing quality")
                    Spacer()
                    Picker("", selection: .constant("High")) {
                        Text("Low (less CPU)").tag("Low")
                        Text("Medium").tag("Medium")
                        Text("High").tag("High")
                    }
                    .frame(width: 160)
                }

                HStack {
                    Text("Buffer size")
                    Spacer()
                    Picker("", selection: .constant(512)) {
                        Text("256 samples").tag(256)
                        Text("512 samples").tag(512)
                        Text("1024 samples").tag(1024)
                    }
                    .frame(width: 160)
                }
            } header: {
                Text("Advanced")
            }
        }
        .formStyle(.grouped)
        .padding()
    }

    private func installDriver() {
        // Placeholder: In production, this would trigger System Extension installation
        driverInstalled = true
    }
}

#Preview {
    GeneralSettingsView()
        .environmentObject(AudioState())
        .frame(width: 580, height: 420)
}
