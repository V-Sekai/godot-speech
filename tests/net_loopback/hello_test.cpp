// CHI-101 Phase A — FTXUI vendoring smoke.
//
// Builds an FTXUI element tree (no Component, no ScreenInteractive),
// renders it into a Screen, and prints the rendered string. Doesn't
// open an interactive PTY — CI-friendly.
//
// If this builds + runs, FTXUI v6.1.9 is properly vendored via
// CMake FetchContent and the net_loopback binary can layer richer
// UI on top in follow-up sub-passes.

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include <cstdio>

int main() {
	using namespace ftxui;

	// Build a tiny element tree that exercises the three layout
	// primitives net_loopback will use most: text, hbox, color.
	auto document = vbox({
			text("CHI-101 Phase A — net_loopback") | bold,
			separator(),
			hbox({
					text("FTXUI version:") | dim,
					text(" v6.1.9 (vendored via FetchContent)"),
			}),
			hbox({
					text("Status:") | dim,
					text(" hello — link works"),
			}) | color(Color::Green),
	});

	auto screen = Screen::Create(Dimension::Fit(document));
	Render(screen, document);

	// `ToString()` emits the final terminal byte stream (with VT100
	// escapes for color); use Print() for stdout, ToString() for
	// programmatic capture.
	const std::string out = screen.ToString();
	std::printf("%s\n", out.c_str());

	std::printf("net_loopback_hello: PASS — FTXUI v6.1.9 rendered %zu bytes\n",
			out.size());
	return 0;
}
