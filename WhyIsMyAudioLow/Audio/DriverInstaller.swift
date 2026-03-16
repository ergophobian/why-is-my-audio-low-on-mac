import Foundation
import AppKit

/// Handles installation and management of the AudioBoost HAL driver.
class DriverInstaller {
    static let driverName = "AudioBoostDriver"
    static let installPath = "/Library/Audio/Plug-Ins/HAL/\(driverName).driver"

    /// Check if the driver is currently installed.
    static var isDriverInstalled: Bool {
        FileManager.default.fileExists(atPath: installPath)
    }

    /// Install the driver from the app bundle to /Library/Audio/Plug-Ins/HAL/
    /// Requires admin privileges via AppleScript authorization.
    static func installDriver(completion: @escaping (Bool, String) -> Void) {
        guard let driverSourcePath = Bundle.main.path(forResource: driverName, ofType: "driver") else {
            completion(false, "Driver bundle not found in app resources.")
            return
        }

        let script = """
        do shell script "\
        sudo mkdir -p /Library/Audio/Plug-Ins/HAL && \
        sudo rm -rf \(installPath) && \
        sudo cp -R '\(driverSourcePath)' \(installPath) && \
        sudo chown -R root:wheel \(installPath) && \
        sudo killall coreaudiod 2>/dev/null || true\
        " with administrator privileges
        """

        var error: NSDictionary?
        if let appleScript = NSAppleScript(source: script) {
            appleScript.executeAndReturnError(&error)
            if let error = error {
                completion(false, error[NSAppleScript.errorMessage] as? String ?? "Unknown error")
            } else {
                completion(true, "Driver installed successfully. Audio will restart momentarily.")
            }
        } else {
            completion(false, "Failed to create installation script.")
        }
    }

    /// Uninstall the driver.
    static func uninstallDriver(completion: @escaping (Bool, String) -> Void) {
        let script = """
        do shell script "\
        sudo rm -rf \(installPath) && \
        sudo killall coreaudiod 2>/dev/null || true\
        " with administrator privileges
        """

        var error: NSDictionary?
        if let appleScript = NSAppleScript(source: script) {
            appleScript.executeAndReturnError(&error)
            if let error = error {
                completion(false, error[NSAppleScript.errorMessage] as? String ?? "Unknown error")
            } else {
                completion(true, "Driver uninstalled successfully.")
            }
        } else {
            completion(false, "Failed to create uninstall script.")
        }
    }

    /// Restart CoreAudio daemon to pick up driver changes.
    static func restartCoreAudio() {
        let script = """
        do shell script "sudo killall coreaudiod 2>/dev/null || true" with administrator privileges
        """
        var error: NSDictionary?
        NSAppleScript(source: script)?.executeAndReturnError(&error)
    }
}
