// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PACKETLOSSESTIMATOR_H_
#define MUMBLE_MUMBLE_PACKETLOSSESTIMATOR_H_

/// Smooths the loss rate of the UDP packets the server receives from us.
class PacketLossEstimator {
public:
	static constexpr int MAX_PERCENT = 25;

	/// Feeds the cumulative counters the server reported for the UDP packets it received from us.
	void update(unsigned int good, unsigned int lost);
	/// Smoothed loss in percent, rounded, capped at MAX_PERCENT.
	int lossPercent() const;

private:
	bool m_hasBaseline  = false;
	unsigned int m_good = 0;
	unsigned int m_lost = 0;
	double m_loss       = 0.0;
};

#endif // MUMBLE_MUMBLE_PACKETLOSSESTIMATOR_H_
