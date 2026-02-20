cmake_minimum_required(VERSION 3.24)
project(ZitaRev1OS VERSION 0.1.0)

if(MSVC)
    add_compile_options(/wd4819 /utf-8)
endif()

if(DEFINED ENV{JUCE_PATH})
    set(JUCE_DIR "$ENV{JUCE_PATH}")
else()
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE")
endif()
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

juce_add_plugin(ZitaRev1OS
    COMPANY_NAME           "OpenSource"
    IS_SYNTH               FALSE
    NEEDS_MIDI_INPUT       FALSE
    NEEDS_MIDI_OUTPUT      FALSE
    PLUGIN_MANUFACTURER_CODE Opsc
    PLUGIN_CODE            Ztro
    FORMATS                VST3 Standalone
    PRODUCT_NAME           "ZitaRev1OS"
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(ZitaRev1OS)

target_sources(ZitaRev1OS
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/zita/reverb.cc
        Source/zita/pareq.cc
)

target_compile_definitions(ZitaRev1OS
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    PRIVATE
        _USE_MATH_DEFINES
)

target_include_directories(ZitaRev1OS
    PRIVATE
        Source
        Source/zita
)

target_link_libraries(ZitaRev1OS
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
