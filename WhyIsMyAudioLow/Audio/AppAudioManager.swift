import AppKit
import CoreAudio

/// Manages detection and volume control for individual applications' audio streams.
///
/// Uses CoreAudio's HAL plugin interface to discover which apps are producing audio
/// and allows per-app volume adjustment through the virtual audio driver.
class AppAudioManager {
    /// Information about a running app that may produce audio.
    struct AppInfo {
        let bundleID: String
        let name: String
        let icon: NSImage
        let pid: pid_t
    }

    /// Get a list of currently running apps that could produce audio.
    ///
    /// Filters to regular (visible) applications. In a full implementation,
    /// this would also query the virtual audio driver to determine which
    /// apps are actively producing audio.
    ///
    /// - Returns: Array of app info tuples
    func getRunningAudioApps() -> [AppInfo] {
        let workspace = NSWorkspace.shared
        return workspace.runningApplications
            .filter { $0.activationPolicy == .regular }
            .compactMap { app -> AppInfo? in
                guard let bundleID = app.bundleIdentifier,
                      let name = app.localizedName else { return nil }

                let icon = app.icon ?? NSImage(systemSymbolName: "app.fill", accessibilityDescription: nil) ?? NSImage()

                return AppInfo(
                    bundleID: bundleID,
                    name: name,
                    icon: icon,
                    pid: app.processIdentifier
                )
            }
            .sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }

    /// Set the volume for a specific app's audio stream.
    ///
    /// Communicates with the virtual audio driver to adjust the volume
    /// of audio from the specified process.
    ///
    /// - Parameters:
    ///   - pid: Process ID of the target application
    ///   - volume: Volume multiplier (0.0 to 5.0)
    func setAppVolume(pid: pid_t, volume: Double) {
        // In production, this communicates with the HAL plugin to set
        // per-stream volume for the given PID.
        let clampedVolume = max(0, min(volume, 5.0))
        print("[AppAudioManager] Set volume for PID \(pid) to \(clampedVolume)")
    }

    /// Get the current volume for a specific app.
    ///
    /// - Parameter pid: Process ID of the target application
    /// - Returns: Current volume multiplier, or nil if not found
    func getAppVolume(pid: pid_t) -> Double? {
        // In production, queries the HAL plugin for current per-stream volume
        return nil
    }

    /// Check if the virtual audio driver is installed and available.
    ///
    /// - Returns: true if the driver is detected in the system
    func isDriverInstalled() -> Bool {
        // Check for our virtual audio device in the system's audio devices
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

        // Look for our virtual device by manufacturer
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
