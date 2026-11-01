// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PacketLossEstimator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
// A report covering this many packets (5 s of 20 ms packets) moves the estimate halfway to its sample; smaller ones,
// such as the pings sent while the user is silent, move it proportionally less.
constexpr double FULL_WEIGHT         = 0.5;
constexpr double FULL_WEIGHT_PACKETS = 250.0;
} // namespace

void PacketLossEstimator::update(unsigned int good, unsigned int lost) {
	if (!m_hasBaseline || good < m_good) {
		m_hasBaseline = true;
		m_good        = good;
		m_lost        = lost;
		return;
	}

	const unsigned int received = good - m_good;
	// lost shrinks when a packet that was counted as lost arrives late
	const unsigned int newlyLost = lost > m_lost ? lost - m_lost : 0;
	m_good                       = good;
	m_lost                       = lost;

	// 64 bit, as the two deltas of counters reported by the server can add up to more than 32 bit
	const auto total = static_cast< double >(std::uint64_t{ received } + newlyLost);
	if (total == 0) {
		return;
	}

	const double sample = static_cast< double >(newlyLost) / total;
	const double weight = FULL_WEIGHT * std::min(1.0, total / FULL_WEIGHT_PACKETS);
	m_loss += weight * (sample - m_loss);
}

int PacketLossEstimator::lossPercent() const {
	return std::min(MAX_PERCENT, static_cast< int >(std::lround(m_loss * 100.0)));
}
