cmake_minimum_required(VERSION 3.24)
project(Sediments VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE -------------------------------------------------------------------
# Uses a JUCE checkout next to this file if present, otherwise downloads it.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt)
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# --- libtorch ---------------------------------------------------------------
# Normal case: pass -DCMAKE_PREFIX_PATH=C:/libtorch (the folder you unzipped).
# Recent libtorch releases require C++20 (set above).
# LT_TORCH_ROOT is an alternative that links torch_cpu + c10 by hand.
set(LT_TORCH_ROOT "" CACHE PATH "Optional: libtorch root to link manually (include/ and lib/)")

add_library(lt_torch INTERFACE)
if(LT_TORCH_ROOT)
    target_include_directories(lt_torch INTERFACE
        ${LT_TORCH_ROOT}/include
        ${LT_TORCH_ROOT}/include/torch/csrc/api/include)
    target_link_directories(lt_torch INTERFACE ${LT_TORCH_ROOT}/lib)
    target_link_libraries(lt_torch INTERFACE torch_cpu c10)
    if(UNIX)
        target_link_options(lt_torch INTERFACE "-Wl,-rpath,${LT_TORCH_ROOT}/lib")
        target_compile_definitions(lt_torch INTERFACE _GLIBCXX_USE_CXX11_ABI=1)
    endif()
    set(LT_TORCH_LIB_DIR ${LT_TORCH_ROOT}/lib)
else()
    find_package(Torch REQUIRED)
    target_link_libraries(lt_torch INTERFACE ${TORCH_LIBRARIES})
    get_filename_component(LT_TORCH_LIB_DIR "${TORCH_INSTALL_PREFIX}/lib" ABSOLUTE)
endif()

set(LT_CORE_SOURCES
    Source/RaveModel.cpp
    Source/Corpus.cpp
    Source/LearnedTerrain.cpp
    Source/Analysis.cpp
    Source/GrainPlayer.cpp
    Source/Spatial.cpp
    Source/LatentEngine.cpp)

# --- Fonts bundled with the plugin (SIL Open Font License, see Resources/fonts) ---
juce_add_binary_data(LtFonts
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        Resources/fonts/AtkinsonHyperlegible-Regular.ttf
        Resources/fonts/AtkinsonHyperlegible-Bold.ttf
        Resources/fonts/OpenDyslexic-Regular.otf
        Resources/fonts/OpenDyslexic-Bold.otf)
set_target_properties(LtFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(LT_UI_SOURCES
    Source/ui/UiCore.cpp
    Source/ui/Widgets.cpp
    Source/ui/MapView.cpp
    Source/ui/Panels.cpp)

# --- The plugin ---------------------------------------------------------------
# Audio Unit on macOS too (Logic, GarageBand, Reaper all accept it).
set(LT_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND LT_FORMATS AU)
endif()

juce_add_plugin(Sediments
    COMPANY_NAME "Antoine Boucherikha"
    BUNDLE_ID com.boucherikha.sediments
    PRODUCT_NAME "Sediments"
    PLUGIN_MANUFACTURER_CODE Lpfl
    PLUGIN_CODE Sdmt
    FORMATS ${LT_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    # The VST3 manifest helper would have to load libtorch at build time; skip it.
    VST3_AUTO_MANIFEST FALSE)

target_sources(Sediments PRIVATE
    ${LT_CORE_SOURCES}
    ${LT_UI_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Sediments PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Sediments
    PRIVATE
        lt_torch
        LtFonts
        juce::juce_dsp
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

if(MSVC)
    target_compile_options(Sediments PRIVATE /bigobj /utf-8)
endif()

# On Windows, put the libtorch DLLs next to the Standalone .exe so it runs as-is.
# On macOS, libtorch travels inside each bundle (Contents/Frameworks), so the
# plugin works on its own with nothing else installed.
if(APPLE)
    target_link_options(Sediments INTERFACE "-Wl,-rpath,@loader_path/../Frameworks")
    file(GLOB LT_TORCH_DYLIBS "${LT_TORCH_LIB_DIR}/*.dylib")
    foreach(fmt VST3 AU Standalone)
        if(TARGET Sediments_${fmt})
            add_custom_command(TARGET Sediments_${fmt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:Sediments_${fmt}>/Frameworks"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LT_TORCH_DYLIBS} "$<TARGET_BUNDLE_CONTENT_DIR:Sediments_${fmt}>/Frameworks"
                COMMENT "Copying libtorch into the ${fmt} bundle")
        endif()
    endforeach()
endif()

if(WIN32)
    file(GLOB LT_TORCH_DLLS "${LT_TORCH_LIB_DIR}/*.dll")
    add_custom_command(TARGET Sediments_Standalone POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LT_TORCH_DLLS} "$<TARGET_FILE_DIR:Sediments_Standalone>"
        COMMENT "Copying libtorch DLLs next to the standalone app")
endif()

# --- Command-line test (optional) -------------------------------------------
option(LT_BUILD_CLI "Build the sediments_cli test tool" OFF)
if(LT_BUILD_CLI)
    juce_add_console_app(sediments_cli PRODUCT_NAME "sediments_cli")
    target_sources(sediments_cli PRIVATE ${LT_CORE_SOURCES} tools/sediments_cli.cpp)
    target_compile_definitions(sediments_cli PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(sediments_cli PRIVATE lt_torch juce::juce_dsp juce::juce_audio_formats juce::juce_audio_basics juce::juce_core
                                     juce::juce_recommended_config_flags)
endif()

# --- UI snapshot dev tool (optional) ---------------------------------------
option(LT_BUILD_SNAPSHOT "Build the snapshot dev tool" OFF)
if(LT_BUILD_SNAPSHOT)
    juce_add_gui_app(sediments_snapshot PRODUCT_NAME "sediments_snapshot")
    target_sources(sediments_snapshot PRIVATE ${LT_CORE_SOURCES} ${LT_UI_SOURCES} Source/PluginProcessor.cpp Source/PluginEditor.cpp tools/snapshot.cpp)
    target_compile_definitions(sediments_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
                               JucePlugin_Name="Sediments")
    target_link_libraries(sediments_snapshot PRIVATE lt_torch LtFonts juce::juce_dsp juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
