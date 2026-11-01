// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_JITTERTIMESTAMP_H_
#define MUMBLE_MUMBLE_JITTERTIMESTAMP_H_

#include <cstdint>

namespace JitterTimestamp {

/// a - b for Speex jitter buffer timestamps, which wrap around (same arithmetic as LT32/GE32 in jitter.c).
inline std::int32_t diff(std::uint32_t a, std::uint32_t b) {
	return static_cast< std::int32_t >(a - b);
}

/// Whether jitter_buffer_put() discarded this packet as hopelessly late, given the pointer right after the put. A
/// pointer of 0 is treated as a reset buffer, which keeps anything.
inline bool isTooLate(std::uint32_t pointer, std::uint32_t timestamp, std::uint32_t span, std::uint32_t delayStep) {
	return pointer != 0 && diff(timestamp + span + delayStep, pointer) < 0;
}

} // namespace JitterTimestamp

#endif
