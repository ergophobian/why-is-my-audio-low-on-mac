import Foundation
import CoreAudio
import Combine

class AudioState: ObservableObject {
    @Published var masterVolume: Double {
        didSet { UserDefaults.standard.set(masterVolume, forKey: "masterVolume") }
    }
    @Published var selectedPreset: EQPreset {
        didSet { UserDefaults.standard.set(selectedPreset.rawValue, forKey: "selectedPreset") }
    }
    @Published var eqBands: [Double] {
        didSet { UserDefaults.standard.set(eqBands, forKey: "eqBands") }
    }
    @Published var outputDeviceID: AudioDeviceID?
    @Published var isEnabled: Bool {
        didSet { UserDefaults.standard.set(isEnabled, forKey: "isEnabled") }
    }
    @Published var appVolumes: [String: Double] = [:]
    @Published var launchAtLogin: Bool {
        didSet { UserDefaults.standard.set(launchAtLogin, forKey: "launchAtLogin") }
    }
    @Published var autoEnableOnLaunch: Bool {
        didSet { UserDefaults.standard.set(autoEnableOnLaunch, forKey: "autoEnableOnLaunch") }
    }

    // BlackHole state
    @Published var isBlackHoleInstalled: Bool = false
    @Published var originalOutputDevice: String?
    @Published var isAudioBoostActive: Bool = false
    @Published var routingStatus: String = ""

    /// The saved device ID to restore when disabling audio boost.
    var originalOutputDeviceID: AudioDeviceID?

    static let bandFrequencies: [String] = [
        "32", "64", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"
    ]

    static let bandFrequencyValues: [Double] = [
        32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
    ]

    var masterVolumePercent: Int {
        Int(masterVolume * 100)
    }

    let audioEngine = AudioEngine()

    private var cancellables = Set<AnyCancellable>()

    init() {
        // Load persisted settings
        let defaults = UserDefaults.standard
        self.masterVolume = defaults.double(forKey: "masterVolume") > 0
            ? defaults.double(forKey: "masterVolume") : 1.0
        self.selectedPreset = EQPreset(rawValue: defaults.string(forKey: "selectedPreset") ?? "") ?? .flat
        self.eqBands = (defaults.array(forKey: "eqBands") as? [Double])
            ?? Array(repeating: 0.0, count: 10)
        self.isEnabled = defaults.object(forKey: "isEnabled") != nil
            ? defaults.bool(forKey: "isEnabled") : true
        self.launchAtLogin = defaults.bool(forKey: "launchAtLogin")
        self.autoEnableOnLaunch = defaults.object(forKey: "autoEnableOnLaunch") != nil
            ? defaults.bool(forKey: "autoEnableOnLaunch") : true

        // Check BlackHole installation status
        refreshBlackHoleStatus()

        setupBindings()
    }

    private func setupBindings() {
        // Forward volume changes to audio engine
        $masterVolume
            .dropFirst()
            .sink { [weak self] volume in
                self?.audioEngine.setVolume(volume)
            }
            .store(in: &cancellables)

        // Forward EQ changes to audio engine
        $eqBands
            .dropFirst()
            .sink { [weak self] bands in
                self?.audioEngine.setEQBands(bands)
            }
            .store(in: &cancellables)

        // Forward enabled state (skip initial value to avoid crash during init)
        $isEnabled
            .dropFirst()
            .sink { [weak self] enabled in
                if enabled {
                    self?.audioEngine.start()
                } else {
                    self?.audioEngine.stop()
                }
            }
            .store(in: &cancellables)
    }

    /// Refresh the BlackHole installation and routing status.
    func refreshBlackHoleStatus() {
        isBlackHoleInstalled = BlackHoleSetup.isInstalled

        // Check if BlackHole is currently the default output (meaning boost is active)
        if let defaultID = BlackHoleSetup.getDefaultOutputDeviceID(),
           let bhID = BlackHoleSetup.findBlackHoleDeviceID(),
           defaultID == bhID {
            isAudioBoostActive = true
            if let realName = audioEngine.realOutputDeviceName() {
                routingStatus = "BlackHole -> \(realName)"
            } else if let realID = BlackHoleSetup.findRealOutputDevice(excluding: bhID),
                      let realName = BlackHoleSetup.deviceName(for: realID) {
                routingStatus = "BlackHole -> \(realName)"
            } else {
                routingStatus = "BlackHole -> (no output device)"
            }
        } else {
            isAudioBoostActive = false
            if let defaultID = BlackHoleSetup.getDefaultOutputDeviceID(),
               let name = BlackHoleSetup.deviceName(for: defaultID) {
                routingStatus = "Direct: \(name)"
            } else {
                routingStatus = "No routing"
            }
        }
    }

    /// Enable audio boost: set BlackHole as default output, start engine.
    func enableAudioBoost() {
        let result = BlackHoleSetup.enableAudioBoost()
        if result.success {
            originalOutputDeviceID = result.previousDeviceID
            if let prevID = result.previousDeviceID {
                originalOutputDevice = BlackHoleSetup.deviceName(for: prevID)
            }
            isAudioBoostActive = true
            audioEngine.start()
        }
        refreshBlackHoleStatus()
    }

    /// Disable audio boost: stop engine, restore original output device.
    func disableAudioBoost() {
        audioEngine.stop()
        let _ = BlackHoleSetup.disableAudioBoost(restoreDeviceID: originalOutputDeviceID)
        isAudioBoostActive = false
        originalOutputDeviceID = nil
        originalOutputDevice = nil
        refreshBlackHoleStatus()
    }

    func applyPreset(_ preset: EQPreset) {
        selectedPreset = preset
        if preset != .custom {
            eqBands = preset.bands
        }
    }

    func resetEQ() {
        selectedPreset = .flat
        eqBands = EQPreset.flat.bands
    }

    func setAppVolume(bundleID: String, volume: Double) {
        appVolumes[bundleID] = volume
    }
}
