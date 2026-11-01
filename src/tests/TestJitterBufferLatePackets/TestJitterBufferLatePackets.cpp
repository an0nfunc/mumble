// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "JitterTimestamp.h"

#include <speex/speex_jitter.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {
constexpr int TEN_MS = 960; // 10 ms of interleaved stereo at 48 kHz, as AudioOutputSpeech uses
std::vector< std::uintptr_t > destroyed;

void onDestroy(void *data) {
	destroyed.push_back(reinterpret_cast< std::uintptr_t >(data));
}

void put(JitterBuffer *jb, std::uintptr_t id, std::uint32_t timestamp, std::uint32_t span) {
	JitterBufferPacket packet{};
	packet.data      = reinterpret_cast< char * >(id);
	packet.len       = 0;
	packet.timestamp = timestamp;
	packet.span      = span;
	jitter_buffer_put(jb, &packet);
}

struct PutResult {
	std::uint32_t pointerBefore;
	std::uint32_t pointerAfter;
	bool kept;
};

// Fills a fresh buffer with `count` 20 ms packets starting at `start`, plays `played` 10 ms frames, then puts the
// candidate and reports the pointer around that put and whether the buffer kept the candidate.
PutResult putAfterPlaying(std::uint32_t start, int count, int played, std::uint32_t candidateTs,
						  std::uint32_t candidateSpan) {
	JitterBuffer *jb = jitter_buffer_init(TEN_MS);
	jitter_buffer_ctl(jb, JITTER_BUFFER_SET_DESTROY_CALLBACK, reinterpret_cast< void * >(&onDestroy));
	for (int i = 0; i < count; ++i) {
		put(jb, 1000 + static_cast< std::uintptr_t >(i), start + static_cast< std::uint32_t >(i * 2 * TEN_MS),
			2 * TEN_MS);
	}
	for (int i = 0; i < played; ++i) {
		JitterBufferPacket out{};
		spx_int32_t offset = 0;
		jitter_buffer_get(jb, &out, TEN_MS, &offset);
		jitter_buffer_tick(jb);
	}
	PutResult result{};
	result.pointerBefore = static_cast< std::uint32_t >(jitter_buffer_get_pointer_timestamp(jb));
	put(jb, 1, candidateTs, candidateSpan);
	result.pointerAfter = static_cast< std::uint32_t >(jitter_buffer_get_pointer_timestamp(jb));
	destroyed.clear();
	jitter_buffer_reset(jb); // hands every stored packet to the destroy callback
	result.kept = std::find(destroyed.begin(), destroyed.end(), std::uintptr_t{ 1 }) != destroyed.end();
	jitter_buffer_destroy(jb);
	return result;
}
} // namespace

class TestJitterBufferLatePackets : public QObject {
	Q_OBJECT
private slots:
	void matchesJitterBuffer_data();
	void matchesJitterBuffer();
	void freshBufferAcceptsAnything();
};

void TestJitterBufferLatePackets::matchesJitterBuffer_data() {
	QTest::addColumn< quint32 >("start");
	QTest::addColumn< int >("played");
	QTest::addColumn< qint64 >("offsetFromPointer"); // candidate end (ts + span + TEN_MS) relative to the pointer
	QTest::addColumn< bool >("expectKept");

	QTest::newRow("far behind") << quint32(0) << 12 << qint64(-10 * TEN_MS) << false;
	QTest::newRow("one sample too late") << quint32(0) << 12 << qint64(-1) << false;
	QTest::newRow("exactly at limit") << quint32(0) << 12 << qint64(0) << true;
	QTest::newRow("ahead") << quint32(0) << 12 << qint64(4 * TEN_MS) << true;
	QTest::newRow("late across wrap") << quint32(0xFFFFFFFFu - 4 * TEN_MS) << 12 << qint64(-TEN_MS) << false;
	QTest::newRow("kept across wrap") << quint32(0xFFFFFFFFu - 4 * TEN_MS) << 12 << qint64(TEN_MS) << true;
	// 10 packets last 10 gets, so 30 more are misses: the put resets the buffer, which then keeps even a late packet
	QTest::newRow("late after more than 20 misses") << quint32(0) << 40 << qint64(-10 * TEN_MS) << true;
}

void TestJitterBufferLatePackets::matchesJitterBuffer() {
	QFETCH(quint32, start);
	QFETCH(int, played);
	QFETCH(qint64, offsetFromPointer);
	QFETCH(bool, expectKept);

	const std::uint32_t span = 2 * TEN_MS;
	// Probe run to learn where the pointer is before the candidate put.
	const std::uint32_t pointer = putAfterPlaying(start, 10, played, start, span).pointerBefore;
	QVERIFY(pointer != 0);
	const auto candidateTs = static_cast< std::uint32_t >(pointer + offsetFromPointer - span - TEN_MS);

	const PutResult result = putAfterPlaying(start, 10, played, candidateTs, span);
	QCOMPARE(result.pointerBefore, pointer);
	QCOMPARE(result.kept, expectKept);
	QCOMPARE(JitterTimestamp::isTooLate(result.pointerAfter, candidateTs, span, TEN_MS), !result.kept);
}

void TestJitterBufferLatePackets::freshBufferAcceptsAnything() {
	const PutResult result = putAfterPlaying(0, 0, 0, 0x90000000u, 2 * TEN_MS);
	QCOMPARE(result.pointerAfter, 0u);
	QVERIFY(result.kept);
	QVERIFY(!JitterTimestamp::isTooLate(result.pointerAfter, 0x90000000u, 2 * TEN_MS, TEN_MS));
}

QTEST_MAIN(TestJitterBufferLatePackets)
#include "TestJitterBufferLatePackets.moc"
