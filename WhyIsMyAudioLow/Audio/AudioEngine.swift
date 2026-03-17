import AVFoundation
import CoreAudio

/// Manages the audio processing pipeline for system-wide volume boost and EQ.
///
/// Uses BlackHole 2ch as a virtual input device. System audio is routed to BlackHole
/// (by setting it as the default output), captured here as input, processed through
/// DSPProcessor (volume boost + EQ), and played out through the real physical output
/// device (headphones/speakers).
class AudioEngine {
    private var engine: AVAudioEngine?
    private let dspProcessor = DSPProcessor()

    private var currentVolume: Double = 1.0
    private var currentEQBands: [Double] = Array(repeating: 0.0, count: 10)
    private(set) var isRunning: Bool = false

    /// The real output device ID (headphones/speakers) — where processed audio goes.
    private var realOutputDeviceID: AudioDeviceID?
    /// The BlackHole device ID — where system audio arrives as input.
    private var blackHoleDeviceID: AudioDeviceID?

    /// Start the audio processing pipeline.
    ///
    /// Flow: BlackHole 2ch (input) -> DSP processing -> real output device
    /// Caller must set BlackHole as the system default output before calling this.
    func start() {
        guard !isRunning else { return }

        // Find BlackHole device
        guard let bhID = BlackHoleSetup.findBlackHoleDeviceID() else {
            print("[AudioEngine] BlackHole 2ch not found — cannot start.")
            return
        }
        blackHoleDeviceID = bhID

        // Find real output device (the one the user actually hears through)
        guard let realID = realOutputDeviceID ?? BlackHoleSetup.findRealOutputDevice(excluding: bhID) else {
            print("[AudioEngine] No real output device found — cannot start.")
            return
        }
        realOutputDeviceID = realID

        let engine = AVAudioEngine()
        self.engine = engine

        // Set the engine's input device to BlackHole 2ch
        if !setDeviceOnAudioUnit(engine.inputNode.audioUnit!, deviceID: bhID) {
            print("[AudioEngine] Failed to set BlackHole as input device.")
            return
        }

        // Set the engine's output device to the real headphones/speakers
        if !setDeviceOnAudioUnit(engine.outputNode.audioUnit!, deviceID: realID) {
            print("[AudioEngine] Failed to set real output device.")
            return
        }

        let inputNode = engine.inputNode
        let mainMixer = engine.mainMixerNode
        let outputNode = engine.outputNode

        // Get the input format from BlackHole
        let inputFormat = inputNode.outputFormat(forBus: 0)
        guard inputFormat.sampleRate > 0, inputFormat.channelCount > 0 else {
            print("[AudioEngine] Invalid input format from BlackHole: \(inputFormat)")
            return
        }

        // Create a processing format that matches BlackHole's output
        // Use a standard format the mixer can work with
        let processingFormat = AVAudioFormat(
            standardFormatWithSampleRate: inputFormat.sampleRate,
            channels: inputFormat.channelCount
        )!

        // Install a tap on the input to process audio through DSP
        inputNode.installTap(onBus: 0, bufferSize: 512, format: inputFormat) { [weak self] buffer, _ in
            guard let self = self else { return }
            _ = self.dspProcessor.process(
                buffer: buffer,
                volume: self.currentVolume,
                eqBands: self.currentEQBands
            )
        }

        // Connect: input -> mixer -> output
        engine.connect(inputNode, to: mainMixer, format: processingFormat)
        engine.connect(mainMixer, to: outputNode, format: processingFormat)

        do {
            try engine.start()
            isRunning = true
            let inputName = BlackHoleSetup.deviceName(for: bhID) ?? "BlackHole 2ch"
            let outputName = BlackHoleSetup.deviceName(for: realID) ?? "Unknown"
            print("[AudioEngine] Started: \(inputName) -> DSP -> \(outputName)")
        } catch {
            print("[AudioEngine] Failed to start: \(error.localizedDescription)")
            self.engine = nil
        }
    }

    /// Stop the audio processing pipeline.
    func stop() {
        guard isRunning else { return }
        engine?.inputNode.removeTap(onBus: 0)
        engine?.stop()
        engine = nil
        isRunning = false
        print("[AudioEngine] Stopped.")
    }

    /// Set the master volume multiplier.
    /// - Parameter volume: Volume multiplier (0.0 to 5.0, where 1.0 = 100%)
    func setVolume(_ volume: Double) {
        currentVolume = max(0, min(volume, 5.0))
    }

    /// Set the 10-band EQ values.
    /// - Parameter bands: Array of 10 dB values (-12 to +12)
    func setEQBands(_ bands: [Double]) {
        guard bands.count == 10 else { return }
        currentEQBands = bands
    }

    /// Set the real output device (where the user hears audio).
    /// If the engine is running, it will be restarted with the new device.
    func setOutputDevice(_ deviceID: AudioDeviceID) {
        realOutputDeviceID = deviceID
        if isRunning {
            stop()
            start()
        }
    }

    /// List available output audio devices (excludes BlackHole).
    /// - Returns: Array of tuples containing device ID and name
    func listOutputDevices() -> [(id: AudioDeviceID, name: String)] {
        let devices = BlackHoleSetup.allAudioDevices()

        return devices.compactMap { deviceID -> (id: AudioDeviceID, name: String)? in
            guard BlackHoleSetup.hasOutputStreams(deviceID: deviceID) else { return nil }
            guard let name = BlackHoleSetup.deviceName(for: deviceID) else { return nil }

            // Skip BlackHole — it's the virtual input, not a real output
            if name.lowercased().contains("blackhole") { return nil }

            return (id: deviceID, name: name)
        }
    }

    /// Get the name of the current real output device.
    func realOutputDeviceName() -> String? {
        guard let id = realOutputDeviceID else { return nil }
        return BlackHoleSetup.deviceName(for: id)
    }

    // MARK: - Private

    /// Set the current device on an AudioUnit using kAudioOutputUnitProperty_CurrentDevice.
    private func setDeviceOnAudioUnit(_ audioUnit: AudioUnit, deviceID: AudioDeviceID) -> Bool {
        var devID = deviceID
        let status = AudioUnitSetProperty(
            audioUnit,
            kAudioOutputUnitProperty_CurrentDevice,
            kAudioUnitScope_Global,
            0,
            &devID,
            UInt32(MemoryLayout<AudioDeviceID>.size)
        )
        if status != noErr {
            print("[AudioEngine] AudioUnitSetProperty failed for device \(deviceID): \(status)")
            return false
        }
        return true
    }
}
