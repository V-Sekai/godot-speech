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
