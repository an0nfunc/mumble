// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_UDPPATHMONITOR_H_
#define MUMBLE_MUMBLE_UDPPATHMONITOR_H_

#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>

/// Decides whether voice is sent over UDP or tunnelled through TCP, based on recent UDP ping answers.
class UdpPathMonitor {
public:
	using Clock     = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;
	using Duration  = Clock::duration;
	enum class Path { Udp, Tcp };

	static constexpr std::chrono::seconds FALLBACK_WINDOW{ 20 };
	/// Fewer than one probe per second of FALLBACK_WINDOW, so that timer jitter is tolerated and a pause in probing is
	/// not taken for loss.
	static constexpr std::size_t FALLBACK_MIN_PROBES = 15;
	static constexpr std::chrono::seconds RECOVERY_WINDOW{ 10 };
	static constexpr std::size_t RECOVERY_MIN_ANSWERS = 8;
	static constexpr std::chrono::seconds INITIAL_DWELL{ 30 };
	static constexpr std::chrono::minutes MAX_DWELL{ 10 };
	static constexpr std::chrono::minutes FLAP_INTERVAL{ 2 };

	UdpPathMonitor(Path initial, TimePoint now);

	void probeSent(TimePoint now);
	void probeAnswered(TimePoint now);
	/// Applies the switching rules; returns whether path() changed.
	bool update(TimePoint now);
	Path path() const;

private:
	void forget(TimePoint now);
	static std::size_t countSince(const std::deque< TimePoint > &events, TimePoint since);

	Path m_path;
	TimePoint m_lastSwitch;
	Duration m_dwell = INITIAL_DWELL;
	std::optional< TimePoint > m_lastRecovery;
	std::deque< TimePoint > m_sent;
	std::deque< TimePoint > m_answered;
};

#endif
