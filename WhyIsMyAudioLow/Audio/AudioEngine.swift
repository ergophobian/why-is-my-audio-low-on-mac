import AVFoundation
import CoreAudio

/// Manages the audio processing pipeline for system-wide volume boost and EQ.
///
/// The audio engine creates a tap on the virtual audio device (installed via AudioDriver),
/// processes audio through the DSP chain (volume boost + EQ), and routes it to the
/// selected physical output device.
class AudioEngine {
    private var engine: AVAudioEngine?
    private let dspProcessor = DSPProcessor()

    private var currentVolume: Double = 1.0
    private var currentEQBands: [Double] = Array(repeating: 0.0, count: 10)
    private var isRunning: Bool = false

    /// Start the audio processing pipeline.
    /// Creates the AVAudioEngine graph: input (virtual device) -> processing -> output (physical device).
    func start() {
        guard !isRunning else { return }

        let engine = AVAudioEngine()
        self.engine = engine

        let inputNode = engine.inputNode
        let outputNode = engine.outputNode
        let mainMixer = engine.mainMixerNode

        let format = inputNode.outputFormat(forBus: 0)

        // Install a tap on the input node for processing
        inputNode.installTap(onBus: 0, bufferSize: 512, format: format) { [weak self] buffer, _ in
            guard let self = self else { return }
            let processed = self.dspProcessor.process(
                buffer: buffer,
                volume: self.currentVolume,
                eqBands: self.currentEQBands
            )
            // In a full implementation, route processed buffer to output
            _ = processed
        }

        engine.connect(inputNode, to: mainMixer, format: format)
        engine.connect(mainMixer, to: outputNode, format: format)

        do {
            try engine.start()
            isRunning = true
        } catch {
            print("[AudioEngine] Failed to start: \(error.localizedDescription)")
        }
    }

    /// Stop the audio processing pipeline.
    func stop() {
        guard isRunning else { return }
        engine?.inputNode.removeTap(onBus: 0)
        engine?.stop()
        engine = nil
        isRunning = false
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

    /// Set the output audio device.
    /// - Parameter deviceID: CoreAudio AudioDeviceID for the output device
    func setOutputDevice(_ deviceID: AudioDeviceID) {
        // In a full implementation, this would configure the AVAudioEngine's
        // output to use the specified device via CoreAudio HAL APIs.
        var deviceID = deviceID
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDefaultOutputDevice,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        let status = AudioObjectSetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &address,
            0,
            nil,
            UInt32(MemoryLayout<AudioDeviceID>.size),
            &deviceID
        )

        if status != noErr {
            print("[AudioEngine] Failed to set output device: \(status)")
        }
    }

    /// List available output audio devices.
    /// - Returns: Array of tuples containing device ID and name
    func listOutputDevices() -> [(id: AudioDeviceID, name: String)] {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        var dataSize: UInt32 = 0
        var status = AudioObjectGetPropertyDataSize(
            AudioObjectID(kAudioObjectSystemObject),
            &address,
            0,
            nil,
            &dataSize
        )

        guard status == noErr else { return [] }

        let deviceCount = Int(dataSize) / MemoryLayout<AudioDeviceID>.size
        var deviceIDs = [AudioDeviceID](repeating: 0, count: deviceCount)

        status = AudioObjectGetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &address,
            0,
            nil,
            &dataSize,
            &deviceIDs
        )

        guard status == noErr else { return [] }

        return deviceIDs.compactMap { deviceID -> (id: AudioDeviceID, name: String)? in
            // Check if device has output channels
            var outputAddress = AudioObjectPropertyAddress(
                mSelector: kAudioDevicePropertyStreams,
                mScope: kAudioDevicePropertyScopeOutput,
                mElement: kAudioObjectPropertyElementMain
            )

            var outputSize: UInt32 = 0
            let outputStatus = AudioObjectGetPropertyDataSize(deviceID, &outputAddress, 0, nil, &outputSize)
            guard outputStatus == noErr, outputSize > 0 else { return nil }

            // Get device name
            var nameAddress = AudioObjectPropertyAddress(
                mSelector: kAudioDevicePropertyDeviceNameCFString,
                mScope: kAudioObjectPropertyScopeGlobal,
                mElement: kAudioObjectPropertyElementMain
            )

            var name: CFString = "" as CFString
            var nameSize = UInt32(MemoryLayout<CFString>.size)
            let nameStatus = AudioObjectGetPropertyData(deviceID, &nameAddress, 0, nil, &nameSize, &name)
            guard nameStatus == noErr else { return nil }

            return (id: deviceID, name: name as String)
        }
    }
}
