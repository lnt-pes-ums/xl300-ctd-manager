#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

#include "Config.hpp"
#include "CtdManager.hpp"

// Owns everything main.cpp used to do directly before 2026-08-27: the DDS
// participant/topics/writers, the CtdManager instance, and the publish loop
// (its own background thread). main() reduces to construct -> initialize ->
// start -> idle-wait on the shutdown token -> stop -- this is the shape every
// future manager built on this pattern should follow, not a main() that does
// the work itself.
class CtdApp
{
  public:
    ~CtdApp();

    // Loads configPath, creates the DDS participant/topics/writers, and
    // constructs (but does not start) CtdManager. Logs why and returns false
    // on any failure -- config parse error, no DDS participant.
    bool initialize(const std::string &configPath);

    // Starts CtdManager's receive thread and this app's own publish-loop
    // thread. No-op if initialize() failed or start() was already called.
    void start();

    // Stops the publish loop and CtdManager, releases the DDS participant
    // (which cascades to its topics/publishers/writers). Safe to call more
    // than once, and from the destructor if the caller doesn't.
    void stop();

  private:
    void publishLoop();
    // Publishes the current CTD snapshot if the device allows it and the
    // data isn't stale -- the one formatter shared by both the timer-driven
    // path and the publish_on_data_rx path in publishLoop(), so they can
    // never drift apart.
    void publishCtdSampleIfFresh(const CtdSnapshot &snap);
    void publishHeartbeat();

    AppConfig cfg_;
    std::unique_ptr<CtdManager> ctd_mgr_;

    eprosima::fastdds::dds::DomainParticipant *dp_ = nullptr;
    eprosima::fastdds::dds::TypeSupport t_ctd_;
    eprosima::fastdds::dds::TypeSupport t_hb_;
    eprosima::fastdds::dds::DataWriter *w_ctd_ = nullptr;
    eprosima::fastdds::dds::DataWriter *w_hb_ = nullptr;

    int ctd_interval_ms_ = 1000;
    int hb_interval_ms_ = 1000;
    bool ctd_debug_ = false;
    bool hb_debug_ = false;
    bool publish_on_data_rx_ = false;
    uint64_t last_published_pkt_rx_ = 0;
    uint32_t hb_seq_ = 0;

    std::atomic<bool> running_{false};
    std::thread publish_thread_;
};
