#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

class IceCreamAudioProcessor final : public juce::AudioProcessor
{
public:
    IceCreamAudioProcessor();
    ~IceCreamAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameterState() noexcept
    {
        return parameters;
    }

    int getSequencerActiveStep() const noexcept
    {
        return sequencerActiveStep.load();
    }

    juce::MidiKeyboardState& getKeyboardState() noexcept
    {
        return keyboardState;
    }

private:
    static constexpr int tableLength = 2048;
    static constexpr int tableCount = 256;
    static constexpr int maximumVoices = 16;

    enum class EnvelopeStage
    {
        attack,
        decay,
        sustain,
        release
    };

    struct Voice
    {
        int note = -1;
        bool active = false;
        float velocity = 0.0f;
        float envelope = 0.0f;
        float releaseStep = 0.0f;
        EnvelopeStage envelopeStage = EnvelopeStage::attack;
        float filterEnvelope = 0.0f;
        float filterEnvelopeReleaseStep = 0.0f;
        EnvelopeStage filterEnvelopeStage = EnvelopeStage::attack;
        double currentMidiNote = 69.0;
        double targetMidiNote = 69.0;
        double phase = 0.0;
        double phaseDelta = 0.0;
        double phase2 = 0.0;
        double phaseDelta2 = 0.0;
        double harmonixPhase1 = 0.0;
        double harmonixPhase2 = 0.0;
        double characterPhase1 = 0.0;
        double characterPhase2 = 0.0;
        double characterWavePhase1 = 0.0;
        double characterWavePhase2 = 0.0;
        float crusherHeldSample1 = 0.0f;
        float crusherHeldSample2 = 0.0f;
        int crusherSamplesUntilRefresh1 = 0;
        int crusherSamplesUntilRefresh2 = 0;
        double filterState1 = 0.0;
        double filterState2 = 0.0;
        double filterG = 0.0;
        double filterK = 2.0;
        double filterTrackingRatio = 1.0;
        int filterType = 0;
        std::uint32_t noiseState1 = 0x13579bdu;
        std::uint32_t noiseState2 = 0x2468aceu;
        std::uint32_t characterNoiseState1 = 0x13579bdu;
        std::uint32_t characterNoiseState2 = 0x2468aceu;
        std::uint64_t age = 0;
    };

    using ParameterTree = juce::AudioProcessorValueTreeState;

    static ParameterTree::ParameterLayout createParameterLayout();

    void loadEmbeddedWavetables();
    void resetVoices();
    void startVoice (int midiNote, float velocity);
    void releaseVoice (int midiNote);
    void beginRelease (Voice& voice);
    void handleMidiMessage (const juce::MidiMessage& message);
    void renderRange (juce::AudioBuffer<float>& buffer, int startSample, int endSample);
    void processDelay (juce::AudioBuffer<float>& buffer);
    void processReverb (juce::AudioBuffer<float>& buffer);
    void processEqualizer (juce::AudioBuffer<float>& buffer);
    void resetEffects();
    bool advanceAmpEnvelope (Voice& voice,
                             float attackSeconds,
                             float decaySeconds,
                             float sustainLevel) const noexcept;
    void advanceFilterEnvelope (Voice& voice,
                                float attackSeconds,
                                float decaySeconds,
                                float sustainLevel) const noexcept;
    float readTable (const std::vector<float>& tables,
                     double phase,
                     double phaseDelta) const noexcept;
    float readWaveform (float normalizedSelector,
                        double phase,
                        double phaseDelta,
                        std::uint32_t& noiseState) const noexcept;
    float processVoiceFilter (Voice& voice, float input) const noexcept;
    static float nextNoiseSample (std::uint32_t& state) noexcept;
    static float envelopeTimeSeconds (float normalizedValue) noexcept;

    ParameterTree parameters;
    juce::MidiKeyboardState keyboardState;
    std::atomic<float>* ampAttack = nullptr;
    std::atomic<float>* ampDecay = nullptr;
    std::atomic<float>* ampRelease = nullptr;
    std::atomic<float>* ampSustain = nullptr;
    std::atomic<float>* mainVolume = nullptr;
    std::atomic<float>* oscillator1Volume = nullptr;
    std::atomic<float>* oscillator2Volume = nullptr;
    std::atomic<float>* oscillator2Frequency = nullptr;
    std::atomic<float>* oscillator1Octave = nullptr;
    std::atomic<float>* oscillator2Octave = nullptr;
    std::atomic<float>* oscillator1Waveform = nullptr;
    std::atomic<float>* oscillator2Waveform = nullptr;
    std::atomic<float>* bitCrusherOscillator1On = nullptr;
    std::atomic<float>* bitCrusherOscillator2On = nullptr;
    std::atomic<float>* bitCrusherAmount = nullptr;
    std::atomic<float>* bitCrusherBits = nullptr;
    std::atomic<float>* polyMode = nullptr;
    std::atomic<float>* glideOn = nullptr;
    std::atomic<float>* glideRate = nullptr;
    std::atomic<float>* harmonixOn = nullptr;
    std::atomic<float>* characterAmount = nullptr;
    std::atomic<float>* delayMix = nullptr;
    std::atomic<float>* delayOn = nullptr;
    std::atomic<float>* delayTime = nullptr;
    std::atomic<float>* delayFeedback = nullptr;
    std::atomic<float>* reverbDamping = nullptr;
    std::atomic<float>* reverbMix = nullptr;
    std::atomic<float>* reverbOn = nullptr;
    std::atomic<float>* reverbRoomSize = nullptr;
    std::atomic<float>* reverbWidth = nullptr;
    std::array<std::atomic<float>*, 8> equalizerGains {};
    std::atomic<float>* filterEnvelopeAmount = nullptr;
    std::atomic<float>* filterEnvelopeAttack = nullptr;
    std::atomic<float>* filterEnvelopeDecay = nullptr;
    std::atomic<float>* filterEnvelopeRelease = nullptr;
    std::atomic<float>* filterEnvelopeSustain = nullptr;
    std::atomic<float>* filterCutoff = nullptr;
    std::atomic<float>* filterResonance = nullptr;
    std::atomic<float>* filterTracking = nullptr;
    std::atomic<float>* filterType = nullptr;
    std::atomic<float>* sequencerPitchOn = nullptr;
    std::atomic<float>* sequencerFilterOn = nullptr;
    std::atomic<float>* sequencerSmooth = nullptr;
    std::atomic<float>* sequencerSmoothAttack = nullptr;
    std::atomic<float>* sequencerRate = nullptr;
    std::atomic<float>* sequencerFreeRunning = nullptr;
    std::array<std::atomic<float>*, 16> sequencerSteps {};

    std::array<Voice, maximumVoices> voices;
    std::vector<float> sawTables;
    std::vector<float> triangleTables;
    std::vector<float> delayBufferLeft;
    std::vector<float> delayBufferRight;
    std::size_t delayWritePosition = 0;
    bool delayWasEnabled = false;
    bool reverbWasEnabled = false;
    juce::Reverb reverbProcessor;
    std::array<juce::IIRFilter, 8> equalizerLeft;
    std::array<juce::IIRFilter, 8> equalizerRight;

    double currentSampleRate = 44100.0;
    double currentTempoBpm = 120.0;
    double sequencerPhase = 0.0;
    std::atomic<int> sequencerActiveStep { 0 };
    std::array<bool, 128> heldNotes {};
    std::array<int, 128> heldNoteOrder {};
    int heldNoteCount = 0;
    std::uint64_t nextVoiceAge = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IceCreamAudioProcessor)
};
