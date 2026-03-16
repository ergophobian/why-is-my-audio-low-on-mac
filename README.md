# Why Is My Audio Low on Mac?

Free, open-source system-wide volume booster and EQ for macOS. Because your earbuds shouldn't sound like trash.

## What it does

- **Volume boost up to 500%** — way past what macOS allows
- **10-band parametric EQ** — real biquad filters, not a toy
- **Presets** — Bass Boost, Warm, Vocal Clarity, Loudness, Flat, or fully Custom
- **Per-app volume control** — Spotify at 300%, Discord at 80%
- **Menu bar app** — always accessible, out of the way
- **No distortion** — tanh soft clipping keeps audio clean at high volumes

## Why this exists

macOS volume is too quiet with wired earbuds. Every solution is either $39 paid software or broken. So we built one that actually works — and made it free.

## Install

### Homebrew (coming soon)

```bash
brew install --cask why-is-my-audio-low-on-mac
```

### From source

```bash
git clone https://github.com/ergophobian/why-is-my-audio-low-on-mac.git
cd why-is-my-audio-low-on-mac
swift build

# Build and install the audio driver (requires admin password)
cd AudioDriver
bash build.sh
sudo cp -R build/AudioBoostDriver.driver /Library/Audio/Plug-Ins/HAL/
sudo chown -R root:wheel /Library/Audio/Plug-Ins/HAL/AudioBoostDriver.driver
sudo killall coreaudiod
```

## How it works

1. A virtual audio driver (CoreAudio HAL plugin) becomes your default output device
2. All system audio routes through it into a ring buffer
3. DSP engine applies your volume boost and EQ settings (using Apple's Accelerate/vDSP)
4. Processed audio forwards to your real headphones/speakers

No kernel extensions. No network access. Fully offline. Fully auditable.

## Requirements

- macOS 13+ (Ventura or later)
- Apple Silicon or Intel Mac

## Tech stack

- Swift 5.9 / SwiftUI (app)
- C (CoreAudio HAL audio driver)
- Apple Accelerate/vDSP (DSP processing)

## EQ Presets

| Preset | What it does |
|--------|-------------|
| Bass Boost | +8dB at 60-120Hz, warmth |
| Warm | Low-mid boost, smooth highs |
| Vocal Clarity | Mid boost (1-4kHz) |
| Loudness | Bass + treble smile curve |
| Flat | No EQ, just volume boost |
| Custom | Full 10-band control |

## Security

- No network access (fully offline)
- No analytics or telemetry
- No microphone access
- Admin password only for one-time driver install
- Open source — audit every line

## License

MIT
