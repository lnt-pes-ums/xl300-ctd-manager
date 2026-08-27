#pragma once
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "TransportConfig.hpp" // uuv_common: TransportConfig, ChannelTransportRef

using json = nlohmann::json;

// ============================================================================
// Config model, adapted from the MQTT-based ctd_manager prototype at
// workspace-mqtt/xl300-ctd-manager/src/Config.hpp -- the same sensor-agnostic
// uniform schema shared across every UUV sensor manager (svp, ins, ctd, ...), see
// ../xl300-svp-manager/Config.hpp for the DDS-side sibling this was copied from.
// Same structural shape as svp_manager's Config.hpp (Tier-1, k3s-scheduled, pure
// sensor-in publisher: no role/ownership_strength/primary_deadline_ms/sub_topics).
// CommandChannelConfig is kept for schema uniformity with svp_manager even though
// the shipped VALEPORT Bathy2 CTD device takes no init commands in practice.
//
// TransportConfig/ChannelTransportRef moved to uuv_common/TransportConfig.hpp
// (2026-08-27) -- they were byte-identical to svp_manager's copies, found by
// actually compiling Transport.cpp standalone against uuv_common and hitting a
// missing-header error, not by inspection. Everything below this point
// (SensorConfig/DeviceConfig/InputChannelConfig/CommandChannelConfig/DdsConfig)
// stays here: genuinely per-sensor, not shared -- see uuv_common/README.md.
// ============================================================================

#define LOWEST_PUBLISH_INT_MS 100

// ----------------------------------------------------------------------------
// Debug / logging
// ----------------------------------------------------------------------------

struct DebugConfig {
    bool        enabled = false;
    std::string level   = "warn";   // error | warn | info | debug
};

// ----------------------------------------------------------------------------
// Input channel (device.input_channels.<name>).
// ----------------------------------------------------------------------------

struct InputChannelConfig {
    bool                enabled         = true;
    bool                debug           = false;
    int                 data_timeout_ms = 2000;
    std::string         format;
    ChannelTransportRef transport;
};

// ----------------------------------------------------------------------------
// Command channel (device.output_channels.commands) -- kept for schema parity
// with svp_manager/ins_manager. The real VALEPORT Bathy2 CTD device (unlike the
// SVP unit) is not known to require startup commands over this transport, so the
// shipped ctd_config.json ships an empty, disabled init_commands list -- see
// README.md's TODO if a real device turns out to need one.
// ----------------------------------------------------------------------------

struct PeriodicCommandConfig {
    std::string name;
    std::string command;
    int         send_interval_ms = 1000;
    bool        enabled          = true;
};

struct InitCommandsConfig {
    bool                     enabled           = true;
    bool                     send_on_reconnect = false;
    std::vector<std::string> commands;
};

struct PeriodicCommandsConfig {
    bool                               enabled = false;
    std::vector<PeriodicCommandConfig> commands;
};

struct CommandChannelConfig {
    bool                   debug                  = false;
    int                    inter_command_delay_ms = 200;
    int                    app_start_timeout_ms   = 0;
    ChannelTransportRef    transport;
    InitCommandsConfig     init_commands;
    PeriodicCommandsConfig periodic_commands;
};

// ----------------------------------------------------------------------------
// DDS -- one pub topic per DDS_Topic_Contract entry this app owns (sensors/ctd,
// health/<node>). No sub_topics (nothing subscribed) and no role/
// ownership_strength/primary_deadline_ms (Tier-1-equivalent, k3s reschedules on
// failure instead of active-standby failover) -- see xl300-dds-v2's
// DDS_Topic_Contract.md §5/§6 (topic table) for domain_id/partition/QoS-profile
// values; this struct only carries the per-app config, not the contract itself.
// ----------------------------------------------------------------------------

struct DdsTopicConfig {
    std::string name;
    std::string topic;
    bool        debug               = false;
    int         publish_interval_ms = 1000;
};

struct DdsConfig {
    bool        enabled      = true;
    int         domain_id    = 10;   // xl300-dds-v2 config/dds_domain.yaml: domain_id 10
    std::string profile_file = "uuv_interfaces/xl300-dds-v2/qos/xl300_profiles.xml";
    std::vector<DdsTopicConfig> pub_topics;

    const DdsTopicConfig* find(const std::string& name) const {
        for (auto& t : pub_topics) if (t.name == name) return &t;
        return nullptr;
    }
};

// ----------------------------------------------------------------------------
// Device (sensor_config.devices[]).
// ----------------------------------------------------------------------------

struct DeviceConfig {
    int         id                     = 1;
    std::string name                   = "device";
    bool        enabled                = true;
    bool        publish_enabled        = true;
    bool        publish_raw_data       = false;
    bool        publish_stale_data     = false;
    bool        validate_checksum      = true;   // no effect: Bathy2 frames carry no checksum byte

    std::map<std::string, InputChannelConfig> input_channels;

    bool                 has_commands = false;
    CommandChannelConfig commands;
};

// ----------------------------------------------------------------------------
// sensor_config — transports + devices.
// ----------------------------------------------------------------------------

struct SensorConfig {
    std::vector<TransportConfig> transports;
    std::vector<DeviceConfig>    devices;

    const TransportConfig* findTransport(const std::string& id) const {
        for (auto& t : transports) if (t.id == id) return &t;
        return nullptr;
    }
};

// ----------------------------------------------------------------------------
// Top-level application config.
// ----------------------------------------------------------------------------

struct AppConfig {
    std::string  schema_version = "1.0";
    std::string  sensor;
    DebugConfig  debug;
    DdsConfig    dds;
    SensorConfig sensor_config;

    static AppConfig fromFile(const std::string& path);
    static AppConfig fromJsonFile(const std::string& path);
};
