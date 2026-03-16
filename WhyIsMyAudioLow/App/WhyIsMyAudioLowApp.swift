import SwiftUI

@main
struct WhyIsMyAudioLowApp: App {
    @StateObject private var audioState = AudioState()

    var body: some Scene {
        MenuBarExtra("Audio Boost", systemImage: "speaker.wave.3.fill") {
            MenuBarView()
                .environmentObject(audioState)
        }
        .menuBarExtraStyle(.window)

        Settings {
            SettingsWindow()
                .environmentObject(audioState)
        }
    }
}
