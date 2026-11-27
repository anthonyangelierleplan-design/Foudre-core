juce_add_console_app(FoudreBanc
    PRODUCT_NAME "foudre-banc"
    COMPANY_NAME "Foudre Audio")

set_target_properties(FoudreBanc PROPERTIES OUTPUT_NAME "foudre-banc")

target_sources(FoudreBanc PRIVATE
    Source/Main.cpp
    Source/Console.cpp
    Source/Capture.cpp)

target_compile_definitions(FoudreBanc PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

if(MSVC)
    target_compile_options(FoudreBanc PRIVATE /utf-8)
endif()

target_link_libraries(FoudreBanc
    PRIVATE
        foudre::core
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
