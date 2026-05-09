#include "LufsDetector.h"
#include <cmath>
#include <memory>

namespace Stylus
{

namespace
{

// BS.1770-4 K-weighting filter coefficients @ 48 kHz.
// Stage 1: high-shelf pre-filter (~+4 dB above ~1.5 kHz)
//   models the head/torso transfer that boosts mid/high frequencies
//   in the human auditory system.
// Stage 2: RLB high-pass (~38 Hz)
//   attenuates low frequencies that contribute little to perceived loudness.
struct BiquadCoeffs
{
    double b0, b1, b2, a1, a2;
};

constexpr BiquadCoeffs kPreFilter {
    1.53512485958697,
   -2.69169618940638,
    1.19839281085285,
   -1.69065929318241,
    0.73248077421585
};

constexpr BiquadCoeffs kRlbFilter {
    1.0,
   -2.0,
    1.0,
   -1.99004745483398,
    0.99007225036621
};

struct BiquadState
{
    double z1 = 0.0;
    double z2 = 0.0;

    // Direct-form-II transposed: numerically stable, one mul-add per coeff.
    double process(double x, const BiquadCoeffs& c)
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

} // namespace

float LufsDetector::detect(const juce::File& file,
                           juce::AudioFormatManager& formatManager,
                           std::function<bool()> shouldExit)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) return 0.0f;
    const int     numChannels = juce::jmin(2, (int) reader->numChannels);
    const juce::int64 numSamples = reader->lengthInSamples;
    if (numChannels == 0 || numSamples <= 0) return 0.0f;

    // BS.1770 channel weights for stereo: equal weight on L and R.
    // Mono is treated as a single channel at the same weight (the spec
    // is for multichannel content; mono just integrates the one stream).
    constexpr double kChannelWeight = 1.0;

    BiquadState pre[2];
    BiquadState rlb[2];

    constexpr int kBlockSamples = 8192;
    juce::AudioBuffer<float> buffer(numChannels, kBlockSamples);

    double sumSquares  = 0.0;
    juce::int64 totalSamples = 0;
    juce::int64 pos = 0;

    while (pos < numSamples)
    {
        if (shouldExit && shouldExit()) return 0.0f;

        const int n = (int) juce::jmin<juce::int64>(kBlockSamples, numSamples - pos);
        if (! reader->read(&buffer, 0, n, pos, true, numChannels > 1)) break;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* in = buffer.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
            {
                const double after_pre = pre[ch].process((double) in[i], kPreFilter);
                const double weighted  = rlb[ch].process(after_pre,    kRlbFilter);
                sumSquares += kChannelWeight * weighted * weighted;
            }
        }

        totalSamples += n;
        pos += n;
    }

    if (totalSamples == 0) return 0.0f;

    // BS.1770 integrated loudness:
    //   L = -0.691 + 10*log10( Sum_over_ch( weight_ch * meanSquare_ch ) )
    // For stereo, weight = 1.0 on both L and R, so the bracketed term is
    // meanSquare_L + meanSquare_R (NOT their average). Since sumSquares
    // already accumulates K-weighted x^2 across both channels at weight 1,
    // dividing by per-channel totalSamples gives exactly that sum:
    //   sumSquares / N == sum_L_sq/N + sum_R_sq/N == meanSquare_L + meanSquare_R
    // For mono, numChannels=1, the sum collapses to meanSquare alone -
    // same divisor still applies.
    const double bs1770Quantity = sumSquares / (double) totalSamples;
    if (bs1770Quantity <= 0.0) return 0.0f;

    const double lufs = -0.691 + 10.0 * std::log10(bs1770Quantity);
    return (float) lufs;
}

} // namespace Stylus
