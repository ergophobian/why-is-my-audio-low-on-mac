import Foundation
import AppKit
import CoreAudio

/// Manages BlackHole 2ch virtual audio device setup for system-wide audio processing.
/// Replaces the custom HAL driver approach that required Apple Developer signing.
class BlackHoleSetup {
    static let driverPath = "/Library/Audio/Plug-Ins/HAL/BlackHole2ch.driver"
    static let blackHoleDeviceName = "BlackHole 2ch"
    static let downloadURL = "https://existential.audio/blackhole/"

    /// Check if BlackHole 2ch driver is installed on disk.
    static var isInstalled: Bool {
        FileManager.default.fileExists(atPath: driverPath)
    }

    /// Find the BlackHole 2ch audio device ID by name.
    /// Returns nil if BlackHole is not installed or not recognized by CoreAudio.
    static func findBlackHoleDeviceID() -> AudioDeviceID? {
        let devices = allAudioDevices()
        for deviceID in devices {
            if let name = deviceName(for: deviceID), name == blackHoleDeviceName {
                return deviceID
            }
        }
        return nil
    }

    /// Get the current default output device ID.
    static func getDefaultOutputDeviceID() -> AudioDeviceID? {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDefaultOutputDevice,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        var deviceID: AudioDeviceID = 0
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        let status = AudioObjectGetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &address, 0, nil, &size, &deviceID
        )
        guard status == noErr, deviceID != 0 else { return nil }
        return deviceID
    }

    /// Get the name for a given audio device ID.
    static func deviceName(for deviceID: AudioDeviceID) -> String? {
        var nameAddress = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyDeviceNameCFString,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        var name: CFString = "" as CFString
        var nameSize = UInt32(MemoryLayout<CFString>.size)
        let status = AudioObjectGetPropertyData(deviceID, &nameAddress, 0, nil, &nameSize, &name)
        guard status == noErr else { return nil }
        return name as String
    }

    /// Set the system default output device.
    static func setDefaultOutputDevice(_ deviceID: AudioDeviceID) -> Bool {
        var deviceID = deviceID
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDefaultOutputDevice,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        let status = AudioObjectSetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &address, 0, nil,
            UInt32(MemoryLayout<AudioDeviceID>.size),
            &deviceID
        )
        if status != noErr {
            print("[BlackHoleSetup] Failed to set default output device: \(status)")
        }
        return status == noErr
    }

    /// Find the first real (non-virtual) output device.
    /// Skips BlackHole and aggregate devices. Returns the device the user most likely
    /// wants audio to come out of (headphones, speakers, etc.).
    static func findRealOutputDevice(excluding blackHoleID: AudioDeviceID? = nil) -> AudioDeviceID? {
        let devices = allAudioDevices()
        for deviceID in devices {
            // Skip BlackHole
            if let bhID = blackHoleID, deviceID == bhID { continue }

            guard let name = deviceName(for: deviceID) else { continue }

            // Skip known virtual/aggregate devices
            let lowerName = name.lowercased()
            if lowerName.contains("blackhole") { continue }
            if lowerName.contains("aggregate") { continue }
            if lowerName.contains("multi-output") { continue }

            // Check if device has output streams
            if hasOutputStreams(deviceID: deviceID) {
                return deviceID
            }
        }
        return nil
    }

    /// Get all audio device IDs on the system.
    static func allAudioDevices() -> [AudioDeviceID] {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        var dataSize: UInt32 = 0
        var status = AudioObjectGetPropertyDataSize(
            AudioObjectID(kAudioObjectSystemObject),
            &address, 0, nil, &dataSize
        )
        guard status == noErr, dataSize > 0 else { return [] }

        let deviceCount = Int(dataSize) / MemoryLayout<AudioDeviceID>.size
        var deviceIDs = [AudioDeviceID](repeating: 0, count: deviceCount)
        status = AudioObjectGetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &address, 0, nil, &dataSize, &deviceIDs
        )
        guard status == noErr else { return [] }
        return deviceIDs
    }

    /// Check whether a device has output streams.
    static func hasOutputStreams(deviceID: AudioDeviceID) -> Bool {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyStreams,
            mScope: kAudioDevicePropertyScopeOutput,
            mElement: kAudioObjectPropertyElementMain
        )
        var size: UInt32 = 0
        let status = AudioObjectGetPropertyDataSize(deviceID, &address, 0, nil, &size)
        return status == noErr && size > 0
    }

    /// Check whether a device has input streams.
    static func hasInputStreams(deviceID: AudioDeviceID) -> Bool {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyStreams,
            mScope: kAudioDevicePropertyScopeInput,
            mElement: kAudioObjectPropertyElementMain
        )
        var size: UInt32 = 0
        let status = AudioObjectGetPropertyDataSize(deviceID, &address, 0, nil, &size)
        return status == noErr && size > 0
    }

    /// Install BlackHole via Homebrew. Falls back to opening the download page.
    static func install(completion: @escaping (Bool, String) -> Void) {
        DispatchQueue.global(qos: .userInitiated).async {
            // Try brew install first
            let process = Process()
            process.executableURL = URL(fileURLWithPath: "/bin/zsh")
            process.arguments = ["-l", "-c", "brew install --cask blackhole-2ch"]

            let pipe = Pipe()
            process.standardOutput = pipe
            process.standardError = pipe

            do {
                try process.run()
                process.waitUntilExit()

                if process.terminationStatus == 0 {
                    DispatchQueue.main.async {
                        completion(true, "BlackHole 2ch installed successfully. You may need to restart the app.")
                    }
                } else {
                    // brew failed — open download page
                    DispatchQueue.main.async {
                        if let url = URL(string: downloadURL) {
                            NSWorkspace.shared.open(url)
                        }
                        completion(false, "Homebrew install failed. Opened the BlackHole download page in your browser — install manually.")
                    }
                }
            } catch {
                // Process launch failed — open download page
                DispatchQueue.main.async {
                    if let url = URL(string: downloadURL) {
                        NSWorkspace.shared.open(url)
                    }
                    completion(false, "Could not run Homebrew. Opened the BlackHole download page in your browser — install manually.")
                }
            }
        }
    }

    /// Enable audio boost by setting BlackHole 2ch as the default output device.
    /// Returns the previous output device ID so it can be restored later.
    @discardableResult
    static func enableAudioBoost() -> (success: Bool, previousDeviceID: AudioDeviceID?, message: String) {
        guard let blackHoleID = findBlackHoleDeviceID() else {
            return (false, nil, "BlackHole 2ch device not found. Is it installed?")
        }

        // Remember the current output device before switching
        let previousID = getDefaultOutputDeviceID()

        // Don't switch if already on BlackHole
        if let prev = previousID, prev == blackHoleID {
            return (true, previousID, "BlackHole is already the default output device.")
        }

        if setDefaultOutputDevice(blackHoleID) {
            return (true, previousID, "Audio routing enabled: system output -> BlackHole -> boost -> your device.")
        } else {
            return (false, previousID, "Failed to set BlackHole as default output device.")
        }
    }

    /// Disable audio boost by restoring the original output device.
    static func disableAudioBoost(restoreDeviceID: AudioDeviceID?) -> (success: Bool, message: String) {
        guard let restoreID = restoreDeviceID else {
            // Try to find a real device to restore to
            if let realDevice = findRealOutputDevice() {
                if setDefaultOutputDevice(realDevice) {
                    let name = deviceName(for: realDevice) ?? "Unknown"
                    return (true, "Restored output to \(name).")
                }
            }
            return (false, "No device to restore to.")
        }

        if setDefaultOutputDevice(restoreID) {
            let name = deviceName(for: restoreID) ?? "Unknown"
            return (true, "Restored output to \(name).")
        } else {
            return (false, "Failed to restore output device.")
        }
    }
}
