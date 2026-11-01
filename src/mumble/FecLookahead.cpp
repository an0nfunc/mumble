// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "FecLookahead.h"

#include "JitterTimestamp.h"
#include "MumbleProtocol.h"

#include <opus.h>

#include <algorithm>
#include <utility>

void FecLookahead::insert(std::uint32_t timestamp, std::uint32_t span, QByteArray payload) {
	if (static_cast< std::size_t >(payload.size()) > Mumble::Protocol::MAX_UDP_PACKET_SIZE) {
		return;
	}

	const auto sameTimestamp = [timestamp](const Packet &packet) { return packet.timestamp == timestamp; };
	if (std::any_of(m_packets.begin(), m_packets.end(), sameTimestamp)) {
		return;
	}

	if (m_packets.size() >= MAX_PACKETS) {
		m_packets.erase(std::min_element(m_packets.begin(), m_packets.end(), [](const Packet &a, const Packet &b) {
			return JitterTimestamp::diff(a.timestamp, b.timestamp) < 0;
		}));
	}

	m_packets.push_back({ timestamp, span, std::move(payload) });
}

void FecLookahead::prune(std::uint32_t pointer) {
	std::erase_if(m_packets, [pointer](const Packet &packet) {
		return JitterTimestamp::diff(packet.timestamp + packet.span, pointer) <= 0;
	});
}

const FecLookahead::Packet *FecLookahead::nextAfter(std::uint32_t gapStart, std::uint32_t maxGap) const {
	const Packet *best        = nullptr;
	std::int32_t bestDistance = 0;
	for (const Packet &packet : m_packets) {
		const std::int32_t distance = JitterTimestamp::diff(packet.timestamp, gapStart);
		if (distance > 0 && static_cast< std::uint32_t >(distance) <= maxGap && (!best || distance < bestDistance)) {
			best         = &packet;
			bestDistance = distance;
		}
	}
	return best;
}

std::size_t FecLookahead::size() const {
	return m_packets.size();
}

std::optional< FecLookahead::Recovery > FecLookahead::skipToNext(JitterBuffer *jitter, std::uint32_t gapStart,
																 std::uint32_t frameSize, std::uint32_t maxGap,
																 void (*releasePacket)(void *)) const {
	const Packet *next = nextAfter(gapStart, maxGap);
	if (!next) {
		return std::nullopt;
	}

	const auto gap = static_cast< std::uint32_t >(JitterTimestamp::diff(next->timestamp, gapStart));
	if (gap > frameSize) {
		JitterBufferPacket skipped{};
		spx_int32_t offset = 0;
		if (jitter_buffer_get(jitter, &skipped, static_cast< int >(gap - frameSize), &offset) == JITTER_BUFFER_OK) {
			releasePacket(skipped.data);
		}
	}

	if (static_cast< std::uint32_t >(jitter_buffer_get_pointer_timestamp(jitter)) != next->timestamp) {
		return std::nullopt;
	}
	return Recovery{ next->payload, gap };
}

bool FecLookahead::mayCarryFec(const QByteArray &opusPacket) {
	if (opusPacket.isEmpty()) {
		return false;
	}
#ifdef MUMBLE_HAVE_OPUS_PACKET_HAS_LBRR
	return opus_packet_has_lbrr(reinterpret_cast< const unsigned char * >(opusPacket.constData()),
								static_cast< opus_int32 >(opusPacket.size()))
		   == 1;
#else
	// TOC configurations below 16 are SILK-only or hybrid, the only modes that carry LBRR data
	return (static_cast< unsigned char >(opusPacket[0]) >> 3) < 16;
#endif
}
