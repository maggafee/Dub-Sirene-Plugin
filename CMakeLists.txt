cmake_minimum_required(VERSION 3.22)
project(DUB_SIRENE_VST VERSION 1.2.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE: lokal (-DJUCE_DIR=...) oder automatisch von GitHub
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.10 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

file(GLOB DUB_ASSETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.png ${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.jpg)
juce_add_binary_data(DubAssets SOURCES ${DUB_ASSETS})
set_target_properties(DubAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(DUB_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DUB_FORMATS AU)
endif()

function(dub_plugin target name code synth)
    juce_add_plugin(${target}
        PRODUCT_NAME "${name}"
        COMPANY_NAME "GeyserHaus e.V."
        BUNDLE_ID "de.geyserhaus.${target}"
        MANUFACTURER_CODE Gysr
        PLUGIN_CODE ${code}
        IS_SYNTH ${synth}
        NEEDS_MIDI_INPUT ${synth}
        NEEDS_MIDI_OUTPUT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon.png
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "Dub Tape Echo verarbeitet dein Eingangssignal."
        VST3_CATEGORIES $<IF:$<BOOL:${synth}>,Instrument,Fx>
        FORMATS ${DUB_FORMATS})
    juce_generate_juce_header(${target})
    target_sources(${target} PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(${target} PUBLIC
        DUB_INSTRUMENT=$<IF:$<BOOL:${synth}>,1,0>
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(${target} PRIVATE DubAssets juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endfunction()

dub_plugin(DubSirene "Dub Sirene" DbSr TRUE)
dub_plugin(DubTapeEcho "Dubasco Tape Echo" DbEc FALSE)

# Prüfprogramme (nur lokal: -DDUB_TESTS=ON)
option(DUB_TESTS "Build test renderers" OFF)
if(DUB_TESTS)
    foreach(kind Siren Echo)
        juce_add_console_app(DubTest${kind} PRODUCT_NAME "DubTest${kind}")
        juce_generate_juce_header(DubTest${kind})
        target_sources(DubTest${kind} PRIVATE Tests/Test.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
        if(kind STREQUAL "Siren")
            target_compile_definitions(DubTest${kind} PRIVATE DUB_INSTRUMENT=1 JucePlugin_Name="Dub Sirene")
        else()
            target_compile_definitions(DubTest${kind} PRIVATE DUB_INSTRUMENT=0 JucePlugin_Name="Dubasco Tape Echo")
        endif()
        target_compile_definitions(DubTest${kind} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(DubTest${kind} PRIVATE DubAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    endforeach()
endif()
