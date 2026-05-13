// CHI-101 Phase A — in-process UDP relay that applies a
// `NetworkProfile` to traffic flowing between a picoquic client and
// a picoquic server. Lets the real-QUIC backend exercise LAN / WAN
// / lossy conditions without changing picoquic itself.
//
// Topology:
//
//   client picoquic   →  [front_port]  RelayProxy  [server_addr]  →  server picoquic
//                     ←                                              ←
//
// One UDP socket bound to 127.0.0.1:front_port carries traffic in
// both directions:
//   * First non-server datagram captures the client's source port →
//     subsequent server-bound traffic is recognized by that port.
//   * Packets arriving from the server address are forwarded back
//     to the captured client.
//
// Network profile effects (`InProcessNetTransport`-style):
//   * `base_latency_ms`     — every packet queues with deliver_at_us
//   * `jitter_sigma_ms`     — half-normal extra delay
//   * `loss_prob`           — packet dropped at submission
//   * `reorder_window` +
//     `reorder_prob`        — swap with one of the last N queued
//
// The relay runs its own background thread driven by select() with a
// short timeout so the deliver_at queue can fire on time.

#pragma once

#include "middleware.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <random>
#include <thread>
#include <vector>

class UDPRelayProxy {
public:
	struct Counters {
		std::atomic<int> c2s_received{ 0 };
		std::atomic<int> c2s_forwarded{ 0 };
		std::atomic<int> c2s_dropped{ 0 };
		std::atomic<int> c2s_reordered{ 0 };
		std::atomic<int> s2c_received{ 0 };
		std::atomic<int> s2c_forwarded{ 0 };
		std::atomic<int> s2c_dropped{ 0 };
		std::atomic<int> s2c_reordered{ 0 };
	};

	UDPRelayProxy(sockaddr_in server_addr, NetworkProfile profile) :
			server_addr_(server_addr),
			profile_(profile),
			rng_(profile.seed ^ 0xCA571DA7CAFEull) {}

	~UDPRelayProxy() { stop(); }

	// Spin up the relay socket + thread. Returns the local UDP port
	// the relay is bound to (client picoquic should send to this
	// port), or 0 on failure.
	uint16_t start() {
		front_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (front_fd_ < 0) {
			return 0;
		}
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		addr.sin_port = 0;
		if (::bind(front_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
			::close(front_fd_);
			front_fd_ = -1;
			return 0;
		}
		socklen_t slen = sizeof(addr);
		if (::getsockname(front_fd_, reinterpret_cast<sockaddr *>(&addr), &slen) != 0) {
			::close(front_fd_);
			front_fd_ = -1;
			return 0;
		}
		front_port_ = ntohs(addr.sin_port);

		if (::pipe(wake_pipe_) != 0) {
			::close(front_fd_);
			front_fd_ = -1;
			return 0;
		}
		stop_.store(false, std::memory_order_release);
		thread_ = std::thread([this] { relay_loop(); });
		return front_port_;
	}

	void stop() {
		if (!thread_.joinable()) {
			return;
		}
		stop_.store(true, std::memory_order_release);
		char b = 0;
		(void)!::write(wake_pipe_[1], &b, 1);
		thread_.join();
		if (front_fd_ >= 0) {
			::close(front_fd_);
			front_fd_ = -1;
		}
		if (wake_pipe_[0] >= 0) {
			::close(wake_pipe_[0]);
			wake_pipe_[0] = -1;
		}
		if (wake_pipe_[1] >= 0) {
			::close(wake_pipe_[1]);
			wake_pipe_[1] = -1;
		}
	}

	uint16_t front_port() const { return front_port_; }
	const Counters &counters() const { return counters_; }

private:
	enum Direction : uint8_t {
		C2S = 0,
		S2C = 1
	};

	struct InFlight {
		std::vector<uint8_t> data;
		sockaddr_in dst;
		uint64_t deliver_at_us;
		Direction dir;
	};

	void relay_loop() {
		uint8_t buf[2048];
		while (!stop_.load(std::memory_order_acquire)) {
			const uint64_t now_us = wall_us();
			int64_t timeout_us = 100'000; // 100 ms idle wake
			if (!queue_.empty()) {
				uint64_t soonest = queue_.front().deliver_at_us;
				for (const auto &p : queue_) {
					if (p.deliver_at_us < soonest) {
						soonest = p.deliver_at_us;
					}
				}
				int64_t delta = static_cast<int64_t>(soonest) - static_cast<int64_t>(now_us);
				if (delta < 0) {
					delta = 0;
				}
				if (delta < timeout_us) {
					timeout_us = delta;
				}
			}

			fd_set rfds;
			FD_ZERO(&rfds);
			FD_SET(front_fd_, &rfds);
			FD_SET(wake_pipe_[0], &rfds);
			int sockmax = std::max(front_fd_, wake_pipe_[0]);
			timeval tv;
			tv.tv_sec = static_cast<long>(timeout_us / 1'000'000);
			tv.tv_usec = static_cast<long>(timeout_us % 1'000'000);
			(void)::select(sockmax + 1, &rfds, nullptr, nullptr, &tv);

			if (FD_ISSET(wake_pipe_[0], &rfds)) {
				char drainbuf[16];
				(void)!::read(wake_pipe_[0], drainbuf, sizeof(drainbuf));
				if (stop_.load(std::memory_order_acquire)) {
					break;
				}
			}

			if (FD_ISSET(front_fd_, &rfds)) {
				sockaddr_in src{};
				socklen_t slen = sizeof(src);
				ssize_t n = ::recvfrom(front_fd_, buf, sizeof(buf), 0,
						reinterpret_cast<sockaddr *>(&src), &slen);
				if (n > 0) {
					handle_incoming(buf, static_cast<size_t>(n), src, now_us);
				}
			}

			drain_ready(wall_us());
		}
	}

	void handle_incoming(const uint8_t *data, size_t len, const sockaddr_in &src,
			uint64_t now_us) {
		const bool from_server = src.sin_addr.s_addr == server_addr_.sin_addr.s_addr &&
				src.sin_port == server_addr_.sin_port;
		Direction dir;
		sockaddr_in dst{};
		if (from_server) {
			if (!client_known_) {
				return; // server response with no client to forward to
			}
			dir = S2C;
			dst = client_addr_;
		} else {
			if (!client_known_) {
				client_addr_ = src;
				client_known_ = true;
			}
			dir = C2S;
			dst = server_addr_;
		}
		if (dir == C2S) {
			counters_.c2s_received.fetch_add(1, std::memory_order_relaxed);
		} else {
			counters_.s2c_received.fetch_add(1, std::memory_order_relaxed);
		}

		// Loss.
		if (profile_.loss_prob > 0.0) {
			std::uniform_real_distribution<double> u(0.0, 1.0);
			if (u(rng_) < profile_.loss_prob) {
				if (dir == C2S) {
					counters_.c2s_dropped.fetch_add(1, std::memory_order_relaxed);
				} else {
					counters_.s2c_dropped.fetch_add(1, std::memory_order_relaxed);
				}
				return;
			}
		}

		double delay_ms = profile_.base_latency_ms;
		if (profile_.jitter_sigma_ms > 0.0) {
			std::normal_distribution<double> n(0.0, profile_.jitter_sigma_ms);
			delay_ms += std::abs(n(rng_));
		}
		const uint64_t deliver_at_us = now_us + static_cast<uint64_t>(delay_ms * 1000.0);

		InFlight f;
		f.data.assign(data, data + len);
		f.dst = dst;
		f.deliver_at_us = deliver_at_us;
		f.dir = dir;
		queue_.push_back(std::move(f));

		// Reorder: swap with an earlier entry in the same direction.
		if (profile_.reorder_window > 0 && profile_.reorder_prob > 0.0 && queue_.size() >= 2) {
			std::uniform_real_distribution<double> u(0.0, 1.0);
			if (u(rng_) < profile_.reorder_prob) {
				const int window = std::min<int>(
						static_cast<int>(queue_.size()) - 1,
						profile_.reorder_window);
				if (window > 0) {
					std::uniform_int_distribution<int> dpos(1, window);
					const int back_off = dpos(rng_);
					const auto last = queue_.end() - 1;
					std::iter_swap(last, last - back_off);
					if (dir == C2S) {
						counters_.c2s_reordered.fetch_add(1, std::memory_order_relaxed);
					} else {
						counters_.s2c_reordered.fetch_add(1, std::memory_order_relaxed);
					}
				}
			}
		}
	}

	void drain_ready(uint64_t now_us) {
		// Reorder may put a later deliver_at_us at the front, so scan
		// the whole queue. O(n) per drain; n stays small in practice.
		for (auto it = queue_.begin(); it != queue_.end();) {
			if (it->deliver_at_us <= now_us) {
				(void)::sendto(front_fd_, it->data.data(), it->data.size(), 0,
						reinterpret_cast<sockaddr *>(&it->dst), sizeof(it->dst));
				if (it->dir == C2S) {
					counters_.c2s_forwarded.fetch_add(1, std::memory_order_relaxed);
				} else {
					counters_.s2c_forwarded.fetch_add(1, std::memory_order_relaxed);
				}
				it = queue_.erase(it);
			} else {
				++it;
			}
		}
	}

	static uint64_t wall_us() {
		return std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now().time_since_epoch())
				.count();
	}

	sockaddr_in server_addr_;
	NetworkProfile profile_;
	sockaddr_in client_addr_{};
	bool client_known_ = false;

	int front_fd_ = -1;
	uint16_t front_port_ = 0;
	int wake_pipe_[2] = { -1, -1 };

	std::atomic<bool> stop_{ false };
	std::thread thread_;
	std::mt19937_64 rng_;

	std::deque<InFlight> queue_;
	Counters counters_;
};
