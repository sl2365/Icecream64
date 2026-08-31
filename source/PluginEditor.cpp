#include "PluginEditor.h"

#include "PluginProcessor.h"

#include <BinaryData.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

namespace
{
constexpr int editorWidth = 860;
constexpr int editorHeight = 440;
constexpr float controlLabelFontSize = 8.0f;
constexpr float parameterLabelFontSize = 9.5f;
constexpr float sectionBorderThickness = 2.2f;

juce::File getPortableSequencerDirectory()
{
#if JUCE_WINDOWS
    static int moduleAnchor = 0;
    HMODULE module = nullptr;
    const auto address = reinterpret_cast<LPCWSTR> (static_cast<void*> (&moduleAnchor));

    if (GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            address,
                            &module) != 0)
    {
        std::array<wchar_t, 32768> modulePath {};
        const auto length = GetModuleFileNameW (
            module, modulePath.data(), static_cast<DWORD> (modulePath.size()));

        if (length > 0 && length < modulePath.size())
        {
            return juce::File (juce::String (modulePath.data()))
                .getParentDirectory()
                .getChildFile ("Data")
                .getChildFile ("Seq");
        }
    }
#endif

    return juce::File::getSpecialLocation (juce::File::currentApplicationFile)
        .getParentDirectory()
        .getChildFile ("Data")
        .getChildFile ("Seq");
}

juce::File getPortablePresetDirectory()
{
    return getPortableSequencerDirectory()
        .getParentDirectory()
        .getChildFile ("Presets");
}

juce::File getPortableSettingsFile()
{
    return getPortableSequencerDirectory()
        .getParentDirectory()
        .getChildFile ("Settings.ini");
}

struct InterfaceSettings
{
    bool modernDarkTheme = false;
    float scale = 1.0f;
};

InterfaceSettings loadInterfaceSettings()
{
    InterfaceSettings settings;
    const auto settingsFile = getPortableSettingsFile();
    if (! settingsFile.existsAsFile())
        return settings;

    juce::StringArray lines;
    lines.addLines (settingsFile.loadFileAsString());
    bool inInterfaceSection = false;
    for (auto line : lines)
    {
        line = line.trim();
        if (line.startsWithChar ('['))
        {
            inInterfaceSection = line.equalsIgnoreCase ("[Interface]");
            continue;
        }

        if (! inInterfaceSection || line.isEmpty()
            || line.startsWithChar (';') || line.startsWithChar ('#'))
        {
            continue;
        }

        const auto separator = line.indexOfChar ('=');
        if (separator <= 0)
            continue;

        const auto key = line.substring (0, separator).trim();
        const auto value = line.substring (separator + 1).trim();
        if (key.equalsIgnoreCase ("Theme"))
        {
            settings.modernDarkTheme = value.equalsIgnoreCase ("ModernDark")
                                      || value.equalsIgnoreCase ("Dark")
                                      || value == "1";
        }
        else if (key.equalsIgnoreCase ("ZoomPercent"))
        {
            const auto percent = static_cast<float> (value.getDoubleValue());
            if (percent >= 75.0f && percent <= 200.0f)
                settings.scale = percent / 100.0f;
        }
    }

    return settings;
}

bool writeInterfaceSettings (bool modernDarkTheme, float scale)
{
    const auto settingsFile = getPortableSettingsFile();
    const auto directoryResult = settingsFile.getParentDirectory().createDirectory();
    if (directoryResult.failed())
        return false;

    juce::String contents { "; IceCream interface settings\r\n" };
    contents << "[Interface]\r\n";
    contents << "Theme=" << (modernDarkTheme ? "ModernDark" : "ClassicLight")
             << "\r\n";
    contents << "ZoomPercent="
             << juce::String (juce::jlimit (0.75f, 2.0f, scale) * 100.0f, 2)
             << "\r\n";
    return settingsFile.replaceWithText (contents);
}

const juce::Identifier knobStyleProperty { "icecreamKnobStyle" };
const juce::Identifier eqSliderStyleProperty { "icecreamEqSliderStyle" };
const juce::Identifier compactFilterTypeProperty { "icecreamCompactFilterType" };

void setPointingHandCursorRecursively (juce::Component& component)
{
    component.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    for (auto* child : component.getChildren())
        setPointingHandCursorRecursively (*child);
}

void applyPointingHandCursors (juce::Component& component)
{
    for (auto* child : component.getChildren())
    {
        if (dynamic_cast<juce::Button*> (child) != nullptr
            || dynamic_cast<juce::Slider*> (child) != nullptr
            || dynamic_cast<juce::ComboBox*> (child) != nullptr)
        {
            setPointingHandCursorRecursively (*child);
        }
        else
        {
            applyPointingHandCursors (*child);
        }
    }
}

void setOriginalKnobStyle (juce::Slider& slider,
                           const juce::String& style,
                           juce::Colour faceColour)
{
    slider.getProperties().set (knobStyleProperty, juce::var { style });
    slider.setColour (juce::Slider::rotarySliderFillColourId, faceColour);
}

void applyOriginalParameterKnobStyle (juce::Slider& slider,
                                      const juce::String& parameterID)
{
    if (parameterID == "p31_osc2_osc1" || parameterID == "p32_osc2_rate")
        setOriginalKnobStyle (slider, "oscillator", juce::Colour (0xff43aa72));
    else if (parameterID == "p26_osc1_volume" || parameterID == "p27_osc2_volume")
        setOriginalKnobStyle (slider, "oscillator", juce::Colour (0xff347fbd));
    else if (parameterID == "p29_osc1_octave" || parameterID == "p30_osc2_octave")
        setOriginalKnobStyle (slider, "oscillator", juce::Colour (0xffcad51e));
    else if (parameterID == "p28_osc2_frequency")
        setOriginalKnobStyle (slider, "oscillator", juce::Colour (0xffc96c3b));
    else if (parameterID == "p16_filter_cutoff" || parameterID == "p17_filter_res")
        setOriginalKnobStyle (slider, "filter", juce::Colour (0xff343433));
    else if (parameterID == "p18_filter_tracking")
        setOriginalKnobStyle (slider, "filterTrack", juce::Colour (0xff4d8051));
    else if (parameterID == "p00_amp_attack")
        setOriginalKnobStyle (slider, "amp", juce::Colour (0xffdb3329));
    else if (parameterID == "p01_amp_decay")
        setOriginalKnobStyle (slider, "amp", juce::Colour (0xffddc829));
    else if (parameterID == "p03_amp_sustain")
        setOriginalKnobStyle (slider, "amp", juce::Colour (0xff63b956));
    else if (parameterID == "p02_amp_release")
        setOriginalKnobStyle (slider, "amp", juce::Colour (0xff4a9fc5));
    else if (parameterID == "p12_filter_env_attack"
             || parameterID == "p13_filter_env_decay"
             || parameterID == "p15_filter_env_sustain"
             || parameterID == "p14_filter_env_release"
             || parameterID == "p11_filter_env_amount")
        setOriginalKnobStyle (slider, "filterEnvelope", juce::Colour (0xffd7d9d7));
    else if (parameterID == "p21_glide_rate" || parameterID == "p44_character")
        setOriginalKnobStyle (slider, "control", juce::Colour (0xff3d96bd));
    else if (parameterID == "p36_reverb_room")
        setOriginalKnobStyle (slider, "effectColour", juce::Colour (0xffff2100));
    else if (parameterID == "p33_reverb_damp")
        setOriginalKnobStyle (slider, "effectColour", juce::Colour (0xffffd500));
    else if (parameterID == "p37_reverb_width")
        setOriginalKnobStyle (slider, "effectColour", juce::Colour (0xff98ff00));
    else if (parameterID == "p34_reverb_mix")
        setOriginalKnobStyle (slider, "effectColour", juce::Colour (0xff00f59b));
    else if (parameterID == "p09_delay_time")
        setOriginalKnobStyle (slider, "effectColour", juce::Colour (0xffffd500));
    else if (parameterID == "p10_delay_feedback")
        setOriginalKnobStyle (slider, "effectColour", juce::Colour (0xff98ff00));
    else if (parameterID == "p07_delay_mix")
        setOriginalKnobStyle (slider, "effectColour", juce::Colour (0xff00f59b));
    else if (parameterID == "p24_main_volume")
        setOriginalKnobStyle (slider, "master", juce::Colour (0xfff25a24));
}

class DevelopmentLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DevelopmentLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, juce::Colours::black);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    void setModernDarkTheme (bool enabled) noexcept
    {
        modernDarkTheme = enabled;
    }

    bool isModernDarkTheme() const noexcept
    {
        return modernDarkTheme;
    }

    void drawRotarySlider (juce::Graphics& graphics,
                           int x,
                           int y,
                           int width,
                           int height,
                           float sliderPosition,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        const auto size = static_cast<float> (std::min (width, height));
        const auto centreX = static_cast<float> (x) + 0.5f * static_cast<float> (width);
        auto centreY = static_cast<float> (y) + 0.5f * static_cast<float> (height);
        const auto angle = rotaryStartAngle
                         + sliderPosition * (rotaryEndAngle - rotaryStartAngle);
        const auto style = slider.getProperties()
                               .getWithDefault (knobStyleProperty,
                                                juce::var { "modern" })
                               .toString();
        if (style == "filter")
            centreY += 6.0f;
        const auto faceColour = slider.findColour (
            juce::Slider::rotarySliderFillColourId);
        const auto themedTickColour = [this] (juce::Colour lightThemeColour)
        {
            return modernDarkTheme ? juce::Colour (0xffe2e6ea)
                                   : lightThemeColour;
        };

        const auto ellipse = [centreX, centreY] (float radius)
        {
            return juce::Rectangle<float> {
                centreX - radius, centreY - radius,
                radius * 2.0f, radius * 2.0f
            };
        };

        const auto drawPointer = [&] (float radius,
                                      juce::Colour colour,
                                      float thickness,
                                      float innerScale = 0.12f,
                                      float outerScale = 0.76f)
        {
            const auto directionX = std::sin (angle);
            const auto directionY = -std::cos (angle);
            graphics.setColour (colour);
            graphics.drawLine (
                centreX + directionX * radius * innerScale,
                centreY + directionY * radius * innerScale,
                centreX + directionX * radius * outerScale,
                centreY + directionY * radius * outerScale,
                thickness);
        };

        const auto drawFace = [&] (float radius,
                                   juce::Colour colour,
                                   juce::Colour rimColour)
        {
            const auto outer = ellipse (radius);
            graphics.setColour (juce::Colours::black.withAlpha (0.28f));
            graphics.fillEllipse (outer.translated (1.0f, 1.4f));
            graphics.setColour (rimColour);
            graphics.fillEllipse (outer);

            const auto inner = outer.reduced (juce::jmax (1.1f, radius * 0.11f));
            juce::ColourGradient gradient (
                colour.brighter (0.42f), inner.getX(), inner.getY(),
                colour.darker (0.34f), inner.getRight(), inner.getBottom(), false);
            gradient.addColour (0.48, colour);
            graphics.setGradientFill (gradient);
            graphics.fillEllipse (inner);
            graphics.setColour (juce::Colours::white.withAlpha (0.34f));
            graphics.drawEllipse (inner.reduced (0.6f), 0.9f);
        };

        const auto drawTicks = [&] (float outerRadius,
                                    float tickLength,
                                    int count,
                                    juce::Colour colour,
                                    float thickness)
        {
            graphics.setColour (colour);
            for (int mark = 0; mark < count; ++mark)
            {
                const auto proportion = count > 1
                    ? static_cast<float> (mark) / static_cast<float> (count - 1)
                    : 0.0f;
                const auto tickAngle = rotaryStartAngle
                                     + proportion * (rotaryEndAngle - rotaryStartAngle);
                const auto directionX = std::sin (tickAngle);
                const auto directionY = -std::cos (tickAngle);
                graphics.drawLine (
                    centreX + directionX * (outerRadius - tickLength),
                    centreY + directionY * (outerRadius - tickLength),
                    centreX + directionX * outerRadius,
                    centreY + directionY * outerRadius,
                    thickness);
            }
        };

        const auto drawLamp = [&] (float offsetX,
                                   float offsetY,
                                   juce::Colour colour,
                                   float lampRadius,
                                   float threshold)
        {
            constexpr auto fadeWidth = 0.13f;
            const auto lampLevel = juce::jlimit (
                0.0f, 1.0f,
                (sliderPosition - (threshold - fadeWidth)) / fadeWidth);
            const auto litColour = colour.darker (
                3.0f * (1.0f - lampLevel));
            const auto bounds = juce::Rectangle<float> {
                centreX + offsetX - lampRadius,
                centreY + offsetY - lampRadius,
                lampRadius * 2.0f,
                lampRadius * 2.0f
            };
            graphics.setColour (juce::Colour (0xff3a3028));
            graphics.fillEllipse (bounds.expanded (1.0f));
            juce::ColourGradient lampGradient (
                litColour.brighter (0.65f), bounds.getX(), bounds.getY(),
                litColour.darker (0.2f), bounds.getRight(), bounds.getBottom(), false);
            graphics.setGradientFill (lampGradient);
            graphics.fillEllipse (bounds);
            graphics.setColour (juce::Colours::white.withAlpha (
                0.03f + 0.51f * lampLevel));
            graphics.fillEllipse (bounds.getX() + lampRadius * 0.35f,
                                  bounds.getY() + lampRadius * 0.25f,
                                  lampRadius * 0.55f,
                                  lampRadius * 0.55f);
        };

        if (style == "oscillator" || style == "control" || style == "crusher")
        {
            const auto outerRadius = juce::jmax (6.0f, size * 0.5f - 1.0f);
            const auto controlStyle = style == "control";
            const auto crusherStyle = style == "crusher";
            const auto tickLength = controlStyle ? 3.1f : (crusherStyle ? 3.5f : 4.3f);
            const auto tickCount = controlStyle ? 14 : (crusherStyle ? 13 : 16);
            drawTicks (outerRadius, tickLength, tickCount,
                       themedTickColour (juce::Colour (0xff202225)),
                       crusherStyle ? 2.0f : 2.2f);

            const auto faceRadius = juce::jmax (
                4.0f, outerRadius - (controlStyle ? 4.4f : 5.8f));
            drawFace (faceRadius, faceColour, juce::Colour (0xff34383a));
            drawPointer (faceRadius, juce::Colour (0xfffff4cf),
                         juce::jmax (1.4f, faceRadius * 0.15f));
            return;
        }

        if (style == "filter")
        {
            const auto outerRadius = juce::jmax (7.0f, size * 0.5f - 1.0f);
            constexpr auto lampRadius = 4.0f;
            const auto lampOffset = outerRadius + 1.5f;
            const auto pi = juce::MathConstants<float>::pi;
            const std::array<float, 7> lampAngles {
                1.5f * pi, 5.0f * pi / 3.0f, 11.0f * pi / 6.0f,
                2.0f * pi, 13.0f * pi / 6.0f, 7.0f * pi / 3.0f,
                2.5f * pi
            };
            const std::array<juce::Colour, 7> lampColours {
                juce::Colour (0xffff1c27), juce::Colour (0xffff9f05),
                juce::Colour (0xffffdf18), juce::Colour (0xff3bdb2d),
                juce::Colour (0xff16d5e8), juce::Colour (0xff5148db),
                juce::Colour (0xffa83ee8)
            };
            for (std::size_t lamp = 0; lamp < lampAngles.size(); ++lamp)
            {
                const auto lampAngle = lampAngles[lamp];
                const auto threshold = juce::jlimit (
                    0.0f, 1.0f,
                    (lampAngle - rotaryStartAngle)
                        / (rotaryEndAngle - rotaryStartAngle));
                drawLamp (std::sin (lampAngle) * lampOffset,
                          -std::cos (lampAngle) * lampOffset,
                          lampColours[lamp], lampRadius, threshold);
            }

            const auto faceRadius = juce::jmax (5.0f, outerRadius - 8.7f);
            drawFace (faceRadius, juce::Colour (0xff383836),
                      juce::Colour (0xffb5aaa0));
            drawPointer (faceRadius, juce::Colour (0xfff2eee3),
                         juce::jmax (1.8f, faceRadius * 0.14f), 0.05f, 0.8f);
            return;
        }

        if (style == "filterTrack")
        {
            const auto outerRadius = juce::jmax (6.0f, size * 0.5f - 1.0f);
            drawTicks (outerRadius, 2.5f, 11,
                       themedTickColour (juce::Colour (0xff34322e)), 1.4f);
            const auto radius = juce::jmax (5.0f, outerRadius - 3.2f);
            drawFace (radius, juce::Colour (0xff424247),
                      juce::Colour (0xff8b8885));
            drawPointer (radius, faceColour.brighter (0.55f),
                         juce::jmax (1.5f, radius * 0.14f));
            return;
        }

        if (style == "amp")
        {
            const auto outerRadius = juce::jmax (6.0f, size * 0.5f - 1.0f);
            drawTicks (outerRadius, 2.7f, 12,
                       themedTickColour (juce::Colour (0xff303235)), 1.5f);
            const auto radius = juce::jmax (5.0f, outerRadius - 3.5f);
            drawFace (radius, faceColour, juce::Colour (0xff3c3d3d));
            drawPointer (radius, juce::Colour (0xff262728),
                         juce::jmax (1.4f, radius * 0.14f), 0.15f, 0.75f);
            return;
        }

        if (style == "filterEnvelope")
        {
            const auto outerRadius = juce::jmax (6.0f, size * 0.5f - 1.0f);
            drawTicks (outerRadius, 2.7f, 12,
                       themedTickColour (juce::Colour (0xff5b5d5e)), 1.5f);
            const auto radius = juce::jmax (5.0f, outerRadius - 3.5f);
            drawFace (radius, juce::Colour (0xffd8dad8),
                      juce::Colour (0xff777b7c));
            drawPointer (radius, juce::Colour (0xff55595a),
                         juce::jmax (1.3f, radius * 0.13f), 0.12f, 0.72f);
            return;
        }

        if (style == "metal")
        {
            const auto radius = juce::jmax (5.0f, size * 0.5f - 4.2f);
            drawFace (radius, faceColour, juce::Colour (0xff777b7c));
            drawPointer (radius, juce::Colour (0xff55595a),
                         juce::jmax (1.3f, radius * 0.13f), 0.12f, 0.72f);
            return;
        }

        if (style == "effect" || style == "effectColour")
        {
            const auto outerRadius = juce::jmax (6.0f, size * 0.5f - 1.0f);
            drawTicks (outerRadius, 2.5f, 11,
                       themedTickColour (juce::Colour (0xff34322e)), 1.4f);
            const auto radius = juce::jmax (5.0f, outerRadius - 3.2f);
            drawFace (radius, juce::Colour (0xff4a463e),
                      juce::Colour (0xff777268));
            const auto pointerColour = style == "effectColour"
                ? faceColour
                : faceColour.brighter (0.55f);
            const auto pointerThickness = style == "effectColour"
                ? juce::jmax (1.7f, radius * 0.17f)
                : juce::jmax (1.4f, radius * 0.14f);
            drawPointer (radius, pointerColour,
                         pointerThickness, 0.2f, 0.72f);
            return;
        }

        if (style == "master")
        {
            const auto outerRadius = juce::jmax (7.0f, size * 0.5f - 1.0f);
            graphics.setColour (juce::Colours::black.withAlpha (0.32f));
            graphics.fillEllipse (ellipse (outerRadius).translated (1.2f, 1.5f));
            graphics.setColour (juce::Colour (0xff30383b));
            graphics.fillEllipse (ellipse (outerRadius));

            const auto inner = ellipse (outerRadius - 3.2f);
            juce::ColourGradient masterGradient (
                faceColour.brighter (0.58f), inner.getX(), inner.getY(),
                faceColour.darker (0.28f), inner.getRight(), inner.getBottom(), false);
            masterGradient.addColour (0.48, faceColour);
            graphics.setGradientFill (masterGradient);
            graphics.fillEllipse (inner);
            graphics.setColour (juce::Colours::white.withAlpha (0.45f));
            graphics.drawEllipse (inner.reduced (0.7f), 1.1f);
            graphics.fillEllipse (inner.getX() + inner.getWidth() * 0.23f,
                                  inner.getY() + inner.getHeight() * 0.14f,
                                  inner.getWidth() * 0.2f,
                                  inner.getHeight() * 0.11f);
            drawPointer (outerRadius - 3.2f, juce::Colour (0xff7b2618),
                         juce::jmax (1.8f, outerRadius * 0.1f), 0.28f, 0.76f);
            return;
        }

        const auto radius = size * 0.5f - 3.0f;

        const auto knobBounds = juce::Rectangle<float> {
            centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f
        };

        graphics.setColour (juce::Colour (0xff2f3438));
        graphics.fillEllipse (knobBounds);
        graphics.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
        graphics.drawEllipse (knobBounds.reduced (1.0f), 2.0f);

        juce::Path pointer;
        const auto pointerLength = radius * 0.66f;
        const auto pointerThickness = juce::jmax (1.5f, radius * 0.12f);
        pointer.addRoundedRectangle (-pointerThickness * 0.5f,
                                     -radius + 4.0f,
                                     pointerThickness,
                                     pointerLength,
                                     pointerThickness * 0.5f);
        pointer.applyTransform (juce::AffineTransform::rotation (angle)
                                    .translated (centreX, centreY));
        graphics.fillPath (pointer);
    }

    void drawLinearSlider (juce::Graphics& graphics,
                           int x,
                           int y,
                           int width,
                           int height,
                           float sliderPosition,
                           float minimumSliderPosition,
                           float maximumSliderPosition,
                           const juce::Slider::SliderStyle style,
                           juce::Slider& slider) override
    {
        const auto originalEqStyle = static_cast<bool> (
            slider.getProperties().getWithDefault (
                eqSliderStyleProperty, juce::var { false }));
        if (! originalEqStyle || style != juce::Slider::LinearVertical)
        {
            juce::LookAndFeel_V4::drawLinearSlider (
                graphics, x, y, width, height, sliderPosition,
                minimumSliderPosition, maximumSliderPosition, style, slider);
            return;
        }

        const auto centreX = static_cast<float> (x) + 0.5f * width;
        const auto top = static_cast<float> (y) + 0.5f;
        const auto bottom = static_cast<float> (y + height) - 0.5f;
        graphics.setColour (slider.findColour (
            juce::Slider::backgroundColourId));
        graphics.drawLine (centreX, top, centreX, bottom, 1.5f);
        graphics.setColour (slider.findColour (
            juce::Slider::trackColourId));
        graphics.drawLine (centreX, sliderPosition, centreX, bottom, 2.0f);

        const auto handleWidth = juce::jlimit (
            8.0f, 12.0f, static_cast<float> (width) - 2.0f);
        constexpr auto handleHeight = 8.0f;
        const auto handle = juce::Rectangle<float> {
            centreX - handleWidth * 0.5f,
            sliderPosition - handleHeight * 0.5f,
            handleWidth,
            handleHeight
        };

        graphics.setColour (juce::Colours::black.withAlpha (0.28f));
        graphics.fillRoundedRectangle (handle.translated (0.8f, 1.0f), 1.5f);
        juce::ColourGradient handleGradient (
            juce::Colour (0xfffafaf4), handle.getX(), handle.getY(),
            juce::Colour (0xffbfc2c1), handle.getX(), handle.getBottom(), false);
        graphics.setGradientFill (handleGradient);
        graphics.fillRoundedRectangle (handle, 1.5f);
        graphics.setColour (juce::Colour (0xff686d6e));
        graphics.drawRoundedRectangle (handle.reduced (0.5f), 1.5f, 1.0f);
        graphics.setColour (juce::Colours::white.withAlpha (0.72f));
        graphics.drawHorizontalLine (juce::roundToInt (handle.getY() + 1.5f),
                                     handle.getX() + 2.0f,
                                     handle.getRight() - 2.0f);
        graphics.setColour (juce::Colour (0xff8a8e8e));
        graphics.drawHorizontalLine (juce::roundToInt (handle.getCentreY()),
                                     handle.getX() + 2.0f,
                                     handle.getRight() - 2.0f);
    }

    void drawToggleButton (juce::Graphics& graphics,
                           juce::ToggleButton& button,
                           bool,
                           bool) override
    {
        const auto diameter = static_cast<float> (
            juce::jmin (button.getWidth(), button.getHeight()) - 4);
        const auto bounds = juce::Rectangle<float> {
            0.5f * (static_cast<float> (button.getWidth()) - diameter),
            0.5f * (static_cast<float> (button.getHeight()) - diameter),
            diameter,
            diameter
        };

        if (button.getToggleState())
        {
            juce::ColourGradient redGradient (
                juce::Colour (0xffef3338), bounds.getX(), bounds.getY(),
                juce::Colour (0xffa20d14), bounds.getRight(), bounds.getBottom(),
                false);
            redGradient.addColour (0.48, juce::Colour (0xffd71920));
            graphics.setGradientFill (redGradient);
            graphics.fillEllipse (bounds);
        }
        else
        {
            graphics.setColour (juce::Colour (0xff69747c));
            graphics.fillEllipse (bounds);
        }
        graphics.setColour (juce::Colour (0xff25282b));
        graphics.drawEllipse (bounds, 1.5f);
    }

    void drawComboBox (juce::Graphics& graphics,
                       int width,
                       int height,
                       bool isButtonDown,
                       int buttonX,
                       int buttonY,
                       int buttonWidth,
                       int buttonHeight,
                       juce::ComboBox& box) override
    {
        const auto compact = static_cast<bool> (
            box.getProperties().getWithDefault (
                compactFilterTypeProperty, juce::var { false }));
        if (! compact)
        {
            juce::LookAndFeel_V4::drawComboBox (
                graphics, width, height, isButtonDown,
                buttonX, buttonY, buttonWidth, buttonHeight, box);
            return;
        }

        const auto bounds = juce::Rectangle<float> {
            0.5f, 0.5f, static_cast<float> (width) - 1.0f,
            static_cast<float> (height) - 1.0f
        };
        auto background = box.findColour (juce::ComboBox::backgroundColourId);
        if (isButtonDown)
            background = background.darker (0.12f);
        else if (box.isMouseOver (true))
            background = background.brighter (0.12f);
        graphics.setColour (background);
        graphics.fillRoundedRectangle (bounds, 3.0f);
        auto outline = box.findColour (juce::ComboBox::outlineColourId);
        if (box.isMouseOver (true))
            outline = outline.brighter (0.28f);
        graphics.setColour (outline);
        graphics.drawRoundedRectangle (bounds, 3.0f, 1.0f);
    }

    void positionComboBoxText (juce::ComboBox& box,
                               juce::Label& label) override
    {
        const auto compact = static_cast<bool> (
            box.getProperties().getWithDefault (
                compactFilterTypeProperty, juce::var { false }));
        if (! compact)
        {
            juce::LookAndFeel_V4::positionComboBoxText (box, label);
            return;
        }

        label.setBounds (4, 1, juce::jmax (1, box.getWidth() - 8),
                         juce::jmax (1, box.getHeight() - 2));
        label.setFont (juce::FontOptions { parameterLabelFontSize,
                                           juce::Font::bold });
        label.setJustificationType (juce::Justification::centred);
        label.setMinimumHorizontalScale (1.0f);
    }

private:
    bool modernDarkTheme = false;
};

bool isModernDarkTheme (const juce::Component& component)
{
    const auto* lookAndFeel = dynamic_cast<const DevelopmentLookAndFeel*> (
        &component.getLookAndFeel());
    return lookAndFeel != nullptr && lookAndFeel->isModernDarkTheme();
}

juce::Colour themedTextColour (
    const juce::Component& component,
    juce::Colour classicColour = juce::Colour (0xff17191b))
{
    return isModernDarkTheme (component) ? juce::Colour (0xffedf1f4)
                                         : classicColour;
}

void repaintRecursively (juce::Component& component)
{
    component.repaint();
    for (auto* child : component.getChildren())
        repaintRecursively (*child);
}

std::unique_ptr<juce::Drawable> loadDrawableResource (const char* resourceName)
{
    int dataSize = 0;
    const auto* data = BinaryData::getNamedResource (resourceName, dataSize);
    if (data == nullptr || dataSize <= 0)
        return {};

    return juce::Drawable::createFromImageData (data, static_cast<std::size_t> (dataSize));
}

juce::Colour headerPixelColour (char code, bool modernDark)
{
    if (code == 'K')
        return modernDark ? juce::Colours::white : juce::Colours::black;
    if (code == 'W')
        return modernDark ? juce::Colours::black : juce::Colours::white;
    if (code == 'P') return juce::Colour (0xfff316a5);
    if (code == 'R') return juce::Colour (0xfff12624);
    if (code == 'O') return juce::Colour (0xffff8b16);
    if (code == 'Y') return juce::Colour (0xffffe629);
    if (code == 'G') return juce::Colour (0xff28d74d);
    if (code == 'C') return juce::Colour (0xff00dce6);
    if (code == 'B') return juce::Colour (0xff116fd1);
    return juce::Colours::transparentBlack;
}

void drawHeaderChecker (juce::Graphics& graphics, bool modernDark)
{
    static constexpr std::array<const char*, 2> pixels {{
        "WKWKCKGK",
        "KYKWKWKW"
    }};

    constexpr float pixelSize = 7.5f;
    constexpr float left = 441.5f;
    constexpr float top = 11.0f;
    for (std::size_t row = 0; row < pixels.size(); ++row)
    {
        for (std::size_t column = 0; pixels[row][column] != '\0'; ++column)
        {
            const auto bounds = juce::Rectangle<float> {
                left + static_cast<float> (column) * pixelSize,
                top + static_cast<float> (row) * pixelSize,
                pixelSize,
                pixelSize
            };
            graphics.setColour (headerPixelColour (pixels[row][column], modernDark));
            graphics.fillRect (bounds);
            graphics.setColour (juce::Colours::white.withAlpha (0.13f));
            graphics.fillRect (bounds.withHeight (1.2f));
        }
    }

}

void drawIceCreamWordmark (juce::Graphics& graphics, bool modernDark)
{
    static constexpr std::array<std::array<const char*, 5>, 8> glyphs {{
        {{ "11111", "00100", "00100", "00100", "11111" }},
        {{ "11111", "10000", "10000", "10000", "11111" }},
        {{ "11111", "10000", "11100", "10000", "11111" }},
        {{ "11111", "10000", "10000", "10000", "11111" }},
        {{ "11110", "10001", "11111", "10010", "10001" }},
        {{ "11111", "10000", "11100", "10000", "11111" }},
        {{ "01110", "10001", "11111", "10001", "10001" }},
        {{ "10001", "11011", "10101", "10001", "10001" }}
    }};

    constexpr float startX = 311.0f;
    constexpr float startY = 26.0f;
    constexpr float cellWidth = 6.3f;
    constexpr float cellHeight = 7.07f;
    constexpr float letterGap = 5.5f;
    constexpr float outline = 2.8f;
    const auto outlineColour = modernDark ? juce::Colours::white : juce::Colours::black;
    const auto faceTop = modernDark ? juce::Colour (0xff1a1d22) : juce::Colours::white;
    const auto faceBottom = modernDark ? juce::Colours::black : juce::Colour (0xffe7e7e1);

    const auto cellBounds = [=] (std::size_t letter,
                                 std::size_t row,
                                 std::size_t column)
    {
        const auto letterX = startX
            + static_cast<float> (letter) * (5.0f * cellWidth + letterGap);
        return juce::Rectangle<float> {
            letterX + static_cast<float> (column) * cellWidth,
            startY + static_cast<float> (row) * cellHeight,
            cellWidth,
            cellHeight
        };
    };

    graphics.setColour (outlineColour);
    for (std::size_t letter = 0; letter < glyphs.size(); ++letter)
        for (std::size_t row = 0; row < glyphs[letter].size(); ++row)
            for (std::size_t column = 0; glyphs[letter][row][column] != '\0'; ++column)
                if (glyphs[letter][row][column] == '1')
                    graphics.fillRoundedRectangle (
                        cellBounds (letter, row, column).expanded (outline), 2.2f);

    juce::ColourGradient face (faceTop, startX, startY,
                               faceBottom, startX, startY + 5.0f * cellHeight,
                               false);
    graphics.setGradientFill (face);
    juce::Path facePath;
    for (std::size_t letter = 0; letter < glyphs.size(); ++letter)
        for (std::size_t row = 0; row < glyphs[letter].size(); ++row)
            for (std::size_t column = 0; glyphs[letter][row][column] != '\0'; ++column)
                if (glyphs[letter][row][column] == '1')
                    facePath.addRectangle (cellBounds (letter, row, column));
    graphics.fillPath (facePath);

    struct RColourCell
    {
        std::size_t row;
        std::size_t column;
        char colour;
    };
    static constexpr std::array<RColourCell, 4> rColours {{
        { 0, 1, 'G' },
        { 1, 4, 'P' },
        { 2, 0, 'R' },
        { 2, 2, 'Y' }
    }};
    for (const auto& colourCell : rColours)
    {
        const auto bounds = cellBounds (4, colourCell.row, colourCell.column)
                                .reduced (0.4f);
        graphics.setColour (headerPixelColour (colourCell.colour, modernDark));
        graphics.fillRect (bounds);
        graphics.setColour (juce::Colours::white.withAlpha (0.15f));
        graphics.fillRect (bounds.withHeight (0.9f));
    }

}

class HoverComboBox final : public juce::ComboBox
{
public:
    void mouseEnter (const juce::MouseEvent& event) override
    {
        juce::ComboBox::mouseEnter (event);
        repaint();
    }

    void mouseExit (const juce::MouseEvent& event) override
    {
        juce::ComboBox::mouseExit (event);
        repaint();
    }
};

class ParameterControl final : public juce::Component
{
public:
    void useAlignedSingleRowStyle() noexcept
    {
        alignedSingleRowStyle = true;
    }

    void setLabelOffsetY (int newOffsetY) noexcept
    {
        labelOffsetY = newOffsetY;
        repaint();
    }

    void setExtendedLabelSpace (int pixels) noexcept
    {
        extendedLabelSpace = juce::jmax (0, pixels);
    }

    ParameterControl (juce::AudioProcessorValueTreeState& state,
                      const juce::String& parameterID,
                      juce::Colour accentColour)
    {
        auto* parameter = state.getParameter (parameterID);
        jassert (parameter != nullptr);

        if (parameter == nullptr)
            return;

        if (parameterID == "p31_osc2_osc1")
            name = "OSC1";
        else if (parameterID == "p11_filter_env_amount")
            name = "Amnt";
        else
            name = parameter->getName (64);

        if (parameterID == "p19_filter_type")
        {
            name.clear();
            comboBox = std::make_unique<HoverComboBox>();
            comboBox->getProperties().set (compactFilterTypeProperty,
                                           juce::var { true });
            comboBox->addItem ("Low Pass", 1);
            comboBox->addItem ("High Pass", 2);
            comboBox->addItem ("Band Pass", 3);
            comboBox->addItem ("Band Reject", 4);
            comboBox->addItem ("Peaking", 5);
            comboBox->setJustificationType (juce::Justification::centred);
            comboBox->setColour (juce::ComboBox::backgroundColourId,
                                 juce::Colour (0xffc4c7c8));
            comboBox->setColour (juce::ComboBox::outlineColourId, accentColour);
            comboBox->setColour (juce::ComboBox::textColourId,
                                 juce::Colour (0xff181a1c));
            addAndMakeVisible (*comboBox);
            comboBoxAttachment = std::make_unique<
                juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                    state, parameterID, *comboBox);
            return;
        }

        if (dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr)
        {
            toggle = std::make_unique<juce::ToggleButton>();
            toggle->setButtonText ({});
            addAndMakeVisible (*toggle);
            buttonAttachment = std::make_unique<
                juce::AudioProcessorValueTreeState::ButtonAttachment> (
                    state, parameterID, *toggle);
            return;
        }

        slider = std::make_unique<juce::Slider>();
        slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider->setRange (parameter->getNormalisableRange().start,
                          parameter->getNormalisableRange().end);
        slider->setDoubleClickReturnValue (
            true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        slider->setColour (juce::Slider::rotarySliderFillColourId, accentColour);
        slider->textFromValueFunction = [parameter] (double value)
        {
            return parameter->getText (
                parameter->convertTo0to1 (static_cast<float> (value)), 32);
        };
        slider->valueFromTextFunction = [parameter] (const juce::String& text)
        {
            return static_cast<double> (
                parameter->convertFrom0to1 (parameter->getValueForText (text)));
        };
        addAndMakeVisible (*slider);
        applyOriginalParameterKnobStyle (*slider, parameterID);
        sliderAttachment = std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, parameterID, *slider);
    }

    void paint (juce::Graphics& graphics) override
    {
        auto labelBounds = getLocalBounds().reduced (1);
        labelBounds = labelBounds.removeFromBottom (14);
        labelBounds.translate (0, labelOffsetY);
        graphics.setColour (themedTextColour (*this, juce::Colour (0xff181a1c)));
        graphics.setFont (juce::FontOptions { parameterLabelFontSize,
                                              juce::Font::bold });
        graphics.drawText (name, labelBounds,
                           juce::Justification::centred, false);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (1);
        if (extendedLabelSpace > 0)
            area.removeFromBottom (juce::jmin (
                extendedLabelSpace,
                juce::jmax (0, area.getHeight() - 15)));
        area.removeFromBottom (14);

        if (slider != nullptr)
        {
            auto sliderBounds = alignedSingleRowStyle
                                    ? area.withTrimmedBottom (1)
                                    : area;
            const auto style = slider->getProperties()
                                   .getWithDefault (knobStyleProperty,
                                                    juce::var { "modern" })
                                   .toString();
            if (style == "filter")
                sliderBounds = sliderBounds.expanded (5).translated (0, 4);
            slider->setBounds (sliderBounds);
            return;
        }

        if (toggle != nullptr)
        {
            const auto size = juce::jmin (22, juce::jmin (area.getWidth(), area.getHeight()));
            toggle->setBounds (area.withSizeKeepingCentre (size, size));
            return;
        }

        if (comboBox != nullptr)
            comboBox->setBounds (area.withSizeKeepingCentre (
                juce::jmax (1, area.getWidth() - 28), 20));
    }

private:
    juce::String name;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::ComboBox> comboBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboBoxAttachment;
    bool alignedSingleRowStyle = false;
    int labelOffsetY = 0;
    int extendedLabelSpace = 0;
};

class ParameterSection final : public juce::Component
{
public:
    void useEnvelopeSizedControls() noexcept
    {
        envelopeSizedControls = true;
        for (auto& control : controls)
            control->useAlignedSingleRowStyle();
    }

    void useBitcrusherSizedKnobs() noexcept
    {
        envelopeControlHeight = 55;
    }

    void moveLabelsUp (int pixels) noexcept
    {
        for (auto& control : controls)
            control->setLabelOffsetY (-juce::jmax (0, pixels));
    }

    void useFilterLayout() noexcept
    {
        filterLayout = true;
        if (controls.size() == 4)
        {
            controls[1]->setExtendedLabelSpace (7);
            controls[3]->setExtendedLabelSpace (7);
        }
    }

    void useMasterVolumeStyle() noexcept
    {
        masterVolumeStyle = true;
        fixedControlHeight = 63;
    }

    void setBorderColour (juce::Colour newBorderColour) noexcept
    {
        border = newBorderColour;
    }

    ParameterSection (juce::String sectionName,
                      juce::AudioProcessorValueTreeState& state,
                      std::initializer_list<const char*> parameterIDs,
                      int requestedColumns,
                      juce::Colour backgroundColour,
                      juce::Colour accentColour)
        : name (std::move (sectionName)),
          columns (juce::jmax (1, requestedColumns)),
          background (backgroundColour),
          border (accentColour)
    {
        for (const auto* parameterID : parameterIDs)
        {
            auto control = std::make_unique<ParameterControl> (
                state, parameterID, accentColour);
            addAndMakeVisible (*control);
            controls.push_back (std::move (control));
        }
    }

    void paint (juce::Graphics& graphics) override
    {
        if (! masterVolumeStyle)
        {
            const auto bounds = getLocalBounds().toFloat().reduced (
                sectionBorderThickness * 0.5f);
            if (! isModernDarkTheme (*this))
            {
                graphics.setColour (background);
                graphics.fillRoundedRectangle (bounds, 6.0f);
            }
            graphics.setColour (border.withAlpha (0.9f));
            graphics.drawRoundedRectangle (bounds, 6.0f,
                                           sectionBorderThickness);
        }

        graphics.setColour (border.withAlpha (0.9f));
        graphics.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
        if (masterVolumeStyle)
        {
            graphics.drawText (name, getWidth() - 62, 2, 58, 15,
                               juce::Justification::centred, false);
        }
        else
        {
            graphics.drawText (name,
                               getLocalBounds().removeFromTop (18).reduced (7, 0),
                               juce::Justification::centredLeft,
                               false);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (3);
        area.removeFromTop (17);

        const auto count = static_cast<int> (controls.size());
        if (masterVolumeStyle && count == 1)
        {
            controls.front()->setBounds (getWidth() - 62, 18, 58, 63);
            return;
        }

        if (filterLayout && count == 4)
        {
            const auto leftWidth = area.getWidth() / 2;
            const auto topHeight = area.getHeight() / 2;
            const auto topLeft = juce::Rectangle<int> {
                area.getX(), area.getY(), leftWidth, topHeight
            };
            const auto bottomLeft = juce::Rectangle<int> {
                area.getX(), area.getY() + topHeight,
                leftWidth, area.getHeight() - topHeight
            };
            const auto topRight = juce::Rectangle<int> {
                area.getX() + leftWidth, area.getY(),
                area.getWidth() - leftWidth, topHeight
            };
            const auto bottomRight = juce::Rectangle<int> {
                area.getX() + leftWidth, area.getY() + topHeight,
                area.getWidth() - leftWidth, area.getHeight() - topHeight
            };

            controls[1]->setBounds (topLeft.withHeight (
                topLeft.getHeight() + 7));
            controls[3]->setBounds (topRight.withHeight (
                topRight.getHeight() + 7));
            controls[0]->setBounds (bottomLeft.translated (0, 5));
            controls[2]->setBounds (bottomRight.withSizeKeepingCentre (
                bottomRight.getWidth(), 54).translated (0, 5));
            return;
        }

        const auto rows = juce::jmax (1, (count + columns - 1) / columns);
        const auto cellWidth = juce::jmax (1, area.getWidth() / columns);
        const auto cellHeight = juce::jmax (1, area.getHeight() / rows);

        for (int index = 0; index < count; ++index)
        {
            const auto column = index % columns;
            const auto row = index / columns;
            auto x = area.getX() + column * cellWidth;
            const auto y = area.getY() + row * cellHeight;
            auto width = column == columns - 1
                             ? area.getRight() - x
                             : cellWidth;
            if (envelopeSizedControls)
            {
                x = area.getX() + juce::roundToInt (
                    static_cast<float> (column * area.getWidth()) / columns);
                const auto right = area.getX() + juce::roundToInt (
                    static_cast<float> ((column + 1) * area.getWidth()) / columns);
                width = right - x;
            }
            const auto height = row == rows - 1
                                    ? area.getBottom() - y
                                    : cellHeight;
            auto controlBounds = juce::Rectangle<int> { x, y, width, height };
            if (fixedControlHeight > 0)
                controlBounds = controlBounds.withSizeKeepingCentre (
                    width, fixedControlHeight);
            else if (envelopeSizedControls)
                controlBounds = controlBounds.withSizeKeepingCentre (
                    width, envelopeControlHeight);

            controls[static_cast<std::size_t> (index)]->setBounds (controlBounds);
        }
    }

private:
    juce::String name;
    int columns = 1;
    juce::Colour background;
    juce::Colour border;
    bool masterVolumeStyle = false;
    bool envelopeSizedControls = false;
    bool filterLayout = false;
    int envelopeControlHeight = 49;
    int fixedControlHeight = 0;
    std::vector<std::unique_ptr<ParameterControl>> controls;
};

class EqualizerResetButton final : public juce::TextButton
{
public:
    EqualizerResetButton()
        : juce::TextButton ("RESET")
    {
    }

    void paintButton (juce::Graphics& graphics,
                      bool isMouseOverButton,
                      bool isButtonDown) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        auto background = juce::Colour (0xffeef0ff);
        if (isButtonDown)
            background = juce::Colour (0xffaeb2db);
        else if (isMouseOverButton)
            background = juce::Colour (0xffdce0fa);

        graphics.setColour (background);
        graphics.fillRoundedRectangle (bounds, 3.0f);
        graphics.setColour (juce::Colour (0xff5c5d86));
        graphics.drawRoundedRectangle (bounds, 3.0f, 1.0f);
        graphics.setColour (juce::Colour (0xff25273b));
        graphics.setFont (juce::FontOptions { 8.0f, juce::Font::bold });
        graphics.drawText (getButtonText(), getLocalBounds().reduced (2, 0),
                           juce::Justification::centred, false);
    }
};

class EqualizerSection final : public juce::Component
{
public:
    explicit EqualizerSection (juce::AudioProcessorValueTreeState& state)
    {
        static constexpr std::array<const char*, 8> parameterIDs {
            "p45_eq_125", "p46_eq_250", "p47_eq_500", "p48_eq_1k",
            "p49_eq_2k", "p50_eq_4k", "p51_eq_8k", "p52_eq_16k"
        };
        for (std::size_t band = 0; band < sliders.size(); ++band)
        {
            parameters[band] = state.getParameter (parameterIDs[band]);
            jassert (parameters[band] != nullptr);

            auto& slider = sliders[band];
            slider.setSliderStyle (juce::Slider::LinearVertical);
            slider.getProperties().set (eqSliderStyleProperty,
                                        juce::var { true });
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRange (-12.0, 12.0, 0.1);
            slider.setDoubleClickReturnValue (true, 0.0);
            slider.setColour (juce::Slider::trackColourId, juce::Colour (0xff7779a8));
            slider.setColour (juce::Slider::thumbColourId, juce::Colour (0xfff4f4ed));
            slider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xffafb1d2));
            addAndMakeVisible (slider);

            attachments[band] = std::make_unique<
                juce::AudioProcessorValueTreeState::SliderAttachment> (
                    state, parameterIDs[band], slider);
        }

        resetButton.setTooltip ("Set all EQ bands to 0 dB");
        resetButton.onClick = [this] { resetToFlat(); };
        addAndMakeVisible (resetButton);
    }

    void paint (juce::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (
            sectionBorderThickness * 0.5f);
        if (! isModernDarkTheme (*this))
        {
            graphics.setColour (juce::Colour (0xffcdd1f0));
            graphics.fillRoundedRectangle (bounds, 6.0f);
        }
        graphics.setColour (juce::Colour (0xff5c5d86));
        graphics.drawRoundedRectangle (bounds, 6.0f,
                                       sectionBorderThickness);
        graphics.setColour (juce::Colour (0xff5c5d86));
        graphics.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
        graphics.drawText ("EQ",
                           getLocalBounds().removeFromTop (18).reduced (7, 0),
                           juce::Justification::centredLeft,
                           false);

        static constexpr std::array<const char*, 8> frequencyLabels {
            "125", "250", "500", "1K", "2K", "4K", "8K", "16K"
        };
        auto labelLayout = getLocalBounds().reduced (3);
        labelLayout.removeFromTop (17);
        const auto labelArea = labelLayout.removeFromBottom (13);
        const auto cellWidth = juce::jmax (1, labelLayout.getWidth() / 8);
        const auto gridWidth = cellWidth * 8;
        const auto gridX = labelLayout.getCentreX() - gridWidth / 2;

        graphics.setColour (themedTextColour (*this, juce::Colour (0xff25273b)));
        graphics.setFont (juce::FontOptions { 9.0f, juce::Font::bold });
        for (std::size_t band = 0; band < frequencyLabels.size(); ++band)
        {
            const auto x = gridX + static_cast<int> (band) * cellWidth;
            const auto labelBounds = juce::Rectangle<int> {
                x - 3, labelArea.getY(), cellWidth + 6, labelArea.getHeight()
            };
            graphics.drawText (juce::String { frequencyLabels[band] }, labelBounds,
                               juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        resetButton.setBounds (getWidth() - 43, 5, 38, 14);

        auto area = getLocalBounds().reduced (3);
        area.removeFromTop (17);
        area.removeFromBottom (13);
        const auto cellWidth = juce::jmax (1, area.getWidth() / 8);
        const auto gridWidth = cellWidth * 8;
        const auto gridX = area.getCentreX() - gridWidth / 2;

        for (std::size_t band = 0; band < sliders.size(); ++band)
        {
            const auto index = static_cast<int> (band);
            const auto x = gridX + index * cellWidth;
            sliders[band].setBounds (x, area.getY(), cellWidth, area.getHeight());
        }
    }

private:
    void resetToFlat()
    {
        for (auto* parameter : parameters)
        {
            if (parameter == nullptr)
                continue;

            const auto flatValue = parameter->getNormalisableRange().snapToLegalValue (0.0f);
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (flatValue));
            parameter->endChangeGesture();
        }
    }

    std::array<juce::RangedAudioParameter*, 8> parameters {};
    std::array<juce::Slider, 8> sliders;
    EqualizerResetButton resetButton;
    std::array<std::unique_ptr<
        juce::AudioProcessorValueTreeState::SliderAttachment>, 8> attachments;
};

class SequencerActionButton final : public juce::TextButton
{
public:
    explicit SequencerActionButton (const juce::String& text)
        : juce::TextButton (text)
    {
    }

    void paintButton (juce::Graphics& graphics,
                      bool isMouseOverButton,
                      bool isButtonDown) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        auto background = juce::Colour (0xfff3e8f4);
        if (isButtonDown)
            background = juce::Colour (0xffd4acd5);
        else if (isMouseOverButton)
            background = juce::Colour (0xffead3eb);

        graphics.setColour (background);
        graphics.fillRoundedRectangle (bounds, 3.0f);
        graphics.setColour (juce::Colour (0xff8d3b86));
        graphics.drawRoundedRectangle (bounds, 3.0f, 1.0f);
        graphics.setColour (juce::Colour (0xff17191b));
        graphics.setFont (juce::FontOptions { controlLabelFontSize,
                                              juce::Font::bold });
        graphics.drawText (getButtonText(), getLocalBounds().reduced (2, 0),
                           juce::Justification::centred, false);
    }
};

class StepSequencerSection final : public juce::Component,
                                   private juce::Timer
{
public:
    explicit StepSequencerSection (IceCreamAudioProcessor& audioProcessor)
        : processor (audioProcessor)
    {
        auto& state = processor.getParameterState();
        pitchParameter = state.getParameter ("p39_step_pitch_on");
        filterParameter = state.getParameter ("p38_step_filter_on");
        smoothParameter = state.getParameter ("p40_step_smooth");
        smoothAttackParameter = state.getParameter ("p71_seq_smooth_attack");
        rateParameter = state.getParameter ("p41_step_rate");
        freeParameter = state.getParameter ("p70_seq_free");
        jassert (freeParameter != nullptr && pitchParameter != nullptr
                 && filterParameter != nullptr
                 && smoothParameter != nullptr && smoothAttackParameter != nullptr
                 && rateParameter != nullptr);

        static constexpr std::array<const char*, 16> stepIDs {
            "p54_seq_step_01", "p55_seq_step_02", "p56_seq_step_03", "p57_seq_step_04",
            "p58_seq_step_05", "p59_seq_step_06", "p60_seq_step_07", "p61_seq_step_08",
            "p62_seq_step_09", "p63_seq_step_10", "p64_seq_step_11", "p65_seq_step_12",
            "p66_seq_step_13", "p67_seq_step_14", "p68_seq_step_15", "p69_seq_step_16"
        };

        for (std::size_t step = 0; step < stepParameters.size(); ++step)
        {
            stepParameters[step] = state.getParameter (stepIDs[step]);
            jassert (stepParameters[step] != nullptr);
        }

        templateButton.setTooltip ("Load a built-in or saved sequencer template");
        saveButton.setTooltip ("Save the complete sequencer setup to Data\\Seq");
        templateButton.onClick = [this] { showTemplateMenu(); };
        saveButton.onClick = [this] { showSaveDialog(); };
        addAndMakeVisible (templateButton);
        addAndMakeVisible (saveButton);

        for (auto* toggle : { &freeButton, &pitchButton, &filterButton })
        {
            toggle->setButtonText ({});
            addAndMakeVisible (*toggle);
        }

        freeButton.setTooltip (
            "On: continuous free-run. Off: restart from step 1 on note trigger.");
        freeAttachment = std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment> (
                state, "p70_seq_free", freeButton);
        pitchAttachment = std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment> (
                state, "p39_step_pitch_on", pitchButton);
        filterAttachment = std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment> (
                state, "p38_step_filter_on", filterButton);

        configureKnob (smoothAttackSlider, smoothAttackParameter, 0.0, 1.0, 0.0);
        configureKnob (smoothSlider, smoothParameter, 0.0, 1.0, 0.0);
        configureKnob (rateSlider, rateParameter, 0.0, 1.0, 1.0 / 7.0);
        setOriginalKnobStyle (smoothAttackSlider, "effect", juce::Colour (0xffb7b2a8));
        setOriginalKnobStyle (smoothSlider, "effect", juce::Colour (0xffb7b2a8));
        setOriginalKnobStyle (rateSlider, "effect", juce::Colour (0xff9c9588));
        smoothAttackSlider.setTooltip ("Smooth the attack into each sequencer step");
        smoothSlider.setTooltip ("Smooth the release from each sequencer step");
        rateSlider.setTooltip ("Sequencer tempo division");
        addAndMakeVisible (smoothAttackSlider);
        addAndMakeVisible (smoothSlider);
        addAndMakeVisible (rateSlider);

        smoothAttackAttachment = std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, "p71_seq_smooth_attack", smoothAttackSlider);
        smoothAttachment = std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, "p40_step_smooth", smoothSlider);
        rateAttachment = std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, "p41_step_rate", rateSlider);

        updateRateText();
        startTimerHz (30);
    }

    ~StepSequencerSection() override
    {
        if (dragging)
            endStepGestures();
    }

    void paint (juce::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (
            sectionBorderThickness * 0.5f);
        if (! isModernDarkTheme (*this))
        {
            graphics.setColour (juce::Colour (0xffe6d1e9));
            graphics.fillRoundedRectangle (bounds, 6.0f);
        }
        graphics.setColour (juce::Colour (0xff8d3b86));
        graphics.drawRoundedRectangle (bounds, 6.0f,
                                       sectionBorderThickness);

        graphics.setColour (juce::Colour (0xff8d3b86));
        graphics.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
        graphics.drawText ("SEQUENCER", 7, 2, 67, 16,
                           juce::Justification::centredLeft, false);

        graphics.setColour (themedTextColour (*this));
        graphics.setFont (juce::FontOptions { 8.5f,
                                              juce::Font::bold });
        graphics.drawText ("A - SMOOTH - R", 75, 36, 69, 9,
                           juce::Justification::centred, false);

        const auto graph = getGraphBounds();
        const auto stepArea = getGraphContentBounds();
        const auto activeStep = juce::jlimit (0, 15, processor.getSequencerActiveStep());
        const auto columnWidth = static_cast<float> (stepArea.getWidth()) / 16.0f;
        const auto centreY = stepArea.getCentreY();
        constexpr auto graphCornerRadius = 3.0f;
        juce::Path graphClip;
        graphClip.addRoundedRectangle (graph.toFloat(), graphCornerRadius);
        graphics.saveState();
        graphics.reduceClipRegion (graphClip);

        graphics.setColour (juce::Colour (0xffff8bd5));
        graphics.fillRoundedRectangle (graph.toFloat(), graphCornerRadius);
        juce::ColourGradient stepGradient (
            juce::Colours::white.withAlpha (0.32f),
            static_cast<float> (graph.getX()), static_cast<float> (graph.getY()),
            juce::Colours::black.withAlpha (0.24f),
            static_cast<float> (graph.getX()), static_cast<float> (graph.getBottom()),
            false);
        stepGradient.addColour (0.48, juce::Colours::transparentWhite);
        graphics.setGradientFill (stepGradient);
        graphics.fillRoundedRectangle (graph.toFloat(), graphCornerRadius);

        juce::Path contentClip;
        contentClip.addRoundedRectangle (stepArea.toFloat(), 1.5f);
        graphics.reduceClipRegion (contentClip);

        std::array<float, 16> displayedStepValues {};
        for (std::size_t step = 0; step < displayedStepValues.size(); ++step)
        {
            const auto* parameter = stepParameters[step];
            displayedStepValues[step] = parameter != nullptr
                ? parameter->convertFrom0to1 (parameter->getValue())
                : 0.5f;
        }

        for (int step = 0; step < 16; ++step)
        {
            const auto left = static_cast<float> (stepArea.getX())
                            + columnWidth * static_cast<float> (step);
            const auto right = static_cast<float> (stepArea.getX())
                             + columnWidth * static_cast<float> (step + 1);
            const auto cell = juce::Rectangle<float> {
                left, static_cast<float> (stepArea.getY()), right - left,
                static_cast<float> (stepArea.getHeight())
            };

            if ((step % 2) != 0)
            {
                graphics.setColour (juce::Colour (0x18ffffff));
                graphics.fillRect (cell);
            }

            if (step == activeStep)
            {
                graphics.setColour (juce::Colour (0x40fff36a));
                graphics.fillRect (cell);
            }

            graphics.setColour (juce::Colour (0x604b2148));
            graphics.drawVerticalLine (static_cast<int> (std::round (right)),
                                       static_cast<float> (stepArea.getY()),
                                       static_cast<float> (stepArea.getBottom()));
        }

        for (int step = 0; step < 16; ++step)
        {
            const auto left = static_cast<float> (stepArea.getX())
                            + columnWidth * static_cast<float> (step);
            const auto right = static_cast<float> (stepArea.getX())
                             + columnWidth * static_cast<float> (step + 1);

            const auto value = displayedStepValues[static_cast<std::size_t> (step)];
            const auto valueY = static_cast<float> (stepArea.getBottom())
                              - value * static_cast<float> (stepArea.getHeight());
            const auto barTop = juce::jmin (static_cast<float> (centreY), valueY);
            const auto barBottom = juce::jmax (static_cast<float> (centreY), valueY);
            const auto barLeft = static_cast<float> (std::floor (left));
            const auto barRight = static_cast<float> (std::ceil (right));
            graphics.setColour (juce::Colours::black);
            graphics.fillRect (barLeft,
                               barTop,
                               juce::jmax (1.0f, barRight - barLeft),
                               juce::jmax (1.0f, barBottom - barTop));
        }

        const auto releaseSmoothAmount = smoothParameter != nullptr
            ? juce::jlimit (0.0f, 1.0f,
                            smoothParameter->convertFrom0to1 (
                                smoothParameter->getValue()))
            : 0.0f;
        const auto attackSmoothAmount = smoothAttackParameter != nullptr
            ? juce::jlimit (0.0f, 1.0f,
                            smoothAttackParameter->convertFrom0to1 (
                                smoothAttackParameter->getValue()))
            : 0.0f;
        if (releaseSmoothAmount > 0.001f || attackSmoothAmount > 0.001f)
        {
            constexpr auto flattenStart = 0.9f;
            const auto interpolationAmount = juce::jmin (
                releaseSmoothAmount, flattenStart);
            const auto flattenAmount = juce::jlimit (
                0.0f, 1.0f,
                (releaseSmoothAmount - flattenStart) / (1.0f - flattenStart));
            const auto valueToY = [&stepArea] (float value)
            {
                return static_cast<float> (stepArea.getBottom())
                     - value * static_cast<float> (stepArea.getHeight());
            };

            const auto releaseValueAt = [&] (int step, float position)
            {
                const auto currentValue = displayedStepValues[
                    static_cast<std::size_t> (step)];
                const auto nextValue = displayedStepValues[
                    static_cast<std::size_t> ((step + 1) % 16)];
                auto value = currentValue
                    + interpolationAmount * position * (nextValue - currentValue);

                if (step == 15 && interpolationAmount > 0.000001f)
                {
                    const auto transitionStart = 1.0f - interpolationAmount;
                    const auto transitionPosition = juce::jlimit (
                        0.0f, 1.0f,
                        (position - transitionStart) / interpolationAmount);
                    value = currentValue
                        + transitionPosition * (nextValue - currentValue);
                }

                return value + flattenAmount * (0.5f - value);
            };

            const auto displayedValueAt = [&] (int step, float position)
            {
                auto value = releaseValueAt (step, position);
                if (attackSmoothAmount > 0.000001f
                    && position < attackSmoothAmount)
                {
                    const auto previousStep = (step + 15) % 16;
                    const auto previousEndValue = releaseValueAt (previousStep, 1.0f);
                    const auto currentStartValue = releaseValueAt (step, 0.0f);
                    const auto attackProgress = juce::jlimit (
                        0.0f, 1.0f, position / attackSmoothAmount);
                    const auto attackRemainder = (1.0f - attackProgress)
                                               * (1.0f - attackProgress);
                    value += attackRemainder
                        * (previousEndValue - currentStartValue);
                }
                return value;
            };

            juce::Path smoothPath;
            constexpr auto curveSegmentsPerStep = 20;
            for (int step = 0; step < 16; ++step)
            {
                const auto left = static_cast<float> (stepArea.getX())
                                + columnWidth * static_cast<float> (step);
                const auto startValue = displayedValueAt (step, 0.0f);
                if (step == 0)
                    smoothPath.startNewSubPath (left, valueToY (startValue));
                else
                    smoothPath.lineTo (left, valueToY (startValue));

                for (int segment = 1; segment <= curveSegmentsPerStep; ++segment)
                {
                    const auto position = static_cast<float> (segment)
                                        / static_cast<float> (curveSegmentsPerStep);
                    smoothPath.lineTo (
                        left + columnWidth * position,
                        valueToY (displayedValueAt (step, position)));
                }
            }

            graphics.setColour (juce::Colour (0x700f1011));
            graphics.strokePath (smoothPath, juce::PathStrokeType (3.0f));
            graphics.setColour (juce::Colour (0xffd9e322));
            graphics.strokePath (smoothPath, juce::PathStrokeType (1.45f));
        }

        graphics.setColour (juce::Colour (0xff4b2148));
        graphics.drawHorizontalLine (centreY,
                                     static_cast<float> (stepArea.getX()),
                                     static_cast<float> (stepArea.getRight()));
        graphics.restoreState();
        graphics.setColour (juce::Colour (0xff8d3b86));
        graphics.drawRoundedRectangle (graph.toFloat().reduced (1.0f),
                                       graphCornerRadius, 2.0f);

        graphics.setColour (themedTextColour (*this));
        graphics.setFont (juce::FontOptions { parameterLabelFontSize,
                                              juce::Font::bold });
        graphics.drawText ("FREE", 0, 137, 32, 10,
                           juce::Justification::centred, false);
        graphics.drawText ("PITCH", 30, 137, 40, 10,
                           juce::Justification::centred, false);
        graphics.drawText ("FILTER", 66, 137, 36, 10,
                           juce::Justification::centred, false);
        graphics.drawText (rateText, 97, 137, 42, 10,
                           juce::Justification::centred, false);
    }

    void resized() override
    {
        templateButton.setBounds (4, 23, 34, 18);
        saveButton.setBounds (41, 23, 34, 18);
        smoothAttackSlider.setBounds (77, 3, 31, 31);
        smoothSlider.setBounds (111, 3, 31, 31);
        freeButton.setBounds (6, 113, 20, 20);
        pitchButton.setBounds (40, 113, 20, 20);
        filterButton.setBounds (74, 113, 20, 20);
        rateSlider.setBounds (102, 107, 32, 32);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! getGraphContentBounds().contains (event.getPosition()))
            return;

        dragging = true;
        lastEditedStep = -1;
        updateStepsFromMouse (event.position);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        updateMouseCursor (event.getPosition());
        if (dragging)
            updateStepsFromMouse (event.position);
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (dragging)
            endStepGestures();
        updateMouseCursor (event.getPosition());
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        if (! getGraphContentBounds().contains (event.getPosition()))
            return;

        if (dragging)
            endStepGestures();

        for (auto* parameter : stepParameters)
        {
            if (parameter == nullptr)
                continue;

            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->getDefaultValue());
            parameter->endChangeGesture();
        }

        repaint();
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        updateMouseCursor (event.getPosition());
    }

    void mouseEnter (const juce::MouseEvent& event) override
    {
        updateMouseCursor (event.getPosition());
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

private:
    using Pattern = std::array<float, 16>;

    void updateMouseCursor (juce::Point<int> position)
    {
        setMouseCursor (getGraphContentBounds().contains (position)
                            ? juce::MouseCursor::PointingHandCursor
                            : juce::MouseCursor::NormalCursor);
    }

    static float getParameterActualValue (juce::RangedAudioParameter* parameter,
                                          float fallback = 0.0f)
    {
        return parameter != nullptr
            ? parameter->convertFrom0to1 (parameter->getValue())
            : fallback;
    }

    static void setParameterActualValue (juce::RangedAudioParameter* parameter,
                                         float value)
    {
        if (parameter == nullptr)
            return;

        const auto legalValue = parameter->getNormalisableRange().snapToLegalValue (value);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (legalValue));
        parameter->endChangeGesture();
    }

    void applyPattern (const Pattern& pattern)
    {
        if (dragging)
            endStepGestures();

        for (std::size_t step = 0; step < pattern.size(); ++step)
            setParameterActualValue (stepParameters[step], pattern[step]);

        repaint();
    }

    void applyBuiltInTemplate (int menuResult)
    {
        Pattern pattern {};

        if (menuResult == 1)
        {
            for (std::size_t step = 0; step < pattern.size(); ++step)
            {
                pattern[step] = 1.0f
                    - static_cast<float> (step % 8) / 7.0f;
            }
        }
        else if (menuResult == 2)
        {
            for (std::size_t step = 0; step < pattern.size(); ++step)
            {
                const auto phase = juce::MathConstants<float>::twoPi
                                 * static_cast<float> (step)
                                 / static_cast<float> (pattern.size());
                pattern[step] = 0.5f + 0.5f * std::sin (phase);
            }
        }
        else if (menuResult == 3)
        {
            for (std::size_t step = 0; step < pattern.size(); ++step)
                pattern[step] = (step % 2) == 0 ? 0.40f : 0.65f;
        }
        else if (menuResult == 4)
        {
            auto& random = juce::Random::getSystemRandom();
            for (auto& value : pattern)
                value = random.nextFloat();
        }
        else
        {
            return;
        }

        applyPattern (pattern);
    }

    std::vector<juce::File> findSavedTemplates() const
    {
        std::vector<juce::File> files;
        const auto directory = getPortableSequencerDirectory();
        if (! directory.isDirectory())
            return files;

        for (const auto& entry : juce::RangedDirectoryIterator (
                 directory, false, "*.seq", juce::File::findFiles))
        {
            files.push_back (entry.getFile());
        }

        std::sort (files.begin(), files.end(), [] (const auto& left, const auto& right)
        {
            return left.getFileName().compareNatural (right.getFileName()) < 0;
        });

        return files;
    }

    void showTemplateMenu()
    {
        const auto userFiles = findSavedTemplates();
        juce::PopupMenu menu;
        menu.addItem (1, "Saw");
        menu.addItem (2, "Sine");
        menu.addItem (3, "Pulse");
        menu.addItem (4, "Random");

        if (! userFiles.empty())
        {
            menu.addSeparator();
            for (std::size_t index = 0; index < userFiles.size(); ++index)
            {
                menu.addItem (1000 + static_cast<int> (index),
                              userFiles[index].getFileNameWithoutExtension());
            }
        }

        juce::Component::SafePointer<StepSequencerSection> safeThis (this);
        menu.showMenuAsync (
            juce::PopupMenu::Options()
                .withTargetComponent (&templateButton)
                .withMinimumWidth (130)
                .withMaximumNumColumns (1)
                .withStandardItemHeight (20),
            [safeThis, userFiles] (int result)
            {
                if (safeThis == nullptr || result == 0)
                    return;

                if (result >= 1 && result <= 4)
                {
                    safeThis->applyBuiltInTemplate (result);
                    return;
                }

                const auto fileIndex = result - 1000;
                if (fileIndex >= 0 && fileIndex < static_cast<int> (userFiles.size()))
                {
                    safeThis->loadTemplateFile (
                        userFiles[static_cast<std::size_t> (fileIndex)]);
                }
            });
    }

    juce::String getNextUserTemplateName() const
    {
        const auto directory = getPortableSequencerDirectory();
        for (int index = 1; index <= 999; ++index)
        {
            const auto name = "User " + juce::String (index);
            if (! directory.getChildFile (name).withFileExtension ("seq").existsAsFile())
                return name;
        }

        return "New Template";
    }

    void showSaveDialog()
    {
        if (saveDialog != nullptr)
            return;

        saveDialog = std::make_unique<juce::AlertWindow> (
            "Save Sequencer Template",
            "Enter a template name. Using an existing name replaces that file.",
            juce::MessageBoxIconType::NoIcon,
            this);
        const auto modernDark = isModernDarkTheme (*this);
        const auto backgroundColour = modernDark
            ? juce::Colour (0xff15181d) : juce::Colour (0xfff4f4ed);
        const auto textColour = modernDark
            ? juce::Colour (0xffe2e6ea) : juce::Colour (0xff1a2418);
        const auto outlineColour = modernDark
            ? juce::Colour (0xffb255ab) : juce::Colour (0xff526a49);
        saveDialog->setColour (juce::AlertWindow::backgroundColourId,
                               backgroundColour);
        saveDialog->setColour (juce::AlertWindow::textColourId, textColour);
        saveDialog->setColour (juce::AlertWindow::outlineColourId,
                               outlineColour);
        juce::Component::SafePointer<StepSequencerSection> safeThis (this);
        saveDialog->addTextEditor ("name", getNextUserTemplateName(), "Name:");
        auto* nameEditor = saveDialog->getTextEditor ("name");
        if (auto* editor = nameEditor)
        {
            editor->setInputRestrictions (48);
            editor->setColour (juce::TextEditor::backgroundColourId,
                               modernDark ? juce::Colour (0xff252a31)
                                          : juce::Colours::white);
            editor->setColour (juce::TextEditor::textColourId, textColour);
            editor->setColour (juce::TextEditor::highlightColourId,
                               outlineColour.withAlpha (0.72f));
            editor->setColour (juce::TextEditor::highlightedTextColourId,
                               juce::Colours::white);
            editor->setColour (juce::TextEditor::outlineColourId,
                               outlineColour.withAlpha (0.72f));
            editor->setColour (juce::TextEditor::focusedOutlineColourId,
                               outlineColour);
            editor->onReturnKey = [safeThis]
            {
                if (safeThis != nullptr && safeThis->saveDialog != nullptr)
                    safeThis->saveDialog->exitModalState (1);
            };
        }
        saveDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        saveDialog->enterModalState (
            true,
            juce::ModalCallbackFunction::create ([safeThis] (int result)
            {
                if (safeThis != nullptr)
                    safeThis->finishSaveDialog (result);
            }),
            false);

        juce::Component::SafePointer<juce::TextEditor> safeNameEditor (nameEditor);
        juce::MessageManager::callAsync ([safeThis, safeNameEditor]
        {
            if (safeThis != nullptr && safeThis->saveDialog != nullptr)
            {
                auto* const editor = safeThis->getTopLevelComponent();
                auto* const dialog = safeThis->saveDialog.get();
                dialog->centreAroundComponent (
                    editor, dialog->getWidth(), dialog->getHeight());
            }

            if (safeNameEditor != nullptr)
            {
                safeNameEditor->grabKeyboardFocus();
                safeNameEditor->selectAll();
            }
        });
    }

    static juce::String sanitiseTemplateName (const juce::String& proposedName)
    {
        const auto trimmedName = proposedName.trim();
        juce::String safeName;

        for (int index = 0; index < trimmedName.length(); ++index)
        {
            const auto character = trimmedName[index];
            if (juce::CharacterFunctions::isLetterOrDigit (character)
                || character == ' ' || character == '-' || character == '_')
            {
                safeName += character;
            }
            else
            {
                safeName += '_';
            }
        }

        safeName = safeName.trim();
        return safeName.substring (0, juce::jmin (48, safeName.length()));
    }

    void finishSaveDialog (int result)
    {
        auto proposedName = juce::String();
        if (saveDialog != nullptr)
        {
            if (auto* editor = saveDialog->getTextEditor ("name"))
                proposedName = editor->getText();
        }
        saveDialog.reset();

        if (result != 1)
            return;

        const auto safeName = sanitiseTemplateName (proposedName);
        if (safeName.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Template Not Saved",
                "Please use at least one letter or number in the template name.");
            return;
        }

        const auto directory = getPortableSequencerDirectory();
        const auto directoryResult = directory.createDirectory();
        if (directoryResult.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Template Not Saved",
                "IceCream could not create:\n" + directory.getFullPathName()
                    + "\n\n" + directoryResult.getErrorMessage());
            return;
        }

        juce::String contents { "IceCreamSequencerTemplate=1\r\n" };
        contents << "Name=" << safeName << "\r\n";
        contents << "Free=" << juce::String (getParameterActualValue (freeParameter), 6)
                 << "\r\n";
        contents << "Pitch=" << juce::String (getParameterActualValue (pitchParameter), 6)
                 << "\r\n";
        contents << "Filter=" << juce::String (getParameterActualValue (filterParameter), 6)
                 << "\r\n";
        contents << "Smooth=" << juce::String (getParameterActualValue (smoothParameter), 6)
                 << "\r\n";
        contents << "SmoothAttack="
                 << juce::String (getParameterActualValue (smoothAttackParameter), 6)
                 << "\r\n";
        contents << "Rate=" << juce::String (getParameterActualValue (rateParameter), 6)
                 << "\r\n";

        for (std::size_t step = 0; step < stepParameters.size(); ++step)
        {
            const auto stepNumber = juce::String (static_cast<int> (step + 1)).paddedLeft ('0', 2);
            contents << "Step" << stepNumber << "="
                     << juce::String (getParameterActualValue (stepParameters[step], 0.5f), 6)
                     << "\r\n";
        }

        const auto targetFile = directory.getChildFile (safeName).withFileExtension ("seq");
        if (! targetFile.replaceWithText (contents))
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Template Not Saved",
                "IceCream could not write:\n" + targetFile.getFullPathName());
        }
    }

    void loadTemplateFile (const juce::File& file)
    {
        juce::StringArray lines;
        lines.addLines (file.loadFileAsString());

        Pattern pattern {};
        std::array<bool, 16> stepFound {};
        bool validHeader = false;
        float savedFree = -1.0f;
        float savedPitch = -1.0f;
        float savedFilter = -1.0f;
        float savedSmooth = -1.0f;
        float savedSmoothAttack = -1.0f;
        float savedRate = -1.0f;

        for (const auto& rawLine : lines)
        {
            const auto line = rawLine.trim();
            if (line == "IceCreamSequencerTemplate=1")
            {
                validHeader = true;
                continue;
            }

            if (! line.containsChar ('='))
                continue;

            const auto key = line.upToFirstOccurrenceOf ("=", false, false).trim();
            const auto value = line.fromFirstOccurrenceOf ("=", false, false)
                                   .trim().getFloatValue();

            if (key == "Free")       savedFree = value;
            else if (key == "Pitch") savedPitch = value;
            else if (key == "Filter") savedFilter = value;
            else if (key == "Smooth") savedSmooth = value;
            else if (key == "SmoothAttack") savedSmoothAttack = value;
            else if (key == "Rate") savedRate = value;
            else if (key.startsWith ("Step"))
            {
                const auto step = key.substring (4).getIntValue() - 1;
                if (step >= 0 && step < static_cast<int> (pattern.size()))
                {
                    pattern[static_cast<std::size_t> (step)] = juce::jlimit (0.0f, 1.0f, value);
                    stepFound[static_cast<std::size_t> (step)] = true;
                }
            }
        }

        const auto completePattern = std::all_of (
            stepFound.begin(), stepFound.end(), [] (bool found) { return found; });
        if (! validHeader || ! completePattern)
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Template Not Loaded",
                file.getFileName() + " is not a complete IceCream sequencer template.");
            return;
        }

        applyPattern (pattern);
        if (savedFree >= 0.0f)   setParameterActualValue (freeParameter, savedFree);
        if (savedPitch >= 0.0f)  setParameterActualValue (pitchParameter, savedPitch);
        if (savedFilter >= 0.0f) setParameterActualValue (filterParameter, savedFilter);
        if (savedSmooth >= 0.0f) setParameterActualValue (smoothParameter, savedSmooth);
        setParameterActualValue (
            smoothAttackParameter,
            savedSmoothAttack >= 0.0f ? savedSmoothAttack : 0.0f);
        if (savedRate >= 0.0f)   setParameterActualValue (rateParameter, savedRate);
        updateRateText();
        repaint();
    }

    static void configureKnob (juce::Slider& slider,
                               juce::RangedAudioParameter* parameter,
                               double minimum,
                               double maximum,
                               double interval)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (minimum, maximum, interval);
        slider.setColour (juce::Slider::rotarySliderFillColourId,
                          juce::Colour (0xff8d3b86));

        if (parameter != nullptr)
        {
            slider.setDoubleClickReturnValue (
                true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        }
    }

    juce::Rectangle<int> getGraphBounds() const
    {
        return { 3, 46, juce::jmax (1, getWidth() - 6), 61 };
    }

    juce::Rectangle<int> getGraphContentBounds() const
    {
        return getGraphBounds().reduced (2);
    }

    void setStepValue (int step, float value)
    {
        const auto index = static_cast<std::size_t> (juce::jlimit (0, 15, step));
        auto* parameter = stepParameters[index];
        if (parameter == nullptr)
            return;

        if (! gestureActive[index])
        {
            parameter->beginChangeGesture();
            gestureActive[index] = true;
        }

        const auto actualValue = juce::jlimit (0.0f, 1.0f, value);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (actualValue));
    }

    void updateStepsFromMouse (juce::Point<float> position)
    {
        const auto stepArea = getGraphContentBounds().toFloat();
        const auto x = juce::jlimit (
            stepArea.getX(), stepArea.getRight() - 0.001f, position.x);
        const auto y = juce::jlimit (
            stepArea.getY(), stepArea.getBottom(), position.y);
        const auto step = juce::jlimit (
            0, 15, static_cast<int> (
                (x - stepArea.getX()) * 16.0f / stepArea.getWidth()));
        const auto value = juce::jlimit (
            0.0f, 1.0f,
            (stepArea.getBottom() - y) / stepArea.getHeight());

        if (lastEditedStep < 0 || lastEditedStep == step)
        {
            setStepValue (step, value);
        }
        else
        {
            const auto direction = step > lastEditedStep ? 1 : -1;
            const auto distance = std::abs (step - lastEditedStep);
            for (int offset = 1; offset <= distance; ++offset)
            {
                const auto interpolation = static_cast<float> (offset)
                                         / static_cast<float> (distance);
                setStepValue (lastEditedStep + direction * offset,
                              lastEditedValue
                                  + interpolation * (value - lastEditedValue));
            }
        }

        lastEditedStep = step;
        lastEditedValue = value;
        repaint();
    }

    void endStepGestures()
    {
        for (std::size_t step = 0; step < gestureActive.size(); ++step)
        {
            if (gestureActive[step] && stepParameters[step] != nullptr)
                stepParameters[step]->endChangeGesture();
            gestureActive[step] = false;
        }

        dragging = false;
        lastEditedStep = -1;
    }

    void updateRateText()
    {
        static constexpr std::array<const char*, 8> rateNames {
            "1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1"
        };
        const auto actualRate = rateParameter != nullptr
            ? rateParameter->convertFrom0to1 (rateParameter->getValue())
            : 0.428571f;
        const auto index = juce::jlimit (
            0, 7, static_cast<int> (std::lround (actualRate * 7.0f)));
        rateText = rateNames[static_cast<std::size_t> (index)];
    }

    void timerCallback() override
    {
        updateRateText();
        repaint();
    }

    IceCreamAudioProcessor& processor;
    juce::RangedAudioParameter* freeParameter = nullptr;
    juce::RangedAudioParameter* pitchParameter = nullptr;
    juce::RangedAudioParameter* filterParameter = nullptr;
    juce::RangedAudioParameter* smoothParameter = nullptr;
    juce::RangedAudioParameter* smoothAttackParameter = nullptr;
    juce::RangedAudioParameter* rateParameter = nullptr;
    std::array<juce::RangedAudioParameter*, 16> stepParameters {};
    std::array<bool, 16> gestureActive {};
    juce::ToggleButton freeButton;
    juce::ToggleButton pitchButton;
    juce::ToggleButton filterButton;
    SequencerActionButton templateButton { "LOAD" };
    SequencerActionButton saveButton { "SAVE" };
    juce::Slider smoothAttackSlider;
    juce::Slider smoothSlider;
    juce::Slider rateSlider;
    juce::String rateText { "1/16" };
    bool dragging = false;
    int lastEditedStep = -1;
    float lastEditedValue = 0.5f;
    std::unique_ptr<juce::AlertWindow> saveDialog;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        freeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        pitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        filterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        smoothAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        smoothAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        rateAttachment;
};

class PresetActionButton final : public juce::TextButton
{
public:
    explicit PresetActionButton (const juce::String& text)
        : juce::TextButton (text)
    {
    }

    void paintButton (juce::Graphics& graphics,
                      bool isMouseOverButton,
                      bool isButtonDown) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        auto background = juce::Colour (0xffb9cfaa);
        if (isButtonDown)
            background = juce::Colour (0xff91ad80);
        else if (isMouseOverButton)
            background = juce::Colour (0xffcadcbd);

        graphics.setColour (background);
        graphics.fillRoundedRectangle (bounds, 2.5f);
        graphics.setColour (juce::Colour (0xff40543b));
        graphics.drawRoundedRectangle (bounds, 2.5f, 1.0f);
        graphics.setColour (juce::Colour (0xff1a2418));
        graphics.setFont (juce::FontOptions { controlLabelFontSize,
                                              juce::Font::bold });
        graphics.drawText (getButtonText(), getLocalBounds().reduced (2, 0),
                           juce::Justification::centred, false);
    }
};

class AboutContent final : public juce::Component
{
public:
    explicit AboutContent (bool modernDarkTheme)
        : modernDark (modernDarkTheme),
          cosmicBoyLink ("Cosmic Boy",
                         juce::URL { "https://www.cosmicbren.com/audio-tools" }),
          githubLink ("github.com/sl2365/Icecream64",
                      juce::URL { "https://github.com/sl2365/Icecream64" })
    {
        const auto linkColour = modernDark ? juce::Colour (0xff78c8ff)
                                           : juce::Colour (0xff315f9b);
        for (auto* link : { &cosmicBoyLink, &githubLink })
        {
            link->setColour (juce::HyperlinkButton::textColourId, linkColour);
            link->setMouseCursor (juce::MouseCursor::PointingHandCursor);
            addAndMakeVisible (*link);
        }

        cosmicBoyLink.setTooltip ("Open Cosmic Boy's audio tools website");
        githubLink.setTooltip ("Open the IceCream64 GitHub project");
        setSize (330, 142);
    }

    void paint (juce::Graphics& graphics) override
    {
        const auto textColour = modernDark ? juce::Colour (0xffe2e6ea)
                                           : juce::Colour (0xff1a2418);
        const auto accentColour = modernDark ? juce::Colour (0xffb255ab)
                                             : juce::Colour (0xff526a49);

        graphics.setColour (accentColour);
        graphics.setFont (juce::FontOptions { 18.0f, juce::Font::bold });
        graphics.drawText ("ICECREAM v2", 0, 2, getWidth(), 24,
                           juce::Justification::centred, false);

        graphics.setColour (textColour);
        graphics.setFont (juce::FontOptions { 12.0f, juce::Font::bold });
        graphics.drawText ("64-bit VST3 by sl23", 0, 29, getWidth(), 18,
                           juce::Justification::centred, false);

        graphics.setFont (juce::FontOptions { 11.5f });
        graphics.drawText ("Original author:", 38, 52, 125, 18,
                           juce::Justification::centredRight, false);
        graphics.drawFittedText (
            "This is not an exact replica, just a homage to the original plug-in.",
            22, 76, getWidth() - 44, 34,
            juce::Justification::centred, 2, 1.0f);
        graphics.drawText ("Project:", 20, 116, 65, 18,
                           juce::Justification::centredRight, false);
    }

    void resized() override
    {
        cosmicBoyLink.setBounds (166, 52, 91, 18);
        githubLink.setBounds (87, 116, 225, 18);
    }

private:
    bool modernDark = false;
    juce::HyperlinkButton cosmicBoyLink;
    juce::HyperlinkButton githubLink;
};

struct FactoryPresetResource
{
    const char* displayName;
    const char* resourceName;
};

constexpr std::array<FactoryPresetResource, 32> factoryPresetResources {{
    { "1. 100%",          "Factory_01_ini" },
    { "2. All Star",      "Factory_02_ini" },
    { "3. Accordion",     "Factory_03_ini" },
    { "4. Bongo Fiend",   "Factory_04_ini" },
    { "5. Contact",       "Factory_05_ini" },
    { "6. Daft Club",     "Factory_06_ini" },
    { "7. Disco Bass",    "Factory_07_ini" },
    { "8. Disco Fire",    "Factory_08_ini" },
    { "9. G Chords",      "Factory_09_ini" },
    { "10. Glider",       "Factory_10_ini" },
    { "11. Gobber",       "Factory_11_ini" },
    { "12. Jobe So",      "Factory_12_ini" },
    { "13. Joy Padder",   "Factory_13_ini" },
    { "14. March",        "Factory_14_ini" },
    { "15. Out of Breath", "Factory_15_ini" },
    { "16. Plastic Ridim", "Factory_16_ini" },
    { "17. Play Me",      "Factory_17_ini" },
    { "18. Plodder",      "Factory_18_ini" },
    { "19. Pop Star",     "Factory_19_ini" },
    { "20. Professor",    "Factory_20_ini" },
    { "21. Rollin",       "Factory_21_ini" },
    { "22. Sad Ending",   "Factory_22_ini" },
    { "23. Shauns Dead",  "Factory_23_ini" },
    { "24. Simple",       "Factory_24_ini" },
    { "25. Wii Play",     "Factory_25_ini" },
    { "26. Sing Wiv Me",  "Factory_26_ini" },
    { "27. Tha Kick",     "Factory_27_ini" },
    { "28. The Feeling",  "Factory_28_ini" },
    { "29. The Motion",   "Factory_29_ini" },
    { "30. The MSG",      "Factory_30_ini" },
    { "31. Wacko Jacko",  "Factory_31_ini" },
    { "32. Wah Wah",      "Factory_32_ini" }
}};

class PresetSection final : public juce::Component
{
public:
    explicit PresetSection (
        IceCreamAudioProcessor& audioProcessor,
        std::function<void (float)> editorScaleSetter,
        bool initialModernDarkTheme,
        std::function<void (bool)> editorThemeSetter)
        : processor (audioProcessor),
          state (processor.getParameterState()),
          setEditorScale (std::move (editorScaleSetter)),
          setEditorTheme (std::move (editorThemeSetter)),
          modernDarkTheme (initialModernDarkTheme)
    {
        currentPresetName = state.state.getProperty (
            juce::Identifier { "presetName" }, "INITIAL").toString();
        if (currentPresetName.isEmpty())
            currentPresetName = "INITIAL";

        const auto storedPresetFileName = state.state.getProperty (
            juce::Identifier { "presetFile" }).toString();
        if (storedPresetFileName.isNotEmpty()
            && ! storedPresetFileName.containsAnyOf ("\\/:"))
        {
            const auto storedPresetFile = getPortablePresetDirectory()
                                              .getChildFile (storedPresetFileName);
            if (storedPresetFile.existsAsFile())
                currentUserPresetFile = storedPresetFile;
        }

        fileButton.setTooltip ("Load or save a portable IceCream preset");
        previousButton.setTooltip ("Load the previous preset");
        nextButton.setTooltip ("Load the next preset");
        nameButton.setTooltip ("Edit the displayed preset name");

        fileButton.onClick = [this] { showFileMenu(); };
        previousButton.onClick = [this] { loadAdjacentPreset (-1); };
        nextButton.onClick = [this] { loadAdjacentPreset (1); };
        nameButton.onClick = [this] { beginInlineNameEdit(); };

        nameEditor.setMultiLine (false);
        nameEditor.setReturnKeyStartsNewLine (false);
        nameEditor.setInputRestrictions (48);
        nameEditor.setFont (juce::FontOptions { 12.5f });
        nameEditor.setJustification (juce::Justification::centredLeft);
        nameEditor.setIndents (2, 0);
        nameEditor.setScrollbarsShown (false);
        nameEditor.setColour (juce::TextEditor::backgroundColourId,
                              juce::Colours::transparentBlack);
        nameEditor.setColour (juce::TextEditor::textColourId,
                              juce::Colour (0xff283822));
        nameEditor.setColour (juce::TextEditor::highlightColourId,
                              juce::Colour (0xff729b65));
        nameEditor.setColour (juce::TextEditor::highlightedTextColourId,
                              juce::Colours::white);
        nameEditor.setColour (juce::TextEditor::outlineColourId,
                              juce::Colours::transparentBlack);
        nameEditor.setColour (juce::TextEditor::focusedOutlineColourId,
                              juce::Colour (0xff5e7a50));
        nameEditor.onReturnKey = [this] { finishInlineNameEdit (true); };
        nameEditor.onEscapeKey = [this] { finishInlineNameEdit (false); };
        nameEditor.onFocusLost = [this] { finishInlineNameEdit (true); };

        for (auto* button : { &fileButton, &previousButton,
                              &nextButton, &nameButton })
        {
            addAndMakeVisible (*button);
        }
        addChildComponent (nameEditor);

        refreshPresetFiles();
    }

    void paint (juce::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (
            sectionBorderThickness * 0.5f);
        if (! isModernDarkTheme (*this))
        {
            graphics.setColour (juce::Colour (0xffc1cab9));
            graphics.fillRoundedRectangle (bounds, 6.0f);
        }
        graphics.setColour (juce::Colour (0xff526a49));
        graphics.drawRoundedRectangle (bounds, 6.0f,
                                       sectionBorderThickness);

        graphics.setColour (juce::Colour (0xff526a49));
        graphics.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
        graphics.drawText ("PATCH", 7, 2, 50, 15,
                           juce::Justification::centredLeft, false);

        const auto display = getPresetNameBounds();
        graphics.setColour (juce::Colour (0xffa6c78f));
        graphics.fillRoundedRectangle (display.toFloat(), 4.0f);
        graphics.setColour (juce::Colour (0xff5e7a50));
        graphics.drawRoundedRectangle (display.toFloat().reduced (0.5f),
                                       4.0f, 1.0f);
        if (! nameEditor.isVisible())
        {
            graphics.setColour (juce::Colour (0xff283822));
            graphics.setFont (juce::FontOptions { 12.5f });
            graphics.drawFittedText (currentPresetName,
                                     display.reduced (7, 2),
                                     juce::Justification::centredLeft,
                                     1,
                                     0.75f);
        }

        graphics.setColour (themedTextColour (*this, juce::Colour (0xff263522)));

        const auto totalPresetCount = static_cast<int> (
            factoryPresetResources.size() + presetFiles.size());
        juce::String positionText { "--/" + juce::String (totalPresetCount) };
        if (currentPresetIndex >= 0
            && currentPresetIndex < totalPresetCount)
        {
            positionText = juce::String (currentPresetIndex + 1)
                         + "/" + juce::String (totalPresetCount);
        }

        graphics.setFont (juce::FontOptions { controlLabelFontSize,
                                              juce::Font::bold });
        graphics.drawText (positionText, 91, 2, 46, 15,
                           juce::Justification::centredRight, false);
    }

    void resized() override
    {
        previousButton.setBounds (9, 59, 22, 17);
        nextButton.setBounds (34, 59, 22, 17);
        nameButton.setBounds (59, 59, 32, 17);
        fileButton.setBounds (94, 59, 41, 17);
        nameEditor.setBounds (getPresetNameBounds().reduced (5, 5));
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! nameEditor.isVisible())
            return;

        auto* const clickedComponent = event.eventComponent;
        if (clickedComponent == &nameEditor
            || nameEditor.isParentOf (clickedComponent)
            || clickedComponent == &nameButton
            || nameButton.isParentOf (clickedComponent))
        {
            return;
        }

        finishInlineNameEdit (true);
    }

    void mouseWheelMove (
        const juce::MouseEvent& event,
        const juce::MouseWheelDetails& wheel) override
    {
        if (nameEditor.isVisible()
            || event.eventComponent != this
            || ! getPresetNameBounds().contains (event.getPosition())
            || wheel.deltaY == 0.0f)
        {
            juce::Component::mouseWheelMove (event, wheel);
            return;
        }

        loadAdjacentPreset (wheel.deltaY > 0.0f ? -1 : 1);
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        updateMouseCursor (event.getPosition());
    }

    void mouseEnter (const juce::MouseEvent& event) override
    {
        updateMouseCursor (event.getPosition());
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

private:
    void updateMouseCursor (juce::Point<int> position)
    {
        setMouseCursor (getPresetNameBounds().contains (position)
                            ? juce::MouseCursor::PointingHandCursor
                            : juce::MouseCursor::NormalCursor);
    }

    juce::Rectangle<int> getPresetNameBounds() const
    {
        return { 6, 19, getWidth() - 12, 37 };
    }

    std::vector<juce::File> findPresetFiles() const
    {
        std::vector<juce::File> files;
        const auto directory = getPortablePresetDirectory();
        if (! directory.isDirectory())
            return files;

        for (const auto& entry : juce::RangedDirectoryIterator (
                 directory, false, "*.ini", juce::File::findFiles))
        {
            const auto file = entry.getFile();
            if (! file.getFileNameWithoutExtension()
                    .startsWithIgnoreCase ("Original_"))
            {
                files.push_back (file);
            }
        }

        std::sort (files.begin(), files.end(), [] (const auto& left,
                                                   const auto& right)
        {
            return left.getFileName().compareNatural (right.getFileName()) < 0;
        });

        return files;
    }

    static juce::String displayNameForFile (const juce::File& file)
    {
        return file.getFileNameWithoutExtension();
    }

    void refreshPresetFiles()
    {
        presetFiles = findPresetFiles();
        currentPresetIndex = -1;

        if (currentFactoryPresetIndex >= 0
            && currentFactoryPresetIndex
                   < static_cast<int> (factoryPresetResources.size()))
        {
            currentPresetIndex = currentFactoryPresetIndex;
        }
        else if (currentUserPresetFile != juce::File())
        {
            for (std::size_t index = 0; index < presetFiles.size(); ++index)
            {
                if (presetFiles[index].getFullPathName()
                    == currentUserPresetFile.getFullPathName())
                {
                    currentPresetIndex = static_cast<int> (
                        factoryPresetResources.size() + index);
                    break;
                }
            }
        }
        else
        {
            for (std::size_t index = 0;
                 index < factoryPresetResources.size(); ++index)
            {
                if (currentPresetName
                    == factoryPresetResources[index].displayName)
                {
                    currentFactoryPresetIndex = static_cast<int> (index);
                    currentPresetIndex = currentFactoryPresetIndex;
                    break;
                }
            }

            for (std::size_t index = 0; index < presetFiles.size(); ++index)
            {
                if (currentPresetIndex < 0
                    && displayNameForFile (presetFiles[index])
                           == currentPresetName)
                {
                    currentUserPresetFile = presetFiles[index];
                    currentPresetIndex = static_cast<int> (
                        factoryPresetResources.size() + index);
                    break;
                }
            }
        }

        repaint();
    }

    void showFileMenu()
    {
        if (nameEditor.isVisible() && ! finishInlineNameEdit (true))
            return;

        refreshPresetFiles();

        juce::PopupMenu menu;
        menu.addItem (1, "Save");
        menu.addItem (2, "Save As...");
        menu.addSeparator();

        juce::PopupMenu factoryMenu;
        juce::PopupMenu userMenu;
        juce::PopupMenu sizeMenu;
        juce::PopupMenu themeMenu;

        for (std::size_t index = 0;
             index < factoryPresetResources.size(); ++index)
        {
            factoryMenu.addItem (
                1000 + static_cast<int> (index),
                factoryPresetResources[index].displayName);
        }

        for (std::size_t index = 0; index < presetFiles.size(); ++index)
        {
            userMenu.addItem (
                2000 + static_cast<int> (index),
                displayNameForFile (presetFiles[index]));
        }

        menu.addSubMenu ("Factory", factoryMenu, true);
        menu.addSubMenu ("User", userMenu, ! presetFiles.empty());
        menu.addSeparator();
        sizeMenu.addItem (100, "75%");
        sizeMenu.addItem (101, "100%");
        sizeMenu.addItem (102, "125%");
        sizeMenu.addItem (103, "150%");
        sizeMenu.addItem (104, "200%");
        menu.addSubMenu ("Size", sizeMenu, true);
        themeMenu.addItem (120, "Classic Light", true, ! modernDarkTheme);
        themeMenu.addItem (121, "Modern Dark", true, modernDarkTheme);
        menu.addSubMenu ("Theme", themeMenu, true);
        menu.addSeparator();
        menu.addItem (130, "About...");

        juce::Component::SafePointer<PresetSection> safeThis (this);
        const auto files = presetFiles;
        menu.showMenuAsync (
            juce::PopupMenu::Options()
                .withTargetComponent (&fileButton)
                .withMinimumWidth (145)
                .withMaximumNumColumns (1)
                .withStandardItemHeight (20),
            [safeThis, files] (int result)
            {
                if (safeThis == nullptr || result == 0)
                    return;

                if (result == 1)
                {
                    safeThis->saveCurrentPreset();
                    return;
                }

                if (result == 2)
                {
                    safeThis->savePresetAs();
                    return;
                }

                if (result >= 100 && result <= 104)
                {
                    constexpr std::array<float, 5> sizeScales {
                        0.75f, 1.0f, 1.25f, 1.5f, 2.0f
                    };
                    if (safeThis->setEditorScale)
                    {
                        safeThis->setEditorScale (
                            sizeScales[static_cast<std::size_t> (result - 100)]);
                    }
                    return;
                }

                if (result == 120 || result == 121)
                {
                    safeThis->modernDarkTheme = result == 121;
                    if (safeThis->setEditorTheme)
                        safeThis->setEditorTheme (safeThis->modernDarkTheme);
                    return;
                }

                if (result == 130)
                {
                    safeThis->showAboutWindow();
                    return;
                }

                const auto factoryIndex = result - 1000;
                if (factoryIndex >= 0
                    && factoryIndex
                           < static_cast<int> (factoryPresetResources.size()))
                {
                    safeThis->loadFactoryPreset (factoryIndex);
                    return;
                }

                const auto fileIndex = result - 2000;
                if (fileIndex >= 0 && fileIndex < static_cast<int> (files.size()))
                {
                    safeThis->loadPresetFile (
                        files[static_cast<std::size_t> (fileIndex)]);
                }
            });
    }

    void showAboutWindow()
    {
        if (aboutDialog != nullptr)
        {
            aboutDialog->toFront (true);
            return;
        }

        const auto backgroundColour = modernDarkTheme
            ? juce::Colour (0xff15181d) : juce::Colour (0xfff4f4ed);
        const auto textColour = modernDarkTheme
            ? juce::Colour (0xffe2e6ea) : juce::Colour (0xff1a2418);
        const auto outlineColour = modernDarkTheme
            ? juce::Colour (0xffb255ab) : juce::Colour (0xff526a49);

        aboutDialog = std::make_unique<juce::AlertWindow> (
            "About", juce::String(),
            juce::MessageBoxIconType::NoIcon, this);
        aboutDialog->setColour (juce::AlertWindow::backgroundColourId,
                                backgroundColour);
        aboutDialog->setColour (juce::AlertWindow::textColourId, textColour);
        aboutDialog->setColour (juce::AlertWindow::outlineColourId,
                                outlineColour);
        aboutDialog->addCustomComponent (new AboutContent (modernDarkTheme));
        aboutDialog->addButton ("Close", 0,
                                juce::KeyPress (juce::KeyPress::escapeKey));

        juce::Component::SafePointer<PresetSection> safeThis (this);
        aboutDialog->enterModalState (
            true,
            juce::ModalCallbackFunction::create ([safeThis] (int)
            {
                if (safeThis != nullptr)
                    safeThis->aboutDialog.reset();
            }),
            false);

        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr && safeThis->aboutDialog != nullptr)
            {
                auto* const editor = safeThis->getTopLevelComponent();
                auto* const dialog = safeThis->aboutDialog.get();
                dialog->centreAroundComponent (
                    editor, dialog->getWidth(), dialog->getHeight());
            }
        });
    }

    void loadAdjacentPreset (int direction)
    {
        refreshPresetFiles();
        const auto totalPresetCount = static_cast<int> (
            factoryPresetResources.size() + presetFiles.size());
        if (totalPresetCount == 0)
            return;

        if (currentPresetIndex < 0)
            currentPresetIndex = direction >= 0
                ? 0 : totalPresetCount - 1;
        else
            currentPresetIndex = (currentPresetIndex + direction
                                  + totalPresetCount)
                               % totalPresetCount;

        const auto factoryCount = static_cast<int> (factoryPresetResources.size());
        if (currentPresetIndex < factoryCount)
        {
            loadFactoryPreset (currentPresetIndex);
        }
        else
        {
            loadPresetFile (presetFiles[static_cast<std::size_t> (
                currentPresetIndex - factoryCount)]);
        }
    }

    void loadFactoryPreset (int factoryIndex)
    {
        if (factoryIndex < 0
            || factoryIndex >= static_cast<int> (factoryPresetResources.size()))
            return;

        const auto& preset = factoryPresetResources[
            static_cast<std::size_t> (factoryIndex)];
        int dataSize = 0;
        const auto* data = BinaryData::getNamedResource (
            preset.resourceName, dataSize);
        if (data == nullptr || dataSize <= 0)
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Factory Preset Not Loaded",
                juce::String (preset.displayName)
                    + " is missing from this IceCream build.");
            return;
        }

        loadPresetContents (juce::String::fromUTF8 (data, dataSize),
                            preset.displayName,
                            preset.displayName,
                            factoryIndex,
                            {});
    }

    void loadPresetFile (const juce::File& file)
    {
        loadPresetContents (file.loadFileAsString(),
                            file.getFileName(),
                            displayNameForFile (file),
                            -1,
                            file);
    }

    void loadPresetContents (const juce::String& contents,
                             const juce::String& sourceName,
                             const juce::String& fallbackName,
                             int factoryIndex,
                             const juce::File& userFile)
    {
        juce::StringArray lines;
        lines.addLines (contents);

        std::vector<std::pair<juce::RangedAudioParameter*, float>> loadedValues;
        juce::String loadedName;
        bool presetSectionFound = false;
        bool parametersSectionFound = false;
        bool validVersion = false;

        for (const auto& rawLine : lines)
        {
            const auto line = rawLine.trim();
            if (line.isEmpty() || line.startsWithChar (';'))
                continue;

            if (line == "[Preset]")
            {
                presetSectionFound = true;
                parametersSectionFound = false;
                continue;
            }

            if (line == "[Parameters]")
            {
                parametersSectionFound = true;
                continue;
            }

            if (! line.containsChar ('='))
                continue;

            const auto key = line.upToFirstOccurrenceOf ("=", false, false).trim();
            const auto valueText = line.fromFirstOccurrenceOf ("=", false, false).trim();

            if (! parametersSectionFound)
            {
                if (key == "FormatVersion")
                    validVersion = valueText.getIntValue() == 1;
                else if (key == "Name")
                    loadedName = valueText;
                continue;
            }

            if (auto* parameter = state.getParameter (key))
            {
                loadedValues.emplace_back (
                    parameter,
                    juce::jlimit (0.0f, 1.0f, valueText.getFloatValue()));
            }
        }

        if (! presetSectionFound || ! validVersion || loadedValues.empty())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Preset Not Loaded",
                sourceName + " is not a valid IceCream preset.");
            return;
        }

        for (auto* baseParameter : processor.getParameters())
        {
            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (baseParameter);
            if (parameter == nullptr)
                continue;

            auto targetValue = parameter->getDefaultValue();
            for (const auto& loaded : loadedValues)
            {
                if (loaded.first == parameter)
                    targetValue = loaded.second;
            }

            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (targetValue);
            parameter->endChangeGesture();
        }

        currentPresetName = loadedName.isNotEmpty() ? loadedName : fallbackName;
        currentFactoryPresetIndex = factoryIndex;
        currentUserPresetFile = userFile;
        storeCurrentPresetIdentity();
        refreshPresetFiles();
    }

    void beginInlineNameEdit()
    {
        if (nameEditor.isVisible())
            return;

        nameEditor.setText (currentPresetName, false);
        nameEditor.setVisible (true);
        nameEditor.toFront (true);
        nameEditor.grabKeyboardFocus();
        nameEditor.selectAll();
        repaint();
    }

    bool finishInlineNameEdit (bool acceptChanges)
    {
        if (! nameEditor.isVisible() || finishingNameEdit)
            return true;

        const auto safeName = sanitisePresetName (nameEditor.getText());
        if (acceptChanges && safeName.isEmpty())
        {
            juce::ScopedValueSetter<bool> finishing (finishingNameEdit, true);
            nameEditor.setText (currentPresetName, false);
            nameEditor.setVisible (false);
            repaint();
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Preset Not Named",
                "Please use at least one valid character in the preset name.");
            return false;
        }

        {
            juce::ScopedValueSetter<bool> finishing (finishingNameEdit, true);
            nameEditor.setVisible (false);
        }

        if (acceptChanges)
        {
            currentPresetName = safeName;
            storeCurrentPresetIdentity();
        }

        repaint();
        return true;
    }

    static juce::String sanitisePresetName (const juce::String& proposedName)
    {
        const auto trimmedName = proposedName.trim();
        const juce::String forbiddenCharacters { "<>:\"/\\|?*" };
        juce::String safeName;

        for (int index = 0; index < trimmedName.length(); ++index)
        {
            const auto character = trimmedName[index];
            if (character < 32 || forbiddenCharacters.containsChar (character))
                safeName += '_';
            else
                safeName += character;
        }

        safeName = safeName.substring (0, juce::jmin (48, safeName.length()))
                           .trim();
        while (safeName.endsWithChar ('.') || safeName.endsWithChar (' '))
            safeName = safeName.dropLastCharacters (1);

        const auto baseName = safeName.upToFirstOccurrenceOf (
            ".", false, false).toUpperCase();
        const auto isReservedDeviceName = baseName == "CON"
            || baseName == "PRN" || baseName == "AUX" || baseName == "NUL"
            || (baseName.length() == 4
                && (baseName.startsWith ("COM") || baseName.startsWith ("LPT"))
                && baseName[3] >= '1' && baseName[3] <= '9');
        if (isReservedDeviceName)
            safeName = "_" + safeName;

        return safeName.substring (0, juce::jmin (48, safeName.length()));
    }

    void storeCurrentPresetIdentity()
    {
        state.state.setProperty (juce::Identifier { "presetName" },
                                 currentPresetName,
                                 nullptr);

        const auto presetFileProperty = juce::Identifier { "presetFile" };
        if (currentUserPresetFile != juce::File())
        {
            state.state.setProperty (presetFileProperty,
                                     currentUserPresetFile.getFileName(),
                                     nullptr);
        }
        else
        {
            state.state.removeProperty (presetFileProperty, nullptr);
        }
    }

    void saveCurrentPreset()
    {
        if (currentFactoryPresetIndex >= 0
            || currentUserPresetFile == juce::File())
        {
            savePresetAs();
            return;
        }

        savePresetToFile (currentUserPresetFile);
    }

    void savePresetAs()
    {
        const auto safeName = sanitisePresetName (currentPresetName);
        if (safeName.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Preset Not Saved",
                "Please use at least one valid character in the preset name.");
            return;
        }

        const auto targetFile = getPortablePresetDirectory()
                                    .getChildFile (safeName + ".ini");
        savePresetToFile (targetFile);
    }

    bool savePresetToFile (const juce::File& targetFile)
    {
        const auto safeName = sanitisePresetName (currentPresetName);
        if (safeName.isEmpty())
            return false;

        const auto directory = targetFile.getParentDirectory();
        const auto directoryResult = directory.createDirectory();
        if (directoryResult.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Preset Not Saved",
                "IceCream could not create:\n" + directory.getFullPathName()
                    + "\n\n" + directoryResult.getErrorMessage());
            return false;
        }

        int parameterCount = 0;
        juce::String parameterLines;
        for (auto* baseParameter : processor.getParameters())
        {
            if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (baseParameter))
            {
                parameterLines << parameter->getParameterID() << "="
                               << juce::String (parameter->getValue(), 6)
                               << "\r\n";
                ++parameterCount;
            }
        }

        juce::String contents { "; IceCream portable preset\r\n" };
        contents << "[Preset]\r\n";
        contents << "FormatVersion=1\r\n";
        contents << "Name=" << safeName << "\r\n";
        contents << "ParameterCount=" << parameterCount << "\r\n\r\n";
        contents << "[Parameters]\r\n" << parameterLines;

        if (! targetFile.replaceWithText (contents))
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Preset Not Saved",
                "IceCream could not write:\n" + targetFile.getFullPathName());
            return false;
        }

        currentPresetName = safeName;
        currentFactoryPresetIndex = -1;
        currentUserPresetFile = targetFile;
        storeCurrentPresetIdentity();
        refreshPresetFiles();
        return true;
    }

    IceCreamAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    std::function<void (float)> setEditorScale;
    std::function<void (bool)> setEditorTheme;
    PresetActionButton fileButton { "MENU" };
    PresetActionButton previousButton { "<" };
    PresetActionButton nextButton { ">" };
    PresetActionButton nameButton { "NAME" };
    juce::TextEditor nameEditor;
    std::unique_ptr<juce::AlertWindow> aboutDialog;
    std::vector<juce::File> presetFiles;
    juce::String currentPresetName { "INITIAL" };
    int currentPresetIndex = -1;
    int currentFactoryPresetIndex = -1;
    juce::File currentUserPresetFile;
    bool finishingNameEdit = false;
    bool modernDarkTheme = false;
};

class BitcrusherDisplayButton final : public juce::TextButton
{
public:
    void paintButton (juce::Graphics& graphics,
                      bool isMouseOverButton,
                      bool isButtonDown) override
    {
        const auto bounds = getLocalBounds().toFloat();
        auto background = juce::Colour (0xffa6c78f);
        if (isButtonDown)
            background = juce::Colour (0xff8fb679);
        else if (isMouseOverButton)
            background = juce::Colour (0xffb4d29f);

        graphics.setColour (background);
        graphics.fillRoundedRectangle (bounds, 4.0f);
        graphics.setColour (juce::Colour (0xff5e7a50));
        graphics.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
        graphics.setColour (juce::Colour (0xff283822));
        graphics.setFont (juce::FontOptions { 12.5f });
        graphics.drawFittedText (getButtonText(), getLocalBounds().reduced (7, 2),
                                 juce::Justification::centredLeft, 1, 0.75f);
    }
};

class BitcrusherSection final : public juce::Component,
                                private juce::Timer
{
public:
    explicit BitcrusherSection (juce::AudioProcessorValueTreeState& state)
        : amountParameter (state.getParameter ("p06_bitrate")),
          bitsParameter (state.getParameter ("p53_crusher_bits"))
    {
        jassert (amountParameter != nullptr && bitsParameter != nullptr);

        amountSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        amountSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        amountSlider.setRange (0.0, 1.0);
        amountSlider.setDoubleClickReturnValue (
            true,
            amountParameter != nullptr
                ? amountParameter->convertFrom0to1 (amountParameter->getDefaultValue())
                : 0.897211);
        amountSlider.setColour (juce::Slider::rotarySliderFillColourId,
                                juce::Colour (0xff38608b));
        setOriginalKnobStyle (amountSlider, "crusher", juce::Colour (0xff43aa72));
        addAndMakeVisible (amountSlider);
        amountAttachment = std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, "p06_bitrate", amountSlider);

        for (auto* toggle : { &oscillator1Button, &oscillator2Button })
        {
            toggle->setButtonText ({});
            addAndMakeVisible (*toggle);
        }

        oscillator1Attachment = std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment> (
                state, "p04_crusher_osc1", oscillator1Button);
        oscillator2Attachment = std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment> (
                state, "p05_crusher_osc2", oscillator2Button);

        displayButton.setTooltip ("Click to change bit depth");
        displayButton.onClick = [this]
        {
            if (bitsParameter == nullptr)
                return;

            const auto currentIndex = juce::jlimit (
                0, 3, static_cast<int> (std::lround (
                    bitsParameter->convertFrom0to1 (bitsParameter->getValue()))));
            const auto nextIndex = (currentIndex + 1) % 4;
            bitsParameter->beginChangeGesture();
            bitsParameter->setValueNotifyingHost (
                bitsParameter->convertTo0to1 (static_cast<float> (nextIndex)));
            bitsParameter->endChangeGesture();
            updateDisplay();
        };
        addAndMakeVisible (displayButton);

        updateDisplay();
        startTimerHz (30);
    }

    void paint (juce::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (
            sectionBorderThickness * 0.5f);
        if (! isModernDarkTheme (*this))
        {
            graphics.setColour (juce::Colour (0xffcbdcf3));
            graphics.fillRoundedRectangle (bounds, 6.0f);
        }
        graphics.setColour (juce::Colour (0xff38608b));
        graphics.drawRoundedRectangle (bounds, 6.0f,
                                       sectionBorderThickness);

        graphics.setColour (juce::Colour (0xff38608b));
        graphics.setFont (juce::FontOptions { 9.5f, juce::Font::bold });
        graphics.drawText ("BITCRUSHER", 6, 2, 58, 16,
                           juce::Justification::centredLeft, false);
        graphics.setColour (themedTextColour (*this));
        graphics.setFont (juce::FontOptions { parameterLabelFontSize,
                                              juce::Font::bold });
        graphics.drawText ("AMOUNT", 6, 59, 48, 10,
                           juce::Justification::centred, false);
        graphics.drawText ("OSC1", 61, 59, 30, 10,
                           juce::Justification::centred, false);
        graphics.drawText ("OSC2", 104, 59, 30, 10,
                           juce::Justification::centred, false);
    }

    void resized() override
    {
        displayButton.setBounds (65, 5, juce::jmax (1, getWidth() - 69), 22);
        amountSlider.setBounds (9, 22, 42, 38);
        oscillator1Button.setBounds (65, 32, 22, 22);
        oscillator2Button.setBounds (108, 32, 22, 22);
    }

private:
    void timerCallback() override
    {
        updateDisplay();
    }

    void updateDisplay()
    {
        static constexpr std::array<const char*, 4> bitLabels {
            "32-BIT", "24-BIT", "16-BIT", "8-BIT"
        };
        const auto amount = amountParameter != nullptr
                                ? amountParameter->getValue()
                                : 0.897211f;
        const auto index = bitsParameter != nullptr
            ? juce::jlimit (0, 3, static_cast<int> (std::lround (
                  bitsParameter->convertFrom0to1 (bitsParameter->getValue()))))
            : 3;
        const auto text = juce::String (std::lround (100.0f * amount))
                        + "%  " + bitLabels[static_cast<std::size_t> (index)];

        if (displayButton.getButtonText() != text)
            displayButton.setButtonText (text);
    }

    juce::RangedAudioParameter* amountParameter = nullptr;
    juce::RangedAudioParameter* bitsParameter = nullptr;
    juce::Slider amountSlider;
    juce::ToggleButton oscillator1Button;
    juce::ToggleButton oscillator2Button;
    BitcrusherDisplayButton displayButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        oscillator1Attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        oscillator2Attachment;
};

class XYPadSection final : public juce::Component,
                           private juce::Timer
{
public:
    explicit XYPadSection (juce::AudioProcessorValueTreeState& state)
        : cutoffParameter (state.getParameter ("p16_filter_cutoff")),
          resonanceParameter (state.getParameter ("p17_filter_res"))
    {
        jassert (cutoffParameter != nullptr && resonanceParameter != nullptr);
        startTimerHz (30);
    }

    ~XYPadSection() override
    {
        if (dragging)
            endGestures();
    }

    void paint (juce::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (
            sectionBorderThickness * 0.5f);
        if (! isModernDarkTheme (*this))
        {
            graphics.setColour (juce::Colour (0xffdfdfdf));
            graphics.fillRoundedRectangle (bounds, 6.0f);
        }
        graphics.setColour (juce::Colour (0xff4d555b));
        graphics.drawRoundedRectangle (bounds, 6.0f,
                                       sectionBorderThickness);
        graphics.setColour (juce::Colour (0xff4d555b));
        graphics.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
        graphics.drawText ("XY PAD",
                           getLocalBounds().removeFromTop (18).reduced (7, 0),
                           juce::Justification::centredLeft,
                           false);

        const auto pad = getPadBounds().toFloat();
        graphics.setColour (juce::Colour (0xffedede7));
        graphics.fillRoundedRectangle (pad, 4.0f);
        graphics.setColour (juce::Colour (0xffc4c8c6));
        graphics.drawRoundedRectangle (pad.reduced (0.5f), 4.0f, 1.0f);
        graphics.setColour (juce::Colour (0xffdfe2dd));
        graphics.drawVerticalLine (static_cast<int> (pad.getCentreX()),
                                   pad.getY() + 3.0f,
                                   pad.getBottom() - 3.0f);
        graphics.drawHorizontalLine (static_cast<int> (pad.getCentreY()),
                                     pad.getX() + 3.0f,
                                     pad.getRight() - 3.0f);

        graphics.setColour (juce::Colour (0xffb8bcb7));
        graphics.setFont (juce::FontOptions { 16.0f, juce::Font::bold });
        graphics.drawText ("FILTER", pad.toNearestInt(),
                           juce::Justification::centred, false);
        graphics.setFont (juce::FontOptions { controlLabelFontSize,
                                              juce::Font::bold });
        graphics.setColour (juce::Colour (0xff6c716f));
        graphics.drawText ("CUTOFF", pad.toNearestInt().reduced (4, 1),
                           juce::Justification::bottomRight, false);
        graphics.saveState();
        graphics.addTransform (juce::AffineTransform::rotation (
            -juce::MathConstants<float>::halfPi,
            pad.getX() + 7.0f,
            pad.getCentreY()));
        graphics.drawText ("RES",
                           juce::Rectangle<float> {
                               pad.getX() - 0.5f * pad.getHeight() + 7.0f,
                               pad.getCentreY() - 5.0f,
                               pad.getHeight(),
                               10.0f
                           },
                           juce::Justification::centred,
                           false);
        graphics.restoreState();

        const auto position = getDotPosition();
        graphics.setColour (juce::Colours::black.withAlpha (0.22f));
        graphics.fillEllipse (position.x - 7.0f, position.y - 5.5f, 16.0f, 16.0f);
        graphics.setColour (juce::Colour (0xff22c744));
        graphics.fillEllipse (position.x - 8.0f, position.y - 8.0f, 16.0f, 16.0f);
        graphics.setColour (juce::Colour (0xff087f26));
        graphics.drawEllipse (position.x - 8.0f, position.y - 8.0f, 16.0f, 16.0f, 1.2f);
        graphics.setColour (juce::Colours::white.withAlpha (0.55f));
        graphics.fillEllipse (position.x - 4.5f, position.y - 5.0f, 4.0f, 4.0f);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (cutoffParameter == nullptr || resonanceParameter == nullptr
            || ! getPadBounds().contains (event.getPosition()))
            return;

        cutoffParameter->beginChangeGesture();
        resonanceParameter->beginChangeGesture();
        dragging = true;
        updateFromMouse (event.position);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        updateMouseCursor (event.getPosition());
        if (dragging)
            updateFromMouse (event.position);
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (! dragging)
            return;

        updateFromMouse (event.position);
        endGestures();
        updateMouseCursor (event.getPosition());
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        if (cutoffParameter == nullptr || resonanceParameter == nullptr
            || ! getPadBounds().contains (event.getPosition()))
            return;

        cutoffParameter->setValueNotifyingHost (cutoffParameter->getDefaultValue());
        resonanceParameter->setValueNotifyingHost (resonanceParameter->getDefaultValue());
        repaint();
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        updateMouseCursor (event.getPosition());
    }

    void mouseEnter (const juce::MouseEvent& event) override
    {
        updateMouseCursor (event.getPosition());
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

private:
    void updateMouseCursor (juce::Point<int> position)
    {
        setMouseCursor (getPadBounds().contains (position)
                            ? juce::MouseCursor::PointingHandCursor
                            : juce::MouseCursor::NormalCursor);
    }

    juce::Rectangle<int> getPadBounds() const
    {
        auto bounds = getLocalBounds().reduced (5);
        bounds.removeFromTop (14);
        return bounds;
    }

    juce::Point<float> getDotPosition() const
    {
        const auto pad = getPadBounds().toFloat().reduced (8.0f);
        const auto cutoff = cutoffParameter != nullptr ? cutoffParameter->getValue() : 0.5f;
        const auto resonance = resonanceParameter != nullptr
                                   ? resonanceParameter->getValue() : 0.5f;
        return { pad.getX() + cutoff * pad.getWidth(),
                 pad.getBottom() - resonance * pad.getHeight() };
    }

    void updateFromMouse (juce::Point<float> position)
    {
        const auto pad = getPadBounds().toFloat().reduced (8.0f);
        const auto cutoff = juce::jlimit (
            0.0f, 1.0f, (position.x - pad.getX()) / juce::jmax (1.0f, pad.getWidth()));
        const auto resonance = juce::jlimit (
            0.0f, 1.0f, (pad.getBottom() - position.y) / juce::jmax (1.0f, pad.getHeight()));
        cutoffParameter->setValueNotifyingHost (cutoff);
        resonanceParameter->setValueNotifyingHost (resonance);
        repaint();
    }

    void endGestures()
    {
        cutoffParameter->endChangeGesture();
        resonanceParameter->endChangeGesture();
        dragging = false;
    }

    void timerCallback() override
    {
        if (! dragging)
            repaint();
    }

    juce::RangedAudioParameter* cutoffParameter = nullptr;
    juce::RangedAudioParameter* resonanceParameter = nullptr;
    bool dragging = false;
};

class KeyboardSection final : public juce::Component,
                              private juce::Timer
{
public:
    explicit KeyboardSection (juce::MidiKeyboardState& stateToUse)
        : keyboardState (stateToUse)
    {
        startTimerHz (30);
    }

    ~KeyboardSection() override
    {
        stopTimer();
        for (int note = 0; note < static_cast<int> (latchedNotes.size()); ++note)
        {
            if (latchedNotes[static_cast<size_t> (note)]
                || momentaryNotes[static_cast<size_t> (note)])
            {
                keyboardState.noteOff (midiChannel, note, 0.0f);
            }
        }
    }

    void paint (juce::Graphics& graphics) override
    {
        auto bounds = getLocalBounds();
        if (! isModernDarkTheme (*this))
        {
            graphics.setColour (juce::Colour (0xfffffde8));
            graphics.fillRoundedRectangle (bounds.toFloat(), 6.0f);
        }
        graphics.setColour (juce::Colour (0xff6d6652));
        graphics.drawRoundedRectangle (
            bounds.toFloat().reduced (sectionBorderThickness * 0.5f),
            6.0f, sectionBorderThickness);

        auto header = bounds.removeFromTop (18).reduced (7, 0);
        graphics.setColour (juce::Colour (0xff6d6652));
        graphics.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
        graphics.drawText ("KEYBOARD", header,
                           juce::Justification::centredLeft, false);
        graphics.setFont (juce::FontOptions { parameterLabelFontSize,
                                              juce::Font::bold });
        graphics.drawText ("LEFT: PLAY   RIGHT: HOLD", header,
                           juce::Justification::centredRight, false);

        const auto keys = getKeysBounds();
        const auto whiteWidth = static_cast<float> (keys.getWidth())
                              / static_cast<float> (whiteKeyCount);

        for (int key = 0; key < whiteKeyCount; ++key)
        {
            const auto note = noteForWhiteKey (key);
            const auto x = static_cast<float> (keys.getX()) + whiteWidth * key;
            const auto keyBounds = juce::Rectangle<float> {
                x, static_cast<float> (keys.getY()), whiteWidth,
                static_cast<float> (keys.getHeight())
            };
            const auto keyColour = getWhiteKeyColour (note);
            juce::ColourGradient whiteKeyGradient (
                keyColour.brighter (0.035f), keyBounds.getX(), keyBounds.getY(),
                keyColour.darker (0.025f), keyBounds.getX(), keyBounds.getBottom(),
                false);
            graphics.setGradientFill (whiteKeyGradient);
            graphics.fillRect (keyBounds);
            graphics.setColour (juce::Colour (0xff25272a));
            graphics.drawRect (keyBounds, 0.8f);
        }

        for (int octave = 0; octave < octaveCount; ++octave)
        {
            for (int key = 0; key < blackKeysPerOctave; ++key)
            {
                const auto note = firstNote + octave * notesPerOctave
                                + blackNotePattern[static_cast<size_t> (key)];
                const auto keyBounds = getBlackKeyBounds (octave, key, keys, whiteWidth);
                const auto keyColour = getBlackKeyColour (note);
                graphics.setColour (juce::Colours::black.withAlpha (0.24f));
                graphics.fillRoundedRectangle (keyBounds.translated (0.7f, 1.1f), 1.5f);
                juce::ColourGradient blackKeyGradient (
                    keyColour.darker (0.62f), keyBounds.getX(), keyBounds.getY(),
                    keyColour.darker (0.48f), keyBounds.getRight(), keyBounds.getY(),
                    false);
                blackKeyGradient.addColour (0.50, keyColour.brighter (0.72f));
                graphics.setGradientFill (blackKeyGradient);
                graphics.fillRoundedRectangle (keyBounds, 1.5f);
                graphics.setColour (juce::Colour (0xff111126));
                graphics.drawRoundedRectangle (keyBounds.reduced (0.45f),
                                               1.5f, 0.9f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        updateMouseCursor (event.getPosition());
        const auto note = noteAtPosition (event.position);
        if (note < 0)
            return;

        if (latchedNotes[static_cast<size_t> (note)])
        {
            releaseLatchedNote (note);
            return;
        }

        if (event.mods.isRightButtonDown())
            latchNote (note);
        else if (event.mods.isLeftButtonDown())
            startMomentaryNote (note);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        updateMouseCursor (event.getPosition());
        if (! event.mods.isLeftButtonDown() || momentaryNote < 0)
            return;

        const auto note = noteAtPosition (event.position);
        if (note == momentaryNote)
            return;

        releaseMomentaryNote();
        if (note >= 0 && ! latchedNotes[static_cast<size_t> (note)])
            startMomentaryNote (note);
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (! event.mods.isRightButtonDown())
            releaseMomentaryNote();
        updateMouseCursor (event.getPosition());
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        updateMouseCursor (event.getPosition());
    }

    void mouseEnter (const juce::MouseEvent& event) override
    {
        updateMouseCursor (event.getPosition());
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

private:
    void updateMouseCursor (juce::Point<int> position)
    {
        setMouseCursor (getKeysBounds().contains (position)
                            ? juce::MouseCursor::PointingHandCursor
                            : juce::MouseCursor::NormalCursor);
    }

    static constexpr int midiChannel = 1;
    static constexpr int allMidiChannels = 0xffff;
    static constexpr int firstNote = 24;
    static constexpr int notesPerOctave = 12;
    static constexpr int octaveCount = 5;
    static constexpr int whiteKeysPerOctave = 7;
    static constexpr int blackKeysPerOctave = 5;
    static constexpr int whiteKeyCount = octaveCount * whiteKeysPerOctave;
    static constexpr std::array<int, whiteKeysPerOctave> whiteNotePattern {
        0, 2, 4, 5, 7, 9, 11
    };
    static constexpr std::array<int, blackKeysPerOctave> blackNotePattern {
        1, 3, 6, 8, 10
    };
    static constexpr std::array<int, blackKeysPerOctave> blackWhiteKeyPattern {
        0, 1, 3, 4, 5
    };

    juce::Rectangle<int> getKeysBounds() const
    {
        auto keys = getLocalBounds().reduced (5);
        keys.removeFromTop (14);
        return keys;
    }

    static int noteForWhiteKey (int whiteKey)
    {
        return firstNote + (whiteKey / whiteKeysPerOctave) * notesPerOctave
             + whiteNotePattern[static_cast<size_t> (whiteKey % whiteKeysPerOctave)];
    }

    static juce::Rectangle<float> getBlackKeyBounds (
        int octave,
        int key,
        juce::Rectangle<int> keys,
        float whiteWidth)
    {
        const auto whiteIndex = octave * whiteKeysPerOctave
                              + blackWhiteKeyPattern[static_cast<size_t> (key)] + 1;
        const auto x = static_cast<float> (keys.getX())
                     + whiteWidth * static_cast<float> (whiteIndex)
                     - whiteWidth * 0.31f;
        return { x, static_cast<float> (keys.getY()), whiteWidth * 0.62f,
                 static_cast<float> (keys.getHeight()) * 0.62f };
    }

    int noteAtPosition (juce::Point<float> position) const
    {
        const auto keys = getKeysBounds();
        if (! keys.toFloat().contains (position))
            return -1;

        const auto whiteWidth = static_cast<float> (keys.getWidth())
                              / static_cast<float> (whiteKeyCount);

        for (int octave = 0; octave < octaveCount; ++octave)
        {
            for (int key = 0; key < blackKeysPerOctave; ++key)
            {
                if (getBlackKeyBounds (octave, key, keys, whiteWidth).contains (position))
                    return firstNote + octave * notesPerOctave
                         + blackNotePattern[static_cast<size_t> (key)];
            }
        }

        const auto whiteKey = juce::jlimit (
            0, whiteKeyCount - 1,
            static_cast<int> ((position.x - static_cast<float> (keys.getX()))
                              / juce::jmax (1.0f, whiteWidth)));
        return noteForWhiteKey (whiteKey);
    }

    juce::Colour getWhiteKeyColour (int note) const
    {
        if (latchedNotes[static_cast<size_t> (note)])
            return juce::Colour (0xffff9fcf);
        if (momentaryNotes[static_cast<size_t> (note)]
            || keyboardState.isNoteOnForChannels (allMidiChannels, note))
            return juce::Colour (0xffffd2e8);
        return juce::Colour (0xffffffe3);
    }

    juce::Colour getBlackKeyColour (int note) const
    {
        if (latchedNotes[static_cast<size_t> (note)])
            return juce::Colour (0xffa83462);
        if (momentaryNotes[static_cast<size_t> (note)]
            || keyboardState.isNoteOnForChannels (allMidiChannels, note))
            return juce::Colour (0xffcf6290);
        return juce::Colour (0xff24223f);
    }

    void timerCallback() override
    {
        repaint();
    }

    void startMomentaryNote (int note)
    {
        if (momentaryNote == note || latchedNotes[static_cast<size_t> (note)])
            return;

        releaseMomentaryNote();
        momentaryNote = note;
        momentaryNotes[static_cast<size_t> (note)] = true;
        keyboardState.noteOn (midiChannel, note, 0.9f);
        repaint();
    }

    void releaseMomentaryNote()
    {
        if (momentaryNote < 0)
            return;

        momentaryNotes[static_cast<size_t> (momentaryNote)] = false;
        keyboardState.noteOff (midiChannel, momentaryNote, 0.0f);
        momentaryNote = -1;
        repaint();
    }

    void latchNote (int note)
    {
        latchedNotes[static_cast<size_t> (note)] = true;
        keyboardState.noteOn (midiChannel, note, 0.9f);
        repaint();
    }

    void releaseLatchedNote (int note)
    {
        latchedNotes[static_cast<size_t> (note)] = false;
        keyboardState.noteOff (midiChannel, note, 0.0f);
        repaint();
    }

    juce::MidiKeyboardState& keyboardState;
    std::array<bool, 128> latchedNotes {};
    std::array<bool, 128> momentaryNotes {};
    int momentaryNote = -1;
};
}

struct IceCreamAudioProcessorEditor::Content final : public juce::Component
{
    explicit Content (IceCreamAudioProcessor& processor,
                      std::function<void (float)> editorScaleSetter,
                      bool initialModernDarkTheme,
                      std::function<void (bool)> editorThemeSetter)
    {
        setLookAndFeel (&lookAndFeel);
        auto& state = processor.getParameterState();
        headerArtworkLight = loadDrawableResource ("IceCreamHeader_png");
        headerArtworkDark = loadDrawableResource ("IceCreamHeaderDark_png");
        headerCharacter = loadDrawableResource ("IceCreamGirl_png");

        const auto makeSection = [this, &state] (
                                     const juce::String& name,
                                     std::initializer_list<const char*> ids,
                                     int columns,
                                     juce::Colour background,
                                     juce::Colour accent)
        {
            auto section = std::make_unique<ParameterSection> (
                name, state, ids, columns, background, accent);
            addAndMakeVisible (*section);
            return section;
        };

        xyPad = std::make_unique<XYPadSection> (state);
        addAndMakeVisible (*xyPad);
        presets = std::make_unique<PresetSection> (
            processor,
            std::move (editorScaleSetter),
            initialModernDarkTheme,
            [this, editorThemeSetter = std::move (editorThemeSetter)] (bool enabled)
            {
                setModernDarkTheme (enabled);
                if (editorThemeSetter)
                    editorThemeSetter (enabled);
            });
        addAndMakeVisible (*presets);
        addMouseListener (presets.get(), true);
        master = makeSection ("MASTER", { "p24_main_volume" }, 1,
                              juce::Colour (0xfffff3dc), juce::Colour (0xffd34b31));
        equalizer = std::make_unique<EqualizerSection> (state);
        addAndMakeVisible (*equalizer);
        sequencer = std::make_unique<StepSequencerSection> (processor);
        addAndMakeVisible (*sequencer);
        oscillator1 = makeSection ("OSC1",
                                   { "p31_osc2_osc1", "p26_osc1_volume",
                                     "p29_osc1_octave" },
                                   2, juce::Colour (0xfffff6a8), juce::Colour (0xff278f67));
        oscillator1->setBorderColour (juce::Colour (0xffb38a19));
        oscillator2 = makeSection ("OSC2",
                                   { "p32_osc2_rate", "p27_osc2_volume",
                                     "p30_osc2_octave", "p28_osc2_frequency" },
                                   2, juce::Colour (0xffffc8e7), juce::Colour (0xffa83462));
        filter = makeSection ("FILTER",
                              { "p19_filter_type", "p16_filter_cutoff",
                                "p18_filter_tracking", "p17_filter_res" },
                              2, juce::Colour (0xffdfdfdf), juce::Colour (0xff4c555c));
        ampEnvelope = makeSection ("AMP ENV",
                                   { "p00_amp_attack", "p01_amp_decay",
                                     "p03_amp_sustain", "p02_amp_release" },
                                   4, juce::Colour (0xffc6f7c5), juce::Colour (0xff238655));
        filterEnvelope = makeSection ("FILT ENV",
                                      { "p12_filter_env_attack", "p13_filter_env_decay",
                                        "p15_filter_env_sustain", "p14_filter_env_release",
                                        "p11_filter_env_amount" },
                                      5, juce::Colour (0xffffc8cf), juce::Colour (0xffbb4c59));
        control = makeSection ("CONTROL",
                               { "p25_monopoly", "p22_harmonix",
                                 "p20_glide_on", "p21_glide_rate",
                                 "p44_character" },
                               5, juce::Colour (0xffdedede), juce::Colour (0xff536878));
        reverb = makeSection ("REVERB",
                              { "p35_reverb_on", "p36_reverb_room", "p33_reverb_damp",
                                "p37_reverb_width", "p34_reverb_mix" },
                              5, juce::Colour (0xffd9d9d9), juce::Colour (0xff536878));
        delay = makeSection ("DELAY",
                             { "p08_delay_on", "p09_delay_time",
                               "p10_delay_feedback", "p07_delay_mix" },
                             4, juce::Colour (0xffffeebd), juce::Colour (0xff6f6036));
        bitcrusher = std::make_unique<BitcrusherSection> (state);
        addAndMakeVisible (*bitcrusher);
        keyboard = std::make_unique<KeyboardSection> (processor.getKeyboardState());
        addAndMakeVisible (*keyboard);

        control->useEnvelopeSizedControls();
        control->useBitcrusherSizedKnobs();
        control->moveLabelsUp (3);
        reverb->useEnvelopeSizedControls();
        delay->useEnvelopeSizedControls();
        filter->useFilterLayout();
        master->useMasterVolumeStyle();
        applyPointingHandCursors (*this);
        setModernDarkTheme (initialModernDarkTheme);
    }

    ~Content() override
    {
        removeMouseListener (presets.get());
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& graphics) override
    {
        const auto modernDark = lookAndFeel.isModernDarkTheme();
        graphics.fillAll (modernDark ? juce::Colour (0xff15181d)
                                     : juce::Colour (0xfff4f4ed));

        const auto colourBar = juce::Rectangle<float> { 301.0f, 61.0f, 344.0f, 9.0f };
        graphics.setColour (juce::Colours::black.withAlpha (0.24f));
        graphics.fillRoundedRectangle (colourBar.translated (0.0f, 1.0f), 1.5f);
        juce::ColourGradient spectrum (
            juce::Colour (0xff35d34a), colourBar.getX(), colourBar.getCentreY(),
            juce::Colour (0xffef1d24), colourBar.getRight(), colourBar.getCentreY(),
            false);
        spectrum.addColour (0.025, juce::Colour (0xff35d34a));
        spectrum.addColour (0.075, juce::Colour (0xfff20a83));
        spectrum.addColour (0.20, juce::Colour (0xff73005f));
        spectrum.addColour (0.31, juce::Colour (0xff10166f));
        spectrum.addColour (0.43, juce::Colour (0xff008bd0));
        spectrum.addColour (0.52, juce::Colour (0xff00dff1));
        spectrum.addColour (0.64, juce::Colour (0xff00c876));
        spectrum.addColour (0.72, juce::Colour (0xff91df2d));
        spectrum.addColour (0.80, juce::Colour (0xffffe000));
        spectrum.addColour (0.89, juce::Colour (0xffff8200));
        graphics.setGradientFill (spectrum);
        graphics.fillRoundedRectangle (colourBar, 1.5f);

        juce::ColourGradient barSheen (
            juce::Colours::white.withAlpha (0.20f),
            colourBar.getCentreX(), colourBar.getY(),
            juce::Colours::black.withAlpha (0.12f),
            colourBar.getCentreX(), colourBar.getBottom(), false);
        graphics.setGradientFill (barSheen);
        graphics.fillRoundedRectangle (colourBar, 1.5f);

        const auto brandBounds = juce::Rectangle<float> { 309.0f, 6.0f, 358.0f, 82.0f };
        auto* const artwork = modernDark ? headerArtworkDark.get()
                                         : headerArtworkLight.get();
        if (artwork != nullptr)
        {
            artwork->drawWithin (graphics, brandBounds,
                                 juce::RectanglePlacement::centred, 1.0f);
        }

        drawHeaderChecker (graphics, modernDark);
        drawIceCreamWordmark (graphics, modernDark);

        auto* const character = headerCharacter.get();
        if (character != nullptr)
        {
            juce::Graphics::ScopedSaveState characterState (graphics);
            graphics.setImageResamplingQuality (
                juce::Graphics::highResamplingQuality);
            const auto characterBounds = juce::Rectangle<float> {
                590.0f, 3.0f, 81.0f, 83.0f
            };
            character->drawWithin (graphics, characterBounds,
                                   juce::RectanglePlacement::centred, 1.0f);
        }
    }

    void setModernDarkTheme (bool enabled)
    {
        lookAndFeel.setModernDarkTheme (enabled);
        repaintRecursively (*this);
    }

    void resized() override
    {
        xyPad->setBounds (6, 6, 146, 82);
        presets->setBounds (158, 6, 145, 82);
        master->setBounds (673, 6, 181, 82);
        sequencer->setBounds (6, 94, 146, 150);
        oscillator1->setBounds (158, 94, 145, 150);
        oscillator2->setBounds (309, 94, 145, 150);
        filter->setBounds (460, 94, 207, 150);
        ampEnvelope->setBounds (673, 94, 181, 72);
        filterEnvelope->setBounds (673, 172, 181, 72);
        equalizer->setBounds (6, 250, 146, 106);
        control->setBounds (158, 250, 296, 72);
        reverb->setBounds (460, 250, 207, 72);
        delay->setBounds (673, 250, 181, 72);
        bitcrusher->setBounds (6, 362, 146, 72);
        keyboard->setBounds (158, 328, 696, 106);
    }

    DevelopmentLookAndFeel lookAndFeel;
    std::unique_ptr<juce::Drawable> headerArtworkLight;
    std::unique_ptr<juce::Drawable> headerArtworkDark;
    std::unique_ptr<juce::Drawable> headerCharacter;
    std::unique_ptr<XYPadSection> xyPad;
    std::unique_ptr<PresetSection> presets;
    std::unique_ptr<ParameterSection> master;
    std::unique_ptr<EqualizerSection> equalizer;
    std::unique_ptr<StepSequencerSection> sequencer;
    std::unique_ptr<ParameterSection> oscillator1;
    std::unique_ptr<ParameterSection> oscillator2;
    std::unique_ptr<ParameterSection> filter;
    std::unique_ptr<ParameterSection> ampEnvelope;
    std::unique_ptr<ParameterSection> filterEnvelope;
    std::unique_ptr<ParameterSection> control;
    std::unique_ptr<ParameterSection> reverb;
    std::unique_ptr<ParameterSection> delay;
    std::unique_ptr<BitcrusherSection> bitcrusher;
    std::unique_ptr<KeyboardSection> keyboard;
};

IceCreamAudioProcessorEditor::IceCreamAudioProcessorEditor (
    IceCreamAudioProcessor& processor)
    : AudioProcessorEditor (processor)
{
    const auto settings = loadInterfaceSettings();
    modernDarkTheme = settings.modernDarkTheme;
    currentScale = settings.scale;

    content = std::make_unique<Content> (
        processor,
        [this] (float scale)
        {
            currentScale = juce::jlimit (0.75f, 2.0f, scale);
            setSize (static_cast<int> (std::lround (editorWidth * currentScale)),
                     static_cast<int> (std::lround (editorHeight * currentScale)));
        },
        modernDarkTheme,
        [this] (bool enabled)
        {
            modernDarkTheme = enabled;
            scheduleSettingsSave();
        });

    setOpaque (true);
    addAndMakeVisible (*content);
    setResizable (true, true);
    setResizeLimits (645, 330, 1720, 880);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio (
            static_cast<double> (editorWidth) / editorHeight);
    setSize (static_cast<int> (std::lround (editorWidth * currentScale)),
             static_cast<int> (std::lround (editorHeight * currentScale)));
    settingsReady = true;
    scheduleSettingsSave();
}

IceCreamAudioProcessorEditor::~IceCreamAudioProcessorEditor()
{
    stopTimer();
    if (settingsReady)
        writeInterfaceSettings (modernDarkTheme, currentScale);
}

void IceCreamAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour (0xfff4f4ed));
}

void IceCreamAudioProcessorEditor::resized()
{
    if (settingsReady)
    {
        currentScale = juce::jlimit (
            0.75f, 2.0f, static_cast<float> (getWidth()) / editorWidth);
        scheduleSettingsSave();
    }

    content->setTransform (juce::AffineTransform());
    content->setBounds (0, 0, editorWidth, editorHeight);
    content->setTransform (juce::AffineTransform::scale (
        static_cast<float> (getWidth()) / editorWidth,
        static_cast<float> (getHeight()) / editorHeight));
}

void IceCreamAudioProcessorEditor::scheduleSettingsSave()
{
    if (! settingsReady)
        return;

    stopTimer();
    startTimer (350);
}

void IceCreamAudioProcessorEditor::timerCallback()
{
    stopTimer();
    writeInterfaceSettings (modernDarkTheme, currentScale);
}
