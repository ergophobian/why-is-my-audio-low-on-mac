// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "WhyIsMyAudioLow",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .executable(
            name: "WhyIsMyAudioLow",
            targets: ["WhyIsMyAudioLow"]
        )
    ],
    targets: [
        .executableTarget(
            name: "WhyIsMyAudioLow",
            path: "WhyIsMyAudioLow",
            exclude: ["Resources/Assets.xcassets"],
            linkerSettings: [
                .linkedFramework("AVFoundation"),
                .linkedFramework("CoreAudio"),
                .linkedFramework("Accelerate"),
                .linkedFramework("AppKit")
            ]
        )
    ]
)
