// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "FecLookahead.h"
#include "MumbleProtocol.h"

#include <opus.h>
#include <speex/speex_jitter.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace {
constexpr std::uint32_t TEN_MS  = 960;         // interleaved stereo samples per 10 ms, the jitter buffer's unit
constexpr std::uint32_t MAX_GAP = 12 * TEN_MS; // 120 ms, AudioOutputSpeech's decode buffer
constexpr double PI             = 3.14159265358979323846;

QByteArray bytes(char c) {
	return QByteArray(1, c);
}

void keepPacket(void *) {
}

std::vector< void * > released;
void onRelease(void *data) {
	released.push_back(data);
}

double squaredError(const std::vector< float > &a, const std::vector< float > &b) {
	double e = 0.0;
	for (std::size_t i = 0; i < a.size(); ++i) {
		e += (a[i] - b[i]) * (a[i] - b[i]);
	}
	return e;
}
} // namespace

class TestFecLookahead : public QObject {
	Q_OBJECT
private slots:
	void nextAfterFindsClosestFollowingPacket_data();
	void nextAfterFindsClosestFollowingPacket();
	void nextAfterRespectsMaxGap();
	void nextAfterHandlesWrapAround();
	void pruneForgetsPlayedPackets();
	void insertIgnoresDuplicates();
	void insertEvictsOldestWhenFull();
	void insertIgnoresOversizedPayloads();
	void skipToNextLandsOnFollowingPacket_data();
	void skipToNextLandsOnFollowingPacket();
	void fecRecoversLostPacket();
};

void TestFecLookahead::nextAfterFindsClosestFollowingPacket_data() {
	QTest::addColumn< quint32 >("span");
	QTest::newRow("10 ms") << quint32(TEN_MS);
	QTest::newRow("20 ms") << quint32(2 * TEN_MS);
	QTest::newRow("40 ms") << quint32(4 * TEN_MS);
	QTest::newRow("60 ms") << quint32(6 * TEN_MS);
}

void TestFecLookahead::nextAfterFindsClosestFollowingPacket() {
	QFETCH(quint32, span);
	FecLookahead lookahead;
	// Inserted out of order; packet 3 is lost.
	lookahead.insert(5 * span, span, bytes('5'));
	lookahead.insert(4 * span, span, bytes('4'));
	lookahead.insert(2 * span, span, bytes('2'));

	const FecLookahead::Packet *next = lookahead.nextAfter(3 * span, MAX_GAP);
	QVERIFY(next);
	QCOMPARE(next->timestamp, 4 * span);
	QCOMPARE(next->payload, bytes('4'));
	QVERIFY(!lookahead.nextAfter(5 * span, MAX_GAP));
}

void TestFecLookahead::nextAfterRespectsMaxGap() {
	FecLookahead lookahead;
	lookahead.insert(MAX_GAP + TEN_MS, 2 * TEN_MS, bytes('x'));
	QVERIFY(!lookahead.nextAfter(0, MAX_GAP));
	QVERIFY(lookahead.nextAfter(TEN_MS, MAX_GAP));
}

void TestFecLookahead::nextAfterHandlesWrapAround() {
	FecLookahead lookahead;
	const std::uint32_t gapStart = 0xFFFFFFFFu - TEN_MS + 1;
	lookahead.insert(gapStart + 2 * TEN_MS, 2 * TEN_MS, bytes('w')); // wraps past 0
	const FecLookahead::Packet *next = lookahead.nextAfter(gapStart, MAX_GAP);
	QVERIFY(next);
	QCOMPARE(next->payload, bytes('w'));
}

void TestFecLookahead::pruneForgetsPlayedPackets() {
	FecLookahead lookahead;
	lookahead.insert(0, 2 * TEN_MS, bytes('a'));
	lookahead.insert(2 * TEN_MS, 2 * TEN_MS, bytes('b'));
	lookahead.prune(2 * TEN_MS); // packet a ends exactly at the pointer
	QCOMPARE(lookahead.size(), std::size_t(1));
	lookahead.prune(3 * TEN_MS); // b is still being played
	QCOMPARE(lookahead.size(), std::size_t(1));
	lookahead.prune(4 * TEN_MS);
	QCOMPARE(lookahead.size(), std::size_t(0));
}

void TestFecLookahead::insertIgnoresDuplicates() {
	FecLookahead lookahead;
	lookahead.insert(0, TEN_MS, bytes('a'));
	lookahead.insert(0, TEN_MS, bytes('b'));
	QCOMPARE(lookahead.size(), std::size_t(1));
}

void TestFecLookahead::insertIgnoresOversizedPayloads() {
	FecLookahead lookahead;
	const auto limit = static_cast< int >(Mumble::Protocol::MAX_UDP_PACKET_SIZE);
	lookahead.insert(0, TEN_MS, QByteArray(limit + 1, 'x'));
	QCOMPARE(lookahead.size(), std::size_t(0));
	lookahead.insert(TEN_MS, TEN_MS, QByteArray(limit, 'x'));
	QCOMPARE(lookahead.size(), std::size_t(1));
}

void TestFecLookahead::insertEvictsOldestWhenFull() {
	FecLookahead lookahead;
	for (std::uint32_t i = 0; i <= FecLookahead::MAX_PACKETS; ++i) {
		lookahead.insert(i * TEN_MS, TEN_MS, bytes('p'));
	}
	QCOMPARE(lookahead.size(), FecLookahead::MAX_PACKETS);
	const auto newest = static_cast< std::uint32_t >(FecLookahead::MAX_PACKETS) * TEN_MS;
	QVERIFY(!lookahead.nextAfter(0xFFFFFFFFu, 1)); // packet 0 was evicted
	QVERIFY(lookahead.nextAfter(TEN_MS - 1, 1));
	QVERIFY(lookahead.nextAfter(newest - 1, 1));
}

void TestFecLookahead::skipToNextLandsOnFollowingPacket_data() {
	QTest::addColumn< quint32 >("span");
	QTest::addColumn< quint32 >("lost");
	for (const quint32 ms : { 10u, 20u, 40u, 60u }) {
		for (const quint32 lost : { 1u, 2u }) {
			QTest::addRow("%u ms, %u lost", ms, lost) << quint32(ms / 10 * TEN_MS) << lost;
		}
	}
}

// Plays packets from a real jitter buffer the way AudioOutputSpeech does, with packets arriving well ahead of
// playback, and loses `lost` packets. The loss comes after more than 40 packets were timed on arrival, the point from
// which the buffer's delay estimate would shrink the delay.
void TestFecLookahead::skipToNextLandsOnFollowingPacket() {
	QFETCH(quint32, span);
	QFETCH(quint32, lost);
	constexpr quint32 SLACK = 5, FIRST_LOST = 60;
	const quint32 framesPerPacket = span / TEN_MS;
	const quint32 recoveredTs     = (FIRST_LOST + lost) * span;

	std::unique_ptr< JitterBuffer, decltype(&jitter_buffer_destroy) > jitter(jitter_buffer_init(TEN_MS),
																			 &jitter_buffer_destroy);
	jitter_buffer_ctl(jitter.get(), JITTER_BUFFER_SET_DESTROY_CALLBACK, reinterpret_cast< void * >(&keepPacket));
	// AudioOutputSpeech calls this, which turns off the buffer's automatic delay adjustment
	jitter_buffer_update_delay(jitter.get(), nullptr, nullptr);
	const auto pointer = [&]() { return static_cast< quint32 >(jitter_buffer_get_pointer_timestamp(jitter.get())); };

	FecLookahead lookahead;
	released.clear();
	quint32 nextToPut = 0;
	quint32 gets      = 0;
	bool recovered    = false;
	for (quint32 frame = 0; !recovered && frame < 1000;) {
		for (; nextToPut * framesPerPacket <= frame + SLACK * framesPerPacket; ++nextToPut) {
			if (nextToPut < FIRST_LOST || nextToPut >= FIRST_LOST + lost) {
				JitterBufferPacket packet{};
				packet.data      = reinterpret_cast< char * >(static_cast< std::uintptr_t >(nextToPut) + 1);
				packet.timestamp = nextToPut * span;
				packet.span      = span;
				jitter_buffer_put(jitter.get(), &packet);
				lookahead.insert(nextToPut * span, span, QByteArray::number(nextToPut));
			}
		}

		const quint32 gapStart = pointer();
		JitterBufferPacket packet{};
		spx_int32_t offset = 0;
		const int result   = jitter_buffer_get(jitter.get(), &packet, static_cast< int >(TEN_MS), &offset);
		++gets;
		quint32 decodedFrames = 1;
		if (result == JITTER_BUFFER_OK) {
			decodedFrames = packet.span / TEN_MS;
		} else {
			QCOMPARE(result, JITTER_BUFFER_MISSING);
			QCOMPARE(gapStart, FIRST_LOST * span);
			QVERIFY(gets > 40 + SLACK);

			const std::optional< FecLookahead::Recovery > recovery =
				lookahead.skipToNext(jitter.get(), gapStart, TEN_MS, MAX_GAP, &onRelease);
			QVERIFY(recovery);
			QCOMPARE(recovery->gap, lost * span);
			QCOMPARE(recovery->payload, QByteArray::number(FIRST_LOST + lost));
			QCOMPARE(pointer(), recoveredTs);

			JitterBufferPacket following{};
			QCOMPARE(jitter_buffer_get(jitter.get(), &following, static_cast< int >(TEN_MS), &offset),
					 JITTER_BUFFER_OK);
			QCOMPARE(static_cast< quint32 >(following.timestamp), recoveredTs);
			recovered = true;
		}
		for (quint32 i = 0; i < decodedFrames; ++i) {
			jitter_buffer_tick(jitter.get());
		}
		lookahead.prune(pointer());
		frame += decodedFrames;
	}
	QVERIFY(recovered);
	QVERIFY(released.empty());
}

// Encodes speech-like audio close to AudioInput's defaults (mono 48 kHz, 40 kbit/s CBR, APPLICATION_AUDIO, 20 ms
// packets) plus the voice signal hint, with FEC on. Loses one packet and checks that decoding it from the next
// packet's FEC data, with the gap FecLookahead reports, beats plain concealment.
void TestFecLookahead::fecRecoversLostPacket() {
	constexpr int RATE = 48000, FRAME = 960, PACKETS = 60, LOST = 40;
	int error            = 0;
	OpusEncoder *encoder = opus_encoder_create(RATE, 1, OPUS_APPLICATION_AUDIO, &error);
	QCOMPARE(error, OPUS_OK);
	opus_encoder_ctl(encoder, OPUS_SET_VBR(0));
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE(40000));
	opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
	opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
	opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(20));

	// Voiced, non-stationary signal: harmonics of a gliding pitch with a new loudness every packet, so concealment
	// by extrapolation is wrong while FEC carries the real content.
	std::mt19937 random(1);
	std::uniform_real_distribution< float > loudness(0.05f, 0.4f);
	std::vector< QByteArray > packets;
	double phase = 0.0;
	for (int p = 0; p < PACKETS; ++p) {
		std::vector< float > pcm(FRAME);
		const float gain = loudness(random);
		for (int i = 0; i < FRAME; ++i) {
			const double t  = (p * FRAME + i) / double(RATE);
			const double f0 = 110.0 + 90.0 * std::sin(2.0 * PI * 1.3 * t);
			phase += 2.0 * PI * f0 / RATE;
			double s = 0.0;
			for (int h = 1; h <= 8; ++h) {
				s += std::sin(h * phase) / h;
			}
			pcm[static_cast< std::size_t >(i)] = static_cast< float >(gain * s);
		}
		unsigned char out[1275];
		const int len = opus_encode_float(encoder, pcm.data(), FRAME, out, sizeof(out));
		QVERIFY(len > 0);
		packets.emplace_back(reinterpret_cast< const char * >(out), len);
	}
	opus_encoder_destroy(encoder);

	QVERIFY(FecLookahead::mayCarryFec(packets[LOST + 1]));

	FecLookahead lookahead;
	for (int p = 0; p < PACKETS; ++p) {
		if (p != LOST) {
			lookahead.insert(static_cast< std::uint32_t >(p) * 2 * TEN_MS, 2 * TEN_MS,
							 packets[static_cast< std::size_t >(p)]);
		}
	}
	const FecLookahead::Packet *next = lookahead.nextAfter(LOST * 2 * TEN_MS, MAX_GAP);
	QVERIFY(next);
	const std::uint32_t gap = next->timestamp - LOST * 2 * TEN_MS;
	QCOMPARE(gap, 2 * TEN_MS);

	// Stereo decoders like AudioOutputSpeech's. Returns the decoded slot of the lost packet.
	bool allDecoded     = true;
	auto decodeLostSlot = [&](bool useFec, bool lose) {
		OpusDecoder *decoder = opus_decoder_create(RATE, 2, &error);
		opus_decoder_ctl(decoder, OPUS_SET_PHASE_INVERSION_DISABLED(1));
		std::vector< float > slot(2 * FRAME);
		std::vector< float > scratch(2 * FRAME);
		for (int p = 0; p <= LOST; ++p) {
			const QByteArray &packet = packets[static_cast< std::size_t >(p)];
			float *out               = (p == LOST) ? slot.data() : scratch.data();
			int decoded              = 0;
			if (p == LOST && lose) {
				if (useFec) {
					decoded = opus_decode_float(
						decoder, reinterpret_cast< const unsigned char * >(next->payload.constData()),
						static_cast< opus_int32 >(next->payload.size()), out, static_cast< int >(gap / 2), 1);
				} else {
					decoded = opus_decode_float(decoder, nullptr, 0, out, static_cast< int >(gap / 2), 0);
				}
			} else {
				decoded = opus_decode_float(decoder, reinterpret_cast< const unsigned char * >(packet.constData()),
											static_cast< opus_int32 >(packet.size()), out, FRAME, 0);
			}
			allDecoded = allDecoded && decoded == FRAME;
		}
		opus_decoder_destroy(decoder);
		return slot;
	};

	const std::vector< float > reference = decodeLostSlot(false, false);
	const double fecError                = squaredError(decodeLostSlot(true, true), reference);
	const double plcError                = squaredError(decodeLostSlot(false, true), reference);
	QVERIFY(allDecoded);
	qInfo("squared error FEC %f, PLC %f", fecError, plcError);
	QVERIFY(fecError < 0.5 * plcError);
}

QTEST_MAIN(TestFecLookahead)
#include "TestFecLookahead.moc"
