// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PLAYOUTCATCHUP_H_
#define MUMBLE_MUMBLE_PLAYOUTCATCHUP_H_

/// How fast to play a speech stream, judged by the audio buffered beyond the packet being played. Catching up (playing
/// faster) starts above START_FRAMES and stops below STOP_FRAMES, so it does not toggle on every packet; playing slower
/// stretches the time until a late next packet arrives.
class PlayoutCatchUp {
public:
	static constexpr int START_FRAMES = 20;
	static constexpr int STOP_FRAMES  = 10;

	enum class Rate { Normal, Faster, Slower };

	/// @param backlogFrames Buffered audio in 10 ms frames
	void update(int backlogFrames) {
		m_backlogFrames = backlogFrames;
		if (backlogFrames > START_FRAMES) {
			m_active = true;
		} else if (backlogFrames < STOP_FRAMES) {
			m_active = false;
		}
	}

	bool active() const { return m_active; }

	/// Starts an output request. At most one quiet packet is skipped per request: a skipped packet adds no output, so
	/// without a limit one request could decode every packet a sender keeps buffered.
	void startRequest() { m_skippedInRequest = false; }

	/// Whether to skip the quiet packet just decoded, which then counts against the request's limit.
	bool skipQuiet() {
		if (!m_active || m_skippedInRequest) {
			return false;
		}
		m_skippedInRequest = true;
		return true;
	}

	Rate rate() const {
		if (m_active) {
			return Rate::Faster;
		}
		return m_backlogFrames > 0 ? Rate::Normal : Rate::Slower;
	}

private:
	bool m_active           = false;
	bool m_skippedInRequest = false;
	int m_backlogFrames     = 1; // Normal until the first update
};

#endif
