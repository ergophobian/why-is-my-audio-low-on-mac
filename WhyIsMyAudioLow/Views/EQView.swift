import SwiftUI

struct EQView: View {
    @EnvironmentObject var audioState: AudioState

    private let bandLabels = AudioState.bandFrequencies
    private let dbRange: ClosedRange<Double> = -12...12

    var body: some View {
        VStack(spacing: 16) {
            // Preset selector and reset
            HStack {
                Picker("Preset", selection: $audioState.selectedPreset) {
                    ForEach(EQPreset.allCases) { preset in
                        Text(preset.displayName).tag(preset)
                    }
                }
                .frame(width: 200)
                .onChange(of: audioState.selectedPreset) { newPreset in
                    audioState.applyPreset(newPreset)
                }

                Spacer()

                Button("Reset") {
                    audioState.resetEQ()
                }
            }
            .padding(.horizontal)

            // EQ curve visualization
            EQCurveView(bands: audioState.eqBands, dbRange: dbRange)
                .frame(height: 120)
                .padding(.horizontal)

            // 10-band vertical sliders
            HStack(alignment: .bottom, spacing: 8) {
                ForEach(0..<10, id: \.self) { index in
                    EQBandSlider(
                        value: Binding(
                            get: { audioState.eqBands[index] },
                            set: { newValue in
                                audioState.eqBands[index] = newValue
                                audioState.selectedPreset = .custom
                            }
                        ),
                        label: bandLabels[index],
                        dbRange: dbRange
                    )
                }
            }
            .padding(.horizontal)

            Spacer()
        }
        .padding(.vertical)
    }
}

// MARK: - EQ Curve Visualization

struct EQCurveView: View {
    let bands: [Double]
    let dbRange: ClosedRange<Double>

    var body: some View {
        GeometryReader { geometry in
            let width = geometry.size.width
            let height = geometry.size.height

            ZStack {
                // Background grid
                gridLines(width: width, height: height)

                // Zero line
                Path { path in
                    let y = height / 2
                    path.move(to: CGPoint(x: 0, y: y))
                    path.addLine(to: CGPoint(x: width, y: y))
                }
                .stroke(Color.secondary.opacity(0.5), lineWidth: 1)

                // dB labels
                VStack {
                    Text("+12")
                        .font(.system(size: 9).monospacedDigit())
                        .foregroundColor(.secondary)
                    Spacer()
                    Text("0")
                        .font(.system(size: 9).monospacedDigit())
                        .foregroundColor(.secondary)
                    Spacer()
                    Text("-12")
                        .font(.system(size: 9).monospacedDigit())
                        .foregroundColor(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)

                // Smooth EQ curve using Catmull-Rom interpolation
                catmullRomCurvePath(width: width, height: height)
                    .stroke(
                        LinearGradient(
                            colors: [.blue, .purple, .pink],
                            startPoint: .leading,
                            endPoint: .trailing
                        ),
                        lineWidth: 2.5
                    )

                // Filled area under curve
                catmullRomCurveFilledPath(width: width, height: height)
                    .fill(
                        LinearGradient(
                            colors: [.blue.opacity(0.2), .purple.opacity(0.15), .pink.opacity(0.1)],
                            startPoint: .leading,
                            endPoint: .trailing
                        )
                    )

                // Band dots
                ForEach(0..<bands.count, id: \.self) { index in
                    let point = bandPoint(index: index, width: width, height: height)
                    Circle()
                        .fill(Color.white)
                        .frame(width: 6, height: 6)
                        .shadow(color: .accentColor, radius: 3)
                        .position(point)
                }
            }
        }
        .background(Color.black.opacity(0.15))
        .cornerRadius(8)
    }

    private func gridLines(width: CGFloat, height: CGFloat) -> some View {
        Path { path in
            // Vertical lines for each band
            for i in 0..<bands.count {
                let x = bandX(index: i, width: width)
                path.move(to: CGPoint(x: x, y: 0))
                path.addLine(to: CGPoint(x: x, y: height))
            }
            // Horizontal lines at -6 and +6
            for db in stride(from: -12.0, through: 12.0, by: 6.0) {
                let y = dbToY(db: db, height: height)
                path.move(to: CGPoint(x: 0, y: y))
                path.addLine(to: CGPoint(x: width, y: y))
            }
        }
        .stroke(Color.secondary.opacity(0.15), lineWidth: 0.5)
    }

    private func bandX(index: Int, width: CGFloat) -> CGFloat {
        let padding: CGFloat = 20
        let usable = width - padding * 2
        return padding + usable * CGFloat(index) / CGFloat(bands.count - 1)
    }

    private func dbToY(db: Double, height: CGFloat) -> CGFloat {
        let normalized = (db - dbRange.lowerBound) / (dbRange.upperBound - dbRange.lowerBound)
        return height * (1.0 - CGFloat(normalized))
    }

    private func bandPoint(index: Int, width: CGFloat, height: CGFloat) -> CGPoint {
        CGPoint(x: bandX(index: index, width: width), y: dbToY(db: bands[index], height: height))
    }

    // Catmull-Rom spline interpolation for smooth EQ curve
    private func catmullRomCurvePath(width: CGFloat, height: CGFloat) -> Path {
        Path { path in
            let points = (0..<bands.count).map { bandPoint(index: $0, width: width, height: height) }
            guard points.count >= 2 else { return }

            path.move(to: points[0])

            for i in 0..<(points.count - 1) {
                let p0 = i > 0 ? points[i - 1] : points[i]
                let p1 = points[i]
                let p2 = points[i + 1]
                let p3 = (i + 2) < points.count ? points[i + 2] : points[i + 1]

                let segments = 20
                for t in 1...segments {
                    let tNorm = CGFloat(t) / CGFloat(segments)
                    let point = catmullRomPoint(t: tNorm, p0: p0, p1: p1, p2: p2, p3: p3)
                    path.addLine(to: point)
                }
            }
        }
    }

    private func catmullRomCurveFilledPath(width: CGFloat, height: CGFloat) -> Path {
        Path { path in
            let points = (0..<bands.count).map { bandPoint(index: $0, width: width, height: height) }
            guard points.count >= 2 else { return }

            let zeroY = dbToY(db: 0, height: height)

            path.move(to: CGPoint(x: points[0].x, y: zeroY))
            path.addLine(to: points[0])

            for i in 0..<(points.count - 1) {
                let p0 = i > 0 ? points[i - 1] : points[i]
                let p1 = points[i]
                let p2 = points[i + 1]
                let p3 = (i + 2) < points.count ? points[i + 2] : points[i + 1]

                let segments = 20
                for t in 1...segments {
                    let tNorm = CGFloat(t) / CGFloat(segments)
                    let point = catmullRomPoint(t: tNorm, p0: p0, p1: p1, p2: p2, p3: p3)
                    path.addLine(to: point)
                }
            }

            path.addLine(to: CGPoint(x: points.last!.x, y: zeroY))
            path.closeSubpath()
        }
    }

    private func catmullRomPoint(t: CGFloat, p0: CGPoint, p1: CGPoint, p2: CGPoint, p3: CGPoint) -> CGPoint {
        let alpha: CGFloat = 0.5
        let t2 = t * t
        let t3 = t2 * t

        let x = alpha * (
            (2 * p1.x) +
            (-p0.x + p2.x) * t +
            (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 +
            (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3
        )

        let y = alpha * (
            (2 * p1.y) +
            (-p0.y + p2.y) * t +
            (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 +
            (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3
        )

        return CGPoint(x: x, y: y)
    }
}

// MARK: - EQ Band Slider

struct EQBandSlider: View {
    @Binding var value: Double
    let label: String
    let dbRange: ClosedRange<Double>

    var body: some View {
        VStack(spacing: 4) {
            Text(String(format: "%+.0f", value))
                .font(.system(size: 9).monospacedDigit())
                .foregroundColor(value == 0 ? .secondary : .primary)
                .frame(height: 14)

            VerticalSlider(value: $value, range: dbRange)
                .frame(height: 140)

            Text(label)
                .font(.system(size: 10))
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity)
    }
}

// MARK: - Vertical Slider

struct VerticalSlider: View {
    @Binding var value: Double
    let range: ClosedRange<Double>

    var body: some View {
        GeometryReader { geometry in
            let height = geometry.size.height
            let normalized = (value - range.lowerBound) / (range.upperBound - range.lowerBound)
            let thumbY = height * (1.0 - CGFloat(normalized))

            ZStack {
                // Track
                RoundedRectangle(cornerRadius: 2)
                    .fill(Color.secondary.opacity(0.2))
                    .frame(width: 4)

                // Zero line indicator
                Rectangle()
                    .fill(Color.secondary.opacity(0.4))
                    .frame(width: 12, height: 1)
                    .position(x: geometry.size.width / 2, y: height / 2)

                // Active track
                Rectangle()
                    .fill(Color.accentColor.opacity(0.6))
                    .frame(width: 4, height: abs(thumbY - height / 2))
                    .position(x: geometry.size.width / 2, y: (thumbY + height / 2) / 2)

                // Thumb
                Circle()
                    .fill(Color.white)
                    .shadow(radius: 2)
                    .frame(width: 14, height: 14)
                    .position(x: geometry.size.width / 2, y: thumbY)
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { gesture in
                        let normalized = 1.0 - Double(gesture.location.y / height)
                        let clamped = min(max(normalized, 0), 1)
                        value = range.lowerBound + clamped * (range.upperBound - range.lowerBound)
                        // Snap to zero when close
                        if abs(value) < 0.5 {
                            value = 0
                        }
                    }
            )
        }
    }
}

#Preview {
    EQView()
        .environmentObject(AudioState())
        .frame(width: 580, height: 420)
}
