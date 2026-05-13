# CHI-101 Phase A pass-7 — compile godot-speech's engine .cpp files
# against the audio model.
#
# This sub-project builds a `speech_engine` static library from the
# repo's speech_*.cpp sources, using the audio model + shim headers
# in place of the real Godot core/scene/servers tree. Resolves the
# remaining external dependencies (libopus, libsamplerate) via
# system install — brew on macOS, apt-get on Linux.
#
# Currently scoped to **speech_decoder.cpp** (Pass 7a). Adding
# speech_processor.cpp / speech.cpp comes in follow-up sub-passes.

cmake_minimum_required(VERSION 3.20)

# Build against repo root sources.
set(SPEECH_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

# Look up libopus + libsamplerate.
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED opus)
pkg_check_modules(SAMPLERATE REQUIRED samplerate)

add_library(speech_engine STATIC
    ${SPEECH_ROOT}/speech_decoder.cpp
    ${SPEECH_ROOT}/speech_processor.cpp
    ${SPEECH_ROOT}/speech.cpp
)

target_include_directories(speech_engine
    PUBLIC
        ${SPEECH_ROOT}
        ${CMAKE_CURRENT_SOURCE_DIR}/..              # audio model root
        ${CMAKE_CURRENT_SOURCE_DIR}/../shims        # engine-path shims
        ${OPUS_INCLUDE_DIRS}
        ${SAMPLERATE_INCLUDE_DIRS}
)

target_link_libraries(speech_engine
    PUBLIC
        godot_audio_model
        ${OPUS_LIBRARIES}
        ${SAMPLERATE_LIBRARIES}
)

target_link_directories(speech_engine
    PUBLIC
        ${OPUS_LIBRARY_DIRS}
        ${SAMPLERATE_LIBRARY_DIRS}
)

target_compile_options(speech_engine
    PRIVATE
        -Wall -Wextra
        -Wno-unused-parameter
        -Wno-unused-private-field
        # speech.cpp uses GDCLASS macros which our stubs mostly turn
        # into no-ops; the engine code references symbols that don't
        # exist in our model. Permissive enough to compile, strict
        # enough to catch real bugs.
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-unused-but-set-variable
)
