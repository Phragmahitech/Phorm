cmake_minimum_required(VERSION 3.22)

# --- Réglages ----------------------------------------------------------------
set(PHORM_PRODUCT_NAME "Phorm")          # nom du plugin dans le DAW, et nom du fichier .vst3
set(PHORM_COMPANY_NAME "Phragma")            # nom affiché comme éditeur dans le DAW
set(PHORM_BUNDLE_ID    "com.phragma.phorm")
# -----------------------------------------------------------------------------
# Ne changer ni PLUGIN_MANUFACTURER_CODE ni PLUGIN_CODE : ce sont eux (et pas le nom)
# qui permettent aux DAW de retrouver le plugin dans les projets.

# macOS : binaire universel (Apple Silicon + Intel), compatible macOS 10.13+
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Version minimale de macOS")
if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures macOS")
endif()

# Windows : runtime C++ lié en statique -> aucun "Visual C++ Redistributable" à installer
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Phorm VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PHORM_BUILD_TESTS "Compiler les tests" OFF)
set(PHORM_FONT_DIR "" CACHE PATH "Dossier contenant déjà les polices (sinon elles sont téléchargées)")

# --- JUCE (téléchargé automatiquement) ---------------------------------------
# Pour utiliser une copie locale : -DFETCHCONTENT_SOURCE_DIR_JUCE=/chemin/vers/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# --- Polices (licence OFL, voir Assets/) -------------------------------------
# Michroma et IBM Plex Mono, prises dans le dépôt google/fonts à un commit fixe et vérifiées par SHA-256.
set(PHORM_FONTS_COMMIT 66928797f271fed5ee428544e7229685e2d90127)
set(PHORM_FONTS
    "ofl/michroma/Michroma-Regular.ttf|b62301163788bc5b7f8fcac0b74b184e34e1827e577b499ecb724da065098f87"
    "ofl/ibmplexmono/IBMPlexMono-Regular.ttf|6a3412f058c7d8dfd9170c41e85ade48e5156ecb89356110ca57a0a27734af46"
    "ofl/ibmplexmono/IBMPlexMono-Medium.ttf|a9b4c49bb299e05b5f6c481e7fb5e78943d2793249a0c8874ab574a2d1ea6755")

set(PHORM_FONT_FILES "")
foreach(entry IN LISTS PHORM_FONTS)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 font_path)
    list(GET parts 1 font_hash)
    get_filename_component(font_name "${font_path}" NAME)

    if(PHORM_FONT_DIR)
        set(font_file "${PHORM_FONT_DIR}/${font_name}")
        file(SHA256 "${font_file}" actual_hash)
        if(NOT actual_hash STREQUAL font_hash)
            message(FATAL_ERROR "Police inattendue : ${font_file}")
        endif()
    else()
        set(font_file "${CMAKE_BINARY_DIR}/fonts/${font_name}")
        if(NOT EXISTS "${font_file}")
            file(DOWNLOAD
                "https://raw.githubusercontent.com/google/fonts/${PHORM_FONTS_COMMIT}/${font_path}"
                "${font_file}"
                EXPECTED_HASH SHA256=${font_hash}
                TLS_VERIFY ON
                STATUS download_status)
            list(GET download_status 0 download_code)
            if(NOT download_code EQUAL 0)
                message(FATAL_ERROR "Téléchargement impossible : ${font_path} (${download_status})")
            endif()
        endif()
    endif()

    list(APPEND PHORM_FONT_FILES "${font_file}")
endforeach()

# --- Plugin ------------------------------------------------------------------
juce_add_plugin(Phorm
    PRODUCT_NAME                "${PHORM_PRODUCT_NAME}"
    COMPANY_NAME                "${PHORM_COMPANY_NAME}"
    BUNDLE_ID                   "${PHORM_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE    Phrg
    PLUGIN_CODE                 Phrm
    FORMATS                     VST3
    VST3_CATEGORIES             Instrument Synth
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Phorm PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

juce_add_binary_data(PhormAssets SOURCES ${PHORM_FONT_FILES})
set_target_properties(PhormAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_compile_definitions(Phorm PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    PHORM_VERSION="${PROJECT_VERSION}")

target_link_libraries(Phorm
    PRIVATE
        juce::juce_audio_processors
        PhormAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# --- Tests (optionnels) ------------------------------------------------------
if(PHORM_BUILD_TESTS)
    enable_testing()

    # Moteur seul, sans JUCE
    add_executable(PhormEngineTests Tests/EngineTests.cpp)
    target_include_directories(PhormEngineTests PRIVATE Source)
    add_test(NAME engine COMMAND PhormEngineTests)

    # Plugin complet (paramètres, MIDI, état, interface)
    juce_add_console_app(PhormPluginTests PRODUCT_NAME "PhormPluginTests")
    target_sources(PhormPluginTests PRIVATE
        Tests/PluginTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(PhormPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        PHORM_VERSION="${PROJECT_VERSION}")
    target_link_libraries(PhormPluginTests PRIVATE
        juce::juce_audio_processors
        PhormAssets
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME plugin COMMAND PhormPluginTests)

    # Rendu d'une capture de l'interface et d'extraits audio (Tools/Render.cpp)
    juce_add_console_app(PhormRender PRODUCT_NAME "PhormRender")
    target_sources(PhormRender PRIVATE
        Tools/Render.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(PhormRender PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        PHORM_VERSION="${PROJECT_VERSION}")
    target_link_libraries(PhormRender PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        PhormAssets
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()
