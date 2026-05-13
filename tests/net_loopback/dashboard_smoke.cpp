// CHI-101 Phase A — net_loopback step 2 smoke.
//
// Renders the dashboard layout once with a fabricated snapshot
// per profile (lan / wan / lossy / geo) and prints each frame.
// Doesn't run the engine — that's step 3's job (overlay the
// dashboard on top of a live loopback). This smoke just proves
// the layout compiles + renders sensible output without grabbing
// an interactive PTY.

#include "dashboard.h"

#include <ftxui/screen/screen.hpp>

#include <cstdio>

static void render_snapshot(const DashboardSnapshot &s) {
	using namespace ftxui;
	auto element = build_dashboard(s);
	auto screen = Screen::Create(Dimension::Fit(element));
	Render(screen, element);
	std::printf("%s\n", screen.ToString().c_str());
}

int main() {
	// Mirrors loopback_test.cpp's three profiles plus a GEO satellite
	// preset from F4's routing table.
	render_snapshot({
			.profile_label = "lan (5 ms RTT)",
			.tick_ms = 250.0,
			.net_sent = 20,
			.net_delivered = 20,
			.net_dropped = 0,
			.net_reordered = 0,
			.jb_size = 20,
			.jb_max = 32,
			.playback_skips = 0,
			.frames_per_tick = 10,
			.frames_per_tick_max = 10,
			.framing_cursor_agrees = true,
			.jitter_append_agrees = true,
	});

	render_snapshot({
			.profile_label = "wan (80 ms RTT, 8 ms σ)",
			.tick_ms = 320.0,
			.net_sent = 20,
			.net_delivered = 20,
			.net_dropped = 0,
			.net_reordered = 3,
			.jb_size = 18,
			.jb_max = 32,
			.playback_skips = 0,
			.frames_per_tick = 9,
			.frames_per_tick_max = 10,
			.framing_cursor_agrees = true,
			.jitter_append_agrees = true,
	});

	render_snapshot({
			.profile_label = "lossy (80 ms RTT, 8 ms σ, 10% loss)",
			.tick_ms = 320.0,
			.net_sent = 20,
			.net_delivered = 17,
			.net_dropped = 3,
			.net_reordered = 1,
			.jb_size = 17,
			.jb_max = 32,
			.playback_skips = 0,
			.frames_per_tick = 8,
			.frames_per_tick_max = 10,
			.framing_cursor_agrees = true,
			.jitter_append_agrees = true,
	});

	// Stress preset — GEO satellite per F4. Engine would fail
	// to keep up unless caller raises MAX_JITTER_BUFFER_SIZE to
	// >= 200 packets; show what the dashboard reports while it's
	// underprovisioned.
	render_snapshot({
			.profile_label = "geo-sat (2000 ms RTT) — UNDER-PROVISIONED jitter",
			.tick_ms = 2000.0,
			.net_sent = 200,
			.net_delivered = 198,
			.net_dropped = 0,
			.net_reordered = 12,
			.jb_size = 32,
			.jb_max = 32,
			.playback_skips = 166,
			.frames_per_tick = 0,
			.frames_per_tick_max = 10,
			.framing_cursor_agrees = true,
			.jitter_append_agrees = true,
	});

	std::printf("dashboard_smoke: PASS — FTXUI rendered 4 snapshots\n");
	return 0;
}
