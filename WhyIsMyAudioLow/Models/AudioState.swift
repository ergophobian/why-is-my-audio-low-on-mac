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

        setupBindings()
    }

    private func setupBindings() {
        // Forward volume changes to audio engine
        $masterVolume
            .sink { [weak self] volume in
                self?.audioEngine.setVolume(volume)
            }
            .store(in: &cancellables)

        // Forward EQ changes to audio engine
        $eqBands
            .sink { [weak self] bands in
                self?.audioEngine.setEQBands(bands)
            }
            .store(in: &cancellables)

        // Forward enabled state
        $isEnabled
            .sink { [weak self] enabled in
                if enabled {
                    self?.audioEngine.start()
                } else {
                    self?.audioEngine.stop()
                }
            }
            .store(in: &cancellables)
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
