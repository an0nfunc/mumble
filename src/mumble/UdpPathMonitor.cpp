// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "UdpPathMonitor.h"

#include <algorithm>

UdpPathMonitor::UdpPathMonitor(Path initial, TimePoint now) : m_path(initial), m_lastSwitch(now) {
}

void UdpPathMonitor::probeSent(TimePoint now) {
	forget(now);
	m_sent.push_back(now);
}

void UdpPathMonitor::probeAnswered(TimePoint now) {
	forget(now);
	m_answered.push_back(now);
}

bool UdpPathMonitor::update(TimePoint now) {
	forget(now);

	if (m_path == Path::Udp) {
		if (countSince(m_sent, now - FALLBACK_WINDOW) < FALLBACK_MIN_PROBES
			|| countSince(m_answered, now - FALLBACK_WINDOW) > 0) {
			return false;
		}
		const bool flapping = m_lastRecovery && now - *m_lastRecovery < FLAP_INTERVAL;
		m_dwell             = flapping ? std::min< Duration >(2 * m_dwell, MAX_DWELL) : Duration(INITIAL_DWELL);
		m_path              = Path::Tcp;
		m_lastSwitch        = now;
		return true;
	}

	if (now - m_lastSwitch < m_dwell || countSince(m_answered, now - RECOVERY_WINDOW) < RECOVERY_MIN_ANSWERS) {
		return false;
	}
	m_path         = Path::Udp;
	m_lastSwitch   = now;
	m_lastRecovery = now;
	return true;
}

UdpPathMonitor::Path UdpPathMonitor::path() const {
	return m_path;
}

void UdpPathMonitor::forget(TimePoint now) {
	// FALLBACK_WINDOW is the longest window any rule looks at.
	const TimePoint cutoff = now - FALLBACK_WINDOW;
	for (std::deque< TimePoint > *events : { &m_sent, &m_answered }) {
		while (!events->empty() && events->front() <= cutoff) {
			events->pop_front();
		}
	}
}

std::size_t UdpPathMonitor::countSince(const std::deque< TimePoint > &events, TimePoint since) {
	return static_cast< std::size_t >(
		std::count_if(events.begin(), events.end(), [since](TimePoint event) { return event > since; }));
}
