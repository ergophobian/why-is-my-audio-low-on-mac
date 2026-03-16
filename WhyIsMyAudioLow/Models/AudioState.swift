import Foundation
import CoreAudio
import Combine

class AudioState: ObservableObject {
    @Published var masterVolume: Double = 1.0
    @Published var selectedPreset: EQPreset = .flat
    @Published var eqBands: [Double] = Array(repeating: 0.0, count: 10)
    @Published var outputDeviceID: AudioDeviceID?
    @Published var isEnabled: Bool = true
    @Published var appVolumes: [String: Double] = [:]
    @Published var launchAtLogin: Bool = false
    @Published var autoEnableOnLaunch: Bool = true

    static let bandFrequencies: [String] = [
        "32", "64", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"
    ]

    static let bandFrequencyValues: [Double] = [
        32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
    ]

    var masterVolumePercent: Int {
        Int(masterVolume * 100)
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
