#pragma once
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include "Config.hpp"
#include "Transport.hpp"
#include "CtdParser.hpp"

// ── Snapshot ──────────────────────────────────────────────────────────────────

struct CtdSnapshot
{
    CtdData data;
    std::chrono::system_clock::time_point timestamp;
    bool is_valid = false;
};

struct CtdStats
{
    uint64_t pkt_rx = 0;
    uint64_t pkt_errors = 0;
    uint64_t cmd_sent = 0;
    uint64_t cmd_errors = 0;
};

// ── CtdManager ────────────────────────────────────────────────────────────────
//
// Config-driven, transport-abstracted (same template as SvpManager, see
// ../xl300-svp-manager/SvpManager.hpp), simpler still: the VALEPORT Bathy2 CTD
// frame has exactly one fixed 14-field format and no checksum byte, so there is
// no format auto-detection or checksum-validation branch. One thread does
// everything: read the device's ctd_rx channel, parse Bathy2 frames, and send
// the device's startup/periodic commands on the (usually same) commands output
// channel -- all sequential on receiveLoop().
//
// DDS pub plumbing stays in main.cpp (participant/topic/writers), same
// separation of concerns as SvpManager -- this class has no DDS dependency.
class CtdManager
{
  public:
    explicit CtdManager(const AppConfig &cfg);
    ~CtdManager();

    void start();
    void stop();

    CtdSnapshot latestSnapshot();
    bool isConnected() const;
    // "ok" | "degraded" | "disconnected" | "disabled"
    std::string health() const;
    CtdStats stats();

  private:
    AppConfig cfg_;
    const DeviceConfig *active_device_ = nullptr;
    const TransportConfig *rx_tr_cfg_ = nullptr;

    std::map<std::string, std::shared_ptr<ITransport>> transport_pool_;
    std::shared_ptr<ITransport> rx_transport_;
    std::shared_ptr<ITransport> cmd_transport_; // may alias rx_transport_

    std::atomic<bool> running_{false};
    std::thread receive_thread_;
    std::chrono::steady_clock::time_point start_time_;

    std::condition_variable cv_;
    mutable std::mutex cv_mutex_;

    mutable std::mutex snapshot_mutex_;
    CtdSnapshot latest_snapshot_;
    CtdStats stats_;

    void receiveLoop();
    void sendCommand(ITransport *tx, const std::string &cmd, int inter_command_delay_ms);
    void sendInitCommands(ITransport *tx, const DeviceConfig &dev);
};
