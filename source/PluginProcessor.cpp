#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>

namespace
{
constexpr std::size_t expectedTableSamples = 256u * 2048u;
constexpr std::size_t expectedTableBytes = expectedTableSamples * sizeof (float);

float octaveOffsetFromNormalized (float normalizedValue) noexcept
{
    const auto selection = std::lround (
        3.0f * juce::jlimit (0.0f, 1.0f, normalizedValue));
    return static_cast<float> (selection - 1);
}

int waveformIndexFromNormalized (float normalizedValue) noexcept
{
    return juce::jlimit (0, 4, static_cast<int> (std::lround (
        4.0f * juce::jlimit (0.0f, 1.0f, normalizedValue))));
}

constexpr std::array<const char*, 5> waveformNames {
    "SINE", "SAW", "TRIANGLE", "SQUARE", "NOISE"
};

int filterTypeIndexFromValue (float value) noexcept
{
    return juce::jlimit (0, 4, static_cast<int> (std::lround (
        juce::jlimit (0.0f, 0.8f, value) / 0.2f)));
}

constexpr std::array<const char*, 5> filterTypeNames {
    "LOW PASS", "HIGH PASS", "BAND PASS", "BAND REJECT", "PEAKING"
};

constexpr std::array<const char*, 8> equalizerParameterIDs {
    "p45_eq_125", "p46_eq_250", "p47_eq_500", "p48_eq_1k",
    "p49_eq_2k", "p50_eq_4k", "p51_eq_8k", "p52_eq_16k"
};

constexpr std::array<double, 8> equalizerFrequencies {
    125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0
};

constexpr std::array<const char*, 16> sequencerStepParameterIDs {
    "p54_seq_step_01", "p55_seq_step_02", "p56_seq_step_03", "p57_seq_step_04",
    "p58_seq_step_05", "p59_seq_step_06", "p60_seq_step_07", "p61_seq_step_08",
    "p62_seq_step_09", "p63_seq_step_10", "p64_seq_step_11", "p65_seq_step_12",
    "p66_seq_step_13", "p67_seq_step_14", "p68_seq_step_15", "p69_seq_step_16"
};

constexpr std::array<const char*, 8> sequencerRateNames {
    "1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1"
};

// The original sequencer advances twice per conventional displayed note
// division. Keep the original panel labels and preset values, but use the
// measured half-length interval so every rate position matches its playback.
constexpr std::array<double, 8> sequencerBeatsPerStep {
    1.0 / 64.0, 1.0 / 32.0, 1.0 / 16.0, 1.0 / 8.0,
    1.0 / 4.0, 1.0 / 2.0, 1.0, 2.0
};

double filterCutoffHzFromNormalized (float normalizedValue) noexcept
{
    return 20.0 * std::pow (1000.0, juce::jlimit (0.0f, 1.0f, normalizedValue));
}

double filterQFromNormalized (float normalizedValue) noexcept
{
    const auto resonance = juce::jlimit (0.0f, 1.0f, normalizedValue);
    return 0.5 / (1.0 - 0.96 * static_cast<double> (resonance));
}

float applyBitCrusher (float sample,
                       float normalizedAmount,
                       int bitDepth) noexcept
{
    // BITRATE is implemented as amplitude quantisation rather than a
    // sample-rate reducer. The previous sample-and-hold stage created strong
    // staircase aliases that were absent from the original recordings.
    //
    // Literal 16-, 24- and 32-bit quantisation is effectively transparent at
    // this point in a floating-point synth, so the four panel choices use
    // progressively finer musical crusher resolutions. Their grids are
    // deliberately separated by one effective resolution step so that every
    // selection remains audible below the final part of the AMOUNT sweep.
    const auto amount = juce::jlimit (0.0f, 1.0f, normalizedAmount);
    double quantisationLevels = 15.0;
    float profileShapeBlend = 1.0f;

    switch (bitDepth)
    {
        case 32:
            quantisationLevels = 127.0;
            profileShapeBlend = 0.15f;
            break;
        case 24:
            quantisationLevels = 63.0;
            profileShapeBlend = 0.40f;
            break;
        case 16:
            quantisationLevels = 31.0;
            profileShapeBlend = 0.70f;
            break;
        default: break;
    }

    const auto limitedSample = static_cast<double> (
        juce::jlimit (-1.0f, 1.0f, sample));
    const auto quantisedSample = static_cast<float> (
        std::round (limitedSample * quantisationLevels) / quantisationLevels);
    const auto squareSample = sample > 0.0f ? 1.0f
                            : sample < 0.0f ? -1.0f
                                            : 0.0f;
    const auto fullyCrushedSample = quantisedSample
        + profileShapeBlend * (squareSample - quantisedSample);

    // AMOUNT is a true linear wet/dry control. Build the complete selected
    // crusher profile first, including its matched full-wet level lift, then
    // crossfade from the untouched oscillator sample across the entire sweep.
    const auto fullyWetSample = fullyCrushedSample * 1.24f;
    return sample + amount * (fullyWetSample - sample);
}

double characterPhase (double cleanPhase, float normalizedAmount) noexcept
{
    constexpr auto phaseSteps = 6.0;
    const auto amount = static_cast<double> (
        juce::jlimit (0.0f, 1.0f, normalizedAmount));
    const auto wrappedPhase = cleanPhase - std::floor (cleanPhase);
    const auto steppedPhase = std::floor (wrappedPhase * phaseSteps) / phaseSteps;
    auto blendedPhase = wrappedPhase + amount * (steppedPhase - wrappedPhase);

    if (blendedPhase < 0.0)
        blendedPhase += 1.0;

    return blendedPhase;
}

float applyCharacterGrit (float cleanSample, float normalizedAmount) noexcept
{
    constexpr auto waveformLevels = 5.0f;
    constexpr auto drive = 4.0f;
    const auto amount = juce::jlimit (0.0f, 1.0f, normalizedAmount);

    if (amount <= 0.0f)
        return cleanSample;

    const auto limitedSample = juce::jlimit (-1.0f, 1.0f, cleanSample);
    const auto steppedSample = std::round (limitedSample * waveformLevels)
                             / waveformLevels;
    const auto dirtySample = std::tanh (steppedSample * drive) / std::tanh (drive);
    return cleanSample + amount * (dirtySample - cleanSample);
}

float amplifierSustainFromNormalized (float normalizedValue) noexcept
{
    // Removing the measured output-stage saturation from the nine-point
    // sustain sweep leaves the original ADSR response very close to x^0.96.
    return std::pow (juce::jlimit (0.0f, 1.0f, normalizedValue), 0.96f);
}

double midiNoteToHertz (double midiNote) noexcept
{
    // The original VST2 sounds two octaves above the host MIDI note before
    // either oscillator's OCTAVE selector is applied.  The reference probe
    // measures MIDI 57 at 440 Hz with the selector at its lowest position,
    // whereas the standard MIDI mapping alone would produce 220 Hz before
    // that selector's -1 octave offset.
    return 440.0 * std::pow (2.0, (midiNote + 24.0 - 69.0) / 12.0);
}

double glideTimeSecondsFromRate (float normalizedRate) noexcept
{
    // Match the conventional glide-time direction: fully left is immediate,
    // and turning right progressively lengthens the glide up to three seconds.
    const auto glideAmount = static_cast<double> (
        juce::jlimit (0.0f, 1.0f, normalizedRate));
    return 3.0 * glideAmount * glideAmount * glideAmount;
}

struct OriginalParameterSpec
{
    const char* id;
    const char* name;
    float defaultValue;
    bool isSwitch;
    bool isAutomatable;
};

// The IDs, automation flags and normal operating defaults below were read
// directly from the original 32-bit VST2. During development, the two envelope
// attacks default to minimum and the three volume controls default to maximum
// so repeated build tests begin immediately at a useful level. Display names
// are translated from SynthMaker's internal names to the labels printed on the
// original panel. The index prefix keeps every VST3 parameter identity stable.
constexpr std::array<OriginalParameterSpec, 44> originalParameterSpecs {{
    { "p00_amp_attack",          "A",                            0.000000f, false, true  },
    { "p01_amp_decay",           "D",                            0.337500f, false, true  },
    { "p02_amp_release",         "R",                            0.337500f, false, true  },
    { "p03_amp_sustain",         "S",                            0.350000f, false, true  },
    { "p04_crusher_osc1",        "OSC1",                         1.000000f, true,  true  },
    { "p05_crusher_osc2",        "OSC2",                         1.000000f, true,  true  },
    { "p06_bitrate",             "BITRATE",                      0.897211f, false, true  },
    { "p07_delay_mix",           "MIX",                          0.112500f, false, true  },
    { "p08_delay_on",            "ON/OFF",                       1.000000f, true,  true  },
    { "p09_delay_time",          "DELAY",                        0.050000f, false, true  },
    { "p10_delay_feedback",      "FEED",                         0.325000f, false, true  },
    { "p11_filter_env_amount",   "Amount",                       0.000000f, false, true  },
    { "p12_filter_env_attack",   "A",                            0.000000f, false, true  },
    { "p13_filter_env_decay",    "D",                            0.437500f, false, true  },
    { "p14_filter_env_release",  "R",                            0.412500f, false, true  },
    { "p15_filter_env_sustain",  "S",                            0.000000f, false, true  },
    { "p16_filter_cutoff",       "CUTOFF",                       0.843751f, false, true  },
    { "p17_filter_res",          "RES",                          0.337500f, false, true  },
    { "p18_filter_tracking",     "TRACK",                        0.000000f, false, true  },
    { "p19_filter_type",         "TYPE",                         0.000000f, false, true  },
    { "p20_glide_on",            "GLIDE",                        1.000000f, true,  true  },
    { "p21_glide_rate",          "RATE",                         0.337500f, false, true  },
    { "p22_harmonix",            "HARMONIX",                     0.000000f, true,  true  },
    { "p23_knob",                "KNOB",                         1.000000f, false, true  },
    { "p24_main_volume",         "VOLUME",                       1.000000f, false, true  },
    { "p25_monopoly",            "POLY",                         1.000000f, true,  true  },
    { "p26_osc1_volume",         "VOLUME",                       1.000000f, false, true  },
    { "p27_osc2_volume",         "VOLUME",                       1.000000f, false, true  },
    { "p28_osc2_frequency",      "FREQ",                         1.000000f, false, true  },
    { "p29_osc1_octave",         "OCTAVE",                       0.333333f, false, true  },
    { "p30_osc2_octave",         "OCTAVE",                       0.000000f, false, true  },
    { "p31_osc2_osc1",           "OSC",                          0.337500f, false, true  },
    { "p32_osc2_rate",           "OSC2",                         0.337500f, false, true  },
    { "p33_reverb_damp",         "DAMP",                         0.050000f, false, true  },
    { "p34_reverb_mix",          "MIX",                          0.050000f, false, true  },
    { "p35_reverb_on",           "ON/OFF",                       1.000000f, true,  true  },
    { "p36_reverb_room",         "ROOM",                         0.050000f, false, true  },
    { "p37_reverb_width",        "WIDTH",                        0.050000f, false, true  },
    { "p38_step_filter_on",      "FILTER",                       0.000000f, true,  true  },
    { "p39_step_pitch_on",       "PITCH",                        1.000000f, true,  true  },
    { "p40_step_smooth",         "SMOOTH",                       0.125000f, false, true  },
    { "p41_step_rate",           "RATE",                         0.428571f, false, true  },
    { "p42_pitch_waveform",      "PITCH WAVEFORM",               0.125000f, false, false },
    { "p43_filter_waveform",     "FILTER WAVEFORM",              0.000000f, false, false }
}};

// Modern extension. This remains outside the indexed original parameter table
// so the original VST2 parameter identities and order stay unchanged.
constexpr OriginalParameterSpec characterParameterSpec {
    "p44_character", "CHARACTER", 0.000000f, false, true
};

constexpr OriginalParameterSpec sequencerSmoothAttackParameterSpec {
    "p71_seq_smooth_attack", "SMOOTH ATTACK", 0.000000f, false, true
};

constexpr std::array<OriginalParameterSpec, 8> equalizerParameterSpecs {{
    { "p45_eq_125", "125 HZ",  0.000000f, false, true },
    { "p46_eq_250", "250 HZ",  0.000000f, false, true },
    { "p47_eq_500", "500 HZ",  0.000000f, false, true },
    { "p48_eq_1k",  "1 KHZ",   0.000000f, false, true },
    { "p49_eq_2k",  "2 KHZ",   0.000000f, false, true },
    { "p50_eq_4k",  "4 KHZ",   0.000000f, false, true },
    { "p51_eq_8k",  "8 KHZ",   0.000000f, false, true },
    { "p52_eq_16k", "16 KHZ",  0.000000f, false, true }
}};
}

IceCreamAudioProcessor::IceCreamAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    ampAttack = parameters.getRawParameterValue ("p00_amp_attack");
    ampDecay = parameters.getRawParameterValue ("p01_amp_decay");
    ampRelease = parameters.getRawParameterValue ("p02_amp_release");
    ampSustain = parameters.getRawParameterValue ("p03_amp_sustain");
    mainVolume = parameters.getRawParameterValue ("p24_main_volume");
    oscillator1Volume = parameters.getRawParameterValue ("p26_osc1_volume");
    oscillator2Volume = parameters.getRawParameterValue ("p27_osc2_volume");
    oscillator2Frequency = parameters.getRawParameterValue ("p28_osc2_frequency");
    oscillator1Octave = parameters.getRawParameterValue ("p29_osc1_octave");
    oscillator2Octave = parameters.getRawParameterValue ("p30_osc2_octave");
    oscillator1Waveform = parameters.getRawParameterValue ("p31_osc2_osc1");
    oscillator2Waveform = parameters.getRawParameterValue ("p32_osc2_rate");
    bitCrusherOscillator1On = parameters.getRawParameterValue ("p04_crusher_osc1");
    bitCrusherOscillator2On = parameters.getRawParameterValue ("p05_crusher_osc2");
    bitCrusherAmount = parameters.getRawParameterValue ("p06_bitrate");
    bitCrusherBits = parameters.getRawParameterValue ("p53_crusher_bits");
    polyMode = parameters.getRawParameterValue ("p25_monopoly");
    glideOn = parameters.getRawParameterValue ("p20_glide_on");
    glideRate = parameters.getRawParameterValue ("p21_glide_rate");
    harmonixOn = parameters.getRawParameterValue ("p22_harmonix");
    characterAmount = parameters.getRawParameterValue ("p44_character");
    delayMix = parameters.getRawParameterValue ("p07_delay_mix");
    delayOn = parameters.getRawParameterValue ("p08_delay_on");
    delayTime = parameters.getRawParameterValue ("p09_delay_time");
    delayFeedback = parameters.getRawParameterValue ("p10_delay_feedback");
    reverbDamping = parameters.getRawParameterValue ("p33_reverb_damp");
    reverbMix = parameters.getRawParameterValue ("p34_reverb_mix");
    reverbOn = parameters.getRawParameterValue ("p35_reverb_on");
    reverbRoomSize = parameters.getRawParameterValue ("p36_reverb_room");
    reverbWidth = parameters.getRawParameterValue ("p37_reverb_width");
    for (std::size_t band = 0; band < equalizerGains.size(); ++band)
        equalizerGains[band] = parameters.getRawParameterValue (equalizerParameterIDs[band]);
    filterEnvelopeAmount = parameters.getRawParameterValue ("p11_filter_env_amount");
    filterEnvelopeAttack = parameters.getRawParameterValue ("p12_filter_env_attack");
    filterEnvelopeDecay = parameters.getRawParameterValue ("p13_filter_env_decay");
    filterEnvelopeRelease = parameters.getRawParameterValue ("p14_filter_env_release");
    filterEnvelopeSustain = parameters.getRawParameterValue ("p15_filter_env_sustain");
    filterCutoff = parameters.getRawParameterValue ("p16_filter_cutoff");
    filterResonance = parameters.getRawParameterValue ("p17_filter_res");
    filterTracking = parameters.getRawParameterValue ("p18_filter_tracking");
    filterType = parameters.getRawParameterValue ("p19_filter_type");
    sequencerPitchOn = parameters.getRawParameterValue ("p39_step_pitch_on");
    sequencerFilterOn = parameters.getRawParameterValue ("p38_step_filter_on");
    sequencerSmooth = parameters.getRawParameterValue ("p40_step_smooth");
    sequencerSmoothAttack = parameters.getRawParameterValue ("p71_seq_smooth_attack");
    sequencerRate = parameters.getRawParameterValue ("p41_step_rate");
    sequencerFreeRunning = parameters.getRawParameterValue ("p70_seq_free");
    for (std::size_t step = 0; step < sequencerSteps.size(); ++step)
        sequencerSteps[step] = parameters.getRawParameterValue (
            sequencerStepParameterIDs[step]);
    loadEmbeddedWavetables();
}

void IceCreamAudioProcessor::prepareToPlay (double sampleRate, int)
{
    keyboardState.reset();
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    const auto maximumDelaySamples = static_cast<std::size_t> (
        std::ceil (currentSampleRate * 1.6)) + 1u;
    delayBufferLeft.assign (maximumDelaySamples, 0.0f);
    delayBufferRight.assign (maximumDelaySamples, 0.0f);
    reverbProcessor.setSampleRate (currentSampleRate);
    sequencerPhase = 0.0;
    sequencerActiveStep.store (0);
    resetVoices();
    resetEffects();
}

void IceCreamAudioProcessor::releaseResources()
{
    keyboardState.reset();
    resetVoices();
    resetEffects();
}

bool IceCreamAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void IceCreamAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    keyboardState.processNextMidiBuffer (
        midiMessages, 0, buffer.getNumSamples(), true);

    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
                currentTempoBpm = juce::jlimit (20.0, 400.0, *bpm);
        }
    }

    int renderedUntil = 0;

    for (const auto metadata : midiMessages)
    {
        const auto eventPosition = juce::jlimit (0, buffer.getNumSamples(), metadata.samplePosition);
        renderRange (buffer, renderedUntil, eventPosition);
        handleMidiMessage (metadata.getMessage());
        renderedUntil = eventPosition;
    }

    renderRange (buffer, renderedUntil, buffer.getNumSamples());
    processDelay (buffer);
    processReverb (buffer);
    processEqualizer (buffer);
}

juce::AudioProcessorEditor* IceCreamAudioProcessor::createEditor()
{
    return new IceCreamAudioProcessorEditor (*this);
}

bool IceCreamAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String IceCreamAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool IceCreamAudioProcessor::acceptsMidi() const
{
    return true;
}

bool IceCreamAudioProcessor::producesMidi() const
{
    return false;
}

bool IceCreamAudioProcessor::isMidiEffect() const
{
    return false;
}

double IceCreamAudioProcessor::getTailLengthSeconds() const
{
    return 10.0;
}

int IceCreamAudioProcessor::getNumPrograms()
{
    return 1;
}

int IceCreamAudioProcessor::getCurrentProgram()
{
    return 0;
}

void IceCreamAudioProcessor::setCurrentProgram (int)
{
}

const juce::String IceCreamAudioProcessor::getProgramName (int)
{
    return "Default";
}

void IceCreamAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void IceCreamAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto stateXml = parameters.copyState().createXml())
        copyXmlToBinary (*stateXml, destData);
}

void IceCreamAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto stateXml = getXmlFromBinary (data, sizeInBytes))
    {
        if (stateXml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*stateXml));
    }
}

IceCreamAudioProcessor::ParameterTree::ParameterLayout
IceCreamAudioProcessor::createParameterLayout()
{
    ParameterTree::ParameterLayout layout;

    const auto makeParameter = [] (const OriginalParameterSpec& spec)
        -> std::unique_ptr<juce::RangedAudioParameter>
    {
        if (spec.isSwitch)
        {
            const auto attributes = juce::AudioParameterBoolAttributes()
                                        .withAutomatable (spec.isAutomatable);

            return std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { spec.id, 1 },
                spec.name,
                spec.defaultValue >= 0.5f,
                attributes);
        }

        const auto isOctave = std::strcmp (spec.id, "p29_osc1_octave") == 0
                           || std::strcmp (spec.id, "p30_osc2_octave") == 0;
        const auto isWaveform = std::strcmp (spec.id, "p31_osc2_osc1") == 0
                             || std::strcmp (spec.id, "p32_osc2_rate") == 0;
        const auto isFilterType = std::strcmp (spec.id, "p19_filter_type") == 0;
        const auto isStepRate = std::strcmp (spec.id, "p41_step_rate") == 0;
        const auto isEqualizerBand = std::strstr (spec.id, "_eq_") != nullptr;
        const auto attributes = juce::AudioParameterFloatAttributes()
                                    .withAutomatable (spec.isAutomatable)
                                    .withStringFromValueFunction (
                                        [isOctave, isWaveform, isFilterType,
                                         isStepRate, isEqualizerBand] (float value, int)
                                        {
                                            if (isEqualizerBand)
                                                return juce::String (value, 1) + " dB";

                                            if (isFilterType)
                                                return juce::String (
                                                    filterTypeNames[static_cast<std::size_t> (
                                                        filterTypeIndexFromValue (value))]);

                                            if (isStepRate)
                                            {
                                                const auto index = juce::jlimit (
                                                    0, 7, static_cast<int> (std::lround (
                                                        juce::jlimit (0.0f, 1.0f, value) * 7.0f)));
                                                return juce::String (
                                                    sequencerRateNames[static_cast<std::size_t> (
                                                        index)]);
                                            }

                                            if (isWaveform)
                                                return juce::String (
                                                    waveformNames[static_cast<std::size_t> (
                                                        waveformIndexFromNormalized (value))]);

                                            if (isOctave)
                                            {
                                                const auto octaves = octaveOffsetFromNormalized (value);
                                                auto text = juce::String (octaves, 0);
                                                if (octaves > 0.0f)
                                                    text = "+" + text;
                                                return text + " oct";
                                            }

                                            return juce::String (value, 6);
                                        });

        auto range = juce::NormalisableRange<float> { 0.0f, 1.0f };
        if (isFilterType)
            range = juce::NormalisableRange<float> { 0.0f, 0.8f, 0.2f };
        else if (isEqualizerBand)
            range = juce::NormalisableRange<float> { -12.0f, 12.0f, 0.1f };
        else if (isOctave)
            range.interval = 1.0f / 3.0f;
        else if (isWaveform)
            range.interval = 0.25f;
        else if (isStepRate)
            range.interval = 1.0f / 7.0f;

        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { spec.id, 1 },
            spec.name,
            range,
            spec.defaultValue,
            attributes);
    };

    const auto addGroup = [&layout, &makeParameter] (
                              const char* id,
                              const char* name,
                              std::initializer_list<std::size_t> parameterIndices,
                              const OriginalParameterSpec* extension = nullptr)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
            id, name, " / ");

        for (const auto index : parameterIndices)
            group->addChild (makeParameter (originalParameterSpecs[index]));

        if (extension != nullptr)
            group->addChild (makeParameter (*extension));

        layout.add (std::move (group));
    };

    addGroup ("master",      "MASTER",      { 24 });
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
            "equalizer", "EQ", " / ");

        for (const auto& spec : equalizerParameterSpecs)
            group->addChild (makeParameter (spec));

        layout.add (std::move (group));
    }
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
            "sequencer", "SEQUENCER", " / ");

        for (const auto index : { 39u, 38u, 40u, 41u, 42u, 43u })
            group->addChild (makeParameter (originalParameterSpecs[index]));

        const auto stepAttributes = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (std::lround ((2.0f * value - 1.0f) * 100.0f))
                     + "%";
            });

        for (std::size_t step = 0; step < sequencerStepParameterIDs.size(); ++step)
        {
            group->addChild (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { sequencerStepParameterIDs[step], 1 },
                "STEP " + juce::String (step + 1),
                juce::NormalisableRange<float> { 0.0f, 1.0f },
                0.5f,
                stepAttributes));
        }

        group->addChild (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "p70_seq_free", 1 },
            "FREE",
            true));

        layout.add (std::move (group));
    }
    addGroup ("oscillator1", "OSC1",        { 31, 26, 29 });
    addGroup ("oscillator2", "OSC2",        { 32, 27, 30, 28 });
    addGroup ("filter",      "FILTER",      { 19, 16, 18, 17 });
    addGroup ("ampEnvelope", "AMP ENV",     { 0, 1, 3, 2 });
    addGroup ("filterEnv",   "FILT ENV",    { 12, 13, 15, 14, 11 });
    addGroup ("control",     "CONTROL",     { 25, 22, 20, 21 },
              &characterParameterSpec);
    addGroup ("reverb",      "REVERB",      { 35, 36, 33, 37, 34 });
    addGroup ("delay",       "DELAY",       { 8, 9, 10, 7 });
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
            "bitcrusher", "BITCRUSHER", " / ");

        group->addChild (makeParameter (originalParameterSpecs[6]));
        group->addChild (makeParameter (originalParameterSpecs[4]));
        group->addChild (makeParameter (originalParameterSpecs[5]));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "p53_crusher_bits", 1 },
            "BITS",
            juce::StringArray { "32 BIT", "24 BIT", "16 BIT", "8 BIT" },
            3));

        layout.add (std::move (group));
    }
    addGroup ("unidentified", "UNIDENTIFIED", { 23 });
    addGroup ("sequencerExtensions", "SEQUENCER", {},
              &sequencerSmoothAttackParameterSpec);

    return layout;
}

void IceCreamAudioProcessor::loadEmbeddedWavetables()
{
    sawTables.assign (expectedTableSamples, 0.0f);
    triangleTables.assign (expectedTableSamples, 0.0f);

    int sawSize = 0;
    int triangleSize = 0;
    const auto* sawData = BinaryData::getNamedResource ("saw2KtableSM_dat", sawSize);
    const auto* triangleData = BinaryData::getNamedResource ("tri2KtableSM_dat", triangleSize);

    if (sawData != nullptr && static_cast<std::size_t> (sawSize) == expectedTableBytes)
        std::memcpy (sawTables.data(), sawData, expectedTableBytes);

    if (triangleData != nullptr && static_cast<std::size_t> (triangleSize) == expectedTableBytes)
        std::memcpy (triangleTables.data(), triangleData, expectedTableBytes);

    const auto sawHasContent = std::any_of (sawTables.begin(), sawTables.end(),
                                            [] (float value) { return value != 0.0f; });

    if (! sawHasContent)
    {
        for (int table = 1; table < tableCount; ++table)
        {
            for (int sample = 0; sample < tableLength; ++sample)
            {
                const auto phase = static_cast<float> (sample) / static_cast<float> (tableLength);
                sawTables[static_cast<std::size_t> (table * tableLength + sample)]
                    = 2.0f * phase - 1.0f;
            }
        }
    }
}

void IceCreamAudioProcessor::resetVoices()
{
    for (auto& voice : voices)
        voice = {};

    heldNotes.fill (false);
    heldNoteOrder.fill (-1);
    heldNoteCount = 0;
    nextVoiceAge = 1;
}

void IceCreamAudioProcessor::resetEffects()
{
    std::fill (delayBufferLeft.begin(), delayBufferLeft.end(), 0.0f);
    std::fill (delayBufferRight.begin(), delayBufferRight.end(), 0.0f);
    delayWritePosition = 0;
    delayWasEnabled = false;
    reverbWasEnabled = false;
    reverbProcessor.reset();

    for (std::size_t band = 0; band < equalizerLeft.size(); ++band)
    {
        equalizerLeft[band].reset();
        equalizerRight[band].reset();
    }
}

void IceCreamAudioProcessor::processDelay (juce::AudioBuffer<float>& buffer)
{
    const auto enabled = delayOn != nullptr && delayOn->load() >= 0.5f;

    if (! enabled || delayBufferLeft.empty() || delayBufferRight.empty())
    {
        if (delayWasEnabled)
        {
            std::fill (delayBufferLeft.begin(), delayBufferLeft.end(), 0.0f);
            std::fill (delayBufferRight.begin(), delayBufferRight.end(), 0.0f);
            delayWritePosition = 0;
        }

        delayWasEnabled = false;
        return;
    }

    delayWasEnabled = true;
    const auto normalizedTime = juce::jlimit (
        0.0f, 1.0f, delayTime != nullptr ? delayTime->load() : 0.050000f);
    const auto normalizedFeedback = juce::jlimit (
        0.0f, 1.0f, delayFeedback != nullptr ? delayFeedback->load() : 0.325000f);
    const auto wet = juce::jlimit (
        0.0f, 1.0f, delayMix != nullptr ? delayMix->load() : 0.112500f);
    const auto feedback = 0.92f * normalizedFeedback;
    const auto delaySeconds = juce::jmax (
        1.0 / currentSampleRate, static_cast<double> (normalizedTime));
    const auto delaySamples = juce::jlimit<std::size_t> (
        1u,
        delayBufferLeft.size() - 1u,
        static_cast<std::size_t> (std::lround (delaySeconds * currentSampleRate)));
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto readPosition = (delayWritePosition + delayBufferLeft.size() - delaySamples)
                                % delayBufferLeft.size();
        const auto delayedLeft = delayBufferLeft[readPosition];
        const auto delayedRight = delayBufferRight[readPosition];
        const auto inputLeft = left[sample];
        const auto inputRight = right[sample];

        delayBufferLeft[delayWritePosition] = inputLeft + delayedLeft * feedback;
        delayBufferRight[delayWritePosition] = inputRight + delayedRight * feedback;
        // MIX is an additive echo level in the original. Crossfading the dry
        // path made All Star's direct signal 0.403368 times its intended
        // level: almost exactly the eight-decibel shortfall in the A/B file.
        left[sample] = inputLeft + delayedLeft * wet;
        right[sample] = inputRight + delayedRight * wet;

        delayWritePosition = (delayWritePosition + 1u) % delayBufferLeft.size();
    }
}

void IceCreamAudioProcessor::processReverb (juce::AudioBuffer<float>& buffer)
{
    const auto enabled = reverbOn != nullptr && reverbOn->load() >= 0.5f;

    if (! enabled)
    {
        if (reverbWasEnabled)
            reverbProcessor.reset();

        reverbWasEnabled = false;
        return;
    }

    reverbWasEnabled = true;
    juce::Reverb::Parameters effectParameters;
    effectParameters.roomSize = juce::jlimit (
        0.0f, 1.0f, reverbRoomSize != nullptr ? reverbRoomSize->load() : 0.050000f);
    effectParameters.damping = juce::jlimit (
        0.0f, 1.0f, reverbDamping != nullptr ? reverbDamping->load() : 0.050000f);
    effectParameters.width = juce::jlimit (
        0.0f, 1.0f, reverbWidth != nullptr ? reverbWidth->load() : 0.050000f);
    const auto wet = juce::jlimit (
        0.0f, 1.0f, reverbMix != nullptr ? reverbMix->load() : 0.050000f);
    // JUCE's compact reverb internally scales wet by 3 and dry by 2. These
    // compensated values make MIX 0 an exact dry bypass and MIX 1 fully wet.
    effectParameters.wetLevel = wet / 3.0f;
    effectParameters.dryLevel = (1.0f - wet) / 2.0f;
    effectParameters.freezeMode = 0.0f;
    reverbProcessor.setParameters (effectParameters);
    reverbProcessor.processStereo (
        buffer.getWritePointer (0),
        buffer.getWritePointer (1),
        buffer.getNumSamples());
}

void IceCreamAudioProcessor::processEqualizer (juce::AudioBuffer<float>& buffer)
{
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    const auto maximumFrequency = currentSampleRate * 0.45;

    for (std::size_t band = 0; band < equalizerFrequencies.size(); ++band)
    {
        const auto gainDb = juce::jlimit (
            -12.0f,
            12.0f,
            equalizerGains[band] != nullptr ? equalizerGains[band]->load() : 0.0f);
        const auto frequency = juce::jlimit (
            20.0, maximumFrequency, equalizerFrequencies[band]);
        // The octave-spaced graphic bands must be narrower than ordinary
        // tone-control peaks. With Q 1 the 500 Hz through 4 kHz cuts stacked
        // into a broad 7-to-9 dB loss, making the photographed factory curve
        // much darker and quieter than the original. Keep 16K at its previous
        // width while reducing overlap between the first seven bands.
        const auto bandQ = band + 1u == equalizerFrequencies.size() ? 1.0 : 2.0;
        const auto coefficients = juce::IIRCoefficients::makePeakFilter (
            currentSampleRate,
            frequency,
            bandQ,
            static_cast<double> (juce::Decibels::decibelsToGain (gainDb)));

        equalizerLeft[band].setCoefficients (coefficients);
        equalizerRight[band].setCoefficients (coefficients);
        equalizerLeft[band].processSamples (left, buffer.getNumSamples());
        equalizerRight[band].processSamples (right, buffer.getNumSamples());
    }
}

void IceCreamAudioProcessor::startVoice (int midiNote, float velocity)
{
    auto* selectedVoice = static_cast<Voice*> (nullptr);
    const auto isPolyphonic = polyMode == nullptr || polyMode->load() >= 0.5f;
    const auto glideEnabled = glideOn != nullptr && glideOn->load() >= 0.5f;
    auto glideStartMidiNote = static_cast<double> (midiNote);
    auto* newestActiveVoice = static_cast<Voice*> (nullptr);

    if (glideEnabled)
    {
        for (auto& voice : voices)
        {
            if (voice.active
                && (newestActiveVoice == nullptr || voice.age > newestActiveVoice->age))
            {
                newestActiveVoice = &voice;
            }
        }

        if (newestActiveVoice != nullptr)
            glideStartMidiNote = newestActiveVoice->currentMidiNote;
    }

    if (! isPolyphonic)
    {
        // The original switch is illuminated for POLY. In its unlit state,
        // a new note replaces the previous monophonic voice immediately.
        for (auto& voice : voices)
            voice = {};

        selectedVoice = &voices.front();
    }

    for (auto& voice : voices)
    {
        if (! isPolyphonic)
            break;

        if (voice.active && voice.note == midiNote)
        {
            selectedVoice = &voice;
            break;
        }

        if (! voice.active && selectedVoice == nullptr)
            selectedVoice = &voice;
    }

    if (selectedVoice == nullptr)
    {
        selectedVoice = &*std::min_element (
            voices.begin(), voices.end(),
            [] (const Voice& left, const Voice& right) { return left.age < right.age; });
    }

    selectedVoice->note = midiNote;
    selectedVoice->active = true;
    // The original produces exactly the same level at MIDI velocities 16,
    // 32, 48, 64, 80, 100 and 127.
    juce::ignoreUnused (velocity);
    selectedVoice->velocity = 1.0f;
    selectedVoice->envelope = 0.0f;
    selectedVoice->releaseStep = 0.0f;
    selectedVoice->envelopeStage = EnvelopeStage::attack;
    selectedVoice->filterEnvelope = 0.0f;
    selectedVoice->filterEnvelopeReleaseStep = 0.0f;
    selectedVoice->filterEnvelopeStage = EnvelopeStage::attack;
    selectedVoice->currentMidiNote = glideStartMidiNote;
    selectedVoice->targetMidiNote = static_cast<double> (midiNote);
    selectedVoice->phase = 0.0;
    selectedVoice->phaseDelta = midiNoteToHertz (selectedVoice->currentMidiNote)
                              / currentSampleRate;
    selectedVoice->phase2 = 0.0;
    selectedVoice->phaseDelta2 = selectedVoice->phaseDelta;
    selectedVoice->harmonixPhase1 = 0.0;
    selectedVoice->harmonixPhase2 = 0.0;
    selectedVoice->characterPhase1 = 0.0;
    selectedVoice->characterPhase2 = 0.0;
    selectedVoice->characterWavePhase1 = 0.0;
    selectedVoice->characterWavePhase2 = 0.0;
    selectedVoice->filterState1 = 0.0;
    selectedVoice->filterState2 = 0.0;
    selectedVoice->filterG = 0.0;
    selectedVoice->filterK = 2.0;
    selectedVoice->filterTrackingRatio = 1.0;
    selectedVoice->filterType = 0;
    selectedVoice->noiseState1 = 0x13579bdu
                               ^ static_cast<std::uint32_t> (midiNote * 0x9e3779b9u)
                               ^ static_cast<std::uint32_t> (nextVoiceAge);
    selectedVoice->noiseState2 = 0x2468aceu
                               ^ static_cast<std::uint32_t> (midiNote * 0x85ebca6bu)
                               ^ static_cast<std::uint32_t> (nextVoiceAge << 1u);
    selectedVoice->characterNoiseState1 = selectedVoice->noiseState1;
    selectedVoice->characterNoiseState2 = selectedVoice->noiseState2;
    selectedVoice->age = nextVoiceAge++;
}

void IceCreamAudioProcessor::releaseVoice (int midiNote)
{
    for (auto& voice : voices)
    {
        if (voice.active && voice.note == midiNote)
            beginRelease (voice);
    }
}

void IceCreamAudioProcessor::beginRelease (Voice& voice)
{
    if (! voice.active)
        return;

    if (voice.envelopeStage != EnvelopeStage::release)
    {
        const auto normalizedRelease = juce::jlimit (
            0.0f, 1.0f, ampRelease != nullptr ? ampRelease->load() : 0.337500f);
        const auto releaseSeconds = envelopeTimeSeconds (normalizedRelease);

        voice.envelopeStage = EnvelopeStage::release;
        voice.releaseStep = releaseSeconds > 0.0f
                              ? voice.envelope / static_cast<float> (
                                    currentSampleRate * releaseSeconds)
                              : juce::jmax (voice.envelope, 1.0f);
    }

    if (voice.filterEnvelopeStage != EnvelopeStage::release)
    {
        const auto normalizedRelease = juce::jlimit (
            0.0f,
            1.0f,
            filterEnvelopeRelease != nullptr ? filterEnvelopeRelease->load() : 0.412500f);
        const auto releaseSeconds = envelopeTimeSeconds (normalizedRelease);

        voice.filterEnvelopeStage = EnvelopeStage::release;
        voice.filterEnvelopeReleaseStep = releaseSeconds > 0.0f
                                            ? voice.filterEnvelope / static_cast<float> (
                                                  currentSampleRate * releaseSeconds)
                                            : juce::jmax (voice.filterEnvelope, 1.0f);
    }
}

void IceCreamAudioProcessor::handleMidiMessage (const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        const auto noteNumber = juce::jlimit (0, 127, message.getNoteNumber());
        const auto isFirstHeldNote = heldNoteCount == 0;

        if (heldNotes[static_cast<std::size_t> (noteNumber)])
        {
            auto existingIndex = -1;
            for (int index = 0; index < heldNoteCount; ++index)
            {
                if (heldNoteOrder[static_cast<std::size_t> (index)] == noteNumber)
                {
                    existingIndex = index;
                    break;
                }
            }

            if (existingIndex >= 0)
            {
                for (int index = existingIndex; index + 1 < heldNoteCount; ++index)
                {
                    heldNoteOrder[static_cast<std::size_t> (index)] =
                        heldNoteOrder[static_cast<std::size_t> (index + 1)];
                }
                heldNoteOrder[static_cast<std::size_t> (heldNoteCount - 1)] = noteNumber;
            }
        }
        else
        {
            heldNotes[static_cast<std::size_t> (noteNumber)] = true;
            heldNoteOrder[static_cast<std::size_t> (heldNoteCount)] = noteNumber;
            ++heldNoteCount;
        }

        const auto freeRunning = sequencerFreeRunning == nullptr
                              || sequencerFreeRunning->load() >= 0.5f;
        const auto isPolyphonic = polyMode == nullptr || polyMode->load() >= 0.5f;

        if (! freeRunning && (! isPolyphonic || isFirstHeldNote))
        {
            sequencerPhase = 0.0;
            sequencerActiveStep.store (0);
        }

        startVoice (noteNumber, message.getFloatVelocity());
        return;
    }

    if (message.isNoteOff())
    {
        const auto noteNumber = juce::jlimit (0, 127, message.getNoteNumber());
        if (heldNotes[static_cast<std::size_t> (noteNumber)])
        {
            heldNotes[static_cast<std::size_t> (noteNumber)] = false;
            for (int index = 0; index < heldNoteCount; ++index)
            {
                if (heldNoteOrder[static_cast<std::size_t> (index)] != noteNumber)
                    continue;

                for (int shifted = index; shifted + 1 < heldNoteCount; ++shifted)
                {
                    heldNoteOrder[static_cast<std::size_t> (shifted)] =
                        heldNoteOrder[static_cast<std::size_t> (shifted + 1)];
                }
                break;
            }
            heldNoteCount = juce::jmax (0, heldNoteCount - 1);
            heldNoteOrder[static_cast<std::size_t> (heldNoteCount)] = -1;
        }

        const auto isPolyphonic = polyMode == nullptr || polyMode->load() >= 0.5f;
        if (! isPolyphonic && heldNoteCount > 0)
        {
            auto* monoVoice = static_cast<Voice*> (nullptr);
            for (auto& voice : voices)
            {
                if (voice.active && voice.note == noteNumber)
                {
                    monoVoice = &voice;
                    break;
                }
            }

            if (monoVoice != nullptr)
            {
                const auto fallbackNote = heldNoteOrder[
                    static_cast<std::size_t> (heldNoteCount - 1)];
                monoVoice->note = fallbackNote;
                monoVoice->targetMidiNote = static_cast<double> (fallbackNote);
                if (glideOn == nullptr || glideOn->load() < 0.5f)
                    monoVoice->currentMidiNote = monoVoice->targetMidiNote;
                return;
            }
        }

        releaseVoice (noteNumber);
        return;
    }

    if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        heldNotes.fill (false);
        heldNoteOrder.fill (-1);
        heldNoteCount = 0;
        for (auto& voice : voices)
            beginRelease (voice);
    }
}

void IceCreamAudioProcessor::renderRange (juce::AudioBuffer<float>& buffer,
                                          int startSample,
                                          int endSample)
{
    if (startSample >= endSample)
        return;

    const auto normalizedVolume = juce::jlimit (
        0.0f, 1.0f, mainVolume != nullptr ? mainVolume->load() : 1.000000f);
    const auto oscillator1Gain = juce::jlimit (
        0.0f, 1.0f, oscillator1Volume != nullptr ? oscillator1Volume->load() : 1.000000f);
    const auto oscillator2Gain = juce::jlimit (
        0.0f, 1.0f, oscillator2Volume != nullptr ? oscillator2Volume->load() : 1.000000f);
    const auto oscillator1OctaveValue = juce::jlimit (
        0.0f, 1.0f, oscillator1Octave != nullptr ? oscillator1Octave->load() : 0.333333f);
    const auto oscillator2OctaveValue = juce::jlimit (
        0.0f, 1.0f, oscillator2Octave != nullptr ? oscillator2Octave->load() : 0.000000f);
    const auto oscillator2FrequencyValue = juce::jlimit (
        0.0f, 1.0f, oscillator2Frequency != nullptr ? oscillator2Frequency->load() : 1.000000f);
    const auto oscillator1WaveformValue = juce::jlimit (
        0.0f, 1.0f, oscillator1Waveform != nullptr ? oscillator1Waveform->load() : 0.337500f);
    const auto oscillator2WaveformValue = juce::jlimit (
        0.0f, 1.0f, oscillator2Waveform != nullptr ? oscillator2Waveform->load() : 0.337500f);
    const auto oscillator1CrusherEnabled = bitCrusherOscillator1On != nullptr
                                         && bitCrusherOscillator1On->load() >= 0.5f;
    const auto oscillator2CrusherEnabled = bitCrusherOscillator2On != nullptr
                                         && bitCrusherOscillator2On->load() >= 0.5f;
    const auto normalizedBitCrusherAmount = juce::jlimit (
        0.0f, 1.0f, bitCrusherAmount != nullptr ? bitCrusherAmount->load() : 0.897211f);
    const auto bitCrusherDepthIndex = juce::jlimit (
        0, 3, static_cast<int> (std::lround (
            bitCrusherBits != nullptr ? bitCrusherBits->load() : 3.0f)));
    constexpr std::array<int, 4> bitCrusherDepths { 32, 24, 16, 8 };
    const auto bitCrusherDepth = bitCrusherDepths[
        static_cast<std::size_t> (bitCrusherDepthIndex)];
    const auto isPolyphonic = polyMode == nullptr || polyMode->load() >= 0.5f;
    const auto glideEnabled = glideOn != nullptr && glideOn->load() >= 0.5f;
    const auto normalizedGlideRate = juce::jlimit (
        0.0f, 1.0f, glideRate != nullptr ? glideRate->load() : 0.337500f);
    const auto glideTimeSeconds = glideTimeSecondsFromRate (normalizedGlideRate);
    const auto glideSmoothingCoefficient = glideEnabled && glideTimeSeconds > 0.0
        ? std::exp (std::log (0.001) / (glideTimeSeconds * currentSampleRate))
        : 0.0;
    const auto harmonixEnabled = harmonixOn != nullptr && harmonixOn->load() >= 0.5f;
    const auto normalizedCharacter = juce::jlimit (
        0.0f, 1.0f, characterAmount != nullptr ? characterAmount->load() : 1.000000f);
    // Stretch Stage 40.67's preferred 0-50% region across the complete knob.
    // The parallel dry/processed character blend therefore never passes the
    // midpoint where its audible interaction previously began weakening.
    const auto characterMix = 0.5f * normalizedCharacter;
    const auto normalizedFilterCutoff = juce::jlimit (
        0.0f, 1.0f, filterCutoff != nullptr ? filterCutoff->load() : 0.843751f);
    const auto normalizedFilterResonance = juce::jlimit (
        0.0f, 1.0f, filterResonance != nullptr ? filterResonance->load() : 0.337500f);
    const auto normalizedFilterTracking = juce::jlimit (
        0.0f, 1.0f, filterTracking != nullptr ? filterTracking->load() : 0.000000f);
    const auto filterTypeValue = juce::jlimit (
        0.0f, 0.8f, filterType != nullptr ? filterType->load() : 0.000000f);
    const auto normalizedFilterEnvelopeAmount = juce::jlimit (
        0.0f,
        1.0f,
        filterEnvelopeAmount != nullptr ? filterEnvelopeAmount->load() : 0.000000f);
    const auto sequencerPitchEnabled = sequencerPitchOn != nullptr
                                    && sequencerPitchOn->load() >= 0.5f;
    const auto sequencerFilterEnabled = sequencerFilterOn != nullptr
                                     && sequencerFilterOn->load() >= 0.5f;
    const auto normalizedSequencerSmooth = juce::jlimit (
        0.0f, 1.0f, sequencerSmooth != nullptr ? sequencerSmooth->load() : 0.125000f);
    const auto normalizedSequencerSmoothAttack = juce::jlimit (
        0.0f, 1.0f,
        sequencerSmoothAttack != nullptr ? sequencerSmoothAttack->load() : 0.000000f);
    const auto normalizedSequencerRate = juce::jlimit (
        0.0f, 1.0f, sequencerRate != nullptr ? sequencerRate->load() : 0.428571f);
    const auto sequencerIsFreeRunning = sequencerFreeRunning == nullptr
                                     || sequencerFreeRunning->load() >= 0.5f;
    const auto sequencerRateIndex = juce::jlimit (
        0, 7, static_cast<int> (std::lround (normalizedSequencerRate * 7.0f)));
    const auto secondsPerSequencerStep = (60.0 / currentTempoBpm)
        * sequencerBeatsPerStep[static_cast<std::size_t> (sequencerRateIndex)];
    const auto sequencerPhaseIncrement = 1.0
        / juce::jmax (1.0, secondsPerSequencerStep * currentSampleRate);
    std::array<float, 16> sequencerStepValues {};
    for (std::size_t step = 0; step < sequencerStepValues.size(); ++step)
    {
        sequencerStepValues[step] = juce::jlimit (
            0.0f,
            1.0f,
            sequencerSteps[step] != nullptr ? sequencerSteps[step]->load() : 0.5f);
    }
    // The original OSC2 FREQ control is centred around unison.  Measurements
    // at MIDI 57 and the middle octave position give approximately 1179 Hz,
    // 1760 Hz and 2341 Hz at values 0, 0.5 and 1: a linear ratio spanning
    // two-thirds to four-thirds of OSC1, not the old half-to-unison mapping.
    const auto oscillator2FrequencyRatio = (2.0 / 3.0)
                                         * (1.0 + oscillator2FrequencyValue);
    const auto oscillator1PitchRatio = std::pow (
        2.0, octaveOffsetFromNormalized (oscillator1OctaveValue));
    const auto oscillator2PitchRatio = std::pow (
        2.0,
        octaveOffsetFromNormalized (oscillator2OctaveValue))
        * oscillator2FrequencyRatio;

    // The longer original-plug-in sweep shows that HARMONIX is more than a
    // static detune. Its strongest pitch movement follows approximately one
    // modulation cycle per 50 oscillator cycles, so OSC2 FREQ continuously
    // changes the movement rate. The mean OSC2 detune also rises from about
    // +9.29 cents at FREQ 0 to +14.97 cents at FREQ 1. These independent
    // per-oscillator sub-phases reproduce that unusual chip-like interaction
    // without the hard phase resets of the rejected sync approximation.
    constexpr auto harmonixCycleDivisor = 50.0;
    constexpr auto harmonixOscillator1MeanCents = -0.764467;
    constexpr auto harmonixOscillator1DepthCents = 11.8;
    const auto harmonixOscillator2MeanCents = 9.293907
        + (14.967862 - 9.293907) * oscillator2FrequencyValue;
    constexpr auto harmonixOscillator2DepthCents = 8.0;
    const auto attackSeconds = envelopeTimeSeconds (
        ampAttack != nullptr ? ampAttack->load() : 0.000000f);
    const auto decaySeconds = envelopeTimeSeconds (
        ampDecay != nullptr ? ampDecay->load() : 0.337500f);
    const auto sustainLevel = amplifierSustainFromNormalized (
        ampSustain != nullptr ? ampSustain->load() : 0.350000f);
    const auto filterAttackSeconds = envelopeTimeSeconds (
        filterEnvelopeAttack != nullptr ? filterEnvelopeAttack->load() : 0.000000f);
    const auto filterDecaySeconds = envelopeTimeSeconds (
        filterEnvelopeDecay != nullptr ? filterEnvelopeDecay->load() : 0.437500f);
    const auto filterSustainLevel = juce::jlimit (
        0.0f,
        1.0f,
        filterEnvelopeSustain != nullptr ? filterEnvelopeSustain->load() : 0.000000f);

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    if (! isPolyphonic)
    {
        auto* newestVoice = static_cast<Voice*> (nullptr);

        for (auto& voice : voices)
        {
            if (voice.active && (newestVoice == nullptr || voice.age > newestVoice->age))
                newestVoice = &voice;
        }

        for (auto& voice : voices)
        {
            if (voice.active && &voice != newestVoice)
                voice = {};
        }
    }

    for (auto& voice : voices)
    {
        if (! voice.active)
            continue;

        const auto q = filterQFromNormalized (normalizedFilterResonance);
        voice.filterK = 1.0 / q;
        voice.filterType = filterTypeIndexFromValue (filterTypeValue);
    }

    for (int sample = startSample; sample < endSample; ++sample)
    {
        float mixedSample = 0.0f;
        const auto sequencerStepIndex = juce::jlimit (
            0, 15, static_cast<int> (std::floor (sequencerPhase)));
        const auto nextSequencerStepIndex = (sequencerStepIndex + 1) % 16;
        const auto positionWithinStep = static_cast<float> (
            sequencerPhase - std::floor (sequencerPhase));
        const auto currentStepValue = sequencerStepValues[
            static_cast<std::size_t> (sequencerStepIndex)];
        const auto nextStepValue = sequencerStepValues[
            static_cast<std::size_t> (nextSequencerStepIndex)];
        // The original reaches its strongest step interpolation at about 90%.
        // The final tenth then fades the complete sequencer modulation toward
        // its neutral centre, so maximum Smooth has no pitch or filter effect.
        constexpr auto sequencerFlattenStart = 0.9f;
        const auto sequencerInterpolationAmount = juce::jmin (
            normalizedSequencerSmooth, sequencerFlattenStart);
        const auto sequencerFlattenAmount = juce::jlimit (
            0.0f, 1.0f,
            (normalizedSequencerSmooth - sequencerFlattenStart)
                / (1.0f - sequencerFlattenStart));
        auto interpolatedSequencerValue = currentStepValue
            + sequencerInterpolationAmount * positionWithinStep
                * (nextStepValue - currentStepValue);

        // Close the sequence with a genuine crossfade.  The ordinary step
        // response above intentionally retains the original partial smoothing,
        // but that leaves a residual jump when phase wraps from step 16 to
        // step 1.  Use the final part of step 16 as a smoothing window whose
        // length follows Smooth, reaching step 1 exactly at the loop boundary.
        if (sequencerStepIndex == 15 && sequencerInterpolationAmount > 0.000001f)
        {
            const auto loopTransitionStart = 1.0f - sequencerInterpolationAmount;
            const auto loopTransitionPosition = juce::jlimit (
                0.0f, 1.0f,
                (positionWithinStep - loopTransitionStart)
                    / sequencerInterpolationAmount);
            interpolatedSequencerValue = currentStepValue
                + loopTransitionPosition * (nextStepValue - currentStepValue);
        }
        auto sequencerValue = interpolatedSequencerValue
            + sequencerFlattenAmount * (0.5f - interpolatedSequencerValue);

        // Attack Smooth is independent of the existing release-side response.
        // At each boundary it begins at the preceding step's existing output,
        // then curves into the current step.  A value of zero leaves the
        // established p40_step_smooth algorithm bit-for-bit unchanged.
        if (normalizedSequencerSmoothAttack > 0.000001f
            && positionWithinStep < normalizedSequencerSmoothAttack)
        {
            const auto previousSequencerStepIndex = (sequencerStepIndex + 15) % 16;
            const auto previousStepValue = sequencerStepValues[
                static_cast<std::size_t> (previousSequencerStepIndex)];
            auto previousReleaseEndValue = previousStepValue
                + sequencerInterpolationAmount * (currentStepValue - previousStepValue);

            if (previousSequencerStepIndex == 15
                && sequencerInterpolationAmount > 0.000001f)
            {
                previousReleaseEndValue = currentStepValue;
            }

            const auto flattenedPreviousEndValue = previousReleaseEndValue
                + sequencerFlattenAmount * (0.5f - previousReleaseEndValue);
            const auto flattenedCurrentStartValue = currentStepValue
                + sequencerFlattenAmount * (0.5f - currentStepValue);
            const auto attackProgress = juce::jlimit (
                0.0f, 1.0f,
                positionWithinStep / normalizedSequencerSmoothAttack);
            const auto attackRemainder = (1.0f - attackProgress)
                                       * (1.0f - attackProgress);
            sequencerValue += attackRemainder
                * (flattenedPreviousEndValue - flattenedCurrentStartValue);
        }
        const auto sequencerPitchRatio = sequencerPitchEnabled
            ? 2.0 * static_cast<double> (sequencerValue)
            : 1.0;
        const auto bipolarSequencerValue = 2.0f * sequencerValue - 1.0f;
        const auto sequencerFilterOffset = sequencerFilterEnabled
            ? 0.75f * bipolarSequencerValue
            : 0.0f;
        sequencerActiveStep.store (sequencerStepIndex);

        for (auto& voice : voices)
        {
            if (! voice.active)
                continue;

            if (! advanceAmpEnvelope (voice, attackSeconds, decaySeconds, sustainLevel))
                continue;

            advanceFilterEnvelope (
                voice, filterAttackSeconds, filterDecaySeconds, filterSustainLevel);

            voice.currentMidiNote = voice.targetMidiNote
                                  + (voice.currentMidiNote - voice.targetMidiNote)
                                        * glideSmoothingCoefficient;

            if (std::abs (voice.currentMidiNote - voice.targetMidiNote) < 0.0001)
                voice.currentMidiNote = voice.targetMidiNote;

            const auto basePhaseDelta = midiNoteToHertz (voice.currentMidiNote)
                                      / currentSampleRate
                                      * sequencerPitchRatio;
            const auto oscillator1BasePhaseDelta = basePhaseDelta * oscillator1PitchRatio;
            const auto oscillator2BasePhaseDelta = basePhaseDelta * oscillator2PitchRatio;

            const auto characterMovement1 = std::sin (
                juce::MathConstants<double>::twoPi * voice.characterPhase1);
            const auto characterMovement2 = std::sin (
                juce::MathConstants<double>::twoPi * voice.characterPhase2);
            const auto characterCents1 = 24.0 * characterMovement1;
            const auto characterCents2 = 32.0 * characterMovement2;
            const auto characterPitchRatio1 = std::pow (2.0, characterCents1 / 1200.0);
            const auto characterPitchRatio2 = std::pow (2.0, characterCents2 / 1200.0);

            if (harmonixEnabled)
            {
                const auto oscillator1Movement = std::sin (
                    juce::MathConstants<double>::twoPi * voice.harmonixPhase1);
                const auto oscillator2Movement = std::sin (
                    juce::MathConstants<double>::twoPi * voice.harmonixPhase2);
                const auto oscillator1Cents = harmonixOscillator1MeanCents
                                            + harmonixOscillator1DepthCents
                                                * oscillator1Movement;
                const auto oscillator2Cents = harmonixOscillator2MeanCents
                                            + harmonixOscillator2DepthCents
                                                * oscillator2Movement;

                voice.phaseDelta = oscillator1BasePhaseDelta
                    * std::pow (2.0, oscillator1Cents / 1200.0);
                voice.phaseDelta2 = oscillator2BasePhaseDelta
                    * std::pow (2.0, oscillator2Cents / 1200.0);
            }
            else
            {
                voice.phaseDelta = oscillator1BasePhaseDelta;
                voice.phaseDelta2 = oscillator2BasePhaseDelta;
            }

            const auto characterWavePhaseDelta1 = voice.phaseDelta
                                                * characterPitchRatio1;
            const auto characterWavePhaseDelta2 = voice.phaseDelta2
                                                * characterPitchRatio2;

            // Tracking follows the gliding pitch. At full tracking, every 12
            // MIDI-note steps doubles cutoff from the original MIDI-zero base.
            voice.filterTrackingRatio = std::pow (
                2.0,
                voice.currentMidiNote * normalizedFilterTracking / 12.0);

            // Amount moves upward through the same broad normalized cutoff
            // range as the original CUTOFF control. At zero, the Stage 18
            // filter response is unchanged.
            const auto modulatedCutoff = juce::jlimit (
                0.0f,
                1.0f,
                normalizedFilterCutoff
                    + normalizedFilterEnvelopeAmount * voice.filterEnvelope
                    + sequencerFilterOffset);
            const auto cutoffHz = juce::jlimit (
                20.0,
                currentSampleRate * 0.45,
                filterCutoffHzFromNormalized (modulatedCutoff)
                    * voice.filterTrackingRatio);
            voice.filterG = std::tan (
                juce::MathConstants<double>::pi * cutoffHz / currentSampleRate);

            const auto cleanOscillator1Sample = readWaveform (
                oscillator1WaveformValue,
                voice.phase,
                voice.phaseDelta,
                voice.noiseState1);
            const auto cleanOscillator2Sample = readWaveform (
                oscillator2WaveformValue,
                voice.phase2,
                voice.phaseDelta2,
                voice.noiseState2);

            auto characterOscillator1Sample = readWaveform (
                oscillator1WaveformValue,
                characterPhase (voice.characterWavePhase1, 1.0f),
                characterWavePhaseDelta1,
                voice.characterNoiseState1);
            auto characterOscillator2Sample = readWaveform (
                oscillator2WaveformValue,
                characterPhase (voice.characterWavePhase2, 1.0f),
                characterWavePhaseDelta2,
                voice.characterNoiseState2);

            characterOscillator1Sample = applyCharacterGrit (
                characterOscillator1Sample, 1.0f);
            characterOscillator2Sample = applyCharacterGrit (
                characterOscillator2Sample, 1.0f);

            auto oscillator1Sample = cleanOscillator1Sample
                + characterMix
                    * (characterOscillator1Sample - cleanOscillator1Sample);
            auto oscillator2Sample = cleanOscillator2Sample
                + characterMix
                    * (characterOscillator2Sample - cleanOscillator2Sample);

            if (oscillator1CrusherEnabled)
                oscillator1Sample = applyBitCrusher (
                    oscillator1Sample,
                    normalizedBitCrusherAmount,
                    bitCrusherDepth);
            else
            {
                voice.crusherHeldSample1 = oscillator1Sample;
                voice.crusherSamplesUntilRefresh1 = 0;
            }

            if (oscillator2CrusherEnabled)
                oscillator2Sample = applyBitCrusher (
                    oscillator2Sample,
                    normalizedBitCrusherAmount,
                    bitCrusherDepth);
            else
            {
                voice.crusherHeldSample2 = oscillator2Sample;
                voice.crusherSamplesUntilRefresh2 = 0;
            }

            const auto oscillatorSample = oscillator1Sample * oscillator1Gain
                                        + oscillator2Sample * oscillator2Gain;
            const auto filteredSample = processVoiceFilter (voice, oscillatorSample);

            mixedSample += filteredSample * voice.envelope;

            voice.phase += voice.phaseDelta;
            voice.phase -= std::floor (voice.phase);
            voice.phase2 += voice.phaseDelta2;
            voice.phase2 -= std::floor (voice.phase2);
            voice.characterWavePhase1 += characterWavePhaseDelta1;
            voice.characterWavePhase1 -= std::floor (voice.characterWavePhase1);
            voice.characterWavePhase2 += characterWavePhaseDelta2;
            voice.characterWavePhase2 -= std::floor (voice.characterWavePhase2);

            // Keep these phases moving while HARMONIX is off, just as the two
            // oscillator phases continue to run. Switching it on therefore
            // reveals the interaction without resetting or retriggering it.
            voice.harmonixPhase1 += oscillator1BasePhaseDelta / harmonixCycleDivisor;
            voice.harmonixPhase1 -= std::floor (voice.harmonixPhase1);
            voice.harmonixPhase2 += oscillator2BasePhaseDelta / harmonixCycleDivisor;
            voice.harmonixPhase2 -= std::floor (voice.harmonixPhase2);

            // The global Character movement is deliberately independent of
            // HARMONIX. Both sub-phases continue at all settings, allowing the
            // two controls to be combined without resets or mode switching.
            voice.characterPhase1 += oscillator1BasePhaseDelta / 384.0;
            voice.characterPhase1 -= std::floor (voice.characterPhase1);
            voice.characterPhase2 += oscillator2BasePhaseDelta / 320.0;
            voice.characterPhase2 -= std::floor (voice.characterPhase2);

        }

        // In trigger mode the original sequence continues through the exact
        // amplifier release tail. advanceAmpEnvelope clears a voice when it
        // reaches zero, so this stops on that same sample rather than at key-up.
        const auto sequencerShouldAdvance = sequencerIsFreeRunning || std::any_of (
            voices.begin(), voices.end(), [] (const Voice& voice) { return voice.active; });
        if (sequencerShouldAdvance)
        {
            sequencerPhase += sequencerPhaseIncrement;
            if (sequencerPhase >= 16.0)
                sequencerPhase -= 16.0 * std::floor (sequencerPhase / 16.0);
        }

        // The reference gain sweep proves that main volume is linear and is
        // applied after saturation. A 0.76 drive and 1.051 makeup gain match
        // both the measured single-sine curve and the two-oscillator level.
        mixedSample = 1.051f * std::tanh (0.76f * mixedSample)
                    * normalizedVolume;
        left[sample] += mixedSample;
        right[sample] += mixedSample;
    }
}

bool IceCreamAudioProcessor::advanceAmpEnvelope (Voice& voice,
                                                  float attackSeconds,
                                                  float decaySeconds,
                                                  float sustainLevel) const noexcept
{
    const auto sampleRate = static_cast<float> (currentSampleRate);

    if (voice.envelopeStage == EnvelopeStage::attack)
    {
        if (attackSeconds <= 0.0f)
        {
            voice.envelope = 1.0f;
            voice.envelopeStage = EnvelopeStage::decay;
        }
        else
        {
            voice.envelope += 1.0f / (sampleRate * attackSeconds);

            if (voice.envelope >= 1.0f)
            {
                voice.envelope = 1.0f;
                voice.envelopeStage = EnvelopeStage::decay;
            }
        }
    }

    if (voice.envelopeStage == EnvelopeStage::decay)
    {
        if (decaySeconds <= 0.0f || voice.envelope <= sustainLevel)
        {
            voice.envelope = sustainLevel;
            voice.envelopeStage = EnvelopeStage::sustain;
        }
        else
        {
            const auto decayDistance = 1.0f - sustainLevel;
            voice.envelope -= decayDistance / (sampleRate * decaySeconds);

            if (voice.envelope <= sustainLevel)
            {
                voice.envelope = sustainLevel;
                voice.envelopeStage = EnvelopeStage::sustain;
            }
        }
    }

    if (voice.envelopeStage == EnvelopeStage::sustain)
        voice.envelope = sustainLevel;

    if (voice.envelopeStage == EnvelopeStage::release)
    {
        voice.envelope = juce::jmax (0.0f, voice.envelope - voice.releaseStep);

        if (voice.envelope <= 0.0f)
        {
            voice = {};
            return false;
        }
    }

    return true;
}

void IceCreamAudioProcessor::advanceFilterEnvelope (Voice& voice,
                                                      float attackSeconds,
                                                      float decaySeconds,
                                                      float sustainLevel) const noexcept
{
    const auto sampleRate = static_cast<float> (currentSampleRate);

    if (voice.filterEnvelopeStage == EnvelopeStage::attack)
    {
        if (attackSeconds <= 0.0f)
        {
            voice.filterEnvelope = 1.0f;
            voice.filterEnvelopeStage = EnvelopeStage::decay;
        }
        else
        {
            voice.filterEnvelope += 1.0f / (sampleRate * attackSeconds);

            if (voice.filterEnvelope >= 1.0f)
            {
                voice.filterEnvelope = 1.0f;
                voice.filterEnvelopeStage = EnvelopeStage::decay;
            }
        }
    }

    if (voice.filterEnvelopeStage == EnvelopeStage::decay)
    {
        if (decaySeconds <= 0.0f || voice.filterEnvelope <= sustainLevel)
        {
            voice.filterEnvelope = sustainLevel;
            voice.filterEnvelopeStage = EnvelopeStage::sustain;
        }
        else
        {
            const auto decayDistance = 1.0f - sustainLevel;
            voice.filterEnvelope -= decayDistance / (sampleRate * decaySeconds);

            if (voice.filterEnvelope <= sustainLevel)
            {
                voice.filterEnvelope = sustainLevel;
                voice.filterEnvelopeStage = EnvelopeStage::sustain;
            }
        }
    }

    if (voice.filterEnvelopeStage == EnvelopeStage::sustain)
        voice.filterEnvelope = sustainLevel;

    if (voice.filterEnvelopeStage == EnvelopeStage::release)
    {
        voice.filterEnvelope = juce::jmax (
            0.0f,
            voice.filterEnvelope - voice.filterEnvelopeReleaseStep);
    }
}

float IceCreamAudioProcessor::envelopeTimeSeconds (float normalizedValue) noexcept
{
    const auto normalized = juce::jlimit (0.0f, 1.0f, normalizedValue);
    // All Star's release value 0.548654 reaches silence in about 0.165 s,
    // which is x^3 seconds. The former five-second scale made every attack,
    // decay and release five times too long.
    return normalized * normalized * normalized;
}

float IceCreamAudioProcessor::readTable (const std::vector<float>& tables,
                                         double phase,
                                         double phaseDelta) const noexcept
{
    if (tables.size() != expectedTableSamples)
        return 0.0f;

    const auto frequency = phaseDelta * currentSampleRate;
    const auto maximumHarmonic = frequency > 0.0
                                   ? static_cast<int> ((currentSampleRate * 0.5) / frequency)
                                   : 1;
    const auto table = juce::jlimit (1, tableCount - 1, maximumHarmonic);
    const auto tableOffset = static_cast<std::size_t> (table * tableLength);
    const auto tablePosition = phase * static_cast<double> (tableLength);
    const auto index0 = static_cast<int> (tablePosition) & (tableLength - 1);
    const auto index1 = (index0 + 1) & (tableLength - 1);
    const auto fraction = static_cast<float> (tablePosition - std::floor (tablePosition));
    const auto sample0 = tables[tableOffset + static_cast<std::size_t> (index0)];
    const auto sample1 = tables[tableOffset + static_cast<std::size_t> (index1)];

    return sample0 + fraction * (sample1 - sample0);
}

float IceCreamAudioProcessor::readWaveform (float normalizedSelector,
                                            double phase,
                                            double phaseDelta,
                                            std::uint32_t& noiseState) const noexcept
{
    switch (waveformIndexFromNormalized (normalizedSelector))
    {
        case 0:
            return static_cast<float> (std::sin (phase * juce::MathConstants<double>::twoPi));

        case 1:
            return readTable (sawTables, phase, phaseDelta);

        case 2:
            return readTable (triangleTables, phase, phaseDelta);

        case 3:
        {
            const auto shiftedPhase = phase + 0.5 - std::floor (phase + 0.5);
            return readTable (sawTables, phase, phaseDelta)
                 - readTable (sawTables, shiftedPhase, phaseDelta);
        }

        case 4:
            // The original noise position is about 1.17 dB hotter than a
            // unit-range uniform source after the shared output saturation.
            // This calibration brings its measured RMS in line with the
            // original while preserving the same broadband distribution.
            return 1.2f * nextNoiseSample (noiseState);

        default:
            break;
    }

    return 0.0f;
}

float IceCreamAudioProcessor::nextNoiseSample (std::uint32_t& state) noexcept
{
    if (state == 0u)
        state = 0x6d2b79f5u;

    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;

    const auto normalized = static_cast<float> (state & 0x00ffffffu)
                          / static_cast<float> (0x007fffffu);
    return normalized - 1.0f;
}

float IceCreamAudioProcessor::processVoiceFilter (Voice& voice, float input) const noexcept
{
    // Topology-preserving state-variable filter. Its simultaneous outputs map
    // directly to the five modes exposed by the original plug-in.
    const auto a1 = 1.0 / (1.0 + voice.filterG * (voice.filterG + voice.filterK));
    const auto a2 = voice.filterG * a1;
    const auto a3 = voice.filterG * a2;
    const auto v3 = static_cast<double> (input) - voice.filterState2;
    const auto band = a1 * voice.filterState1 + a2 * v3;
    const auto low = voice.filterState2 + a2 * voice.filterState1 + a3 * v3;

    voice.filterState1 = 2.0 * band - voice.filterState1;
    voice.filterState2 = 2.0 * low - voice.filterState2;

    if (! std::isfinite (voice.filterState1) || ! std::isfinite (voice.filterState2))
    {
        voice.filterState1 = 0.0;
        voice.filterState2 = 0.0;
        return 0.0f;
    }

    const auto high = static_cast<double> (input) - voice.filterK * band - low;

    switch (voice.filterType)
    {
        case 0: return static_cast<float> (low);
        case 1: return static_cast<float> (high);
        case 2: return static_cast<float> (band);
        case 3: return static_cast<float> (low + high);
        case 4: return static_cast<float> (low - high);
        default: break;
    }

    return input;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IceCreamAudioProcessor();
}
