#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Mutually "prime-ish" delay lengths in ms for the 8 FDN lines (at size = 1.0)
    constexpr std::array<float, 8> baseDelaysMs { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.3f, 73.1f };
    constexpr float maxSizeScale = 2.0f;
    constexpr float maxPreDelayMs = 500.0f;
}

AlphaVerbProcessor::AlphaVerbProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout AlphaVerbProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<P> (juce::ParameterID { "mix", 1 }, "Mix",
                                      R (0.0f, 1.0f, 0.001f), 0.35f));
    p.push_back (std::make_unique<P> (juce::ParameterID { "decay", 1 }, "Decay (s)",
                                      R (0.2f, 20.0f, 0.01f, 0.4f), 2.5f));
    p.push_back (std::make_unique<P> (juce::ParameterID { "size", 1 }, "Space",
                                      R (0.3f, 1.0f, 0.001f), 0.7f));
    p.push_back (std::make_unique<P> (juce::ParameterID { "predelay", 1 }, "Predelay (ms)",
                                      R (0.0f, maxPreDelayMs, 0.1f, 0.5f), 10.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID { "brightness", 1 }, "Brightness",
                                      R (0.0f, 1.0f, 0.001f), 0.6f));
    p.push_back (std::make_unique<P> (juce::ParameterID { "lowdecay", 1 }, "Low Decay x",
                                      R (0.3f, 1.5f, 0.001f), 1.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID { "width", 1 }, "Stereo Width",
                                      R (0.0f, 1.5f, 0.001f), 1.0f));

    return { p.begin(), p.end() };
}

void AlphaVerbProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;

    lineSize = (int) std::ceil (sr * 0.001 * 73.1 * maxSizeScale) + 8;
    for (auto& l : lines)
        l.assign ((size_t) lineSize, 0.0f);
    lpState.fill (0.0f);
    writePos = 0;

    preBuf.assign ((size_t) (sr * maxPreDelayMs * 0.001) + 8, 0.0f);
    preWrite = 0;

    const float apMs[2] { 5.1f, 7.7f };
    for (int i = 0; i < 2; ++i)
    {
        apLen[(size_t) i] = (int) (sr * 0.001 * apMs[i]);
        apBuf[(size_t) i].assign ((size_t) apLen[(size_t) i] + 1, 0.0f);
        apPos[(size_t) i] = 0;
    }
    lowDampState = 0.0f;
}

bool AlphaVerbProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
}

void AlphaVerbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    const float mix        = apvts.getRawParameterValue ("mix")->load();
    const float decay      = apvts.getRawParameterValue ("decay")->load();
    const float size       = apvts.getRawParameterValue ("size")->load();
    const float preMs      = apvts.getRawParameterValue ("predelay")->load();
    const float brightness = apvts.getRawParameterValue ("brightness")->load();
    const float lowDecay   = apvts.getRawParameterValue ("lowdecay")->load();
    const float width      = apvts.getRawParameterValue ("width")->load();

    const int preSamples = juce::jlimit (0, (int) preBuf.size() - 1, (int) (preMs * 0.001f * (float) sr));

    // per-line delay lengths and feedback gains (RT60 law: g = 10^(-3 * d / (sr * T)))
    std::array<int, N> delaySamples;
    std::array<float, N> gain;
    for (int i = 0; i < N; ++i)
    {
        const float d = baseDelaysMs[(size_t) i] * 0.001f * (float) sr * (size * maxSizeScale * 0.5f + 0.5f);
        delaySamples[(size_t) i] = juce::jlimit (4, lineSize - 2, (int) d);
        gain[(size_t) i] = std::pow (10.0f, -3.0f * (float) delaySamples[(size_t) i] / ((float) sr * decay));
    }

    // damping low-pass coefficient (higher brightness = higher cutoff)
    const float cutoff = juce::jmap (brightness, 1500.0f, 18000.0f);
    const float lpCoef = std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) sr);

    // low-frequency decay shaping: crossover at ~300 Hz
    const float lowCoef = std::exp (-juce::MathConstants<float>::twoPi * 300.0f / (float) sr);

    const float norm = 1.0f / std::sqrt ((float) N); // orthonormal Hadamard scaling
    const float dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
    const float wetGain = std::sin (mix * juce::MathConstants<float>::halfPi);

    for (int n = 0; n < numSamples; ++n)
    {
        const float inL = left[n];
        const float inR = right[n];

        // predelay (mono sum)
        preBuf[(size_t) preWrite] = 0.5f * (inL + inR);
        int rp = preWrite - preSamples;
        if (rp < 0) rp += (int) preBuf.size();
        float x = preBuf[(size_t) rp];
        if (++preWrite >= (int) preBuf.size()) preWrite = 0;

        // input diffusion (allpass chain)
        for (int a = 0; a < 2; ++a)
        {
            auto& buf = apBuf[(size_t) a];
            auto& pos = apPos[(size_t) a];
            const float g = 0.6f;
            const float delayed = buf[(size_t) pos];
            const float v = x - g * delayed;
            buf[(size_t) pos] = v;
            x = delayed + g * v;
            if (++pos >= apLen[(size_t) a]) pos = 0;
        }

        // read delay lines
        std::array<float, N> out;
        for (int i = 0; i < N; ++i)
        {
            int r = writePos - delaySamples[(size_t) i];
            if (r < 0) r += lineSize;
            out[(size_t) i] = lines[(size_t) i][(size_t) r];
        }

        // wet taps for stereo (alternate lines left / right)
        float wetL = 0.0f, wetR = 0.0f;
        for (int i = 0; i < N; ++i)
            ((i & 1) ? wetR : wetL) += out[(size_t) i] * ((i & 2) ? -1.0f : 1.0f);
        wetL *= 0.5f;
        wetR *= 0.5f;

        // Hadamard feedback matrix (fast Walsh-Hadamard transform, 8 points)
        std::array<float, N> h = out;
        for (int len = 1; len < N; len <<= 1)
            for (int i = 0; i < N; i += len << 1)
                for (int j = i; j < i + len; ++j)
                {
                    const float a = h[(size_t) j], b = h[(size_t) (j + len)];
                    h[(size_t) j] = a + b;
                    h[(size_t) (j + len)] = a - b;
                }

        // damping + gain + write back
        for (int i = 0; i < N; ++i)
        {
            float v = h[(size_t) i] * norm;

            // high-frequency damping (one-pole LP)
            lpState[(size_t) i] = v * (1.0f - lpCoef) + lpState[(size_t) i] * lpCoef;
            v = lpState[(size_t) i];

            lines[(size_t) i][(size_t) writePos] = v * gain[(size_t) i] + x * norm;
        }
        if (++writePos >= lineSize) writePos = 0;

        // approximate low-decay multiplier by blending lowpassed wet back in/out
        lowDampState = wetL * (1.0f - lowCoef) + lowDampState * lowCoef;
        const float lowPart = lowDampState;
        wetL += lowPart * (lowDecay - 1.0f);
        wetR += lowPart * (lowDecay - 1.0f);

        // width (mid/side)
        const float mid  = 0.5f * (wetL + wetR);
        const float side = 0.5f * (wetL - wetR) * width;
        wetL = mid + side;
        wetR = mid - side;

        left[n]  = inL * dryGain + wetL * wetGain;
        right[n] = inR * dryGain + wetR * wetGain;
    }
}

juce::AudioProcessorEditor* AlphaVerbProcessor::createEditor()
{
    return new AlphaVerbEditor (*this);
}

void AlphaVerbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AlphaVerbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AlphaVerbProcessor();
}
