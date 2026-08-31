#pragma once

#include <JuceHeader.h>

#include <memory>

class IceCreamAudioProcessor;

class IceCreamAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit IceCreamAudioProcessorEditor (IceCreamAudioProcessor& processor);
    ~IceCreamAudioProcessorEditor() override;

    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void scheduleSettingsSave();

    struct Content;
    std::unique_ptr<Content> content;
    bool modernDarkTheme = false;
    float currentScale = 1.0f;
    bool settingsReady = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IceCreamAudioProcessorEditor)
};
