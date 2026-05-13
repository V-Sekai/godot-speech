# CHI-101 Phase A — net_loopback test binary.
#
# Standalone end-to-end voice loopback driven through the audio
# model. Uses FTXUI v6.1.9 as the terminal UI layer (per the
# decision doc — replaces the prior Dear ImGui pick from PR #16).
#
# Currently scoped to:
#   * Vendor FTXUI v6.1.9 via FetchContent (no source-tree copy)
#   * Build a `net_loopback_hello` smoke that constructs an FTXUI
#     element, dumps its rendered output as a string, and exits
#     cleanly without grabbing an interactive PTY (CI-friendly)
#
# Subsequent sub-passes will:
#   * Wire SpeechProcessor (sender) + Speech (receiver) into the
#     loop with a synthetic jitter middleware
#   * Add observer Components for the slang_validate kernels
#   * Layer in real picoquic transport from
#     V-Sekai-fire/multiplayer-fabric-godot/modules/http3 @ 1c3e475

cmake_minimum_required(VERSION 3.20)
project(net_loopback CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Pin FTXUI to v6.1.9 (released 2025-05-07).
# https://github.com/ArthurSonzogni/FTXUI/releases/tag/v6.1.9
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG        v6.1.9
  GIT_SHALLOW    TRUE
)

# Don't build FTXUI's own examples/tests.
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(ftxui)

# Picoquic — vendor via FetchContent. picoquic's master branch is
# what people use (last tag is "draft-16-final" from QUIC pre-RFC).
# Pin a known-good master commit. The picotls TLS backend is auto-
# fetched via PICOQUIC_FETCH_PTLS; the picotls-openssl layer needs
# system OpenSSL (libssl-dev on Linux, openssl@3 from brew on macOS).
#
# BUILD_LOGLIB=ON forces picoquic-log to build — the sockloop.c
# objects in picoquic-core reference picoquic_set_qlog, so anything
# that pulls in sockloop (i.e. the real packet-loop tests) needs the
# loglib symbols at link time.
set(PICOQUIC_FETCH_PTLS ON CACHE BOOL "" FORCE)
set(BUILD_LOGLIB ON CACHE BOOL "" FORCE)
FetchContent_Declare(picoquic
  GIT_REPOSITORY https://github.com/private-octopus/picoquic.git
  GIT_TAG        f54239a39145
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(picoquic)

# Reuse the audio_model speech_engine library so the receive +
# capture paths are exercised through the unedited godot-speech
# .cpp files. The audio_model CMake target sits one level up.
add_subdirectory(../godot_audio_model godot_audio_model_build)

# Smoke test: construct an FTXUI element + render to a string in
# headless mode. Proves the vendoring works without grabbing a PTY.
add_executable(net_loopback_hello
  hello_test.cpp
)

target_link_libraries(net_loopback_hello
  PRIVATE
    ftxui::screen
    ftxui::dom
)

target_compile_options(net_loopback_hello
  PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)

# Step 1: assertion-based loopback driver. Sender (SpeechProcessor)
# -> NetMiddleware (synthetic jitter/loss/reorder) -> Speech
# receiver. No UI; pure pass/fail.
add_executable(net_loopback_driver
  loopback_test.cpp
)

target_link_libraries(net_loopback_driver
  PRIVATE
    speech_engine
)

target_compile_options(net_loopback_driver
  PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)

# Step 2: FTXUI dashboard smoke. Renders the live-state dashboard
# layout once per profile snapshot. Headless — no ScreenInteractive,
# no PTY.
add_executable(net_loopback_dashboard_smoke
  dashboard_smoke.cpp
)

target_link_libraries(net_loopback_dashboard_smoke
  PRIVATE
    ftxui::screen
    ftxui::dom
)

target_compile_options(net_loopback_dashboard_smoke
  PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)

# Step 3: kernel observer overlay. Drives the live loopback and
# renders the dashboard with kernel-vs-engine agreement markers
# flipped to red if anything diverges. Same regression-guard
# contract as godot_audio_model_agreement, but in the loopback
# context.
add_executable(net_loopback_observer
  observer_test.cpp
)

target_link_libraries(net_loopback_observer
  PRIVATE
    speech_engine
    ftxui::screen
    ftxui::dom
)

target_compile_options(net_loopback_observer
  PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)

# Step 4a: picoquic vendoring smoke. Constructs a picoquic_quic_t
# context, queries the library version, and frees it. Doesn't open
# sockets — proves the FetchContent vendoring + header path + link
# work.
add_executable(net_loopback_picoquic_smoke
  picoquic_smoke.cpp
)

target_link_libraries(net_loopback_picoquic_smoke
  PRIVATE
    picoquic-core
)

target_compile_options(net_loopback_picoquic_smoke
  PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)

# Step 4b: real picoquic datagram loopback. Spawns a server in a
# background thread (picoquic_start_network_thread) and a client on
# the main thread (picoquic_packet_loop), negotiates
# max_datagram_frame_size, round-trips N datagrams, and asserts the
# server received exactly what was sent. Uses the picoquic-shipped
# test cert (CN=test.example.com); the client installs
# picoquic_set_null_verifier because the cert isn't signed by a real
# CA.
add_executable(net_loopback_picoquic_datagram
  picoquic_loopback_test.cpp
)

target_link_libraries(net_loopback_picoquic_datagram
  PRIVATE
    picoquic-core
    picoquic-log
)

target_compile_definitions(net_loopback_picoquic_datagram
  PRIVATE
    PICOQUIC_LOOPBACK_CERT_PATH="${picoquic_SOURCE_DIR}/certs/cert.pem"
    PICOQUIC_LOOPBACK_KEY_PATH="${picoquic_SOURCE_DIR}/certs/key.pem"
)

target_compile_options(net_loopback_picoquic_datagram
  PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)
