cmake_minimum_required(VERSION 3.25)

project(IceCream VERSION 1.25.0 LANGUAGES C CXX)

set(JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../_Tools/JUCE/_8.0.15")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE 8.0.15 was not found. Expected: ${JUCE_DIR}/CMakeLists.txt")
endif()

add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

juce_add_plugin(IceCream
    VERSION 1.25.0
    COMPANY_NAME "SynthMaker"
    BUNDLE_ID "com.synthmaker.icecream"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST FALSE
    PLUGIN_MANUFACTURER_CODE SyMk
    FORMATS VST3
    VST3_CATEGORIES Instrument Synth
    PRODUCT_NAME "ICECREAM"
    # Keep this last: its final '[' is significant to CMake list parsing.
    # Original VST2 unique ID: 0x5536775B, text "U6w[".
    PLUGIN_CODE "U6w[")

juce_generate_juce_header(IceCream)

file(GLOB FACTORY_PRESET_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/Data/Presets/Factory_*.ini")
list(LENGTH FACTORY_PRESET_FILES FACTORY_PRESET_COUNT)
if(NOT FACTORY_PRESET_COUNT EQUAL 32)
    message(FATAL_ERROR
        "Expected exactly 32 embedded factory presets, found ${FACTORY_PRESET_COUNT}")
endif()

juce_add_binary_data(IceCreamBinaryData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        resources/IceCreamHeader.png
        resources/IceCreamHeaderDark.png
        resources/IceCreamGirl.png
        resources/saw2KtableSM.dat
        resources/tri2KtableSM.dat
        ${FACTORY_PRESET_FILES})

target_sources(IceCream
    PRIVATE
        PluginEditor.cpp
        PluginEditor.h
        PluginProcessor.cpp
        PluginProcessor.h)

target_compile_features(IceCream PRIVATE cxx_std_17)

target_compile_definitions(IceCream
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(IceCream
    PRIVATE
        IceCreamBinaryData
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
