// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_FECLOOKAHEAD_H_
#define MUMBLE_MUMBLE_FECLOOKAHEAD_H_

#include <QByteArray>

#include <speex/speex_jitter.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

/// Copies of the Opus packets in a jitter buffer, so that a missing packet can be decoded from the in-band FEC data
/// of the packet that follows it. Timestamps and spans are in jitter buffer units. Not thread-safe.
class FecLookahead {
public:
	struct Packet {
		std::uint32_t timestamp;
		std::uint32_t span;
		QByteArray payload;
	};

	struct Recovery {
		/// The packet whose FEC data covers the gap.
		QByteArray payload;
		/// Length of the gap in jitter buffer units.
		std::uint32_t gap;
	};

	/// The jitter buffer's own capacity (SPEEX_JITTER_MAX_BUFFER_SIZE).
	static constexpr std::size_t MAX_PACKETS = 200;

	/// Stores a packet unless one with the same timestamp is stored already or its payload is larger than servers
	/// forward over UDP, which bounds the memory per stream. Evicts the oldest packet when full.
	void insert(std::uint32_t timestamp, std::uint32_t span, QByteArray payload);
	/// Forgets every packet that ends at or before `pointer`.
	void prune(std::uint32_t pointer);
	/// The packet that starts closest after `gapStart`, if it starts at most `maxGap` later.
	const Packet *nextAfter(std::uint32_t gapStart, std::uint32_t maxGap) const;
	std::size_t size() const;

	/// To be called after jitter_buffer_get() returned JITTER_BUFFER_MISSING for the `frameSize` long frame that
	/// started at `gapStart`. If a stored packet starts at most `maxGap` after `gapStart`, moves the jitter buffer's
	/// pointer to that packet and returns it. Returns nothing if there is no such packet or the pointer cannot be
	/// moved there. A packet the jitter buffer hands out while moving the pointer goes to `releasePacket`.
	std::optional< Recovery > skipToNext(JitterBuffer *jitter, std::uint32_t gapStart, std::uint32_t frameSize,
										 std::uint32_t maxGap, void (*releasePacket)(void *)) const;

	/// Whether the packet may contain FEC data for the packet before it.
	static bool mayCarryFec(const QByteArray &opusPacket);

private:
	std::vector< Packet > m_packets;
};

#endif
