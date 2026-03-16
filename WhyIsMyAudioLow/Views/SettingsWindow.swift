import SwiftUI

struct SettingsWindow: View {
    @EnvironmentObject var audioState: AudioState

    var body: some View {
        TabView {
            GeneralSettingsView()
                .tabItem {
                    Label("General", systemImage: "gear")
                }
                .tag(0)

            EQView()
                .tabItem {
                    Label("EQ", systemImage: "slider.vertical.3")
                }
                .tag(1)

            AppsVolumeView()
                .tabItem {
                    Label("Apps", systemImage: "square.grid.2x2")
                }
                .tag(2)

            AboutView()
                .tabItem {
                    Label("About", systemImage: "info.circle")
                }
                .tag(3)
        }
        .environmentObject(audioState)
        .frame(width: 600, height: 450)
    }
}

#Preview {
    SettingsWindow()
        .environmentObject(AudioState())
}
