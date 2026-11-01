// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "PacketLossEstimator.h"

class TestPacketLossEstimator : public QObject {
	Q_OBJECT
private slots:
	void startsAtZero();
	void firstReportOnlySetsBaseline();
	void convergesToSteadyLoss();
	void smallSamplesBarelyMoveTheEstimate();
	void lateArrivalsDoNotCountAsLoss();
	void restartsWhenCountersReset();
	void capsAtMaximum();
	void roundsTinyLossToZero();
	void largeDeltasDoNotWrap();
};

namespace {
// Feeds `intervals` reports of `perInterval` packets each, `lossPercent` of them lost.
void feed(PacketLossEstimator &estimator, unsigned int &good, unsigned int &lost, int intervals,
		  unsigned int perInterval, unsigned int lossPercent) {
	for (int i = 0; i < intervals; ++i) {
		const unsigned int lostNow = perInterval * lossPercent / 100;
		good += perInterval - lostNow;
		lost += lostNow;
		estimator.update(good, lost);
	}
}
} // namespace

void TestPacketLossEstimator::startsAtZero() {
	QCOMPARE(PacketLossEstimator().lossPercent(), 0);
}

void TestPacketLossEstimator::firstReportOnlySetsBaseline() {
	PacketLossEstimator estimator;
	estimator.update(1000, 500); // a session that already lost a lot before we started listening
	QCOMPARE(estimator.lossPercent(), 0);
}

void TestPacketLossEstimator::convergesToSteadyLoss() {
	PacketLossEstimator estimator;
	unsigned int good = 0, lost = 0;
	estimator.update(good, lost);
	feed(estimator, good, lost, 20, 250, 10); // 5 s of 20 ms packets per report
	QCOMPARE(estimator.lossPercent(), 10);
}

void TestPacketLossEstimator::smallSamplesBarelyMoveTheEstimate() {
	PacketLossEstimator estimator;
	unsigned int good = 0, lost = 0;
	estimator.update(good, lost);
	feed(estimator, good, lost, 1, 5, 20); // one lost probe out of five while silent
	QVERIFY(estimator.lossPercent() <= 1);
}

void TestPacketLossEstimator::lateArrivalsDoNotCountAsLoss() {
	PacketLossEstimator estimator;
	estimator.update(0, 0);
	estimator.update(240, 10);
	const int before = estimator.lossPercent();
	estimator.update(490, 9); // one of the "lost" packets arrived late
	QVERIFY(estimator.lossPercent() < before);
}

void TestPacketLossEstimator::restartsWhenCountersReset() {
	PacketLossEstimator estimator;
	unsigned int good = 0, lost = 0;
	estimator.update(good, lost);
	feed(estimator, good, lost, 20, 250, 10);
	estimator.update(10, 0); // counters went backwards: new baseline, no bogus sample
	QCOMPARE(estimator.lossPercent(), 10);
}

void TestPacketLossEstimator::capsAtMaximum() {
	PacketLossEstimator estimator;
	unsigned int good = 0, lost = 0;
	estimator.update(good, lost);
	feed(estimator, good, lost, 20, 250, 60);
	QCOMPARE(estimator.lossPercent(), PacketLossEstimator::MAX_PERCENT);
}

void TestPacketLossEstimator::roundsTinyLossToZero() {
	PacketLossEstimator estimator;
	estimator.update(0, 0);
	estimator.update(999, 1); // 0.1 %
	QCOMPARE(estimator.lossPercent(), 0);
}

void TestPacketLossEstimator::largeDeltasDoNotWrap() {
	PacketLossEstimator estimator;
	estimator.update(0, 0);
	estimator.update(0xFFFFFF00u, 0x200u); // received + lost exceeds 32 bit
	QCOMPARE(estimator.lossPercent(), 0);
}

QTEST_MAIN(TestPacketLossEstimator)
#include "TestPacketLossEstimator.moc"
