import SwiftUI
import AppKit

@main
struct WhyIsMyAudioLowApp: App {
    @StateObject private var audioState = AudioState()

    var body: some Scene {
        WindowGroup {
            MainWindow()
                .environmentObject(audioState)
        }
        .windowStyle(.titleBar)
        .defaultSize(width: 620, height: 500)
        .windowResizability(.contentMinSize)
    }
}
