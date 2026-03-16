# why-is-my-audio-low-on-mac — Design Spec

Free, open-source volume booster and system-wide EQ for macOS.

---

## Problem

macOS caps headphone volume at 100% with no built-in way to boost beyond that limit. There is no system-wide EQ — Apple only offers per-app audio controls in limited contexts. Paid alternatives ($5-$39) exist but most are buggy, abandoned, or overpriced for what amounts to a volume knob and a few sliders.

Users need:
- Volume amplification past the OS limit (quiet headphones, low-mastered audio)
- Bass boost and EQ without per-app plugins
- Per-app volume control (Discord too loud, Spotify too quiet)
- Something free that just works

## Solution

A native macOS app that installs a lightweight virtual audio driver, routes all system audio through a DSP pipeline (volume boost + parametric EQ + soft clipping), and outputs to the real hardware device. No subscriptions, no telemetry, no network access.

### Core Features

- **Volume boost up to 500%** with soft clipping (no distortion at high gain)
- **10-band parametric EQ** with draggable frequency curve
- **6 presets** — Bass Boost, Warm, Vocal Clarity, Loudness, Flat, Custom
- **Per-app volume control** — independent sliders for each running audio app
- **Menu bar icon** with quick-access dropdown + full settings window
- **Native SwiftUI** — dark/light mode, vibrancy, feels like a first-party Apple app

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│  System Audio (Spotify, Browser, Games, etc.)   │
└──────────────────────┬──────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────┐
│  Virtual Audio Driver (CoreAudio HAL Plugin)     │
│  C++ · /Library/Audio/Plug-Ins/HAL/             │
│  Registers as default output device              │
│  Captures all system audio streams               │
└──────────────────────┬───────────────────────────┘
                       │ raw PCM frames
                       ▼
┌──────────────────────────────────────────────────┐
│  DSP Engine (Swift + Accelerate/vDSP)            │
│  1. Volume amplification (0-500%)                │
│  2. 10-band parametric EQ                        │
│  3. Bass shelf boost                             │
│  4. Soft clipping (tanh saturation)              │
└──────────────────────┬───────────────────────────┘
                       │ processed PCM frames
                       ▼
┌──────────────────────────────────────────────────┐
│  Real Output Device (headphones, speakers, DAC)  │
└──────────────────────────────────────────────────┘
```

### 1. Virtual Audio Driver

- **Language:** C++
- **Type:** CoreAudio HAL plugin (user-space, not kernel extension)
- **Install path:** `/Library/Audio/Plug-Ins/HAL/WhyIsMyAudioLow.driver`
- **Behavior:** Registers as a virtual output device. When set as default output, all system audio routes through it. The driver hands raw PCM buffers to the DSP engine via shared memory / IPC, receives processed audio back, and forwards to the selected real output device.
- **Sample rates:** 44.1kHz, 48kHz, 96kHz (match real device)
- **Channels:** Stereo (2ch). Pass-through for higher channel counts.
- **Latency target:** < 5ms added latency

### 2. DSP Engine

- **Language:** Swift
- **Framework:** Apple Accelerate (vDSP for FFT, vector math)
- **Pipeline per buffer:**
  1. Per-app volume scaling (if per-app control active)
  2. Volume amplification (linear gain, 0.0x to 5.0x)
  3. 10-band parametric EQ (biquad filter cascade)
  4. Soft clipping via `tanh` saturation to prevent digital clipping at high gain
- **EQ bands:** 10 user-adjustable center frequencies spanning 31Hz to 16kHz
- **Processing:** Float32, vectorized via vDSP for minimal CPU overhead

### 3. SwiftUI App

- **Menu bar agent** (LSUIElement) — no dock icon by default
- **Menu bar dropdown:** quick controls (volume slider, preset picker, output device)
- **Settings window:** full configuration with tabbed interface
- **IPC:** XPC connection between app and driver for real-time parameter updates

---

## Presets

| Preset | Description | EQ Shape |
|--------|-------------|----------|
| **Bass Boost** | +8dB at 60-120Hz, slight warmth at 200Hz | Low shelf up, gentle roll-off above 200Hz |
| **Warm** | Gentle low-mid boost (100-400Hz), smooth high roll-off above 8kHz | Mild hump in low-mids, subtle treble cut |
| **Vocal Clarity** | Mid boost at 1-4kHz, slight bass cut below 100Hz | Scoop at lows, peak in presence range |
| **Loudness** | Volume boost + bass/treble smile curve (Fletcher-Munson compensation) | U-shape: +6dB at 60Hz, +4dB at 12kHz |
| **Flat** | No EQ processing, volume boost only | Flat line |
| **Custom** | Full EQ sliders unlocked, user saves own curve | User-defined |

Presets are stored as JSON. Custom presets can be exported/imported.

---

## UI

### Menu Bar Dropdown

```
┌─────────────────────────────┐
│  🔊  Volume          275%   │
│  ├────────●──────────────┤  │
│                             │
│  Preset: Bass Boost    ▾    │
│                             │
│  Output: AirPods Pro   ▾    │
│                             │
│  ─────────────────────────  │
│  Settings...                │
│  Quit                       │
└─────────────────────────────┘
```

- Master volume slider: 0% to 500%, continuous
- Preset selector: dropdown with all 6 presets
- Output device picker: lists all available output devices
- Settings button opens the full window

### Settings Window

**Tabs:**

1. **General**
   - Launch at login (toggle)
   - Auto-switch to virtual device on launch (toggle)
   - Restore real output device on quit (toggle)
   - Menu bar icon style (default / minimal / hidden)

2. **EQ**
   - 10-band frequency curve — smooth draggable bezier, not blocky bars
   - Each band: center frequency, gain (-12dB to +12dB), Q factor
   - Preset selector with save/delete for custom presets
   - Real-time visual feedback as audio plays
   - Reset button

3. **Apps**
   - List of currently running apps producing audio
   - Independent volume slider per app (0-200%)
   - Mute toggle per app
   - Apps auto-appear/disappear as they start/stop audio

4. **About**
   - App version and build
   - GitHub repository link
   - Open-source license (MIT)
   - Credits and acknowledgments

### Design Principles

- Native macOS appearance — NSVisualEffectView vibrancy, SF Symbols, system fonts
- Dark and light mode support (automatic)
- Smooth EQ curve rendering using Core Graphics bezier paths
- Responsive — settings changes apply in real-time, no "Apply" button
- Accessible — full VoiceOver support, keyboard navigation

---

## Security & Permissions

| Requirement | Reason |
|-------------|--------|
| **Admin password** | One-time prompt during driver installation to `/Library/Audio/Plug-Ins/HAL/` |
| **Accessibility permission** | Required for per-app audio detection (optional — app works without it, just no per-app control) |
| **No microphone access** | The virtual driver captures output audio only, never input |
| **No network access** | Fully offline. Zero HTTP requests. No update checker, no analytics, no telemetry |
| **Code-signed driver** | Signed with Developer ID for Gatekeeper. Notarized for macOS 13+ |
| **Open source** | Full source on GitHub — anyone can audit the driver and DSP code |

---

## Tech Stack

| Component | Technology |
|-----------|-----------|
| App UI | Swift 5.9+, SwiftUI |
| Audio driver | C++17, CoreAudio HAL API |
| DSP processing | Swift, Apple Accelerate framework (vDSP) |
| IPC (app ↔ driver) | XPC Services |
| Build system | Xcode 15+, Swift Package Manager |
| Minimum OS | macOS 13 Ventura |
| Distribution | Homebrew cask, GitHub Releases (.dmg), manual .pkg installer |
| License | MIT |

---

## File Structure

```
why-is-my-audio-low-on-mac/
├── App/
│   ├── WhyIsMyAudioLowApp.swift       # App entry point, menu bar agent
│   ├── Views/
│   │   ├── MenuBarView.swift           # Dropdown popover
│   │   ├── SettingsView.swift          # Tabbed settings window
│   │   ├── EQView.swift                # Draggable EQ curve
│   │   ├── AppsView.swift              # Per-app volume sliders
│   │   └── AboutView.swift
│   ├── DSP/
│   │   ├── DSPEngine.swift             # Volume + EQ + clipping pipeline
│   │   ├── ParametricEQ.swift          # Biquad filter implementation
│   │   └── SoftClipper.swift           # tanh saturation
│   ├── Models/
│   │   ├── Preset.swift
│   │   ├── EQBand.swift
│   │   └── AudioDevice.swift
│   └── Services/
│       ├── DriverManager.swift         # Install/uninstall/status of HAL driver
│       ├── DeviceManager.swift         # Output device enumeration + switching
│       └── AppAudioManager.swift       # Per-app audio detection
├── Driver/
│   ├── WhyIsMyAudioLowDriver.cpp      # CoreAudio HAL plugin
│   ├── PlugIn.cpp                      # Plugin entry points
│   └── Info.plist
├── Installer/
│   ├── install.sh                      # Driver installation script
│   └── uninstall.sh
├── docs/
│   └── design-spec.md                  # This file
├── Package.swift
└── LICENSE
```

---

## Build & Install

```bash
# Clone
git clone https://github.com/user/why-is-my-audio-low-on-mac.git
cd why-is-my-audio-low-on-mac

# Build
xcodebuild -scheme WhyIsMyAudioLow -configuration Release

# Install driver (requires admin)
sudo ./Installer/install.sh

# Or via Homebrew
brew install --cask why-is-my-audio-low-on-mac
```

---

## Non-Goals

- No input/microphone processing (this is output-only)
- No audio recording or streaming
- No surround sound / spatial audio manipulation
- No iOS/iPadOS version
- No paid tier or in-app purchases
