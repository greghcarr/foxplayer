#include "BpmDetector.h"
#include <cmath>
#include <vector>
#include <numeric>

namespace Stylus
{

double BpmDetector::detect(const juce::File& audioFile,
                            juce::AudioFormatManager& formatManager,
                            const Settings& settings)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audioFile));
    if (reader == nullptr)
        return 0.0;

    const double sampleRate   = reader->sampleRate;
    const int    numChannels  = static_cast<int>(reader->numChannels);
    const int64_t totalSamples = reader->lengthInSamples;

    if (sampleRate <= 0.0 || totalSamples < static_cast<int64_t>(sampleRate * 4))
        return 0.0; // too short

    const int64_t samplesToRead = static_cast<int64_t>(
        std::min(static_cast<double>(totalSamples),
                 settings.analysisSeconds * sampleRate));

    // --- Read audio into a mono float buffer ---
    const int blockSize = 4096;
    juce::AudioBuffer<float> block(numChannels, blockSize);

    std::vector<float> mono;
    mono.reserve(static_cast<size_t>(samplesToRead));

    int64_t remaining = samplesToRead;
    int64_t position  = 0;

    while (remaining > 0)
    {
        const int toRead = static_cast<int>(std::min<int64_t>(remaining, blockSize));
        block.clear();
        reader->read(&block, 0, toRead, position, true, true);

        for (int s = 0; s < toRead; ++s)
        {
            float sum = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                sum += block.getSample(c, s);
            mono.push_back(sum / static_cast<float>(numChannels));
        }

        position  += toRead;
        remaining -= toRead;
    }

    if (mono.empty())
        return 0.0;

    // --- Rectify (absolute value) to get energy ---
    for (auto& s : mono)
        s = std::abs(s);

    // --- Downsample to envelopeRate Hz by averaging blocks ---
    const int decimFactor = std::max(1, static_cast<int>(sampleRate / settings.envelopeRate));
    const int envLen      = static_cast<int>(mono.size()) / decimFactor;
    if (envLen < 4)
        return 0.0;

    std::vector<float> env(static_cast<size_t>(envLen));
    for (int i = 0; i < envLen; ++i)
    {
        float sum = 0.0f;
        for (int j = 0; j < decimFactor; ++j)
            sum += mono[static_cast<size_t>(i * decimFactor + j)];
        env[static_cast<size_t>(i)] = sum / static_cast<float>(decimFactor);
    }

    // --- Light low-pass: running average (box filter) ---
    const int w = settings.lpfOrder;
    if (w > 1 && envLen > w)
    {
        std::vector<float> smoothed(env.size());
        for (int i = 0; i < envLen; ++i)
        {
            float s = 0.0f;
            int   n = 0;
            for (int k = std::max(0, i - w / 2); k <= std::min(envLen - 1, i + w / 2); ++k, ++n)
                s += env[static_cast<size_t>(k)];
            smoothed[static_cast<size_t>(i)] = s / static_cast<float>(n);
        }
        env = std::move(smoothed);
    }

    // --- Subtract mean (remove DC) ---
    const float mean = std::accumulate(env.begin(), env.end(), 0.0f) / static_cast<float>(envLen);
    for (auto& s : env)
        s -= mean;

    // --- Autocorrelation over the BPM lag range ---
    const double envRate = static_cast<double>(settings.envelopeRate);
    const int lagMin = static_cast<int>(60.0 / settings.maxBpm * envRate);
    const int lagMax = static_cast<int>(60.0 / settings.minBpm * envRate);

    if (lagMin < 1 || lagMax >= envLen)
        return 0.0;

    double bestCorr = -1.0;
    int    bestLag  = lagMin;

    for (int lag = lagMin; lag <= lagMax; ++lag)
    {
        double corr = 0.0;
        const int limit = envLen - lag;
        for (int i = 0; i < limit; ++i)
            corr += static_cast<double>(env[static_cast<size_t>(i)])
                  * static_cast<double>(env[static_cast<size_t>(i + lag)]);

        // Normalise by number of products so shorter lags don't win by default.
        corr /= static_cast<double>(limit);

        if (corr > bestCorr)
        {
            bestCorr = corr;
            bestLag  = lag;
        }
    }

    if (bestCorr <= 0.0)
        return 0.0;

    const double bpm = 60.0 / (static_cast<double>(bestLag) / envRate);

    // Fold into 60-200 range in case we landed on a harmonic.
    double result = bpm;
    while (result < settings.minBpm) result *= 2.0;
    while (result > settings.maxBpm) result /= 2.0;

    // Round to one decimal place.
    return std::round(result * 10.0) / 10.0;
}

} // namespace Stylus
