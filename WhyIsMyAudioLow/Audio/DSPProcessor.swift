import AVFoundation
import Accelerate

/// Handles digital signal processing for volume boost and EQ.
///
/// Uses Apple's Accelerate framework (vDSP) for high-performance audio processing.
/// Implements cascaded biquad filters for a real 10-band parametric EQ, volume
/// amplification up to 500%, and tanh soft clipping to prevent distortion.
class DSPProcessor {

    /// Center frequencies for the 10-band EQ
    static let bandFrequencies: [Double] = [32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]

    /// Q factor for EQ bands (controls bandwidth — 1.4 gives ~1 octave width)
    private let bandQ: Double = 1.4

    /// Biquad filter state for each band, per channel (supports stereo)
    private var biquadSetups: [[vDSP_biquad_Setup?]] = []
    /// Delay values for biquad filters — vDSP_biquad needs 2*sections+2 Doubles per setup
    private var biquadDelays: [[[Double]]] = []

    /// Current sample rate — recalculate coefficients when this changes
    private var currentSampleRate: Double = 0
    private var currentBands: [Double] = Array(repeating: 0, count: 10)
    private var channelCount: Int = 0

    /// Process an audio buffer with volume boost and EQ.
    func process(buffer: AVAudioPCMBuffer, volume: Double, eqBands: [Double]) -> AVAudioPCMBuffer {
        guard let channelData = buffer.floatChannelData else { return buffer }

        let frameCount = Int(buffer.frameLength)
        let channels = Int(buffer.format.channelCount)
        let sampleRate = buffer.format.sampleRate

        // Rebuild biquad filters if sample rate, bands, or channel count changed
        if sampleRate != currentSampleRate || eqBands != currentBands || channels != channelCount {
            rebuildBiquads(sampleRate: sampleRate, bands: eqBands, channels: channels)
        }

        for channel in 0..<channels {
            let data = channelData[channel]

            // 1. Apply 10-band biquad EQ
            applyBiquadEQ(data: data, frameCount: frameCount, channel: channel)

            // 2. Apply volume scaling using vDSP
            var volumeFloat = Float(volume)
            vDSP_vsmul(data, 1, &volumeFloat, data, 1, vDSP_Length(frameCount))

            // 3. Apply soft clipping to prevent harsh distortion
            applySoftClipping(data: data, frameCount: frameCount)
        }

        return buffer
    }

    /// Rebuild all biquad filter setups when parameters change.
    private func rebuildBiquads(sampleRate: Double, bands: [Double], channels: Int) {
        for channelSetups in biquadSetups {
            for setup in channelSetups {
                if let setup = setup {
                    vDSP_biquad_DestroySetup(setup)
                }
            }
        }

        currentSampleRate = sampleRate
        currentBands = bands
        channelCount = channels

        biquadSetups = []
        biquadDelays = []

        for _ in 0..<channels {
            var channelSetups: [vDSP_biquad_Setup?] = []
            var channelDelays: [[Double]] = []

            for bandIndex in 0..<10 {
                let gainDB = bands[bandIndex]

                if abs(gainDB) < 0.01 {
                    channelSetups.append(nil)
                    channelDelays.append([Double](repeating: 0, count: 4 + 2))
                    continue
                }

                let coeffs = peakingEQCoefficients(
                    frequency: DSPProcessor.bandFrequencies[bandIndex],
                    gainDB: gainDB,
                    q: bandQ,
                    sampleRate: sampleRate
                )

                let setup = vDSP_biquad_CreateSetup(coeffs, 1)
                channelSetups.append(setup)
                // vDSP_biquad needs (2 * sections + 2) Doubles for delay storage
                channelDelays.append([Double](repeating: 0, count: 4))
            }

            biquadSetups.append(channelSetups)
            biquadDelays.append(channelDelays)
        }
    }

    /// Apply cascaded biquad EQ filters to audio data.
    private func applyBiquadEQ(data: UnsafeMutablePointer<Float>, frameCount: Int, channel: Int) {
        guard channel < biquadSetups.count else { return }

        // Convert to Double for vDSP_biquadD processing
        var inputD = [Double](repeating: 0, count: frameCount)
        var outputD = [Double](repeating: 0, count: frameCount)
        vDSP_vspdp(data, 1, &inputD, 1, vDSP_Length(frameCount))

        for bandIndex in 0..<10 {
            guard let setup = biquadSetups[channel][bandIndex] else { continue }

            vDSP_biquadD(
                setup,
                &biquadDelays[channel][bandIndex],
                inputD, 1,
                &outputD, 1,
                vDSP_Length(frameCount)
            )

            // Output becomes input for next band (cascaded)
            inputD = outputD
        }

        // Convert back to Float
        vDSP_vdpsp(inputD, 1, data, 1, vDSP_Length(frameCount))
    }

    /// Calculate biquad coefficients for a peaking EQ filter.
    ///
    /// Uses the Audio EQ Cookbook formula by Robert Bristow-Johnson.
    /// Returns [b0/a0, b1/a0, b2/a0, a1/a0, a2/a0] normalized coefficients.
    private func peakingEQCoefficients(frequency: Double, gainDB: Double, q: Double, sampleRate: Double) -> [Double] {
        let A = pow(10.0, gainDB / 40.0)
        let w0 = 2.0 * Double.pi * frequency / sampleRate
        let sinW0 = sin(w0)
        let cosW0 = cos(w0)
        let alpha = sinW0 / (2.0 * q)

        let b0 = 1.0 + alpha * A
        let b1 = -2.0 * cosW0
        let b2 = 1.0 - alpha * A
        let a0 = 1.0 + alpha / A
        let a1 = -2.0 * cosW0
        let a2 = 1.0 - alpha / A

        return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0]
    }

    /// Apply soft clipping using tanh to prevent harsh digital distortion.
    private func applySoftClipping(data: UnsafeMutablePointer<Float>, frameCount: Int) {
        var count = Int32(frameCount)
        var input = [Float](repeating: 0, count: frameCount)
        var output = [Float](repeating: 0, count: frameCount)

        memcpy(&input, data, frameCount * MemoryLayout<Float>.size)
        vvtanhf(&output, &input, &count)
        memcpy(data, &output, frameCount * MemoryLayout<Float>.size)
    }

    deinit {
        for channelSetups in biquadSetups {
            for setup in channelSetups {
                if let setup = setup {
                    vDSP_biquad_DestroySetup(setup)
                }
            }
        }
    }
}
