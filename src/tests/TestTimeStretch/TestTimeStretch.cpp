// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "TimeStretch.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {
constexpr unsigned int CHANNELS  = 2;
constexpr double SAMPLE_RATE     = 48000.0;
constexpr std::size_t PACKET     = 1920; // 40 ms
constexpr std::size_t SHORTENED  = PACKET * 2 / 3;
constexpr std::size_t LENGTHENED = PACKET * 3 / 2;
constexpr float SENTINEL         = 12345.0f;

/// Period of the 200 Hz sine in frames.
constexpr std::size_t SINE_PERIOD = 240;

/// RisingSine200 has a growing amplitude, so neighbouring periods differ and a wrong crossfade shows as a jump.
enum class Signal { Sine200, RisingSine200, Harmonic150 };

float sampleAt(Signal signal, std::size_t frame) {
	const double t = static_cast< double >(frame) / SAMPLE_RATE;
	if (signal == Signal::Sine200) {
		return static_cast< float >(0.5 * std::sin(2.0 * M_PI * 200.0 * t));
	}
	if (signal == Signal::RisingSine200) {
		const double amplitude = 0.2 + 0.6 * static_cast< double >(frame) / static_cast< double >(PACKET);
		return static_cast< float >(amplitude * std::cos(2.0 * M_PI * 200.0 * t));
	}
	const double amplitudes[] = { 0.4, 0.2, 0.12, 0.08 };
	double value              = 0.0;
	for (std::size_t k = 0; k < 4; ++k) {
		const double harmonic = static_cast< double >(k + 1);
		value += amplitudes[k] * std::sin(2.0 * M_PI * 150.0 * harmonic * t + 0.3 * harmonic);
	}
	return static_cast< float >(value);
}

/// Interleaved stereo with the right channel at `rightGain` times the left one.
std::vector< float > makeInput(Signal signal, std::size_t frames, float rightGain = 1.0f) {
	std::vector< float > samples(frames * CHANNELS);
	for (std::size_t i = 0; i < frames; ++i) {
		samples[i * CHANNELS]     = sampleAt(signal, i);
		samples[i * CHANNELS + 1] = rightGain * samples[i * CHANNELS];
	}
	return samples;
}

std::vector< float > stretch(const std::vector< float > &in, std::size_t target, std::size_t capacity) {
	std::vector< float > out(capacity * CHANNELS);
	const std::size_t written =
		TimeStretch::process(in.data(), in.size() / CHANNELS, CHANNELS, target, out.data(), capacity);
	out.resize(written * CHANNELS);
	return out;
}

std::vector< float > stretch(const std::vector< float > &in, std::size_t target) {
	return stretch(in, target, std::max(target, in.size() / CHANNELS) + TimeStretch::MAX_PERIOD);
}

/// Lag in [100, 400] with the highest normalised autocorrelation of the left channel.
std::size_t dominantLag(const std::vector< float > &samples) {
	const std::size_t frames = samples.size() / CHANNELS;
	std::size_t bestLag      = 0;
	double best              = -2.0;
	for (std::size_t lag = 100; lag <= 400; ++lag) {
		double cross = 0.0, energyA = 0.0, energyB = 0.0;
		for (std::size_t i = 0; i + lag < frames; ++i) {
			const double a = samples[i * CHANNELS];
			const double b = samples[(i + lag) * CHANNELS];
			cross += a * b;
			energyA += a * a;
			energyB += b * b;
		}
		const double value = cross / std::sqrt(energyA * energyB);
		if (value > best) {
			best    = value;
			bestLag = lag;
		}
	}
	return bestLag;
}

float maxStep(const std::vector< float > &samples) {
	float step = 0.0f;
	for (std::size_t i = CHANNELS; i < samples.size(); ++i) {
		step = std::max(step, std::abs(samples[i] - samples[i - CHANNELS]));
	}
	return step;
}

std::size_t distance(std::size_t a, std::size_t b) {
	return a > b ? a - b : b - a;
}
} // namespace

Q_DECLARE_METATYPE(Signal)

class TestTimeStretch : public QObject {
	Q_OBJECT
private slots:
	void reachesTarget_data();
	void reachesTarget();
	void keepsPitch_data();
	void keepsPitch();
	void keepsPacketEdges_data();
	void keepsPacketEdges();
	void hasNoDiscontinuity_data();
	void hasNoDiscontinuity();
	void keepsChannelsAligned_data();
	void keepsChannelsAligned();
	void copiesUnchangedInput_data();
	void copiesUnchangedInput();
	void neverWritesPastCapacity_data();
	void neverWritesPastCapacity();
	void staysPeriodic_data();
	void staysPeriodic();
};

namespace {
void addDirections() {
	QTest::addColumn< std::size_t >("target");
	QTest::newRow("shorten") << SHORTENED;
	QTest::newRow("lengthen") << LENGTHENED;
}

void addSignalsAndDirections() {
	QTest::addColumn< Signal >("signal");
	QTest::addColumn< std::size_t >("target");
	QTest::newRow("sine shorten") << Signal::Sine200 << SHORTENED;
	QTest::newRow("sine lengthen") << Signal::Sine200 << LENGTHENED;
	QTest::newRow("harmonic shorten") << Signal::Harmonic150 << SHORTENED;
	QTest::newRow("harmonic lengthen") << Signal::Harmonic150 << LENGTHENED;
}
} // namespace

void TestTimeStretch::reachesTarget_data() {
	QTest::addColumn< Signal >("signal");
	QTest::addColumn< std::size_t >("target");
	QTest::addColumn< std::size_t >("tolerance");
	QTest::newRow("harmonic shorten") << Signal::Harmonic150 << SHORTENED << TimeStretch::MAX_PERIOD;
	QTest::newRow("harmonic lengthen") << Signal::Harmonic150 << LENGTHENED << TimeStretch::MAX_PERIOD;
	// With a known period and enough room, no step may move away from the target.
	QTest::newRow("sine shorten") << Signal::Sine200 << SHORTENED << SINE_PERIOD / 2;
	QTest::newRow("sine lengthen") << Signal::Sine200 << std::size_t{ 2300 } << SINE_PERIOD / 2;
}

void TestTimeStretch::reachesTarget() {
	QFETCH(Signal, signal);
	QFETCH(std::size_t, target);
	QFETCH(std::size_t, tolerance);
	const std::vector< float > out = stretch(makeInput(signal, PACKET), target);
	const std::size_t frames       = out.size() / CHANNELS;
	QVERIFY2(distance(frames, target) <= tolerance, qPrintable(QString::number(frames)));
	QVERIFY(target < PACKET ? frames < PACKET : frames > PACKET);
}

void TestTimeStretch::keepsPitch_data() {
	addSignalsAndDirections();
}

void TestTimeStretch::keepsPitch() {
	QFETCH(Signal, signal);
	QFETCH(std::size_t, target);
	const std::vector< float > in  = makeInput(signal, PACKET);
	const std::vector< float > out = stretch(in, target);
	const double inLag             = static_cast< double >(dominantLag(in));
	const double outLag            = static_cast< double >(dominantLag(out));
	QVERIFY(out.size() != in.size());
	QVERIFY2(std::abs(outLag - inLag) <= 0.02 * inLag, qPrintable(QString("%1 vs %2").arg(outLag).arg(inLag)));
}

void TestTimeStretch::keepsPacketEdges_data() {
	addSignalsAndDirections();
}

void TestTimeStretch::keepsPacketEdges() {
	QFETCH(Signal, signal);
	QFETCH(std::size_t, target);
	const std::vector< float > in  = makeInput(signal, PACKET);
	const std::vector< float > out = stretch(in, target);
	const std::size_t edge         = TimeStretch::MIN_PERIOD * CHANNELS;
	QVERIFY(out.size() != in.size());
	QVERIFY(std::equal(in.begin(), in.begin() + edge, out.begin()));
	QVERIFY(std::equal(in.end() - edge, in.end(), out.end() - edge));
}

void TestTimeStretch::hasNoDiscontinuity_data() {
	QTest::addColumn< Signal >("signal");
	QTest::addColumn< std::size_t >("target");
	QTest::newRow("sine shorten") << Signal::Sine200 << SHORTENED;
	QTest::newRow("sine lengthen") << Signal::Sine200 << LENGTHENED;
	QTest::newRow("rising sine shorten") << Signal::RisingSine200 << SHORTENED;
	QTest::newRow("rising sine lengthen") << Signal::RisingSine200 << LENGTHENED;
}

void TestTimeStretch::hasNoDiscontinuity() {
	QFETCH(Signal, signal);
	QFETCH(std::size_t, target);
	const std::vector< float > in  = makeInput(signal, PACKET);
	const std::vector< float > out = stretch(in, target);
	QVERIFY(out.size() != in.size());
	QVERIFY2(maxStep(out) <= 1.25f * maxStep(in), qPrintable(QString("%1 vs %2").arg(maxStep(out)).arg(maxStep(in))));
}

void TestTimeStretch::keepsChannelsAligned_data() {
	addDirections();
}

void TestTimeStretch::keepsChannelsAligned() {
	QFETCH(std::size_t, target);
	const std::vector< float > out = stretch(makeInput(Signal::Harmonic150, PACKET, 0.5f), target);
	QVERIFY(out.size() != PACKET * CHANNELS);
	for (std::size_t i = 0; i < out.size(); i += CHANNELS) {
		QCOMPARE(out[i + 1], 0.5f * out[i]);
	}
}

void TestTimeStretch::copiesUnchangedInput_data() {
	QTest::addColumn< std::size_t >("frames");
	QTest::addColumn< std::size_t >("target");
	QTest::newRow("too short to shorten") << 4 * TimeStretch::MIN_PERIOD - 1 << std::size_t{ 240 };
	QTest::newRow("too short to lengthen") << 4 * TimeStretch::MIN_PERIOD - 1 << std::size_t{ 960 };
	QTest::newRow("target already met") << PACKET << PACKET;
}

void TestTimeStretch::copiesUnchangedInput() {
	QFETCH(std::size_t, frames);
	QFETCH(std::size_t, target);
	const std::vector< float > in = makeInput(Signal::Harmonic150, frames);
	QCOMPARE(stretch(in, target), in);
}

void TestTimeStretch::neverWritesPastCapacity_data() {
	QTest::addColumn< std::size_t >("frames");
	QTest::addColumn< std::size_t >("target");
	QTest::addColumn< std::size_t >("capacity");
	QTest::newRow("lengthen beyond capacity") << PACKET << std::size_t{ 4000 } << std::size_t{ 2200 };
	QTest::newRow("lengthen with capacity below input") << PACKET << LENGTHENED << std::size_t{ 1500 };
	QTest::newRow("shorten with capacity below result") << PACKET << SHORTENED << std::size_t{ 1000 };
	QTest::newRow("copy with capacity below input") << std::size_t{ 400 } << std::size_t{ 400 } << std::size_t{ 300 };
}

void TestTimeStretch::neverWritesPastCapacity() {
	QFETCH(std::size_t, frames);
	QFETCH(std::size_t, target);
	QFETCH(std::size_t, capacity);
	const std::vector< float > in = makeInput(Signal::Harmonic150, frames);
	const std::size_t guard       = 64 * CHANNELS;
	std::vector< float > out(capacity * CHANNELS + guard, SENTINEL);

	const std::size_t written = TimeStretch::process(in.data(), frames, CHANNELS, target, out.data(), capacity);
	QVERIFY(written <= capacity);
	QVERIFY(std::all_of(out.end() - static_cast< std::ptrdiff_t >(guard), out.end(),
						[](float sample) { return sample == SENTINEL; }));
	if (capacity >= frames) {
		// An operation that does not fit is skipped rather than cutting off the packet end.
		const std::size_t edge = TimeStretch::MIN_PERIOD * CHANNELS;
		QVERIFY(std::equal(in.end() - static_cast< std::ptrdiff_t >(edge), in.end(),
						   out.begin() + static_cast< std::ptrdiff_t >(written * CHANNELS - edge)));
	}
}

// A splice at a lag that is not the pitch period crossfades the signal with a shifted copy of itself, which shows as
// a stationary tone that is no longer periodic. Short packets leave too little room for low voices.
void TestTimeStretch::staysPeriodic_data() {
	QTest::addColumn< double >("frequency");
	QTest::addColumn< std::size_t >("frames");
	QTest::addColumn< bool >("shorten");
	for (const double frequency : { 100.0, 120.0, 200.0 }) {
		for (const std::size_t frames : { std::size_t{ 960 }, std::size_t{ 1920 } }) {
			for (const bool shorten : { true, false }) {
				QTest::newRow(qPrintable(
					QString("%1 Hz, %2 frames, %3").arg(frequency).arg(frames).arg(shorten ? "shorten" : "lengthen")))
					<< frequency << frames << shorten;
			}
		}
	}
}

void TestTimeStretch::staysPeriodic() {
	QFETCH(double, frequency);
	QFETCH(std::size_t, frames);
	QFETCH(bool, shorten);
	std::vector< float > in(frames * CHANNELS);
	for (std::size_t i = 0; i < frames; ++i) {
		in[i * CHANNELS] = in[i * CHANNELS + 1] =
			static_cast< float >(0.5 * std::sin(2.0 * M_PI * frequency * static_cast< double >(i) / SAMPLE_RATE));
	}
	const std::vector< float > out = stretch(in, shorten ? frames * 2 / 3 : frames * 3 / 2);

	const std::size_t period = static_cast< std::size_t >(SAMPLE_RATE / frequency) * CHANNELS;
	double error = 0.0, energy = 0.0;
	for (std::size_t i = period; i < out.size(); ++i) {
		const double difference = static_cast< double >(out[i]) - static_cast< double >(out[i - period]);
		error += difference * difference;
		energy += static_cast< double >(out[i]) * static_cast< double >(out[i]);
	}
	const double relativeError = std::sqrt(error / energy);
	QVERIFY2(relativeError < 0.05, qPrintable(QString::number(relativeError)));
}

QTEST_MAIN(TestTimeStretch)
#include "TestTimeStretch.moc"
