cmake_minimum_required(VERSION 3.16..3.22)

get_filename_component(CURRENT_FOLDER ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${CURRENT_FOLDER} VERSION 0.0.1)

set(JUCE_PATH "/Users/jaketyler/Documents/Code/Libraries/JUCE/Library/")
add_subdirectory(${JUCE_PATH} "${PROJECT_BINARY_DIR}/JUCE")

juce_add_plugin(${PROJECT_NAME}
    COPY_PLUGIN_AFTER_BUILD TRUE
    COMPANY_NAME "dev.juce.plugins"
    PLUGIN_MANUFACTURER_CODE Juce
    PLUGIN_CODE Juc3
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "${PROJECT_NAME}")

juce_generate_juce_header(${PROJECT_NAME})

target_sources(${PROJECT_NAME} 
PRIVATE
    Plugin/PluginEditor.cpp
    Plugin/PluginProcessor.cpp
    main.cpp)

target_compile_definitions(${PROJECT_NAME} 
PUBLIC
    JUCE_DISABLE_JUCE_VERSION_PRINTING=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(${PROJECT_NAME}
PRIVATE
    juce::juce_audio_utils
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
