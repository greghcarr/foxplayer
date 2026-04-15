#include "KeyDetector.h"
#include <cmath>
#include <vector>
#include <array>
#include <numeric>
#include <algorithm>

namespace FoxPlayer
{

// Krumhansl-Schmuckler key profiles (major and minor).
static const std::array<double, 12> kMajorProfile = {
    6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88
};
static const std::array<double, 12> kMinorProfile = {
    6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17
};

static const char* kNoteNames[12] = {
    "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};

static double pearsonCorrelation(const double* a, const double* b, int n)
{
    double meanA = 0.0, meanB = 0.0;
    for (int i = 0; i < n; ++i) { meanA += a[i]; meanB += b[i]; }
    meanA /= n; meanB /= n;

    double num = 0.0, denA = 0.0, denB = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double da = a[i] - meanA;
        const double db = b[i] - meanB;
        num  += da * db;
        denA += da * da;
        denB += db * db;
    }

    const double den = std::sqrt(denA * denB);
    return den > 0.0 ? num / den : 0.0;
}

juce::String KeyDetector::detect(const juce::File& audioFile,
                                   juce::AudioFormatManager& formatManager,
                                   const Settings& settings)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audioFile));
    if (reader == nullptr)
        return {};

    const double  sampleRate   = reader->sampleRate;
    const int     numChannels  = static_cast<int>(reader->numChannels);
    const int64_t totalSamples = reader->lengthInSamples;

    if (sampleRate <= 0.0 || totalSamples < static_cast<int64_t>(sampleRate * 4))
        return {};

    const int fftSize = 1 << settings.fftOrder;
    const int hopSize = fftSize / 2;

    const int64_t samplesToRead = static_cast<int64_t>(
        std::min(static_cast<double>(totalSamples),
                 settings.analysisSeconds * sampleRate));

    juce::dsp::FFT fft(settings.fftOrder);

    // Accumulated chroma vector (12 pitch classes).
    std::array<double, 12> chroma {};
    chroma.fill(0.0);
    int frameCount = 0;

    std::vector<float> window(static_cast<size_t>(fftSize));
    for (int i = 0; i < fftSize; ++i)
        window[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * i / (fftSize - 1));

    std::vector<juce::dsp::Complex<float>> fftBuffer(static_cast<size_t>(fftSize));
    juce::AudioBuffer<float> block(numChannels, fftSize);

    for (int64_t pos = 0; pos + fftSize <= samplesToRead; pos += hopSize)
    {
        block.clear();
        reader->read(&block, 0, fftSize, pos, true, true);

        // Mix to mono and apply window.
        for (int s = 0; s < fftSize; ++s)
        {
            float mono = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                mono += block.getSample(c, s);
            fftBuffer[static_cast<size_t>(s)] = { mono / static_cast<float>(numChannels) * window[static_cast<size_t>(s)], 0.0f };
        }

        fft.perform(fftBuffer.data(), fftBuffer.data(), false);

        // Map FFT bins to 12 chroma classes.
        const int halfSize = fftSize / 2;
        for (int bin = 1; bin < halfSize; ++bin)
        {
            const double freq = static_cast<double>(bin) * sampleRate / fftSize;
            if (freq < 27.5 || freq > 4186.0) continue; // piano range

            // Convert frequency to MIDI pitch, then to chroma class.
            const double midi   = 69.0 + 12.0 * std::log2(freq / 440.0);
            const int    chClass = static_cast<int>(std::round(midi)) % 12;
            const auto&  c = fftBuffer[static_cast<size_t>(bin)];
            const double mag = std::sqrt(static_cast<double>(c.real() * c.real() + c.imag() * c.imag()));
            chroma[static_cast<size_t>((chClass + 12) % 12)] += mag;
        }

        ++frameCount;
    }

    if (frameCount == 0)
        return {};

    // Normalise chroma.
    const double chromaSum = std::accumulate(chroma.begin(), chroma.end(), 0.0);
    if (chromaSum <= 0.0) return {};
    for (auto& v : chroma) v /= chromaSum;

    // Correlate chroma against all 24 major/minor key profiles.
    double bestScore = -2.0;
    int    bestKey   = 0;
    bool   bestMinor = false;

    std::array<double, 12> rotated;

    for (int root = 0; root < 12; ++root)
    {
        // Rotate profiles to match root.
        for (int i = 0; i < 12; ++i)
            rotated[static_cast<size_t>(i)] = kMajorProfile[static_cast<size_t>((i + root) % 12)];
        double score = pearsonCorrelation(chroma.data(), rotated.data(), 12);
        if (score > bestScore) { bestScore = score; bestKey = root; bestMinor = false; }

        for (int i = 0; i < 12; ++i)
            rotated[static_cast<size_t>(i)] = kMinorProfile[static_cast<size_t>((i + root) % 12)];
        score = pearsonCorrelation(chroma.data(), rotated.data(), 12);
        if (score > bestScore) { bestScore = score; bestKey = root; bestMinor = true; }
    }

    return juce::String(kNoteNames[bestKey]) + (bestMinor ? "m" : "");
}

} // namespace FoxPlayer
