// CHI-101 Phase A — FTXUI dashboard layout for the loopback test.
//
// Pure-function build: takes a snapshot of the engine state and
// returns an FTXUI `Element` that lays out the live signals as
// terminal text. Interactive mode (ScreenInteractive + Components)
// builds Components that wrap this layout; headless mode just
// renders one frame to a Screen string and exits.
//
// Layout sketch:
//
//   ┌── CHI-101 net_loopback — profile: lan ────────────────────┐
//   │ tick: 250 ms                                              │
//   │                                                           │
//   │ NET   sent: 20  delivered: 20  dropped: 0  reordered: 0   │
//   │ JB    size: 20  max:  20    skips: 0                      │
//   │                                                           │
//   │ FRAMES per tick:     ████░░░░░░░░░░  4 / 10               │
//   │ JB    fill:          ████████████░░ 20 / 32               │
//   │                                                           │
//   │ kernel ⇔ engine: ✔ FramingCursor agrees  ✔ JitterAppend   │
//   └───────────────────────────────────────────────────────────┘

#pragma once

#include <ftxui/dom/elements.hpp>

#include <string>

struct DashboardSnapshot {
	std::string profile_label;
	double tick_ms = 0.0;

	int net_sent = 0;
	int net_delivered = 0;
	int net_dropped = 0;
	int net_reordered = 0;

	int jb_size = 0;
	int jb_max = 16;
	int playback_skips = 0;

	int frames_per_tick = 0;
	int frames_per_tick_max = 10;

	bool framing_cursor_agrees = true;
	bool jitter_append_agrees = true;
};

// Build the dashboard element tree from a snapshot. Pure function;
// the interactive loop wraps this in a Renderer Component that
// re-builds it every frame.
inline ftxui::Element build_dashboard(const DashboardSnapshot &s) {
	using namespace ftxui;

	auto gauge_row = [](const std::string &label, int cur, int max) {
		const float ratio = max > 0
				? static_cast<float>(cur) / static_cast<float>(max)
				: 0.0f;
		return hbox({
				text(label) | size(WIDTH, EQUAL, 26),
				gauge(ratio) | flex,
				text("  " + std::to_string(cur) + " / " + std::to_string(max)) | size(WIDTH, EQUAL, 12),
		});
	};

	auto kv = [](const std::string &k, const std::string &v) {
		return hbox({
				text(k) | dim | size(WIDTH, EQUAL, 12),
				text(v),
		});
	};

	auto agree_marker = [](const std::string &name, bool ok) {
		return hbox({
				text(ok ? " ✔ " : " ✘ ") | (ok ? color(Color::Green) : color(Color::Red)),
				text(name),
				text(ok ? " agrees" : " DIVERGES") | (ok ? dim : color(Color::Red)),
		});
	};

	auto header = hbox({
			text("CHI-101 net_loopback") | bold,
			text("  — profile: "),
			text(s.profile_label) | color(Color::Yellow),
	});

	auto net_row = hbox({
			text(" NET ") | color(Color::Cyan) | size(WIDTH, EQUAL, 8),
			kv("sent:", std::to_string(s.net_sent)) | size(WIDTH, EQUAL, 20),
			kv("delivered:", std::to_string(s.net_delivered)) | size(WIDTH, EQUAL, 22),
			kv("dropped:", std::to_string(s.net_dropped)) | size(WIDTH, EQUAL, 18),
			kv("reordered:", std::to_string(s.net_reordered)),
	});

	auto jb_row = hbox({
			text(" JB  ") | color(Color::Cyan) | size(WIDTH, EQUAL, 8),
			kv("size:", std::to_string(s.jb_size)) | size(WIDTH, EQUAL, 20),
			kv("max:", std::to_string(s.jb_max)) | size(WIDTH, EQUAL, 22),
			kv("skips:", std::to_string(s.playback_skips)),
	});

	auto agreement_row = hbox({
			text(" KERNEL ⇔ ENGINE: ") | color(Color::Cyan),
			agree_marker("FramingCursor", s.framing_cursor_agrees),
			text("  "),
			agree_marker("JitterAppend", s.jitter_append_agrees),
	});

	return window(
			text(" " + s.profile_label + " — tick " + std::to_string(static_cast<int>(s.tick_ms)) + " ms ") | bold,
			vbox({
					header,
					separator(),
					net_row,
					jb_row,
					separator(),
					gauge_row("FRAMES per tick:", s.frames_per_tick, s.frames_per_tick_max),
					gauge_row("JB fill:", s.jb_size, s.jb_max),
					separator(),
					agreement_row,
			}));
}
