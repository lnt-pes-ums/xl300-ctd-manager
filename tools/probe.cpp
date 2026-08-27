// probe.cpp — minimal standalone DDS subscriber to verify ctd_manager is actually
// publishing over the bus, independent of whether the real/simulated CTD device
// is connected.
//
// Subscribes:
//   health/<node> (Heartbeat, `diagnostics` partition)  -> proves the pub/sub
//     pipeline works end-to-end (DDS discovery, QoS, delivery) even with no
//     device connected. Filters on node == "ctd_manager" client-side since the
//     topic is shared by every subsystem/manager (see DDS_Topic_Contract.md).
//   sensors/ctd (CtdSample, `mission` partition)          -> shows real data once
//     ctd_manager is actually receiving something on its configured transport.
//
// Run in a SEPARATE terminal alongside ctd_manager:
//   export FASTRTPS_DEFAULT_PROFILES_FILE=$PWD/uuv_interfaces/xl300-dds-v2/qos/xl300_profiles.xml
//   ./build/probe
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

#include "health.h"
#include "healthPubSubTypes.h"
#include "ctd.h"
#include "ctdPubSubTypes.h"
#include "contract_constants.hpp"
#include "DdsNode.hpp"
#include "ShutdownToken.hpp"
#include "SignalHandler.hpp"

#include <chrono>
#include <iostream>
#include <thread>

using namespace eprosima::fastdds::dds;

static void print_sample(const char *topic, const xl300::Heartbeat &hb, int n)
{
    if (hb.node() != "ctd_manager")
        return; // shared topic -- ignore other nodes
    std::cout << "[" << topic << "] #" << n << " node=" << hb.node() << " status=" << hb.status() << " seq=" << hb.seq()
              << " data_age_ms=" << hb.data_age_ms() << "\n";
}
static void print_sample(const char *topic, const xl300::CtdSample &s, int n)
{
    std::cout << "[" << topic << "] #" << n << " depth=" << s.depth() << " water_temp=" << s.water_temp()
              << " salinity=" << s.salinity() << " sound_vel=" << s.sound_vel() << " valid=" << s.valid() << "\n";
}

template <class T> class PrintListener : public DataReaderListener
{
  public:
    explicit PrintListener(std::string topic) : topic_(std::move(topic)) {}
    void on_data_available(DataReader *r) override
    {
        T s;
        SampleInfo info;
        while (r->take_next_sample(&s, &info) == ReturnCode_t::RETCODE_OK)
            if (info.valid_data)
                print_sample(topic_.c_str(), s, ++count_);
    }

  private:
    std::string topic_;
    int count_ = 0;
};

int main()
{
    ShutdownToken shutdown;
    SignalHandler::initialize(shutdown);

    using namespace xl300::contract;

    DomainParticipant *dp = uuv_common::createParticipant("xl300_domain10", kDomainId);
    if (!dp)
    {
        std::cerr << "FATAL: no participant\n";
        return 1;
    }

    TypeSupport t_hb(new xl300::HeartbeatPubSubType());
    t_hb.register_type(dp);
    TypeSupport t_ctd(new xl300::CtdSamplePubSubType());
    t_ctd.register_type(dp);

    Topic *tp_hb = dp->create_topic(topics::kHealth.topic, t_hb.get_type_name(), TOPIC_QOS_DEFAULT);
    Topic *tp_ctd = dp->create_topic(topics::kSensorsCtd.topic, t_ctd.get_type_name(), TOPIC_QOS_DEFAULT);

    // health/<node> is on the `diagnostics` partition, sensors/ctd on `mission` --
    // see main.cpp's pub_diag/pub_mission.
    SubscriberQos sq_diag = SUBSCRIBER_QOS_DEFAULT;
    sq_diag.partition().push_back(topics::kHealth.partition);
    Subscriber *sub_diag = dp->create_subscriber(sq_diag);
    SubscriberQos sq_mission = SUBSCRIBER_QOS_DEFAULT;
    sq_mission.partition().push_back(topics::kSensorsCtd.partition);
    Subscriber *sub_mission = dp->create_subscriber(sq_mission);

    auto hb_lis = new PrintListener<xl300::Heartbeat>(topics::kHealth.topic);
    auto ctd_lis = new PrintListener<xl300::CtdSample>(topics::kSensorsCtd.topic);
    uuv_common::createReader(sub_diag, tp_hb, topics::kHealth.qos_profile, hb_lis);
    uuv_common::createReader(sub_mission, tp_ctd, topics::kSensorsCtd.qos_profile, ctd_lis);

    std::cout << "[probe] listening: health/<node> (diagnostics, node=ctd_manager) + sensors/ctd (mission)\n"
              << "[probe] a heartbeat every ~1s proves ctd_manager's pub/sub works end-to-end\n"
              << "[probe] sensors/ctd only appears once the device is connected and parsing\n";
    while (!shutdown.requested())
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    DomainParticipantFactory::get_instance()->delete_participant(dp);
    std::cout << "[probe] stopped\n";
    return 0;
}
