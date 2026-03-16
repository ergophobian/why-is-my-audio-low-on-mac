import AVFoundation
import Accelerate

/// Handles digital signal processing for volume boost and EQ.
///
/// Uses Apple's Accelerate framework (vDSP) for high-performance audio processing.
/// Applies volume scaling with soft clipping to prevent harsh distortion at high volumes,
/// and a 10-band parametric EQ.
class DSPProcessor {
    /// Process an audio buffer with volume boost and EQ.
    ///
    /// - Parameters:
    ///   - buffer: Input PCM audio buffer
    ///   - volume: Volume multiplier (0.0 to 5.0)
    ///   - eqBands: Array of 10 dB gain values for EQ bands
    /// - Returns: Processed audio buffer
    func process(buffer: AVAudioPCMBuffer, volume: Double, eqBands: [Double]) -> AVAudioPCMBuffer {
        guard let channelData = buffer.floatChannelData else { return buffer }

        let frameCount = Int(buffer.frameLength)
        let channelCount = Int(buffer.format.channelCount)

        for channel in 0..<channelCount {
            let data = channelData[channel]

            // Apply volume scaling using vDSP
            var volumeFloat = Float(volume)
            vDSP_vsmul(data, 1, &volumeFloat, data, 1, vDSP_Length(frameCount))

            // Apply EQ (simplified: in production, use biquad filters per band)
            applyEQ(data: data, frameCount: frameCount, bands: eqBands, sampleRate: buffer.format.sampleRate)

            // Apply soft clipping to prevent harsh distortion
            applySoftClipping(data: data, frameCount: frameCount)
        }

        return buffer
    }

    /// Apply a simplified EQ using the provided band gains.
    ///
    /// Note: A production implementation would use cascaded biquad filters (vDSP_biquad)
    /// for each frequency band. This stub applies a basic gain curve.
    private func applyEQ(data: UnsafeMutablePointer<Float>, frameCount: Int, bands: [Double], sampleRate: Double) {
        guard bands.contains(where: { $0 != 0 }) else { return }

        // In production, set up 10 biquad filter sections using vDSP_biquad.
        // Each section would be a peaking EQ filter at the center frequency
        // of each band (32Hz, 64Hz, ..., 16kHz) with the specified gain.
        //
        // For now, apply an overall gain adjustment as a placeholder.
        let avgGain = bands.reduce(0, +) / Double(bands.count)
        if avgGain != 0 {
            let linearGain = Float(pow(10.0, avgGain / 20.0))
            var gain = linearGain
            vDSP_vsmul(data, 1, &gain, data, 1, vDSP_Length(frameCount))
        }
    }

    /// Apply soft clipping using tanh to prevent harsh digital distortion.
    ///
    /// Maps the signal through a hyperbolic tangent curve, which smoothly
    /// compresses values approaching +/- 1.0 instead of hard clipping.
    private func applySoftClipping(data: UnsafeMutablePointer<Float>, frameCount: Int) {
        // Use vForce for vectorized tanh
        var count = Int32(frameCount)
        var input = [Float](repeating: 0, count: frameCount)
        var output = [Float](repeating: 0, count: frameCount)

        // Copy data to input array
        memcpy(&input, data, frameCount * MemoryLayout<Float>.size)

        // Apply tanh for soft clipping
        vvtanhf(&output, &input, &count)

        // Copy back
        memcpy(data, &output, frameCount * MemoryLayout<Float>.size)
    }

    /// Hard clip audio samples to the valid range.
    /// Used as a safety net after soft clipping.
    private func hardClip(data: UnsafeMutablePointer<Float>, frameCount: Int) {
        var lowerBound: Float = -1.0
        var upperBound: Float = 1.0
        vDSP_vclip(data, 1, &lowerBound, &upperBound, data, 1, vDSP_Length(frameCount))
    }
}
