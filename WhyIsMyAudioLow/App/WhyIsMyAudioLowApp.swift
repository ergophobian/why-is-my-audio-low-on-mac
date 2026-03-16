import SwiftUI
import AppKit

class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        // Allow the app to become active and open windows even without a Dock icon
        NSApp.setActivationPolicy(.accessory)
    }
}

@main
struct WhyIsMyAudioLowApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
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
