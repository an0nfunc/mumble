// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_TIMESTRETCH_H_
#define MUMBLE_MUMBLE_TIMESTRETCH_H_

#include <cstddef>

/// Time-domain harmonic scaling of a single decoded packet (Liang, Faerber, Girod, IEEE Trans. Multimedia 2003):
/// whole pitch periods are removed or repeated with a crossfade, so the pitch is kept and no delay is added.
namespace TimeStretch {

/// Shortest and longest pitch period searched, in frames at 48 kHz (2.5 ms and 15 ms).
constexpr std::size_t MIN_PERIOD = 120;
constexpr std::size_t MAX_PERIOD = 720;

/// Writes `frames` frames of interleaved audio (`channels` channels, 48 kHz) from `in` to `out` with the duration
/// changed towards `targetFrames` and the pitch unchanged. Returns the number of frames written, never more than
/// `outCapacityFrames`. The result lands within one pitch period of the target unless the packet runs out of room or
/// has no clear pitch period left (unvoiced speech, or a period that does not fit twice into the rest of the packet).
/// The first and last MIN_PERIOD frames are copied unchanged so that consecutive packets join without a jump; packets
/// shorter than 4 * MIN_PERIOD are copied as they are. `in` and `out` must not overlap.
/// Real-time safe: no allocation, locks or exceptions.
std::size_t process(const float *in, std::size_t frames, unsigned int channels, std::size_t targetFrames, float *out,
					std::size_t outCapacityFrames);

} // namespace TimeStretch

#endif
