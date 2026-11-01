// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "PlayoutCatchUp.h"

class TestPlayoutCatchUp : public QObject {
	Q_OBJECT
private slots:
	void startsAboveStartThreshold();
	void staysActiveUntilBelowStopThreshold();
	void staysInactiveBetweenThresholds();
	void rateFollowsBacklog();
	void skipsOneQuietPacketPerRequest();
};

void TestPlayoutCatchUp::startsAboveStartThreshold() {
	PlayoutCatchUp catchUp;
	QVERIFY(!catchUp.active());

	catchUp.update(PlayoutCatchUp::START_FRAMES);
	QVERIFY(!catchUp.active());

	catchUp.update(PlayoutCatchUp::START_FRAMES + 1);
	QVERIFY(catchUp.active());
}

void TestPlayoutCatchUp::staysActiveUntilBelowStopThreshold() {
	PlayoutCatchUp catchUp;
	catchUp.update(PlayoutCatchUp::START_FRAMES + 1);

	catchUp.update(PlayoutCatchUp::STOP_FRAMES);
	QVERIFY(catchUp.active());

	catchUp.update(PlayoutCatchUp::STOP_FRAMES - 1);
	QVERIFY(!catchUp.active());
}

void TestPlayoutCatchUp::staysInactiveBetweenThresholds() {
	PlayoutCatchUp catchUp;
	catchUp.update(PlayoutCatchUp::START_FRAMES + 1);
	catchUp.update(PlayoutCatchUp::STOP_FRAMES - 1);

	catchUp.update(PlayoutCatchUp::START_FRAMES);
	QVERIFY(!catchUp.active());
}

void TestPlayoutCatchUp::rateFollowsBacklog() {
	PlayoutCatchUp catchUp;
	QCOMPARE(catchUp.rate(), PlayoutCatchUp::Rate::Normal);

	catchUp.update(0);
	QCOMPARE(catchUp.rate(), PlayoutCatchUp::Rate::Slower);

	catchUp.update(PlayoutCatchUp::STOP_FRAMES);
	QCOMPARE(catchUp.rate(), PlayoutCatchUp::Rate::Normal);

	catchUp.update(PlayoutCatchUp::START_FRAMES + 1);
	QCOMPARE(catchUp.rate(), PlayoutCatchUp::Rate::Faster);

	catchUp.update(PlayoutCatchUp::STOP_FRAMES - 1);
	QCOMPARE(catchUp.rate(), PlayoutCatchUp::Rate::Normal);
}

void TestPlayoutCatchUp::skipsOneQuietPacketPerRequest() {
	PlayoutCatchUp catchUp;
	catchUp.startRequest();
	QVERIFY(!catchUp.skipQuiet());

	catchUp.update(PlayoutCatchUp::START_FRAMES + 1);
	QVERIFY(catchUp.skipQuiet());
	QVERIFY(!catchUp.skipQuiet());

	catchUp.startRequest();
	QVERIFY(catchUp.skipQuiet());
}

QTEST_MAIN(TestPlayoutCatchUp)
#include "TestPlayoutCatchUp.moc"
