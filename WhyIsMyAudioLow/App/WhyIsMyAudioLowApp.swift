import SwiftUI

@main
struct WhyIsMyAudioLowApp: App {
    @StateObject private var audioState = AudioState()

    var body: some Scene {
        MenuBarExtra {
            MenuBarView()
                .environmentObject(audioState)
        } label: {
            Label("Audio Boost", systemImage: audioState.isEnabled ? "speaker.wave.3.fill" : "speaker.slash.fill")
        }
        .menuBarExtraStyle(.window)

        Settings {
            SettingsWindow()
                .environmentObject(audioState)
        }
    }
}
