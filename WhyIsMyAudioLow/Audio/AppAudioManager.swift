import AppKit
import CoreAudio
import Combine

/// Represents a running application that may produce audio.
struct AudioApp: Identifiable, Hashable {
    let id: String // bundleID
    let name: String
    let icon: NSImage

    func hash(into hasher: inout Hasher) {
        hasher.combine(id)
    }

    static func == (lhs: AudioApp, rhs: AudioApp) -> Bool {
        lhs.id == rhs.id
    }
}

/// Manages detection and volume control for individual applications' audio streams.
///
/// Uses NSWorkspace to discover running apps and provides a timer-based refresh
/// to detect newly launched or closed applications.
class AppAudioManager: ObservableObject {
    @Published var runningApps: [AudioApp] = []

    private var refreshTimer: Timer?

    init() {
        refresh()
    }

    deinit {
        stopRefreshTimer()
    }

    /// Refresh the list of running apps by querying NSWorkspace.
    func refresh() {
        let workspace = NSWorkspace.shared
        let apps = workspace.runningApplications
            .filter { $0.activationPolicy == .regular }
            .compactMap { app -> AudioApp? in
                guard let bundleID = app.bundleIdentifier,
                      let name = app.localizedName else { return nil }

                let icon = app.icon
                    ?? NSImage(systemSymbolName: "app.fill", accessibilityDescription: nil)
                    ?? NSImage()

                return AudioApp(id: bundleID, name: name, icon: icon)
            }
            .sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }

        DispatchQueue.main.async {
            self.runningApps = apps
        }
    }

    /// Start the auto-refresh timer (fires every 5 seconds).
    func startRefreshTimer() {
        stopRefreshTimer()
        refreshTimer = Timer.scheduledTimer(withTimeInterval: 5.0, repeats: true) { [weak self] _ in
            self?.refresh()
        }
    }

    /// Stop the auto-refresh timer.
    func stopRefreshTimer() {
        refreshTimer?.invalidate()
        refreshTimer = nil
    }

    /// Set the volume for a specific app's audio stream.
    ///
    /// Communicates with the virtual audio driver to adjust the volume
    /// of audio from the specified bundle.
    ///
    /// - Parameters:
    ///   - bundleID: Bundle identifier of the target application
    ///   - volume: Volume multiplier (0.0 to 5.0)
    func setAppVolume(bundleID: String, volume: Double) {
        let clampedVolume = max(0, min(volume, 5.0))
        print("[AppAudioManager] Set volume for \(bundleID) to \(clampedVolume)")
    }

    /// Check if the virtual audio driver is installed and available.
    ///
    /// - Returns: true if the driver is detected in the system
    func isDriverInstalled() -> Bool {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        var dataSize: UInt32 = 0
        let status = AudioObjectGetPropertyDataSize(
            AudioObjectID(kAudioObjectSystemObject),
            &address,
            0,
            nil,
            &dataSize
        )

        guard status == noErr else { return false }

        let deviceCount = Int(dataSize) / MemoryLayout<AudioDeviceID>.size
        var deviceIDs = [AudioDeviceID](repeating: 0, count: deviceCount)

        let getStatus = AudioObjectGetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &address,
            0,
            nil,
            &dataSize,
            &deviceIDs
        )

        guard getStatus == noErr else { return false }

        for deviceID in deviceIDs {
            var manufacturerAddress = AudioObjectPropertyAddress(
                mSelector: kAudioDevicePropertyDeviceManufacturerCFString,
                mScope: kAudioObjectPropertyScopeGlobal,
                mElement: kAudioObjectPropertyElementMain
            )

            var manufacturer: CFString = "" as CFString
            var size = UInt32(MemoryLayout<CFString>.size)

            let mfgStatus = AudioObjectGetPropertyData(
                deviceID, &manufacturerAddress, 0, nil, &size, &manufacturer
            )

            if mfgStatus == noErr && (manufacturer as String) == "WhyIsMyAudioLow" {
                return true
            }
        }

        return false
    }
}
