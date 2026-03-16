import Foundation

enum EQPreset: String, CaseIterable, Identifiable, Codable {
    case flat
    case bassBoost
    case warm
    case vocalClarity
    case loudness
    case custom

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .flat: return "Flat"
        case .bassBoost: return "Bass Boost"
        case .warm: return "Warm"
        case .vocalClarity: return "Vocal Clarity"
        case .loudness: return "Loudness"
        case .custom: return "Custom"
        }
    }

    /// 10-band EQ values in dB, corresponding to:
    /// 32Hz, 64Hz, 125Hz, 250Hz, 500Hz, 1kHz, 2kHz, 4kHz, 8kHz, 16kHz
    var bands: [Double] {
        switch self {
        case .flat:
            return [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
        case .bassBoost:
            return [8, 6, 4, 2, 0, 0, 0, 0, 0, 0]
        case .warm:
            return [4, 3, 2, 1, 0, -1, -1, -2, -3, -4]
        case .vocalClarity:
            return [-2, -1, 0, 2, 4, 5, 4, 3, 1, 0]
        case .loudness:
            return [6, 4, 0, -2, -1, 0, 2, 4, 5, 6]
        case .custom:
            return [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
        }
    }
}
