# CHI-101 Phase A pass-3 — Godot audio-system model.
#
# Builds a static library `godot_audio_model` containing:
#
#   * Core type stand-ins (core/)
#   * AudioDriver base class (audio/)
#   * Verbatim copy of the engine's CoreAudio driver (coreaudio/),
#     wired up via header-path shims (shims/) so the copy itself
#     stays unedited.
#
# This is Pass 3 of the implementation outline in
# manuals/decisions/2026-05-12-speech-isolation.md.  Subsequent
# passes (4–7) add AudioEffectCapture, AudioStreamGenerator(Playback),
# AudioStreamPlayer wrappers, and finally link speech.cpp /
# speech_processor.cpp / speech_decoder.cpp against this library.
#
# CoreAudio support is mac-only; the rest of the library builds on
# every platform (Linux/Windows callers get the AudioDriver base
# class without a concrete driver attached, which is enough for the
# math-layer slang_validate tests).

cmake_minimum_required(VERSION 3.20)
project(godot_audio_model CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MODEL_SOURCES
    audio/audio_driver.cpp
    audio/audio_server.cpp
)

set(MODEL_HEADERS
    core/audio_frame.h
    core/core_types.h
    core/error_list.h
    core/error_macros.h
    core/math_funcs.h
    core/memory.h
    core/mutex.h
    core/packed_arrays.h
    core/ref_counted.h
    core/ring_buffer.h
    core/string.h
    core/typedefs.h
    core/variant.h
    core/vector.h
    core/vector2.h
    audio/audio_driver.h
    audio/audio_effect.h
    audio/audio_effect_capture.h
    audio/audio_server.h
    audio/audio_stream.h
    audio/audio_stream_generator.h
    audio/audio_stream_player.h
    audio/node.h
)

if(APPLE)
    list(APPEND MODEL_SOURCES coreaudio/audio_driver_coreaudio.mm)
    list(APPEND MODEL_HEADERS coreaudio/audio_driver_coreaudio.h)
endif()

add_library(godot_audio_model STATIC
    ${MODEL_SOURCES}
    ${MODEL_HEADERS}
)

target_include_directories(godot_audio_model
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/shims
)

target_compile_options(godot_audio_model
    PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-private-field
)

if(APPLE)
    # Engine guards the CoreAudio driver behind COREAUDIO_ENABLED.
    target_compile_definitions(godot_audio_model
        PUBLIC
            COREAUDIO_ENABLED
            MACOS_ENABLED
    )

    set_source_files_properties(coreaudio/audio_driver_coreaudio.mm
        PROPERTIES
            COMPILE_FLAGS "-fobjc-arc"
    )

    target_link_libraries(godot_audio_model
        PUBLIC
            "-framework AudioUnit"
            "-framework CoreAudio"
            "-framework AudioToolbox"
            "-framework Foundation"
    )
endif()

# Smoke test: constructs the driver, queries its name, and exits.
# Useful early indicator that the model wires up correctly without
# spinning the full audio thread.
add_executable(godot_audio_model_smoke
    smoke_test.cpp
)
target_link_libraries(godot_audio_model_smoke
    PRIVATE
        godot_audio_model
)
