// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "UdpPathMonitor.h"

#include <chrono>
#include <optional>
#include <random>
#include <set>

using namespace std::chrono_literals;
using Path = UdpPathMonitor::Path;

namespace {
const UdpPathMonitor::TimePoint T0{};

// One probe per second from `from` for `seconds`, each answered after `delay` with probability `answerRate`.
// Returns the time of the first switch, if any.
std::optional< UdpPathMonitor::Duration > run(UdpPathMonitor &monitor, UdpPathMonitor::TimePoint from, int seconds,
											  double answerRate, std::chrono::milliseconds delay = 200ms) {
	std::mt19937 random(1);
	std::bernoulli_distribution answered(answerRate);
	std::multiset< UdpPathMonitor::TimePoint > pending;
	for (auto now = from; now < from + std::chrono::seconds(seconds); now += 100ms) {
		while (!pending.empty() && *pending.begin() <= now) {
			monitor.probeAnswered(*pending.begin());
			pending.erase(pending.begin());
		}
		if ((now - from) % 1s == 0ms) {
			monitor.probeSent(now);
			if (answered(random)) {
				pending.insert(now + delay);
			}
			if (monitor.update(now)) {
				return now - T0;
			}
		}
	}
	return std::nullopt;
}

long long seconds(UdpPathMonitor::Duration duration) {
	return std::chrono::duration_cast< std::chrono::seconds >(duration).count();
}

// Falls back, then answers every probe until the monitor switches back to UDP. Returns the time spent on TCP.
std::optional< UdpPathMonitor::Duration > flap(UdpPathMonitor &monitor, UdpPathMonitor::TimePoint &now) {
	const std::optional< UdpPathMonitor::Duration > fellBack = run(monitor, now, 25, 0.0);
	if (!fellBack || monitor.path() != Path::Tcp) {
		return std::nullopt;
	}
	now = T0 + *fellBack + 1s;

	const std::optional< UdpPathMonitor::Duration > recovered = run(monitor, now, 15 * 60, 1.0);
	if (!recovered || monitor.path() != Path::Udp) {
		return std::nullopt;
	}
	now = T0 + *recovered + 1s;

	return *recovered - *fellBack;
}
} // namespace

class TestUdpPathMonitor : public QObject {
	Q_OBJECT
private slots:
	void startsOnHintedPath();
	void fallsBackAfterTwentySilentSeconds();
	void doesNotFallBackWithoutProbes();
	void toleratesHeavyRandomLoss();
	void recoversAfterDwell();
	void needsEightOfTenAnswersToRecover();
	void dwellDoublesWhenFlappingAndIsCapped();
	void dwellResetsAfterStablePeriod();
};

void TestUdpPathMonitor::startsOnHintedPath() {
	QCOMPARE(UdpPathMonitor(Path::Udp, T0).path(), Path::Udp);
	QCOMPARE(UdpPathMonitor(Path::Tcp, T0).path(), Path::Tcp);
}

void TestUdpPathMonitor::fallsBackAfterTwentySilentSeconds() {
	UdpPathMonitor monitor(Path::Udp, T0);

	QVERIFY(!run(monitor, T0, 60, 1.0));
	QCOMPARE(monitor.path(), Path::Udp);

	const std::optional< UdpPathMonitor::Duration > switched = run(monitor, T0 + 60s, 60, 0.0);
	QVERIFY(switched);
	QCOMPARE(monitor.path(), Path::Tcp);
	QVERIFY(*switched >= 79s);
	QVERIFY(*switched <= 81s);
}

void TestUdpPathMonitor::doesNotFallBackWithoutProbes() {
	UdpPathMonitor monitor(Path::Udp, T0);

	for (auto now = T0; now < T0 + 120s; now += 1s) {
		QVERIFY(!monitor.update(now));
	}
	QCOMPARE(monitor.path(), Path::Udp);
}

void TestUdpPathMonitor::toleratesHeavyRandomLoss() {
	UdpPathMonitor monitor(Path::Udp, T0);

	// 30 % loss in each direction, answered at the worst round trip of a bad line.
	const std::optional< UdpPathMonitor::Duration > switched = run(monitor, T0, 60 * 60, 0.49, 3600ms);
	QVERIFY2(!switched, switched ? qPrintable(QString::number(seconds(*switched))) : "");
	QCOMPARE(monitor.path(), Path::Udp);
}

void TestUdpPathMonitor::recoversAfterDwell() {
	UdpPathMonitor monitor(Path::Tcp, T0);

	const std::optional< UdpPathMonitor::Duration > switched = run(monitor, T0, 60, 1.0);
	QVERIFY(switched);
	QCOMPARE(monitor.path(), Path::Udp);
	QVERIFY(*switched >= 30s);
	QVERIFY(*switched <= 31s);
}

void TestUdpPathMonitor::needsEightOfTenAnswersToRecover() {
	UdpPathMonitor monitor(Path::Tcp, T0);

	auto now = T0;
	for (; now < T0 + 120s; now += 1s) {
		monitor.probeSent(now);
		QVERIFY(!monitor.update(now));
		if (seconds(now - T0) % 10 < 6) {
			monitor.probeAnswered(now + 200ms);
		}
	}
	QCOMPARE(monitor.path(), Path::Tcp);

	bool switched = false;
	for (const auto end = now + 60s; now < end && !switched; now += 1s) {
		monitor.probeSent(now);
		switched = monitor.update(now);
		if (seconds(now - T0) % 10 < 8) {
			monitor.probeAnswered(now + 200ms);
		}
	}
	QVERIFY(switched);
	QCOMPARE(monitor.path(), Path::Udp);
}

void TestUdpPathMonitor::dwellDoublesWhenFlappingAndIsCapped() {
	UdpPathMonitor monitor(Path::Udp, T0);
	auto now = T0;

	for (const long long expected : { 30, 60, 120, 240, 480, 600, 600, 600 }) {
		const std::optional< UdpPathMonitor::Duration > dwell = flap(monitor, now);
		QVERIFY(dwell);
		QCOMPARE(seconds(*dwell), expected);
	}
}

void TestUdpPathMonitor::dwellResetsAfterStablePeriod() {
	UdpPathMonitor monitor(Path::Udp, T0);
	auto now = T0;

	std::optional< UdpPathMonitor::Duration > dwell = flap(monitor, now);
	QVERIFY(dwell);
	QCOMPARE(seconds(*dwell), 30);
	dwell = flap(monitor, now);
	QVERIFY(dwell);
	QCOMPARE(seconds(*dwell), 60);

	QVERIFY(!run(monitor, now, 3 * 60, 1.0));
	now += 3min;

	dwell = flap(monitor, now);
	QVERIFY(dwell);
	QCOMPARE(seconds(*dwell), 30);
}

QTEST_MAIN(TestUdpPathMonitor)
#include "TestUdpPathMonitor.moc"
