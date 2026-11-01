// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "TimeStretch.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace TimeStretch {

namespace {
	/// Frames at each packet edge that are copied unchanged.
	constexpr std::size_t KEPT_EDGE = MIN_PERIOD;
	/// The coarse pitch search runs on a signal decimated by this factor and is refined by +-REFINE frames.
	constexpr std::size_t DECIMATION = 4;
	constexpr std::size_t REFINE     = DECIMATION - 1;

	/// Below this, the segments around the best lag are too unalike for a crossfade to sound continuous.
	constexpr double MIN_CORRELATION = 0.7;

	static_assert(MIN_PERIOD % DECIMATION == 0, "The coarse lag grid must contain MIN_PERIOD");

	/// Normalised cross-correlation between x[0, lag) and x[lag, 2 * lag).
	double correlation(const float *x, std::size_t lag) {
		double cross   = 0.0;
		double energyA = 0.0;
		double energyB = 0.0;
		for (std::size_t i = 0; i < lag; ++i) {
			const double a = static_cast< double >(x[i]);
			const double b = static_cast< double >(x[i + lag]);
			cross += a * b;
			energyA += a * a;
			energyB += b * b;
		}
		const double norm = energyA * energyB;
		return norm > 1e-20 ? cross / std::sqrt(norm) : 0.0;
	}

	/// The lag in [first, last] with the highest correlation(x, lag), the shortest one on a tie.
	std::size_t strongestLag(const float *x, std::size_t first, std::size_t last) {
		std::size_t strongest = first;
		double best           = -2.0;
		for (std::size_t lag = first; lag <= last; ++lag) {
			const double value = correlation(x, lag);
			if (value > best) {
				best      = value;
				strongest = lag;
			}
		}
		return strongest;
	}

	/// Pitch period of in[start, start + 2 * maxPeriod), with MIN_PERIOD <= maxPeriod <= MAX_PERIOD, or 0 if even the
	/// best lag correlates below MIN_CORRELATION.
	std::size_t findPeriod(const float *in, unsigned int channels, std::size_t start, std::size_t maxPeriod) {
		float mono[2 * MAX_PERIOD];
		float decimated[2 * MAX_PERIOD / DECIMATION];

		const std::size_t length = 2 * maxPeriod;
		for (std::size_t i = 0; i < length; ++i) {
			const float *frame = in + (start + i) * channels;
			float sum          = 0.0f;
			for (unsigned int c = 0; c < channels; ++c) {
				sum += frame[c];
			}
			mono[i] = sum;
		}
		// The box filter is a crude anti-aliasing filter, which is enough to locate the correlation peak.
		for (std::size_t i = 0; i < length / DECIMATION; ++i) {
			const float *block = mono + i * DECIMATION;
			decimated[i]       = std::accumulate(block, block + DECIMATION, 0.0f);
		}

		const std::size_t center =
			strongestLag(decimated, MIN_PERIOD / DECIMATION, maxPeriod / DECIMATION) * DECIMATION;
		const std::size_t period =
			strongestLag(mono, std::max(MIN_PERIOD, center - REFINE), std::min(maxPeriod, center + REFINE));
		// When the room left in the packet caps the search, a lag at the cap may stand in for a longer true period.
		if (maxPeriod < MAX_PERIOD && period + REFINE >= maxPeriod) {
			return 0;
		}
		return correlation(mono, period) >= MIN_CORRELATION ? period : 0;
	}

	/// Appends frames to the output, dropping whatever does not fit into its capacity.
	class Output {
	public:
		Output(float *out, std::size_t capacity, unsigned int channels)
			: m_out(out), m_capacity(capacity), m_channels(channels) {}

		std::size_t written() const { return m_written; }

		void copy(const float *in, std::size_t frames) {
			const std::size_t count = std::min(frames, m_capacity - m_written);
			std::copy(in, in + count * m_channels, m_out + m_written * m_channels);
			m_written += count;
		}

		/// Writes `frames` frames that start like `from` and end like `to`, with raised-cosine weights.
		void crossfade(const float *from, const float *to, std::size_t frames) {
			const std::size_t count = std::min(frames, m_capacity - m_written);
			// cos(i * step) by the Chebyshev recurrence, which avoids a std::cos call per frame.
			const double step   = M_PI / static_cast< double >(frames - 1);
			const double twoCos = 2.0 * std::cos(step);
			double cosPrevious  = std::cos(step);
			double cosCurrent   = 1.0;
			float *dst          = m_out + m_written * m_channels;
			for (std::size_t i = 0; i < count; ++i) {
				const float fadeOut = static_cast< float >(0.5 + 0.5 * cosCurrent);
				const float fadeIn  = 1.0f - fadeOut;
				for (unsigned int c = 0; c < m_channels; ++c) {
					const std::size_t index = i * m_channels + c;
					dst[index]              = from[index] * fadeOut + to[index] * fadeIn;
				}
				const double cosNext = twoCos * cosCurrent - cosPrevious;
				cosPrevious          = cosCurrent;
				cosCurrent           = cosNext;
			}
			m_written += count;
		}

	private:
		float *m_out;
		std::size_t m_capacity;
		unsigned int m_channels;
		std::size_t m_written = 0;
	};
} // namespace

std::size_t process(const float *in, std::size_t frames, unsigned int channels, std::size_t targetFrames, float *out,
					std::size_t outCapacityFrames) {
	Output output(out, outCapacityFrames, channels);
	if (frames < 4 * MIN_PERIOD || targetFrames == frames) {
		output.copy(in, frames);
		return output.written();
	}

	const bool shorten    = targetFrames < frames;
	const std::size_t end = frames - KEPT_EDGE;
	// Output length if the rest of the input were copied unchanged.
	std::size_t length = frames;
	std::size_t pos    = KEPT_EDGE;
	output.copy(in, pos);

	while (end - pos >= 2 * MIN_PERIOD && (shorten ? length > targetFrames : length < targetFrames)) {
		const std::size_t distance = shorten ? length - targetFrames : targetFrames - length;
		// An operation changes the length by its period, which only gets closer to the target if it is shorter than
		// twice the remaining distance.
		if (2 * distance <= MIN_PERIOD) {
			break;
		}
		const std::size_t period = findPeriod(in, channels, pos, std::min(MAX_PERIOD, (end - pos) / 2));
		if (period == 0 || period >= 2 * distance || (!shorten && length + period > outCapacityFrames)) {
			break;
		}

		const float *a = in + pos * channels;
		const float *b = in + (pos + period) * channels;
		if (shorten) {
			output.crossfade(a, b, period);
			length -= period;
		} else {
			output.copy(a, period);
			output.crossfade(b, a, period);
			output.copy(b, period);
			length += period;
		}
		pos += 2 * period;
	}

	output.copy(in + pos * channels, frames - pos);
	return output.written();
}

} // namespace TimeStretch
