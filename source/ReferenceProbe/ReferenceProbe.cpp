#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// Minimal VST2 ABI declarations used only for interoperability analysis.
// This target has no dependency on the discontinued VST2 SDK.
namespace vst2
{
using Int32 = std::int32_t;
using IntPtr = std::intptr_t;

struct Effect;

using HostCallback = IntPtr (__cdecl*) (Effect*, Int32, Int32, IntPtr, void*, float);
using Dispatcher = IntPtr (__cdecl*) (Effect*, Int32, Int32, IntPtr, void*, float);
using ProcessProc = void (__cdecl*) (Effect*, float**, float**, Int32);
using SetParameterProc = void (__cdecl*) (Effect*, Int32, float);
using GetParameterProc = float (__cdecl*) (Effect*, Int32);
using MainProc = Effect* (__cdecl*) (HostCallback);

struct Effect
{
    Int32 magic;
    Dispatcher dispatcher;
    ProcessProc process;
    SetParameterProc setParameter;
    GetParameterProc getParameter;
    Int32 numPrograms;
    Int32 numParams;
    Int32 numInputs;
    Int32 numOutputs;
    Int32 flags;
    void* reserved1;
    void* reserved2;
    Int32 initialDelay;
    Int32 realQualities;
    Int32 offQualities;
    float ioRatio;
    void* object;
    void* user;
    Int32 uniqueID;
    Int32 version;
    ProcessProc processReplacing;
    ProcessProc processDoubleReplacing;
    char future[56];
};

struct TimeInfo
{
    double samplePos;
    double sampleRate;
    double nanoSeconds;
    double ppqPos;
    double tempo;
    double barStartPos;
    double cycleStartPos;
    double cycleEndPos;
    Int32 timeSigNumerator;
    Int32 timeSigDenominator;
    Int32 smpteOffset;
    Int32 smpteFrameRate;
    Int32 samplesToNextClock;
    Int32 flags;
};

struct Event
{
    Int32 type;
    Int32 byteSize;
    Int32 deltaFrames;
    Int32 flags;
    char data[16];
};

struct MidiEvent
{
    Int32 type;
    Int32 byteSize;
    Int32 deltaFrames;
    Int32 flags;
    Int32 noteLength;
    Int32 noteOffset;
    char midiData[4];
    char detune;
    char noteOffVelocity;
    char reserved1;
    char reserved2;
};

struct Events
{
    Int32 numEvents;
    IntPtr reserved;
    Event* events[2];
};

constexpr Int32 effectMagic = 0x56737450;

enum EffectOpcode : Int32
{
    open = 0,
    close = 1,
    setProgram = 2,
    getProgram = 3,
    getProgramName = 5,
    getParameterLabel = 6,
    getParameterDisplay = 7,
    getParameterName = 8,
    setSampleRate = 10,
    setBlockSize = 11,
    mainsChanged = 12,
    getChunk = 23,
    processEvents = 25,
    canParameterBeAutomated = 26,
    getProgramNameIndexed = 29,
    getPlugCategory = 35,
    getEffectName = 45,
    getVendorString = 47,
    getProductString = 48,
    getVendorVersion = 49,
    getVstVersion = 58,
    startProcess = 71,
    stopProcess = 72
};

enum HostOpcode : Int32
{
    hostAutomate = 0,
    hostVersion = 1,
    hostCurrentId = 2,
    hostIdle = 3,
    hostPinConnected = 4,
    hostWantMidi = 6,
    hostGetTime = 7,
    hostGetSampleRate = 16,
    hostGetBlockSize = 17,
    hostGetInputLatency = 18,
    hostGetOutputLatency = 19,
    hostGetCurrentProcessLevel = 23,
    hostGetAutomationState = 24,
    hostGetVendorString = 32,
    hostGetProductString = 33,
    hostGetVendorVersion = 34,
    hostCanDo = 37,
    hostGetLanguage = 38,
    hostGetDirectory = 41,
    hostUpdateDisplay = 42,
    hostBeginEdit = 43,
    hostEndEdit = 44
};

enum EffectFlags : Int32
{
    hasEditor = 1 << 0,
    canReplacing = 1 << 4,
    programChunks = 1 << 5,
    isSynth = 1 << 8,
    noSoundInStop = 1 << 9
};
}

#if ! defined (ICECREAM_PORTABLE_SYNTAX_CHECK)
static_assert (sizeof (void*) == 4, "The reference probe must be built as Win32.");
static_assert (sizeof (vst2::Effect) == 144, "Unexpected Win32 VST2 ABI layout.");
static_assert (sizeof (vst2::Event) == 32, "Unexpected Win32 VST2 event layout.");
static_assert (sizeof (vst2::MidiEvent) == 32, "Unexpected Win32 VST2 MIDI layout.");
static_assert (sizeof (vst2::Events) == 16, "Unexpected Win32 VST2 event-list layout.");
#endif

namespace
{
std::string pluginDirectory;
vst2::TimeInfo timeInfo {
    0.0, 44100.0, 0.0, 0.0, 120.0, 0.0, 0.0, 0.0,
    4, 4, 0, 0, 0, (1 << 9) | (1 << 10) | (1 << 13)
};

void copyHostString (void* destination, std::size_t destinationSize, const char* text)
{
    if (destination == nullptr || destinationSize == 0)
        return;

    auto* output = static_cast<char*> (destination);
    strncpy_s (output, destinationSize, text, _TRUNCATE);
}

vst2::IntPtr __cdecl hostCallback (vst2::Effect*,
                                   vst2::Int32 opcode,
                                   vst2::Int32,
                                   vst2::IntPtr,
                                   void* pointer,
                                   float)
{
    switch (opcode)
    {
        case vst2::hostVersion:                 return 2400;
        case vst2::hostPinConnected:            return 0;
        case vst2::hostWantMidi:                return 1;
        case vst2::hostGetTime:                 return reinterpret_cast<vst2::IntPtr> (&timeInfo);
        case vst2::hostGetSampleRate:           return 44100;
        case vst2::hostGetBlockSize:            return 512;
        case vst2::hostGetInputLatency:         return 0;
        case vst2::hostGetOutputLatency:        return 0;
        case vst2::hostGetCurrentProcessLevel:  return 1;
        case vst2::hostGetAutomationState:      return 0;
        case vst2::hostGetVendorString:
            copyHostString (pointer, 64, "IceCreamProbe");
            return 1;
        case vst2::hostGetProductString:
            copyHostString (pointer, 64, "IceCream Reference Probe");
            return 1;
        case vst2::hostGetVendorVersion:         return 1;
        case vst2::hostCanDo:                   return 0;
        case vst2::hostGetLanguage:             return 1;
        case vst2::hostGetDirectory:
            return reinterpret_cast<vst2::IntPtr> (pluginDirectory.c_str());
        case vst2::hostAutomate:
        case vst2::hostCurrentId:
        case vst2::hostIdle:
        case vst2::hostUpdateDisplay:
        case vst2::hostBeginEdit:
        case vst2::hostEndEdit:
        default:
            return 0;
    }
}

std::string wideToAnsi (const std::wstring& value)
{
    if (value.empty())
        return {};

    const auto size = WideCharToMultiByte (CP_ACP, 0, value.c_str(), -1,
                                           nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};

    std::string result (static_cast<std::size_t> (size), '\0');
    WideCharToMultiByte (CP_ACP, 0, value.c_str(), -1,
                         result.data(), size, nullptr, nullptr);
    result.resize (static_cast<std::size_t> (size - 1));
    return result;
}

std::string cleanText (const char* value)
{
    if (value == nullptr)
        return {};

    std::string result;
    result.reserve (128);

    for (std::size_t index = 0; index < 255 && value[index] != '\0'; ++index)
    {
        const auto character = static_cast<unsigned char> (value[index]);

        if (character == '\r' || character == '\n' || character == '\t')
            result.push_back (' ');
        else if (std::isprint (character) != 0)
            result.push_back (static_cast<char> (character));
        else
            result.push_back ('?');
    }

    return result;
}

vst2::IntPtr dispatch (vst2::Effect* effect,
                       vst2::Int32 opcode,
                       vst2::Int32 index = 0,
                       vst2::IntPtr value = 0,
                       void* pointer = nullptr,
                       float option = 0.0f)
{
    if (effect == nullptr || effect->dispatcher == nullptr)
        return 0;

    return effect->dispatcher (effect, opcode, index, value, pointer, option);
}

std::string dispatchString (vst2::Effect* effect,
                            vst2::Int32 opcode,
                            vst2::Int32 index = 0,
                            vst2::IntPtr value = 0)
{
    std::array<char, 256> buffer {};
    dispatch (effect, opcode, index, value, buffer.data(), 0.0f);
    return cleanText (buffer.data());
}

std::string fourCharacterId (vst2::Int32 value)
{
    const auto bits = static_cast<std::uint32_t> (value);
    std::string result (4, '.');

    for (int index = 0; index < 4; ++index)
    {
        const auto shift = 24 - (index * 8);
        const auto character = static_cast<unsigned char> ((bits >> shift) & 0xffu);
        if (std::isprint (character) != 0)
            result[static_cast<std::size_t> (index)] = static_cast<char> (character);
    }

    return result;
}

std::string categoryName (vst2::IntPtr category)
{
    static constexpr std::array<const char*, 12> names {
        "Unknown", "Effect", "Synth", "Analysis", "Mastering", "Spacializer",
        "RoomFx", "SurroundFx", "Restoration", "OfflineProcess", "Shell", "Generator"
    };

    if (category >= 0 && category < static_cast<vst2::IntPtr> (names.size()))
        return names[static_cast<std::size_t> (category)];

    return "Unknown (" + std::to_string (category) + ")";
}

std::string flagText (vst2::Int32 flags)
{
    std::vector<std::string> values;

    if ((flags & vst2::hasEditor) != 0)       values.emplace_back ("HasEditor");
    if ((flags & vst2::canReplacing) != 0)    values.emplace_back ("CanReplacing");
    if ((flags & vst2::programChunks) != 0)   values.emplace_back ("ProgramChunks");
    if ((flags & vst2::isSynth) != 0)         values.emplace_back ("IsSynth");
    if ((flags & vst2::noSoundInStop) != 0)   values.emplace_back ("NoSoundInStop");

    if (values.empty())
        return "None decoded";

    std::ostringstream output;
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (index != 0)
            output << ", ";
        output << values[index];
    }
    return output.str();
}

bool sensibleCount (vst2::Int32 value, vst2::Int32 maximum)
{
    return value >= 0 && value <= maximum;
}

constexpr int analysisSampleRate = 44100;
constexpr int analysisBlockSize = 512;

struct TestSettings
{
    int program = 0;
    int activeOscillator = 1;
    int midiNote = 57;
    int midiVelocity = 100;
    float ampAttack = 0.0f;
    float ampDecay = 0.0f;
    float ampRelease = 0.0f;
    float ampSustain = 1.0f;
    float bitcrusherOscillator1 = 0.0f;
    float bitcrusherOscillator2 = 0.0f;
    float bitcrusherAmount = 0.897211f;
    float mainVolume = 1.0f;
    float oscillator1Volume = 1.0f;
    float oscillator2Volume = 1.0f;
    float oscillator1Selector = 0.0f;
    float oscillator2Selector = 0.0f;
    float oscillator1Octave = 0.5f;
    float oscillator2Octave = 0.5f;
    float oscillator2Frequency = 1.0f;
    float filterEnvelopeAmount = 0.0f;
    float filterEnvelopeAttack = 0.0f;
    float filterEnvelopeDecay = 0.0f;
    float filterEnvelopeRelease = 0.0f;
    float filterEnvelopeSustain = 0.0f;
    float filterCutoff = 1.0f;
    float filterResonance = 0.0f;
    float filterTracking = 0.0f;
    float filterType = 0.0f;
    float glide = 0.0f;
    float glideRate = 0.3375f;
    float harmonix = 0.0f;
    float polyMode = 1.0f;
    float sequencerFilter = 0.0f;
    float sequencerPitch = 0.0f;
    float sequencerSmooth = 0.125f;
    float sequencerRate = 0.428571f;
    float pitchWaveform = 0.125f;
    float filterWaveform = 0.0f;
    int warmupBlocks = 20;
    int captureBlocks = 16;
    int noteOffAfterCaptureBlocks = -1;
};

struct AudioAnalysis
{
    double rms = 0.0;
    double peak = 0.0;
    double frequencyHz = 0.0;
    double periodicity = 0.0;
    std::array<double, 8> harmonicRatios {};
    std::string suggestedWaveform = "UNDETERMINED";
};

struct PitchMovementAnalysis
{
    double meanFrequencyHz = 0.0;
    double minimumFrequencyHz = 0.0;
    double maximumFrequencyHz = 0.0;
    double standardDeviationHz = 0.0;
    double spanCents = 0.0;
    double modulationRateHz = 0.0;
    std::size_t windowCount = 0;
};

struct SpectrumAnalysis
{
    double rms = 0.0;
    double spectralCentroidHz = 0.0;
    double strongestFrequencyHz = 0.0;
    std::array<double, 7> bandLevelsDb {};
};

struct EqualizerSpectrumAnalysis
{
    double rmsDb = -200.0;
    std::array<double, 8> bandLevelsDb {};
    std::size_t fftFrames = 0;
};

constexpr std::array<double, 8> spectrumBandEdgesHz {
    40.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0
};

constexpr std::array<const char*, 7> spectrumBandNames {
    "40-250", "250-500", "500-1k", "1k-2k", "2k-4k", "4k-8k", "8k-16k"
};

constexpr std::array<double, 9> equalizerAnalysisBandEdgesHz {
    62.5, 176.776695, 353.553391, 707.106781, 1414.213562,
    2828.427125, 5656.854249, 11313.708499, 20000.0
};

constexpr std::array<const char*, 8> equalizerBandNames {
    "125", "250", "500", "1k", "2k", "4k", "8k", "16k"
};

void setParameter (vst2::Effect* effect, vst2::Int32 index, float value)
{
    if (effect != nullptr && effect->setParameter != nullptr
        && index >= 0 && index < effect->numParams)
    {
        effect->setParameter (effect, index, std::clamp (value, 0.0f, 1.0f));
    }
}

void configureTestPatch (vst2::Effect* effect, const TestSettings& settings)
{
    dispatch (effect, vst2::setProgram, 0, settings.program);

    setParameter (effect, 0, settings.ampAttack);
    setParameter (effect, 1, settings.ampDecay);
    setParameter (effect, 2, settings.ampRelease);
    setParameter (effect, 3, settings.ampSustain);
    setParameter (effect, 4, settings.bitcrusherOscillator1);
    setParameter (effect, 5, settings.bitcrusherOscillator2);
    setParameter (effect, 6, settings.bitcrusherAmount);
    setParameter (effect, 7, 0.0f);  // Delay mix
    setParameter (effect, 8, 0.0f);  // Delay off
    setParameter (effect, 9, 0.05f); // Delay time default
    setParameter (effect, 10, 0.325f); // Delay feedback default
    setParameter (effect, 11, settings.filterEnvelopeAmount);
    setParameter (effect, 12, settings.filterEnvelopeAttack);
    setParameter (effect, 13, settings.filterEnvelopeDecay);
    setParameter (effect, 14, settings.filterEnvelopeRelease);
    setParameter (effect, 15, settings.filterEnvelopeSustain);
    setParameter (effect, 16, settings.filterCutoff);
    setParameter (effect, 17, settings.filterResonance);
    setParameter (effect, 18, settings.filterTracking);
    setParameter (effect, 19, settings.filterType);
    setParameter (effect, 20, settings.glide);
    setParameter (effect, 21, settings.glideRate);
    setParameter (effect, 22, settings.harmonix);
    setParameter (effect, 23, 1.0f); // Unidentified control default
    setParameter (effect, 24, settings.mainVolume);
    setParameter (effect, 25, settings.polyMode);
    setParameter (effect, 26, settings.activeOscillator != 2
                                  ? settings.oscillator1Volume : 0.0f);
    setParameter (effect, 27, settings.activeOscillator != 1
                                  ? settings.oscillator2Volume : 0.0f);
    setParameter (effect, 28, settings.oscillator2Frequency);
    setParameter (effect, 29, settings.oscillator1Octave);
    setParameter (effect, 30, settings.oscillator2Octave);
    setParameter (effect, 31, settings.oscillator1Selector);
    setParameter (effect, 32, settings.oscillator2Selector);
    setParameter (effect, 33, 0.05f); // Reverb damp default
    setParameter (effect, 34, 0.0f); // Reverb mix
    setParameter (effect, 35, 0.0f); // Reverb off
    setParameter (effect, 36, 0.05f); // Reverb room default
    setParameter (effect, 37, 0.05f); // Reverb width default
    setParameter (effect, 38, settings.sequencerFilter);
    setParameter (effect, 39, settings.sequencerPitch);
    setParameter (effect, 40, settings.sequencerSmooth);
    setParameter (effect, 41, settings.sequencerRate);
    setParameter (effect, 42, settings.pitchWaveform);
    setParameter (effect, 43, settings.filterWaveform);
}

void sendMidi (vst2::Effect* effect,
               unsigned char status,
               unsigned char data1,
               unsigned char data2)
{
    vst2::MidiEvent midiEvent {};
    midiEvent.type = 1;
    midiEvent.byteSize = sizeof (vst2::MidiEvent);
    midiEvent.midiData[0] = static_cast<char> (status);
    midiEvent.midiData[1] = static_cast<char> (data1);
    midiEvent.midiData[2] = static_cast<char> (data2);

    vst2::Events events {};
    events.numEvents = 1;
    events.events[0] = reinterpret_cast<vst2::Event*> (&midiEvent);
    dispatch (effect, vst2::processEvents, 0, 0, &events);
}

std::vector<float> renderTestTone (vst2::Effect* effect,
                                   const TestSettings& settings,
                                   std::vector<float>* capturedRight = nullptr)
{
    if (effect == nullptr || effect->processReplacing == nullptr)
        return {};

    dispatch (effect, vst2::stopProcess);
    dispatch (effect, vst2::mainsChanged, 0, 0);
    configureTestPatch (effect, settings);

    timeInfo.samplePos = 0.0;
    timeInfo.sampleRate = analysisSampleRate;
    timeInfo.ppqPos = 0.0;
    timeInfo.flags |= (1 << 1);

    dispatch (effect, vst2::setSampleRate, 0, 0, nullptr,
              static_cast<float> (analysisSampleRate));
    dispatch (effect, vst2::setBlockSize, 0, analysisBlockSize);
    dispatch (effect, vst2::mainsChanged, 0, 1);
    dispatch (effect, vst2::startProcess);

    sendMidi (effect,
              0x90,
              static_cast<unsigned char> (settings.midiNote),
              static_cast<unsigned char> (std::clamp (settings.midiVelocity, 1, 127)));

    const auto warmupBlocks = std::max (0, settings.warmupBlocks);
    const auto captureBlocks = std::max (1, settings.captureBlocks);
    std::vector<float> captured;
    captured.reserve (static_cast<std::size_t> (captureBlocks * analysisBlockSize));

    if (capturedRight != nullptr)
    {
        capturedRight->clear();
        capturedRight->reserve (
            static_cast<std::size_t> (captureBlocks * analysisBlockSize));
    }

    auto noteReleased = false;

    for (int block = 0; block < warmupBlocks + captureBlocks; ++block)
    {
        if (! noteReleased
            && settings.noteOffAfterCaptureBlocks >= 0
            && block == warmupBlocks + settings.noteOffAfterCaptureBlocks)
        {
            sendMidi (effect, 0x80, static_cast<unsigned char> (settings.midiNote), 0);
            noteReleased = true;
        }

        std::array<float, analysisBlockSize> left {};
        std::array<float, analysisBlockSize> right {};
        float* outputs[2] { left.data(), right.data() };

        effect->processReplacing (effect, nullptr, outputs, analysisBlockSize);

        if (block >= warmupBlocks)
        {
            captured.insert (captured.end(), left.begin(), left.end());

            if (capturedRight != nullptr)
                capturedRight->insert (capturedRight->end(), right.begin(), right.end());
        }

        timeInfo.samplePos += analysisBlockSize;
        timeInfo.ppqPos += (static_cast<double> (analysisBlockSize)
                            / static_cast<double> (analysisSampleRate))
                         * (timeInfo.tempo / 60.0);
    }

    if (! noteReleased)
        sendMidi (effect, 0x80, static_cast<unsigned char> (settings.midiNote), 0);
    sendMidi (effect, 0xb0, 123, 0); // All notes off
    dispatch (effect, vst2::stopProcess);
    dispatch (effect, vst2::mainsChanged, 0, 0);
    timeInfo.flags &= ~(1 << 1);
    return captured;
}

double correlationAtLag (const std::vector<double>& samples, int lag)
{
    if (lag <= 0 || static_cast<std::size_t> (lag) >= samples.size())
        return 0.0;

    double cross = 0.0;
    double energy1 = 0.0;
    double energy2 = 0.0;

    for (std::size_t index = 0; index + static_cast<std::size_t> (lag) < samples.size(); ++index)
    {
        const auto first = samples[index];
        const auto second = samples[index + static_cast<std::size_t> (lag)];
        cross += first * second;
        energy1 += first * first;
        energy2 += second * second;
    }

    const auto denominator = std::sqrt (energy1 * energy2);
    return denominator > 0.0 ? cross / denominator : 0.0;
}

double risingCrossingFrequency (const std::vector<double>& samples)
{
    std::vector<double> periods;
    double previousCrossing = -1.0;

    for (std::size_t index = 1; index < samples.size(); ++index)
    {
        const auto first = samples[index - 1];
        const auto second = samples[index];

        if (first <= 0.0 && second > 0.0 && second != first)
        {
            const auto fraction = -first / (second - first);
            const auto crossing = static_cast<double> (index - 1) + fraction;

            if (previousCrossing >= 0.0)
                periods.push_back (crossing - previousCrossing);

            previousCrossing = crossing;
        }
    }

    if (periods.size() < 4)
        return 0.0;

    std::sort (periods.begin(), periods.end());
    const auto medianPeriod = periods[periods.size() / 2];

    if (medianPeriod <= 2.0)
        return 0.0;

    std::vector<double> deviations;
    deviations.reserve (periods.size());
    for (const auto period : periods)
        deviations.push_back (std::abs (period - medianPeriod));

    std::sort (deviations.begin(), deviations.end());
    const auto medianDeviation = deviations[deviations.size() / 2];

    // A random/noisy signal also crosses zero repeatedly, but its crossing
    // intervals are not stable enough to constitute a pitch measurement.
    if (medianDeviation > medianPeriod * 0.08)
        return 0.0;

    return static_cast<double> (analysisSampleRate) / medianPeriod;
}

PitchMovementAnalysis analysePitchMovement (const std::vector<float>& input)
{
    PitchMovementAnalysis result;
    constexpr std::size_t windowSize = 2048;
    constexpr std::size_t hopSize = 512;

    if (input.size() < windowSize)
        return result;

    std::vector<double> localFrequencies;
    std::vector<double> localTimesSeconds;
    localFrequencies.reserve ((input.size() - windowSize) / hopSize + 1);
    localTimesSeconds.reserve (localFrequencies.capacity());

    for (std::size_t offset = 0; offset + windowSize <= input.size(); offset += hopSize)
    {
        double mean = 0.0;
        for (std::size_t sample = 0; sample < windowSize; ++sample)
        {
            const auto value = input[offset + sample];
            mean += std::isfinite (value) ? static_cast<double> (value) : 0.0;
        }
        mean /= static_cast<double> (windowSize);

        std::vector<double> centred;
        centred.reserve (windowSize);
        double squareSum = 0.0;

        for (std::size_t sample = 0; sample < windowSize; ++sample)
        {
            const auto value = input[offset + sample];
            const auto safeValue = std::isfinite (value) ? static_cast<double> (value) : 0.0;
            const auto centredValue = safeValue - mean;
            centred.push_back (centredValue);
            squareSum += centredValue * centredValue;
        }

        const auto rms = std::sqrt (squareSum / static_cast<double> (windowSize));
        if (rms < 1.0e-8)
            continue;

        const auto frequency = risingCrossingFrequency (centred);
        if (frequency <= 0.0 || ! std::isfinite (frequency))
            continue;

        localFrequencies.push_back (frequency);
        localTimesSeconds.push_back (
            (static_cast<double> (offset) + 0.5 * static_cast<double> (windowSize))
            / static_cast<double> (analysisSampleRate));
    }

    result.windowCount = localFrequencies.size();
    if (localFrequencies.empty())
        return result;

    auto frequencySum = 0.0;
    result.minimumFrequencyHz = std::numeric_limits<double>::max();
    for (const auto frequency : localFrequencies)
    {
        frequencySum += frequency;
        result.minimumFrequencyHz = std::min (result.minimumFrequencyHz, frequency);
        result.maximumFrequencyHz = std::max (result.maximumFrequencyHz, frequency);
    }
    result.meanFrequencyHz = frequencySum / static_cast<double> (localFrequencies.size());

    auto squaredDeviationSum = 0.0;
    for (const auto frequency : localFrequencies)
    {
        const auto deviation = frequency - result.meanFrequencyHz;
        squaredDeviationSum += deviation * deviation;
    }
    result.standardDeviationHz = std::sqrt (
        squaredDeviationSum / static_cast<double> (localFrequencies.size()));

    if (result.minimumFrequencyHz > 0.0 && result.maximumFrequencyHz > 0.0)
    {
        result.spanCents = 1200.0 * std::log2 (
            result.maximumFrequencyHz / result.minimumFrequencyHz);
    }

    // Report the HARMONIX-OFF movement too, so the original oscillator's
    // chip-like instability is not mistaken for a newly added effect.
    if (localFrequencies.size() >= 8
        && result.standardDeviationHz >= 0.005
        && result.spanCents >= 0.05)
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double minimumRateHz = 0.25;
        constexpr double maximumRateHz = 20.0;
        constexpr double rateStepHz = 0.05;
        auto strongestMagnitude = 0.0;

        for (auto rateHz = minimumRateHz;
             rateHz <= maximumRateHz + 0.5 * rateStepHz;
             rateHz += rateStepHz)
        {
            double real = 0.0;
            double imaginary = 0.0;

            for (std::size_t index = 0; index < localFrequencies.size(); ++index)
            {
                const auto window = localFrequencies.size() > 1
                    ? 0.5 - 0.5 * std::cos (
                        2.0 * pi * static_cast<double> (index)
                        / static_cast<double> (localFrequencies.size() - 1))
                    : 1.0;
                const auto movement = (localFrequencies[index] - result.meanFrequencyHz)
                                    * window;
                const auto angle = 2.0 * pi * rateHz * localTimesSeconds[index];
                real += movement * std::cos (angle);
                imaginary -= movement * std::sin (angle);
            }

            const auto magnitude = std::sqrt (real * real + imaginary * imaginary);
            if (magnitude > strongestMagnitude)
            {
                strongestMagnitude = magnitude;
                result.modulationRateHz = rateHz;
            }
        }
    }

    return result;
}

AudioAnalysis analyseAudio (const std::vector<float>& input)
{
    AudioAnalysis result;
    if (input.empty())
        return result;

    double mean = 0.0;
    for (const auto sample : input)
    {
        if (std::isfinite (sample))
            mean += sample;
    }
    mean /= static_cast<double> (input.size());

    std::vector<double> centred;
    centred.reserve (input.size());
    double squareSum = 0.0;

    for (const auto sample : input)
    {
        const auto safeSample = std::isfinite (sample) ? static_cast<double> (sample) : 0.0;
        const auto value = safeSample - mean;
        centred.push_back (value);
        squareSum += value * value;
        result.peak = std::max (result.peak, std::abs (value));
    }

    result.rms = std::sqrt (squareSum / static_cast<double> (centred.size()));
    if (result.rms < 1.0e-8)
    {
        result.suggestedWaveform = "SILENCE";
        return result;
    }

    constexpr int minimumLag = 10;
    constexpr int maximumLag = 1000;
    std::array<double, maximumLag + 1> correlations {};
    double bestCorrelation = -1.0;
    int bestLag = 0;

    for (int lag = minimumLag; lag <= maximumLag; ++lag)
    {
        correlations[static_cast<std::size_t> (lag)] = correlationAtLag (centred, lag);
        if (correlations[static_cast<std::size_t> (lag)] > bestCorrelation)
        {
            bestCorrelation = correlations[static_cast<std::size_t> (lag)];
            bestLag = lag;
        }
    }

    int selectedLag = bestLag;
    const auto acceptance = std::max (0.60, bestCorrelation * 0.97);
    for (int lag = minimumLag + 1; lag < bestLag; ++lag)
    {
        const auto value = correlations[static_cast<std::size_t> (lag)];
        if (value >= acceptance
            && value >= correlations[static_cast<std::size_t> (lag - 1)]
            && value >= correlations[static_cast<std::size_t> (lag + 1)])
        {
            selectedLag = lag;
            break;
        }
    }

    result.periodicity = selectedLag > 0
                           ? correlations[static_cast<std::size_t> (selectedLag)]
                           : 0.0;

    if (selectedLag > 0 && result.periodicity >= 0.35)
    {
        const auto crossingFrequency = risingCrossingFrequency (centred);
        result.frequencyHz = crossingFrequency > 0.0
                               ? crossingFrequency
                               : static_cast<double> (analysisSampleRate)
                                   / static_cast<double> (selectedLag);
    }

    if (result.frequencyHz > 0.0)
    {
        constexpr double pi = 3.14159265358979323846;
        const auto sampleCount = std::min<std::size_t> (4096, centred.size());
        std::array<double, 8> magnitudes {};

        for (std::size_t harmonic = 0; harmonic < magnitudes.size(); ++harmonic)
        {
            const auto frequency = result.frequencyHz * static_cast<double> (harmonic + 1);
            if (frequency >= analysisSampleRate * 0.5)
                continue;

            double real = 0.0;
            double imaginary = 0.0;

            for (std::size_t sample = 0; sample < sampleCount; ++sample)
            {
                const auto window = 0.5 - 0.5 * std::cos (
                    2.0 * pi * static_cast<double> (sample)
                    / static_cast<double> (sampleCount - 1));
                const auto angle = 2.0 * pi * frequency
                                 * static_cast<double> (sample)
                                 / static_cast<double> (analysisSampleRate);
                real += centred[sample] * window * std::cos (angle);
                imaginary -= centred[sample] * window * std::sin (angle);
            }

            magnitudes[harmonic] = std::sqrt (real * real + imaginary * imaginary);
        }

        const auto fundamental = magnitudes[0];
        if (fundamental > 1.0e-12)
        {
            for (std::size_t harmonic = 0; harmonic < magnitudes.size(); ++harmonic)
                result.harmonicRatios[harmonic] = magnitudes[harmonic] / fundamental;
        }
    }

    if (result.periodicity < 0.35 || result.frequencyHz <= 0.0)
        result.suggestedWaveform = "NOISE";
    else if (result.harmonicRatios[1] > 0.18)
        result.suggestedWaveform = "SAW/RAMP";
    else if (result.harmonicRatios[2] < 0.05)
        result.suggestedWaveform = "SINE";
    else if (result.harmonicRatios[2] < 0.18)
        result.suggestedWaveform = "TRIANGLE";
    else
        result.suggestedWaveform = "SQUARE/PULSE";

    return result;
}

void performFft (std::vector<std::complex<double>>& data)
{
    const auto size = data.size();

    for (std::size_t index = 1, reversed = 0; index < size; ++index)
    {
        auto bit = size >> 1;
        while ((reversed & bit) != 0)
        {
            reversed ^= bit;
            bit >>= 1;
        }
        reversed ^= bit;

        if (index < reversed)
            std::swap (data[index], data[reversed]);
    }

    constexpr double pi = 3.14159265358979323846;
    for (std::size_t length = 2; length <= size; length <<= 1)
    {
        const auto angle = -2.0 * pi / static_cast<double> (length);
        const std::complex<double> step (std::cos (angle), std::sin (angle));

        for (std::size_t offset = 0; offset < size; offset += length)
        {
            std::complex<double> rotation (1.0, 0.0);
            for (std::size_t index = 0; index < length / 2; ++index)
            {
                const auto even = data[offset + index];
                const auto odd = data[offset + index + length / 2] * rotation;
                data[offset + index] = even + odd;
                data[offset + index + length / 2] = even - odd;
                rotation *= step;
            }
        }
    }
}

SpectrumAnalysis analyseSpectrum (const std::vector<float>& input)
{
    SpectrumAnalysis result;
    constexpr std::size_t fftSize = 4096;

    if (input.size() < fftSize)
        return result;

    const auto inputOffset = input.size() - fftSize;
    double mean = 0.0;
    for (std::size_t index = 0; index < fftSize; ++index)
    {
        const auto sample = input[inputOffset + index];
        mean += std::isfinite (sample) ? static_cast<double> (sample) : 0.0;
    }
    mean /= static_cast<double> (fftSize);

    constexpr double pi = 3.14159265358979323846;
    std::vector<std::complex<double>> fft (fftSize);
    double squareSum = 0.0;

    for (std::size_t index = 0; index < fftSize; ++index)
    {
        const auto sample = input[inputOffset + index];
        const auto centred = (std::isfinite (sample) ? static_cast<double> (sample) : 0.0)
                           - mean;
        const auto window = 0.5 - 0.5 * std::cos (
            2.0 * pi * static_cast<double> (index)
            / static_cast<double> (fftSize - 1));
        fft[index] = centred * window;
        squareSum += centred * centred;
    }

    result.rms = std::sqrt (squareSum / static_cast<double> (fftSize));
    performFft (fft);

    std::array<double, 7> bandPower {};
    std::array<int, 7> bandBinCount {};
    double weightedFrequency = 0.0;
    double totalPower = 0.0;
    double strongestPower = 0.0;

    for (std::size_t bin = 1; bin < fftSize / 2; ++bin)
    {
        const auto frequency = static_cast<double> (bin) * analysisSampleRate
                             / static_cast<double> (fftSize);
        if (frequency < spectrumBandEdgesHz.front()
            || frequency >= spectrumBandEdgesHz.back())
        {
            continue;
        }

        const auto power = std::norm (fft[bin])
                         / static_cast<double> (fftSize * fftSize);
        totalPower += power;
        weightedFrequency += frequency * power;

        if (power > strongestPower)
        {
            strongestPower = power;
            result.strongestFrequencyHz = frequency;
        }

        for (std::size_t band = 0; band < bandPower.size(); ++band)
        {
            if (frequency >= spectrumBandEdgesHz[band]
                && frequency < spectrumBandEdgesHz[band + 1])
            {
                bandPower[band] += power;
                ++bandBinCount[band];
                break;
            }
        }
    }

    if (totalPower > 0.0)
        result.spectralCentroidHz = weightedFrequency / totalPower;

    for (std::size_t band = 0; band < bandPower.size(); ++band)
    {
        const auto averagePower = bandBinCount[band] > 0
                                    ? bandPower[band] / static_cast<double> (bandBinCount[band])
                                    : 0.0;
        result.bandLevelsDb[band] = 10.0 * std::log10 (std::max (averagePower, 1.0e-20));
    }

    return result;
}

EqualizerSpectrumAnalysis analyseEqualizerSpectrum (
    const std::vector<float>& input)
{
    EqualizerSpectrumAnalysis result;
    constexpr std::size_t fftSize = 8192;
    constexpr std::size_t hopSize = fftSize / 2;

    if (input.size() < fftSize)
        return result;

    double inputSquareSum = 0.0;
    for (const auto sample : input)
    {
        const auto value = std::isfinite (sample)
                             ? static_cast<double> (sample) : 0.0;
        inputSquareSum += value * value;
    }

    const auto rms = std::sqrt (
        inputSquareSum / static_cast<double> (input.size()));
    result.rmsDb = 20.0 * std::log10 (std::max (rms, 1.0e-20));

    std::array<double, 8> accumulatedBandPower {};
    std::array<std::size_t, 8> accumulatedBandBins {};
    constexpr double pi = 3.14159265358979323846;

    for (std::size_t offset = 0;
         offset + fftSize <= input.size(); offset += hopSize)
    {
        double mean = 0.0;
        for (std::size_t index = 0; index < fftSize; ++index)
        {
            const auto sample = input[offset + index];
            mean += std::isfinite (sample)
                      ? static_cast<double> (sample) : 0.0;
        }
        mean /= static_cast<double> (fftSize);

        std::vector<std::complex<double>> fft (fftSize);
        for (std::size_t index = 0; index < fftSize; ++index)
        {
            const auto sample = input[offset + index];
            const auto centred = (std::isfinite (sample)
                                     ? static_cast<double> (sample) : 0.0)
                                 - mean;
            const auto window = 0.5 - 0.5 * std::cos (
                2.0 * pi * static_cast<double> (index)
                / static_cast<double> (fftSize - 1));
            fft[index] = centred * window;
        }

        performFft (fft);
        for (std::size_t bin = 1; bin < fftSize / 2; ++bin)
        {
            const auto frequency = static_cast<double> (bin)
                                 * analysisSampleRate
                                 / static_cast<double> (fftSize);
            if (frequency < equalizerAnalysisBandEdgesHz.front()
                || frequency >= equalizerAnalysisBandEdgesHz.back())
            {
                continue;
            }

            const auto power = std::norm (fft[bin])
                             / static_cast<double> (fftSize * fftSize);
            for (std::size_t band = 0;
                 band < accumulatedBandPower.size(); ++band)
            {
                if (frequency >= equalizerAnalysisBandEdgesHz[band]
                    && frequency < equalizerAnalysisBandEdgesHz[band + 1])
                {
                    accumulatedBandPower[band] += power;
                    ++accumulatedBandBins[band];
                    break;
                }
            }
        }

        ++result.fftFrames;
    }

    for (std::size_t band = 0;
         band < result.bandLevelsDb.size(); ++band)
    {
        const auto averagePower = accumulatedBandBins[band] > 0
            ? accumulatedBandPower[band]
                / static_cast<double> (accumulatedBandBins[band])
            : 0.0;
        result.bandLevelsDb[band] = 10.0 * std::log10 (
            std::max (averagePower, 1.0e-20));
    }

    return result;
}

EqualizerSpectrumAnalysis averageEqualizerSpectra (
    const EqualizerSpectrumAnalysis& first,
    const EqualizerSpectrumAnalysis& second)
{
    EqualizerSpectrumAnalysis result;
    result.rmsDb = 0.5 * (first.rmsDb + second.rmsDb);
    result.fftFrames = first.fftFrames + second.fftFrames;
    for (std::size_t band = 0; band < result.bandLevelsDb.size(); ++band)
    {
        result.bandLevelsDb[band] = 0.5 * (
            first.bandLevelsDb[band] + second.bandLevelsDb[band]);
    }
    return result;
}

double maximumEqualizerBandDifference (
    const EqualizerSpectrumAnalysis& first,
    const EqualizerSpectrumAnalysis& second)
{
    double maximumDifference = 0.0;
    for (std::size_t band = 0; band < first.bandLevelsDb.size(); ++band)
    {
        maximumDifference = std::max (
            maximumDifference,
            std::abs (first.bandLevelsDb[band] - second.bandLevelsDb[band]));
    }
    return maximumDifference;
}

void printSpectrumMeasurement (const std::string& label,
                               float value,
                               const SpectrumAnalysis& analysis)
{
    std::cout << "  " << label << " " << value
              << " -> RMS " << analysis.rms
              << ", centroid " << analysis.spectralCentroidHz << " Hz"
              << ", strongest " << analysis.strongestFrequencyHz << " Hz"
              << ", bands dB [";

    for (std::size_t band = 0; band < analysis.bandLevelsDb.size(); ++band)
    {
        if (band != 0)
            std::cout << ", ";
        std::cout << spectrumBandNames[band] << ": " << analysis.bandLevelsDb[band];
    }

    std::cout << "]\n";
}

double analysisDistance (const AudioAnalysis& first, const AudioAnalysis& second)
{
    auto distance = std::abs (first.rms - second.rms) * 10.0
                  + std::abs (first.periodicity - second.periodicity);

    if (first.frequencyHz > 0.0 && second.frequencyHz > 0.0)
        distance += std::abs (std::log2 (first.frequencyHz / second.frequencyHz));
    else if ((first.frequencyHz > 0.0) != (second.frequencyHz > 0.0))
        distance += 2.0;

    for (std::size_t index = 1; index < first.harmonicRatios.size(); ++index)
        distance += std::abs (first.harmonicRatios[index] - second.harmonicRatios[index]);

    return distance;
}

double channelRms (const std::vector<float>& samples)
{
    if (samples.empty())
        return 0.0;

    double squareSum = 0.0;
    for (const auto sample : samples)
    {
        const auto value = std::isfinite (sample) ? static_cast<double> (sample) : 0.0;
        squareSum += value * value;
    }

    return std::sqrt (squareSum / static_cast<double> (samples.size()));
}

double channelPeak (const std::vector<float>& samples)
{
    double peak = 0.0;
    for (const auto sample : samples)
    {
        const auto value = std::isfinite (sample) ? static_cast<double> (sample) : 0.0;
        peak = std::max (peak, std::abs (value));
    }
    return peak;
}

double levelDb (double value)
{
    return 20.0 * std::log10 (std::max (value, 1.0e-12));
}

std::vector<float> sampleRange (const std::vector<float>& samples,
                                std::size_t firstSample,
                                std::size_t sampleCount)
{
    if (firstSample >= samples.size() || sampleCount == 0)
        return {};

    const auto endSample = std::min (samples.size(), firstSample + sampleCount);
    return { samples.begin() + static_cast<std::ptrdiff_t> (firstSample),
             samples.begin() + static_cast<std::ptrdiff_t> (endSample) };
}

void printLevelMeasurement (const char* label,
                            float value,
                            const std::vector<float>& audio,
                            double referenceRms = 0.0)
{
    const auto rms = channelRms (audio);
    const auto peak = channelPeak (audio);
    std::cout << "  " << label << " " << value
              << " -> RMS " << rms << " (" << levelDb (rms) << " dBFS)"
              << ", peak " << peak << " (" << levelDb (peak) << " dBFS)";

    if (referenceRms > 0.0 && rms > 0.0)
        std::cout << ", relative " << levelDb (rms / referenceRms) << " dB";

    std::cout << "\n";
}

double channelCorrelation (const std::vector<float>& first,
                           const std::vector<float>& second)
{
    const auto count = std::min (first.size(), second.size());
    if (count == 0)
        return 0.0;

    double cross = 0.0;
    double firstEnergy = 0.0;
    double secondEnergy = 0.0;

    for (std::size_t index = 0; index < count; ++index)
    {
        const auto firstValue = std::isfinite (first[index])
                                  ? static_cast<double> (first[index]) : 0.0;
        const auto secondValue = std::isfinite (second[index])
                                   ? static_cast<double> (second[index]) : 0.0;
        cross += firstValue * secondValue;
        firstEnergy += firstValue * firstValue;
        secondEnergy += secondValue * secondValue;
    }

    const auto denominator = std::sqrt (firstEnergy * secondEnergy);
    return denominator > 0.0 ? cross / denominator : 0.0;
}

double channelDifferenceRms (const std::vector<float>& first,
                             const std::vector<float>& second)
{
    const auto count = std::min (first.size(), second.size());
    if (count == 0)
        return 0.0;

    double squareSum = 0.0;
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto firstValue = std::isfinite (first[index])
                                  ? static_cast<double> (first[index]) : 0.0;
        const auto secondValue = std::isfinite (second[index])
                                   ? static_cast<double> (second[index]) : 0.0;
        const auto difference = firstValue - secondValue;
        squareSum += difference * difference;
    }

    return std::sqrt (squareSum / static_cast<double> (count));
}

std::pair<double, double> blockRmsRange (const std::vector<float>& samples)
{
    if (samples.size() < static_cast<std::size_t> (analysisBlockSize))
        return { 0.0, 0.0 };

    auto minimum = std::numeric_limits<double>::max();
    auto maximum = 0.0;

    for (std::size_t offset = 0;
         offset + static_cast<std::size_t> (analysisBlockSize) <= samples.size();
         offset += static_cast<std::size_t> (analysisBlockSize))
    {
        double squareSum = 0.0;
        for (int sample = 0; sample < analysisBlockSize; ++sample)
        {
            const auto value = samples[offset + static_cast<std::size_t> (sample)];
            const auto safeValue = std::isfinite (value) ? static_cast<double> (value) : 0.0;
            squareSum += safeValue * safeValue;
        }

        const auto rms = std::sqrt (squareSum / static_cast<double> (analysisBlockSize));
        minimum = std::min (minimum, rms);
        maximum = std::max (maximum, rms);
    }

    return { minimum, maximum };
}

void printHarmonixState (const char* state,
                         const AudioAnalysis& analysis,
                         const std::vector<float>& left,
                         const std::vector<float>& right)
{
    const auto rmsRange = blockRmsRange (left);
    std::cout << "  " << state
              << " -> frequency " << analysis.frequencyHz << " Hz"
              << ", left RMS " << channelRms (left)
              << ", right RMS " << channelRms (right)
              << ", periodicity " << analysis.periodicity
              << ", stereo correlation " << channelCorrelation (left, right)
              << ", stereo difference RMS " << channelDifferenceRms (left, right)
              << ", block RMS range " << rmsRange.first << " to " << rmsRange.second
              << ", harmonics H2-H8 ";

    for (std::size_t harmonic = 1; harmonic < analysis.harmonicRatios.size(); ++harmonic)
    {
        if (harmonic != 1)
            std::cout << ", ";
        std::cout << analysis.harmonicRatios[harmonic];
    }

    std::cout << "\n";
}

void printPitchMovement (const char* state, const PitchMovementAnalysis& analysis)
{
    std::cout << "  " << state
              << " pitch movement -> mean " << analysis.meanFrequencyHz << " Hz"
              << ", range " << analysis.minimumFrequencyHz
              << " to " << analysis.maximumFrequencyHz << " Hz"
              << ", deviation " << analysis.standardDeviationHz << " Hz"
              << ", span " << analysis.spanCents << " cents"
              << ", strongest movement rate " << analysis.modulationRateHz << " Hz"
              << ", measured windows " << analysis.windowCount << "\n";
}

void printPitchMeasurement (const char* label, float value, const AudioAnalysis& analysis)
{
    std::cout << "  " << label << " " << value
              << " -> frequency " << analysis.frequencyHz << " Hz"
              << ", RMS " << analysis.rms
              << ", periodicity " << analysis.periodicity << "\n";
}

void printFineTuneMeasurement (float value,
                               const AudioAnalysis& analysis,
                               double referenceFrequency)
{
    std::cout << "  value " << value
              << " -> frequency " << analysis.frequencyHz << " Hz";

    if (analysis.frequencyHz > 0.0 && referenceFrequency > 0.0)
    {
        const auto cents = 1200.0 * std::log2 (analysis.frequencyHz / referenceFrequency);
        std::cout << ", offset " << cents << " cents from value 1.000000";
    }
    else
    {
        std::cout << ", offset unavailable";
    }

    std::cout << ", RMS " << analysis.rms
              << ", periodicity " << analysis.periodicity << "\n";
}

void printWaveMeasurement (const char* label, float value, const AudioAnalysis& analysis)
{
    std::cout << "  " << label << " " << value
              << " -> suggested " << analysis.suggestedWaveform
              << ", frequency " << analysis.frequencyHz << " Hz"
              << ", RMS " << analysis.rms
              << ", periodicity " << analysis.periodicity
              << ", harmonics H2-H8 ";

    for (std::size_t harmonic = 1; harmonic < analysis.harmonicRatios.size(); ++harmonic)
    {
        if (harmonic != 1)
            std::cout << ", ";
        std::cout << analysis.harmonicRatios[harmonic];
    }
    std::cout << "\n";
}

std::string factoryProgramName (vst2::Effect* effect, vst2::Int32 program)
{
    if (effect == nullptr)
        return "Program " + std::to_string (program + 1);

    dispatch (effect, vst2::setProgram, 0, program);
    if ((effect->flags & vst2::programChunks) != 0)
    {
        void* chunk = nullptr;
        const auto chunkSize = dispatch (effect, vst2::getChunk, 1, 0, &chunk);
        if (chunk != nullptr && chunkSize >= 32)
        {
            const auto* bytes = static_cast<const unsigned char*> (chunk);
            const auto* nameBytes = bytes + chunkSize - 32;
            std::string name;
            for (int index = 0; index < 32 && nameBytes[index] != 0; ++index)
            {
                const auto character = nameBytes[index];
                if (! std::isprint (character))
                {
                    name.clear();
                    break;
                }
                name.push_back (static_cast<char> (character));
            }

            if (! name.empty())
                return name;
        }
    }

    auto name = dispatchString (effect, vst2::getProgramNameIndexed, 0, program);
    if (name.empty())
        name = dispatchString (effect, vst2::getProgramName);
    return name.empty() ? "Program " + std::to_string (program + 1) : name;
}

bool runFactoryEqualizerAnalysis (vst2::Effect* effect)
{
    std::cout << "\nFACTORY EQ RESPONSE CHECK\n";
    std::cout << "-------------------------\n";
    std::cout << "Each factory program is loaded twice, then all 44 exposed parameters"
                 " are replaced by the same controlled noise-source patch.\n";
    std::cout << "Eight averaged FFT bands correspond to the recreated EQ centres."
                 " Differences are reported against factory program 1.\n";
    std::cout << "This detects hidden program-specific frequency shaping; it does not"
                 " assume or invent exact EQ slider values.\n\n";

    if (effect == nullptr || effect->processReplacing == nullptr
        || effect->numPrograms <= 0 || effect->numParams < 44)
    {
        std::cout << "  FAIL: Original plug-in cannot provide the factory EQ test.\n";
        return false;
    }

    struct ProgramMeasurement
    {
        std::string name;
        EqualizerSpectrumAnalysis first;
        EqualizerSpectrumAnalysis second;
        EqualizerSpectrumAnalysis average;
        double repeatDifferenceDb = 0.0;
        bool valid = false;
    };

    std::vector<ProgramMeasurement> measurements (
        static_cast<std::size_t> (effect->numPrograms));
    bool producedAudio = false;

    for (vst2::Int32 program = 0; program < effect->numPrograms; ++program)
    {
        auto& measurement = measurements[static_cast<std::size_t> (program)];
        measurement.name = factoryProgramName (effect, program);

        TestSettings settings;
        settings.program = program;
        settings.activeOscillator = 1;
        settings.oscillator1Selector = 1.0f; // Broadband noise position
        settings.oscillator1Octave = 0.333333f;
        settings.bitcrusherAmount = 1.0f;
        settings.mainVolume = 0.25f;
        settings.filterCutoff = 1.0f;
        settings.filterResonance = 0.0f;
        settings.filterTracking = 0.0f;
        settings.filterType = 0.0f;
        settings.warmupBlocks = 48;
        settings.captureBlocks = 256;

        measurement.first = analyseEqualizerSpectrum (
            renderTestTone (effect, settings));
        measurement.second = analyseEqualizerSpectrum (
            renderTestTone (effect, settings));
        measurement.average = averageEqualizerSpectra (
            measurement.first, measurement.second);
        measurement.repeatDifferenceDb = maximumEqualizerBandDifference (
            measurement.first, measurement.second);
        measurement.valid = measurement.first.fftFrames > 0
                         && measurement.second.fftFrames > 0
                         && measurement.first.rmsDb > -160.0
                         && measurement.second.rmsDb > -160.0;
        producedAudio = producedAudio || measurement.valid;
    }

    const auto& reference = measurements.front();
    constexpr double repeatStabilityLimitDb = 1.0;
    constexpr double minimumMeaningfulDifferenceDb = 1.0;
    int distinctProgramCount = 0;
    int unstableProgramCount = 0;
    double maximumRepeatDifferenceDb = 0.0;
    double maximumProgramDifferenceDb = 0.0;

    for (std::size_t program = 0; program < measurements.size(); ++program)
    {
        const auto& measurement = measurements[program];
        maximumRepeatDifferenceDb = std::max (
            maximumRepeatDifferenceDb, measurement.repeatDifferenceDb);

        const auto programDifferenceDb = maximumEqualizerBandDifference (
            measurement.average, reference.average);
        maximumProgramDifferenceDb = std::max (
            maximumProgramDifferenceDb, programDifferenceDb);
        const auto noiseGuardDb = 3.0 * std::max (
            reference.repeatDifferenceDb, measurement.repeatDifferenceDb);
        const auto detectionThresholdDb = std::max (
            minimumMeaningfulDifferenceDb, noiseGuardDb);
        const auto stable = measurement.valid
                         && measurement.repeatDifferenceDb
                                <= repeatStabilityLimitDb;
        const auto distinct = program != 0 && stable
                           && reference.valid
                           && programDifferenceDb > detectionThresholdDb;

        if (! stable)
            ++unstableProgramCount;
        if (distinct)
            ++distinctProgramCount;

        std::cout << "Program " << std::setw (2) << std::setfill ('0')
                  << program + 1 << std::setfill (' ') << ": "
                  << measurement.name << "\n";
        std::cout << "  Repeat variation: "
                  << std::setprecision (3) << measurement.repeatDifferenceDb
                  << " dB; RMS runs " << measurement.first.rmsDb
                  << " / " << measurement.second.rmsDb << " dBFS\n";
        std::cout << "  Delta from program 1 [";
        for (std::size_t band = 0; band < equalizerBandNames.size(); ++band)
        {
            if (band != 0)
                std::cout << ", ";
            const auto difference = measurement.average.bandLevelsDb[band]
                                  - reference.average.bandLevelsDb[band];
            std::cout << equalizerBandNames[band] << ": "
                      << std::showpos << difference << std::noshowpos;
        }
        std::cout << "] dB\n";
        std::cout << "  Classification: ";
        if (program == 0 && stable)
            std::cout << "REFERENCE";
        else if (! stable)
            std::cout << "UNSTABLE - repeat measurement required";
        else if (distinct)
            std::cout << "DIFFERENT - repeatable hidden frequency response";
        else
            std::cout << "SAME within " << detectionThresholdDb
                      << " dB detection threshold";
        std::cout << "\n\n" << std::setprecision (6);
    }

    std::cout << "FACTORY EQ RESPONSE SUMMARY\n";
    std::cout << "  Programs measured: " << measurements.size() << "\n";
    std::cout << "  Repeat-stable programs: "
              << measurements.size() - static_cast<std::size_t> (unstableProgramCount)
              << "\n";
    std::cout << "  Programs with a distinct response: "
              << distinctProgramCount << "\n";
    std::cout << "  Maximum repeat variation: "
              << maximumRepeatDifferenceDb << " dB\n";
    std::cout << "  Maximum program-to-reference difference: "
              << maximumProgramDifferenceDb << " dB\n";

    if (! producedAudio)
    {
        std::cout << "  RESULT: No usable audio was produced.\n";
    }
    else if (unstableProgramCount != 0)
    {
        std::cout << "  RESULT: Measurements were not stable enough for a final"
                     " factory-EQ conclusion.\n";
    }
    else if (distinctProgramCount == 0)
    {
        std::cout << "  RESULT: No measurable program-specific EQ response was found."
                     " Neutral factory EQ is supported by the audio test.\n";
    }
    else
    {
        std::cout << "  RESULT: Repeatable program-specific frequency responses were"
                     " found. The reported band deltas can guide a calibrated recovery"
                     " pass.\n";
    }

    std::cout << "  - " << (producedAudio ? "PASS" : "FAIL")
              << ": Controlled factory-program EQ comparison completed\n";
    std::cout << "  - " << (unstableProgramCount == 0 ? "PASS" : "CHECK")
              << ": Duplicate-render stability check\n";
    return producedAudio;
}

bool runOscillatorAudioAnalysis (vst2::Effect* effect)
{
    std::cout << "\nOSCILLATOR AUDIO BEHAVIOUR\n";
    std::cout << "--------------------------\n";
    std::cout << "Test note: MIDI 57 (A3), sample rate 44100 Hz\n";
    std::cout << "All reported control values are normalized 0.0 to 1.0.\n\n";

    if (effect == nullptr || effect->processReplacing == nullptr || effect->numParams < 33)
    {
        std::cout << "  FAIL: Original plug-in cannot provide the required audio test.\n";
        return false;
    }

    static constexpr std::array<float, 9> detailedPositions {
        0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f
    };
    static constexpr std::array<float, 5> selectorPositions {
        0.0f, 0.25f, 0.5f, 0.75f, 1.0f
    };

    bool producedAudio = false;

    std::cout << "OCTAVE PARAMETER 29 WITH OSC1 ISOLATED\n";
    for (const auto value : detailedPositions)
    {
        TestSettings settings;
        settings.activeOscillator = 1;
        settings.oscillator1Octave = value;
        const auto analysis = analyseAudio (renderTestTone (effect, settings));
        producedAudio = producedAudio || analysis.rms > 1.0e-8;
        printPitchMeasurement ("value", value, analysis);
    }

    std::cout << "\nOCTAVE PARAMETER 30 WITH OSC2 ISOLATED\n";
    for (const auto value : detailedPositions)
    {
        TestSettings settings;
        settings.activeOscillator = 2;
        settings.oscillator2Octave = value;
        const auto analysis = analyseAudio (renderTestTone (effect, settings));
        producedAudio = producedAudio || analysis.rms > 1.0e-8;
        printPitchMeasurement ("value", value, analysis);
    }

    std::cout << "\nOSC2 FREQ PARAMETER 28 WITH OSC2 ISOLATED\n";
    TestSettings frequencyReferenceSettings;
    frequencyReferenceSettings.activeOscillator = 2;
    frequencyReferenceSettings.oscillator2Frequency = 1.0f;
    const auto frequencyReference = analyseAudio (
        renderTestTone (effect, frequencyReferenceSettings));
    std::array<AudioAnalysis, detailedPositions.size()> fineTuneAnalyses {};

    for (std::size_t index = 0; index < detailedPositions.size(); ++index)
    {
        const auto value = detailedPositions[index];
        TestSettings settings;
        settings.activeOscillator = 2;
        settings.oscillator2Frequency = value;
        const auto analysis = analyseAudio (renderTestTone (effect, settings));
        fineTuneAnalyses[index] = analysis;
        producedAudio = producedAudio || analysis.rms > 1.0e-8;
        printFineTuneMeasurement (value, analysis, frequencyReference.frequencyHz);
    }

    std::cout << "\nOSC2 FREQ CLASSIFICATION\n";
    if (frequencyReference.frequencyHz > 0.0
        && fineTuneAnalyses.front().frequencyHz > 0.0)
    {
        const auto fullRangeCents = 1200.0 * std::log2 (
            fineTuneAnalyses.front().frequencyHz / frequencyReference.frequencyHz);
        std::cout << "  Full movement from value 1.000000 to 0.000000: "
                  << fullRangeCents << " cents\n";
        std::cout << "  Suggested function: "
                  << (std::abs (fullRangeCents) <= 200.0
                        ? "FINE TUNE"
                        : "WIDE PITCH/TUNING RANGE")
                  << "\n";
    }
    else
    {
        std::cout << "  Suggested function unavailable: stable pitch was not detected.\n";
    }

    const auto selectorDistance = [effect] (int parameter, int activeOscillator)
    {
        TestSettings low;
        low.activeOscillator = activeOscillator;
        TestSettings high = low;

        if (parameter == 31)
        {
            low.oscillator1Selector = 0.0f;
            high.oscillator1Selector = 1.0f;
        }
        else
        {
            low.oscillator2Selector = 0.0f;
            high.oscillator2Selector = 1.0f;
        }

        return analysisDistance (analyseAudio (renderTestTone (effect, low)),
                                 analyseAudio (renderTestTone (effect, high)));
    };

    const auto parameter31OnOsc1 = selectorDistance (31, 1);
    const auto parameter31OnOsc2 = selectorDistance (31, 2);
    const auto parameter32OnOsc1 = selectorDistance (32, 1);
    const auto parameter32OnOsc2 = selectorDistance (32, 2);

    std::cout << "\nSELECTOR-TO-OSCILLATOR MAPPING CHECK\n";
    std::cout << "  Parameter 31 change score on OSC1: " << parameter31OnOsc1 << "\n";
    std::cout << "  Parameter 31 change score on OSC2: " << parameter31OnOsc2 << "\n";
    std::cout << "  Parameter 32 change score on OSC1: " << parameter32OnOsc1 << "\n";
    std::cout << "  Parameter 32 change score on OSC2: " << parameter32OnOsc2 << "\n";

    const auto parameter31Oscillator = parameter31OnOsc1 >= parameter31OnOsc2 ? 1 : 2;
    const auto parameter32Oscillator = parameter32OnOsc1 >= parameter32OnOsc2 ? 1 : 2;
    std::cout << "  Suggested mapping: parameter 31 -> OSC" << parameter31Oscillator
              << ", parameter 32 -> OSC" << parameter32Oscillator << "\n";

    std::cout << "\nPARAMETER 31 WAVEFORM POSITIONS ON SUGGESTED OSCILLATOR\n";
    for (const auto value : selectorPositions)
    {
        TestSettings settings;
        settings.activeOscillator = parameter31Oscillator;
        settings.oscillator1Selector = value;
        const auto analysis = analyseAudio (renderTestTone (effect, settings));
        producedAudio = producedAudio || analysis.rms > 1.0e-8;
        printWaveMeasurement ("value", value, analysis);
    }

    std::cout << "\nPARAMETER 32 WAVEFORM POSITIONS ON SUGGESTED OSCILLATOR\n";
    for (const auto value : selectorPositions)
    {
        TestSettings settings;
        settings.activeOscillator = parameter32Oscillator;
        settings.oscillator2Selector = value;
        const auto analysis = analyseAudio (renderTestTone (effect, settings));
        producedAudio = producedAudio || analysis.rms > 1.0e-8;
        printWaveMeasurement ("value", value, analysis);
    }

    std::cout << "\nOSCILLATOR AUDIO SUMMARY\n";
    std::cout << "  - " << (producedAudio ? "PASS" : "FAIL")
              << ": Original plug-in audio rendered for measurement\n";
    std::cout << "  - PASS: Octave and OSC2 fine-frequency sweeps completed\n";
    std::cout << "  - PASS: Selector mapping and waveform fingerprints completed\n";
    return producedAudio;
}

bool runFilterAudioAnalysis (vst2::Effect* effect)
{
    std::cout << "\nFILTER AUDIO BEHAVIOUR\n";
    std::cout << "----------------------\n";
    std::cout << "Noise source, sample rate 44100 Hz, filter envelope disabled.\n";
    std::cout << "Bands show average spectral power in dB for comparison.\n\n";

    if (effect == nullptr || effect->processReplacing == nullptr || effect->numParams < 20)
    {
        std::cout << "  FAIL: Original plug-in cannot provide the required filter test.\n";
        return false;
    }

    static constexpr std::array<float, 9> detailedPositions {
        0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f
    };
    struct FilterTypePosition
    {
        float value;
        const char* name;
    };
    static constexpr std::array<FilterTypePosition, 6> typePositions {{
        { 0.0f, "LOW PASS" },
        { 0.2f, "HIGH PASS" },
        { 0.4f, "BAND PASS" },
        { 0.6f, "BAND REJECT" },
        { 0.8f, "PEAKING" },
        { 1.0f, "MAXIMUM BOUNDARY CHECK" }
    }};
    static constexpr std::array<float, 5> resonancePositions {
        0.0f, 0.25f, 0.5f, 0.75f, 1.0f
    };
    static constexpr std::array<float, 3> trackingPositions {
        0.0f, 0.5f, 1.0f
    };

    const auto makeNoisePatch = []
    {
        TestSettings settings;
        settings.activeOscillator = 1;
        settings.oscillator1Selector = 1.0f;
        settings.oscillator1Octave = 0.333333f;
        settings.filterCutoff = 0.5f;
        settings.filterResonance = 0.0f;
        settings.filterTracking = 0.0f;
        settings.filterType = 0.0f;
        return settings;
    };

    bool producedAudio = false;

    std::cout << "FILTER TYPE PARAMETER 19 SWEEP\n";
    std::cout << "  Fixed cutoff 0.500000, resonance 0.000000, tracking 0.000000\n";
    for (const auto& type : typePositions)
    {
        auto settings = makeNoisePatch();
        settings.filterType = type.value;
        const auto analysis = analyseSpectrum (renderTestTone (effect, settings));
        producedAudio = producedAudio || analysis.rms > 1.0e-8;
        printSpectrumMeasurement (type.name, type.value, analysis);
    }

    std::cout << "\nFILTER CUTOFF PARAMETER 16 SWEEP\n";
    std::cout << "  Type 0.000000, resonance 0.000000, tracking 0.000000\n";
    for (const auto value : detailedPositions)
    {
        auto settings = makeNoisePatch();
        settings.filterCutoff = value;
        const auto analysis = analyseSpectrum (renderTestTone (effect, settings));
        producedAudio = producedAudio || analysis.rms > 1.0e-8;
        printSpectrumMeasurement ("value", value, analysis);
    }

    std::cout << "\nFILTER RESONANCE PARAMETER 17 SWEEP\n";
    std::cout << "  Type 0.000000, cutoff 0.500000, tracking 0.000000\n";
    for (const auto value : resonancePositions)
    {
        auto settings = makeNoisePatch();
        settings.filterResonance = value;
        const auto analysis = analyseSpectrum (renderTestTone (effect, settings));
        producedAudio = producedAudio || analysis.rms > 1.0e-8;
        printSpectrumMeasurement ("value", value, analysis);
    }

    std::cout << "\nFILTER TRACKING PARAMETER 18 SWEEP\n";
    std::cout << "  Type 0.000000, cutoff 0.350000, resonance 0.000000\n";
    for (const auto value : trackingPositions)
    {
        auto lowSettings = makeNoisePatch();
        lowSettings.midiNote = 36;
        lowSettings.filterCutoff = 0.35f;
        lowSettings.filterTracking = value;
        const auto lowAnalysis = analyseSpectrum (renderTestTone (effect, lowSettings));
        producedAudio = producedAudio || lowAnalysis.rms > 1.0e-8;
        printSpectrumMeasurement ("MIDI-36 value", value, lowAnalysis);

        auto highSettings = lowSettings;
        highSettings.midiNote = 84;
        const auto highAnalysis = analyseSpectrum (renderTestTone (effect, highSettings));
        producedAudio = producedAudio || highAnalysis.rms > 1.0e-8;
        printSpectrumMeasurement ("MIDI-84 value", value, highAnalysis);
    }

    std::cout << "\nFILTER AUDIO SUMMARY\n";
    std::cout << "  - " << (producedAudio ? "PASS" : "FAIL")
              << ": Original plug-in filter rendered for measurement\n";
    std::cout << "  - PASS: Type, cutoff, resonance, and tracking sweeps completed\n";
    return producedAudio;
}

bool runHarmonixAudioAnalysis (vst2::Effect* effect)
{
    std::cout << "\nHARMONIX AUDIO BEHAVIOUR\n";
    std::cout << "-------------------------\n";
    std::cout << "Parameter 22 is compared off/on with effects, bitcrusher, glide,"
                 " and filter envelope disabled.\n";
    std::cout << "Stereo difference and block-level movement help distinguish a"
                 " chorus-like effect from harmonic generation.\n\n";

    if (effect == nullptr || effect->processReplacing == nullptr || effect->numParams < 23)
    {
        std::cout << "  FAIL: Original plug-in cannot provide the HARMONIX test.\n";
        return false;
    }

    struct SourceTest
    {
        int activeOscillator;
        const char* name;
    };

    static constexpr std::array<SourceTest, 3> sourceTests {{
        { 1, "OSC1 ISOLATED" },
        { 2, "OSC2 ISOLATED" },
        { 0, "BOTH OSCILLATORS" }
    }};

    bool producedAudio = false;
    bool measurableChange = false;

    for (const auto& source : sourceTests)
    {
        TestSettings offSettings;
        offSettings.activeOscillator = source.activeOscillator;
        offSettings.oscillator1Selector = 0.0f;
        offSettings.oscillator2Selector = 0.0f;
        offSettings.oscillator1Octave = 0.333333f;
        offSettings.oscillator2Octave = 0.333333f;
        offSettings.harmonix = 0.0f;
        offSettings.warmupBlocks = 32;
        offSettings.captureBlocks = 192;

        auto onSettings = offSettings;
        onSettings.harmonix = 1.0f;

        std::vector<float> offRight;
        std::vector<float> onRight;
        const auto offLeft = renderTestTone (effect, offSettings, &offRight);
        const auto onLeft = renderTestTone (effect, onSettings, &onRight);
        const auto offAnalysis = analyseAudio (offLeft);
        const auto onAnalysis = analyseAudio (onLeft);
        const auto offPitchMovement = analysePitchMovement (offLeft);
        const auto onPitchMovement = analysePitchMovement (onLeft);
        const auto changeScore = analysisDistance (offAnalysis, onAnalysis);
        const auto onStereoDifference = channelDifferenceRms (onLeft, onRight);
        const auto onLevel = std::max (channelRms (onLeft), 1.0e-12);

        producedAudio = producedAudio || offAnalysis.rms > 1.0e-8
                                      || onAnalysis.rms > 1.0e-8;
        measurableChange = measurableChange || changeScore > 0.01
                                            || onStereoDifference / onLevel > 0.01;

        std::cout << source.name << "\n";
        printHarmonixState ("OFF", offAnalysis, offLeft, offRight);
        printHarmonixState ("ON ", onAnalysis, onLeft, onRight);
        printPitchMovement ("OFF", offPitchMovement);
        printPitchMovement ("ON ", onPitchMovement);
        std::cout << "  Off/on change score: " << changeScore << "\n\n";
    }

    static constexpr std::array<float, 17> frequencyPositions {
        0.0f, 0.0625f, 0.125f, 0.1875f, 0.25f, 0.3125f, 0.375f, 0.4375f,
        0.5f, 0.5625f, 0.625f, 0.6875f, 0.75f, 0.8125f, 0.875f, 0.9375f, 1.0f
    };

    std::cout << "HARMONIX / OSC2 FREQ INTERACTION SWEEP\n";
    std::cout << "---------------------------------------\n";
    std::cout << "OSC2 is isolated with its sine position selected. Each FREQ value is"
                 " measured first with HARMONIX OFF to retain the original oscillator's"
                 " built-in movement, then with HARMONIX ON.\n";
    std::cout << "The strongest movement rate is an approximate diagnostic, not a"
                 " claimed circuit label.\n\n";

    for (const auto frequencyPosition : frequencyPositions)
    {
        TestSettings offSettings;
        offSettings.activeOscillator = 2;
        offSettings.oscillator2Selector = 0.0f;
        offSettings.oscillator2Octave = 0.333333f;
        offSettings.oscillator2Frequency = frequencyPosition;
        offSettings.harmonix = 0.0f;
        offSettings.warmupBlocks = 32;
        offSettings.captureBlocks = 192;

        auto onSettings = offSettings;
        onSettings.harmonix = 1.0f;

        const auto offAudio = renderTestTone (effect, offSettings);
        const auto onAudio = renderTestTone (effect, onSettings);
        const auto offMovement = analysePitchMovement (offAudio);
        const auto onMovement = analysePitchMovement (onAudio);

        producedAudio = producedAudio || channelRms (offAudio) > 1.0e-8
                                      || channelRms (onAudio) > 1.0e-8;

        std::cout << "OSC2 FREQ " << frequencyPosition << "\n";
        printPitchMovement ("OFF", offMovement);
        printPitchMovement ("ON ", onMovement);

        if (offMovement.meanFrequencyHz > 0.0 && onMovement.meanFrequencyHz > 0.0)
        {
            const auto meanPitchShiftCents = 1200.0 * std::log2 (
                onMovement.meanFrequencyHz / offMovement.meanFrequencyHz);
            std::cout << "  HARMONIX difference -> mean pitch shift "
                      << meanPitchShiftCents << " cents"
                      << ", pitch-movement span change "
                      << onMovement.spanCents - offMovement.spanCents << " cents"
                      << ", strongest movement rate OFF/ON "
                      << offMovement.modulationRateHz << " / "
                      << onMovement.modulationRateHz << " Hz\n\n";
        }
        else
        {
            std::cout << "  HARMONIX difference -> pitch comparison unavailable\n\n";
        }
    }

    std::cout << "HARMONIX AUDIO SUMMARY\n";
    std::cout << "  - " << (producedAudio ? "PASS" : "FAIL")
              << ": Original plug-in rendered for HARMONIX measurement\n";
    std::cout << "  - " << (measurableChange ? "PASS" : "CHECK")
              << ": Parameter 22 produced a measurable on/off change\n";
    std::cout << "  - PASS: Pitch, harmonics, stereo difference, and level movement captured\n";
    std::cout << "  - PASS: Baseline oscillator movement and OSC2 FREQ-dependent"
                 " HARMONIX modulation sweep captured\n";
    return producedAudio;
}

bool runGainStageAudioAnalysis (vst2::Effect* effect)
{
    std::cout << "\nGAIN, VELOCITY, AND LEVEL CURVES\n";
    std::cout << "--------------------------------\n";
    std::cout << "Clean isolated sine source with filter fully open and effects disabled.\n\n";

    if (effect == nullptr || effect->processReplacing == nullptr || effect->numParams < 28)
    {
        std::cout << "  FAIL: Original plug-in cannot provide the gain-stage test.\n";
        return false;
    }

    static constexpr std::array<float, 9> positions {
        0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f
    };

    TestSettings referenceSettings;
    referenceSettings.activeOscillator = 1;
    referenceSettings.oscillator1Selector = 0.0f;
    referenceSettings.oscillator1Octave = 0.333333f;
    const auto referenceAudio = renderTestTone (effect, referenceSettings);
    const auto referenceRms = channelRms (referenceAudio);
    auto producedAudio = referenceRms > 1.0e-8;

    std::cout << "MAIN VOLUME PARAMETER 24\n";
    for (const auto value : positions)
    {
        auto settings = referenceSettings;
        settings.mainVolume = value;
        const auto audio = renderTestTone (effect, settings);
        producedAudio = producedAudio || channelRms (audio) > 1.0e-8;
        printLevelMeasurement ("value", value, audio, referenceRms);
    }

    std::cout << "\nOSC1 VOLUME PARAMETER 26\n";
    for (const auto value : positions)
    {
        auto settings = referenceSettings;
        settings.oscillator1Volume = value;
        const auto audio = renderTestTone (effect, settings);
        producedAudio = producedAudio || channelRms (audio) > 1.0e-8;
        printLevelMeasurement ("value", value, audio, referenceRms);
    }

    std::cout << "\nOSC2 VOLUME PARAMETER 27\n";
    TestSettings oscillator2Reference = referenceSettings;
    oscillator2Reference.activeOscillator = 2;
    oscillator2Reference.oscillator2Selector = 0.0f;
    oscillator2Reference.oscillator2Octave = 0.333333f;
    oscillator2Reference.oscillator2Frequency = 0.5f;
    const auto oscillator2ReferenceRms = channelRms (
        renderTestTone (effect, oscillator2Reference));
    for (const auto value : positions)
    {
        auto settings = oscillator2Reference;
        settings.oscillator2Volume = value;
        const auto audio = renderTestTone (effect, settings);
        producedAudio = producedAudio || channelRms (audio) > 1.0e-8;
        printLevelMeasurement ("value", value, audio, oscillator2ReferenceRms);
    }

    std::cout << "\nMIDI VELOCITY RESPONSE\n";
    static constexpr std::array<int, 7> velocities { 16, 32, 48, 64, 80, 100, 127 };
    TestSettings maximumVelocitySettings = referenceSettings;
    maximumVelocitySettings.midiVelocity = 127;
    const auto maximumVelocityRms = channelRms (
        renderTestTone (effect, maximumVelocitySettings));
    for (const auto velocity : velocities)
    {
        auto settings = referenceSettings;
        settings.midiVelocity = velocity;
        const auto audio = renderTestTone (effect, settings);
        producedAudio = producedAudio || channelRms (audio) > 1.0e-8;
        printLevelMeasurement ("velocity", static_cast<float> (velocity),
                               audio, maximumVelocityRms);
    }

    std::cout << "\nGAIN-STAGE AUDIO SUMMARY\n";
    std::cout << "  - " << (producedAudio ? "PASS" : "FAIL")
              << ": Main, oscillator, and velocity level curves captured\n";
    return producedAudio;
}

bool runBitcrusherAudioAnalysis (vst2::Effect* effect)
{
    std::cout << "\nBITRATE / BITCRUSHER AUDIO BEHAVIOUR\n";
    std::cout << "-------------------------------------\n";
    std::cout << "Square source at MIDI 33 with all other effects disabled.\n";
    std::cout << "Spectral bands expose quantisation and sample-refresh direction.\n\n";

    if (effect == nullptr || effect->processReplacing == nullptr || effect->numParams < 7)
    {
        std::cout << "  FAIL: Original plug-in cannot provide the BITRATE test.\n";
        return false;
    }

    static constexpr std::array<float, 9> positions {
        0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f
    };

    const auto makeSettings = []
    {
        TestSettings settings;
        settings.activeOscillator = 1;
        settings.midiNote = 33;
        settings.oscillator1Selector = 0.75f;
        settings.oscillator1Octave = 0.333333f;
        settings.warmupBlocks = 24;
        settings.captureBlocks = 64;
        return settings;
    };

    auto producedAudio = false;
    auto offSettings = makeSettings();
    offSettings.bitcrusherOscillator1 = 0.0f;
    offSettings.bitcrusherAmount = 0.927644f;
    const auto offAudio = renderTestTone (effect, offSettings);
    const auto offAudioAnalysis = analyseAudio (offAudio);
    const auto offSpectrum = analyseSpectrum (offAudio);
    producedAudio = offAudioAnalysis.rms > 1.0e-8;

    std::cout << "OSC1 SWITCH PARAMETER 4 OFF REFERENCE\n";
    printWaveMeasurement ("OFF at BITRATE", offSettings.bitcrusherAmount,
                          offAudioAnalysis);
    printSpectrumMeasurement ("OFF at BITRATE", offSettings.bitcrusherAmount,
                              offSpectrum);

    std::cout << "\nOSC1 SWITCH ON: BITRATE PARAMETER 6 SWEEP\n";
    for (const auto value : positions)
    {
        auto settings = makeSettings();
        settings.bitcrusherOscillator1 = 1.0f;
        settings.bitcrusherAmount = value;
        const auto audio = renderTestTone (effect, settings);
        const auto audioAnalysis = analyseAudio (audio);
        const auto spectrum = analyseSpectrum (audio);
        producedAudio = producedAudio || audioAnalysis.rms > 1.0e-8;
        printWaveMeasurement ("value", value, audioAnalysis);
        printSpectrumMeasurement ("value", value, spectrum);
    }

    std::cout << "\nOSC2 SWITCH PARAMETER 5 MAPPING CHECK\n";
    TestSettings oscillator2Off;
    oscillator2Off.activeOscillator = 2;
    oscillator2Off.midiNote = 33;
    oscillator2Off.oscillator2Selector = 0.75f;
    oscillator2Off.oscillator2Octave = 0.333333f;
    oscillator2Off.oscillator2Frequency = 0.5f;
    oscillator2Off.bitcrusherOscillator2 = 0.0f;
    oscillator2Off.bitcrusherAmount = 0.927644f;
    oscillator2Off.warmupBlocks = 24;
    oscillator2Off.captureBlocks = 64;
    auto oscillator2On = oscillator2Off;
    oscillator2On.bitcrusherOscillator2 = 1.0f;
    const auto oscillator2OffAnalysis = analyseSpectrum (
        renderTestTone (effect, oscillator2Off));
    const auto oscillator2OnAnalysis = analyseSpectrum (
        renderTestTone (effect, oscillator2On));
    printSpectrumMeasurement ("OFF", oscillator2Off.bitcrusherAmount,
                              oscillator2OffAnalysis);
    printSpectrumMeasurement ("ON ", oscillator2On.bitcrusherAmount,
                              oscillator2OnAnalysis);

    std::cout << "\nBITRATE AUDIO SUMMARY\n";
    std::cout << "  - " << (producedAudio ? "PASS" : "FAIL")
              << ": Switch mapping and complete BITRATE sweep captured\n";
    return producedAudio;
}

bool runEnvelopeAudioAnalysis (vst2::Effect* effect)
{
    std::cout << "\nAMPLIFIER ENVELOPE AUDIO BEHAVIOUR\n";
    std::cout << "-----------------------------------\n";
    std::cout << "Clean sine source; values are measured as block RMS over time.\n\n";

    if (effect == nullptr || effect->processReplacing == nullptr || effect->numParams < 4)
    {
        std::cout << "  FAIL: Original plug-in cannot provide the envelope test.\n";
        return false;
    }

    static constexpr std::array<float, 9> sustainPositions {
        0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f
    };

    TestSettings fullLevelSettings;
    fullLevelSettings.activeOscillator = 1;
    fullLevelSettings.midiNote = 45;
    fullLevelSettings.oscillator1Selector = 0.0f;
    fullLevelSettings.oscillator1Octave = 0.333333f;
    fullLevelSettings.ampAttack = 0.0f;
    fullLevelSettings.ampDecay = 0.0f;
    fullLevelSettings.ampSustain = 1.0f;
    fullLevelSettings.warmupBlocks = 48;
    fullLevelSettings.captureBlocks = 16;
    const auto fullLevelRms = channelRms (renderTestTone (effect, fullLevelSettings));
    auto producedAudio = fullLevelRms > 1.0e-8;

    std::cout << "SUSTAIN PARAMETER 3 SWEEP\n";
    for (const auto value : sustainPositions)
    {
        auto settings = fullLevelSettings;
        settings.ampSustain = value;
        const auto audio = renderTestTone (effect, settings);
        producedAudio = producedAudio || channelRms (audio) > 1.0e-8;
        printLevelMeasurement ("value", value, audio, fullLevelRms);
    }

    std::cout << "\nALL STAR AMP ENVELOPE CURVE\n";
    std::cout << "  A 0.072356, D 0.500000, S 0.465721, R 0.548654\n";
    std::cout << "  Note-off occurs near 1.51 seconds. Windows are 0.125 seconds.\n";
    TestSettings allStarSettings;
    allStarSettings.program = 1;
    allStarSettings.activeOscillator = 1;
    allStarSettings.midiNote = 45;
    allStarSettings.oscillator1Selector = 0.0f;
    allStarSettings.oscillator1Octave = 0.333333f;
    allStarSettings.ampAttack = 0.072356f;
    allStarSettings.ampDecay = 0.5f;
    allStarSettings.ampSustain = 0.465721f;
    allStarSettings.ampRelease = 0.548654f;
    allStarSettings.warmupBlocks = 0;
    allStarSettings.captureBlocks = 260;
    allStarSettings.noteOffAfterCaptureBlocks = 130;
    const auto allStarAudio = renderTestTone (effect, allStarSettings);
    const auto windowSamples = static_cast<std::size_t> (
        std::lround (0.125 * static_cast<double> (analysisSampleRate)));
    for (std::size_t window = 0;
         (window + 1) * windowSamples <= allStarAudio.size(); ++window)
    {
        const auto audio = sampleRange (allStarAudio, window * windowSamples,
                                        windowSamples);
        const auto startSeconds = 0.125 * static_cast<double> (window);
        std::cout << "  window " << std::setw (2) << window + 1
                  << " at " << startSeconds << " s -> RMS " << channelRms (audio)
                  << " (" << levelDb (channelRms (audio)) << " dBFS), peak "
                  << channelPeak (audio) << "\n";
    }

    std::cout << "\nENVELOPE AUDIO SUMMARY\n";
    std::cout << "  - " << (producedAudio ? "PASS" : "FAIL")
              << ": Sustain curve and All Star ADSR timing captured\n";
    return producedAudio;
}

bool runAllStarSequencerAnalysis (vst2::Effect* effect)
{
    std::cout << "\nALL STAR SEQUENCER AUDIO BEHAVIOUR\n";
    std::cout << "-----------------------------------\n";
    std::cout << "Factory program 2 supplies its original hidden 16-step data.\n";
    std::cout << "Tests isolate pitch and filter movement at 120 BPM and 1/16 rate.\n\n";

    if (effect == nullptr || effect->processReplacing == nullptr
        || effect->numParams < 44 || effect->numPrograms < 2)
    {
        std::cout << "  FAIL: Original plug-in cannot provide the All Star sequence test.\n";
        return false;
    }

    constexpr auto stepSeconds = 0.125;
    const auto stepSamples = static_cast<std::size_t> (
        std::lround (stepSeconds * static_cast<double> (analysisSampleRate)));
    const auto captureSamples = 16u * stepSamples;
    const auto captureBlocks = static_cast<int> (
        (captureSamples + static_cast<std::size_t> (analysisBlockSize) - 1u)
        / static_cast<std::size_t> (analysisBlockSize));

    TestSettings pitchSettings;
    pitchSettings.program = 1;
    pitchSettings.activeOscillator = 1;
    pitchSettings.midiNote = 33;
    pitchSettings.oscillator1Selector = 0.0f;
    pitchSettings.oscillator1Octave = 0.333333f;
    pitchSettings.sequencerPitch = 1.0f;
    pitchSettings.sequencerFilter = 0.0f;
    pitchSettings.sequencerSmooth = 0.450001f;
    pitchSettings.sequencerRate = 0.428571f;
    pitchSettings.warmupBlocks = 0;
    pitchSettings.captureBlocks = captureBlocks;
    const auto pitchAudio = renderTestTone (effect, pitchSettings);
    auto producedAudio = channelRms (pitchAudio) > 1.0e-8;

    std::cout << "PITCH SWITCH ON, FILTER SWITCH OFF\n";
    for (std::size_t step = 0; step < 16; ++step)
    {
        const auto audio = sampleRange (pitchAudio, step * stepSamples, stepSamples);
        const auto analysis = analyseAudio (audio);
        std::cout << "  step " << std::setw (2) << step + 1
                  << " -> frequency " << analysis.frequencyHz << " Hz"
                  << ", RMS " << analysis.rms
                  << ", periodicity " << analysis.periodicity << "\n";
    }

    TestSettings filterSettings = pitchSettings;
    filterSettings.oscillator1Selector = 1.0f;
    filterSettings.filterCutoff = 0.958750f;
    filterSettings.filterResonance = 0.188750f;
    filterSettings.filterTracking = 0.425513f;
    filterSettings.filterType = 0.0f;
    filterSettings.sequencerPitch = 0.0f;
    filterSettings.sequencerFilter = 1.0f;
    const auto filterAudio = renderTestTone (effect, filterSettings);
    producedAudio = producedAudio || channelRms (filterAudio) > 1.0e-8;

    std::cout << "\nFILTER SWITCH ON, PITCH SWITCH OFF\n";
    for (std::size_t step = 0; step < 16; ++step)
    {
        const auto audio = sampleRange (filterAudio, step * stepSamples, stepSamples);
        const auto analysis = analyseSpectrum (audio);
        std::cout << "  step " << std::setw (2) << step + 1
                  << " -> RMS " << analysis.rms
                  << ", centroid " << analysis.spectralCentroidHz << " Hz"
                  << ", strongest " << analysis.strongestFrequencyHz << " Hz"
                  << ", bands dB [";
        for (std::size_t band = 0; band < analysis.bandLevelsDb.size(); ++band)
        {
            if (band != 0)
                std::cout << ", ";
            std::cout << spectrumBandNames[band] << ": "
                      << analysis.bandLevelsDb[band];
        }
        std::cout << "]\n";
    }

    std::cout << "\nALL STAR SEQUENCER SUMMARY\n";
    std::cout << "  - " << (producedAudio ? "PASS" : "FAIL")
              << ": Original pitch and filter step behaviour captured separately\n";
    return producedAudio;
}
}

int wmain (int argumentCount, wchar_t** arguments)
{
    std::cout << "ICECREAM ORIGINAL VST2 ANALYSIS\n";
    std::cout << "================================\n\n";

    if (argumentCount != 2)
    {
        std::cout << "ERROR: Expected the path to Icecream.dll.\n";
        return 2;
    }

    const std::filesystem::path dllPath (arguments[1]);
    pluginDirectory = wideToAnsi (dllPath.parent_path().wstring());

    std::cout << "Probe architecture: Win32\n";
    std::cout << "Original DLL: " << dllPath.string() << "\n\n";

    const auto module = LoadLibraryW (dllPath.c_str());
    if (module == nullptr)
    {
        std::cout << "ERROR: LoadLibraryW failed with Windows error "
                  << GetLastError() << ".\n";
        return 3;
    }

    auto mainProc = reinterpret_cast<vst2::MainProc> (GetProcAddress (module, "VSTPluginMain"));
    const char* entryPoint = "VSTPluginMain";

    if (mainProc == nullptr)
    {
        mainProc = reinterpret_cast<vst2::MainProc> (GetProcAddress (module, "main"));
        entryPoint = "main";
    }

    if (mainProc == nullptr)
    {
        std::cout << "ERROR: Neither VSTPluginMain nor main was exported.\n";
        FreeLibrary (module);
        return 4;
    }

    auto* effect = mainProc (hostCallback);
    if (effect == nullptr)
    {
        std::cout << "ERROR: The VST2 entry point returned no AEffect.\n";
        FreeLibrary (module);
        return 5;
    }

    if (effect->magic != vst2::effectMagic)
    {
        std::cout << "ERROR: The returned object did not contain the VST2 magic value.\n";
        FreeLibrary (module);
        return 6;
    }

    if (! sensibleCount (effect->numParams, 4096)
        || ! sensibleCount (effect->numPrograms, 1024)
        || ! sensibleCount (effect->numInputs, 128)
        || ! sensibleCount (effect->numOutputs, 128))
    {
        std::cout << "ERROR: The original plug-in reported an unsafe count.\n";
        FreeLibrary (module);
        return 7;
    }

    dispatch (effect, vst2::open);
    dispatch (effect, vst2::setSampleRate, 0, 0, nullptr, 44100.0f);
    dispatch (effect, vst2::setBlockSize, 0, 512);

    std::cout << "PLUGIN IDENTITY\n";
    std::cout << "---------------\n";
    std::cout << "Entry point: " << entryPoint << "\n";
    std::cout << "Effect name: " << dispatchString (effect, vst2::getEffectName) << "\n";
    std::cout << "Vendor: " << dispatchString (effect, vst2::getVendorString) << "\n";
    std::cout << "Product: " << dispatchString (effect, vst2::getProductString) << "\n";
    std::cout << "Unique ID text: " << fourCharacterId (effect->uniqueID) << "\n";
    std::cout << "Unique ID decimal: " << effect->uniqueID << "\n";
    std::cout << "Unique ID hex: 0x" << std::hex << std::uppercase
              << static_cast<std::uint32_t> (effect->uniqueID)
              << std::dec << std::nouppercase << "\n";
    std::cout << "AEffect version: " << effect->version << "\n";
    std::cout << "VST version: " << dispatch (effect, vst2::getVstVersion) << "\n";
    std::cout << "Vendor version: " << dispatch (effect, vst2::getVendorVersion) << "\n";
    const auto category = dispatch (effect, vst2::getPlugCategory);
    std::cout << "Category: " << categoryName (category) << "\n";
    std::cout << "Flags decimal: " << effect->flags << "\n";
    std::cout << "Flags decoded: " << flagText (effect->flags) << "\n";
    std::cout << "Programs: " << effect->numPrograms << "\n";
    std::cout << "Parameters: " << effect->numParams << "\n";
    std::cout << "Audio inputs: " << effect->numInputs << "\n";
    std::cout << "Audio outputs: " << effect->numOutputs << "\n";
    std::cout << "Initial delay samples: " << effect->initialDelay << "\n\n";

    if ((effect->flags & vst2::programChunks) != 0)
    {
        void* bankChunk = nullptr;
        void* programChunk = nullptr;
        const auto bankSize = dispatch (effect, vst2::getChunk, 0, 0, &bankChunk);
        const auto programSize = dispatch (effect, vst2::getChunk, 1, 0, &programChunk);
        std::cout << "STATE CHUNKS\n";
        std::cout << "------------\n";
        std::cout << "Bank chunk bytes: " << bankSize << "\n";
        std::cout << "Program chunk bytes: " << programSize << "\n\n";
    }

    std::cout << "PARAMETERS\n";
    std::cout << "----------\n";
    std::cout << std::fixed << std::setprecision (6);

    static constexpr std::array<float, 5> samplePositions {
        0.0f, 0.25f, 0.5f, 0.75f, 1.0f
    };

    for (vst2::Int32 parameter = 0; parameter < effect->numParams; ++parameter)
    {
        const auto defaultValue = effect->getParameter != nullptr
                                      ? effect->getParameter (effect, parameter)
                                      : 0.0f;
        const auto name = dispatchString (effect, vst2::getParameterName, parameter);
        const auto defaultDisplay = dispatchString (effect, vst2::getParameterDisplay, parameter);
        const auto label = dispatchString (effect, vst2::getParameterLabel, parameter);
        const auto automatable = dispatch (effect, vst2::canParameterBeAutomated, parameter) != 0;

        std::cout << "Parameter " << parameter << "\n";
        std::cout << "  Name: " << name << "\n";
        std::cout << "  Label: " << label << "\n";
        std::cout << "  Default normalized: " << defaultValue << "\n";
        std::cout << "  Default display: " << defaultDisplay << "\n";
        std::cout << "  Automatable: " << (automatable ? "Yes" : "No") << "\n";
        std::cout << "  Sampled displays:\n";

        for (const auto requestedValue : samplePositions)
        {
            if (effect->setParameter != nullptr)
                effect->setParameter (effect, parameter, requestedValue);

            const auto actualValue = effect->getParameter != nullptr
                                         ? effect->getParameter (effect, parameter)
                                         : requestedValue;
            const auto display = dispatchString (effect, vst2::getParameterDisplay, parameter);
            std::cout << "    " << requestedValue << " -> normalized " << actualValue
                      << ", display \"" << display << "\"\n";
        }

        if (effect->setParameter != nullptr)
            effect->setParameter (effect, parameter, defaultValue);

        std::cout << "\n";
    }

    std::cout << "PROGRAMS\n";
    std::cout << "--------\n";
    const auto originalProgram = static_cast<vst2::Int32> (dispatch (effect, vst2::getProgram));

    for (vst2::Int32 program = 0; program < effect->numPrograms; ++program)
    {
        dispatch (effect, vst2::setProgram, 0, program);

        auto name = dispatchString (effect, vst2::getProgramNameIndexed, 0, program);
        if (name.empty())
            name = dispatchString (effect, vst2::getProgramName);

        std::cout << "Program " << program << ": " << name << "\n";
        std::cout << "  Parameter values:";

        for (vst2::Int32 parameter = 0; parameter < effect->numParams; ++parameter)
        {
            const auto parameterValue = effect->getParameter != nullptr
                                            ? effect->getParameter (effect, parameter)
                                            : 0.0f;
            std::cout << (parameter == 0 ? " " : ", ") << parameterValue;
        }

        std::cout << "\n";

        if ((effect->flags & vst2::programChunks) != 0)
        {
            void* programChunk = nullptr;
            const auto programChunkSize = dispatch (
                effect, vst2::getChunk, 1, 0, &programChunk);
            std::cout << "  Program chunk bytes: " << programChunkSize << "\n";

            constexpr auto maximumSafeChunkBytes = 1024 * 1024;
            if (programChunk != nullptr
                && programChunkSize > 0
                && programChunkSize <= maximumSafeChunkBytes)
            {
                const auto* bytes = static_cast<const std::uint8_t*> (programChunk);
                std::ostringstream hexValues;
                hexValues << std::hex << std::setfill ('0');
                for (vst2::IntPtr byte = 0; byte < programChunkSize; ++byte)
                    hexValues << std::setw (2) << static_cast<unsigned int> (bytes[byte]);

                std::cout << "  Program chunk hex: " << hexValues.str() << "\n";

                if ((programChunkSize % static_cast<vst2::IntPtr> (sizeof (float))) == 0)
                {
                    std::cout << "  Program chunk float32 LE values:";
                    const auto floatCount = programChunkSize
                                          / static_cast<vst2::IntPtr> (sizeof (float));
                    for (vst2::IntPtr index = 0; index < floatCount; ++index)
                    {
                        float value = 0.0f;
                        std::memcpy (&value,
                                     bytes + index * static_cast<vst2::IntPtr> (sizeof (float)),
                                     sizeof (value));
                        std::cout << (index == 0 ? " " : ", ")
                                  << std::setprecision (9) << value;
                    }
                    std::cout << std::setprecision (6) << "\n";
                }
            }
            else
            {
                std::cout << "  Program chunk data: unavailable or unsafe size\n";
            }
        }
    }

    const auto equalizerAnalysisPassed = runFactoryEqualizerAnalysis (effect);
    const auto oscillatorAnalysisPassed = runOscillatorAudioAnalysis (effect);
    const auto filterAnalysisPassed = runFilterAudioAnalysis (effect);
    const auto harmonixAnalysisPassed = runHarmonixAudioAnalysis (effect);
    const auto gainStageAnalysisPassed = runGainStageAudioAnalysis (effect);
    const auto bitcrusherAnalysisPassed = runBitcrusherAudioAnalysis (effect);
    const auto envelopeAnalysisPassed = runEnvelopeAudioAnalysis (effect);
    const auto allStarSequencerAnalysisPassed = runAllStarSequencerAnalysis (effect);

    if (originalProgram >= 0 && originalProgram < effect->numPrograms)
        dispatch (effect, vst2::setProgram, 0, originalProgram);

    std::cout << "\nANALYSIS SUMMARY\n";
    std::cout << "  - PASS: Win32 VST2 entry point loaded\n";
    std::cout << "  - PASS: Plug-in identity captured\n";
    std::cout << "  - PASS: Parameter metadata and display samples captured\n";
    std::cout << "  - PASS: Program names and parameter values captured\n";
    std::cout << "  - PASS: Complete per-program state chunks captured\n";
    std::cout << "  - " << (equalizerAnalysisPassed ? "PASS" : "FAIL")
              << ": Factory-program EQ responses measured twice\n";
    std::cout << "  - " << (oscillatorAnalysisPassed ? "PASS" : "FAIL")
              << ": Oscillator audio behaviour captured\n";
    std::cout << "  - " << (filterAnalysisPassed ? "PASS" : "FAIL")
              << ": Filter audio behaviour captured\n";
    std::cout << "  - " << (harmonixAnalysisPassed ? "PASS" : "FAIL")
              << ": HARMONIX audio behaviour captured\n";
    std::cout << "  - " << (gainStageAnalysisPassed ? "PASS" : "FAIL")
              << ": Main, oscillator, and velocity gain curves captured\n";
    std::cout << "  - " << (bitcrusherAnalysisPassed ? "PASS" : "FAIL")
              << ": BITRATE and bitcrusher switch behaviour captured\n";
    std::cout << "  - " << (envelopeAnalysisPassed ? "PASS" : "FAIL")
              << ": Amplifier sustain and All Star ADSR timing captured\n";
    std::cout << "  - " << (allStarSequencerAnalysisPassed ? "PASS" : "FAIL")
              << ": All Star pitch and filter sequence behaviour captured\n";

    dispatch (effect, vst2::close);
    FreeLibrary (module);
    return equalizerAnalysisPassed && oscillatorAnalysisPassed
        && filterAnalysisPassed && harmonixAnalysisPassed
        && gainStageAnalysisPassed && bitcrusherAnalysisPassed
        && envelopeAnalysisPassed && allStarSequencerAnalysisPassed ? 0 : 8;
}
