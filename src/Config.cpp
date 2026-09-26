#include "Config.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static TransportConfig parseTransport(const json &t)
{
    TransportConfig cfg;
    cfg.id = t.value("id", cfg.id);
    cfg.enabled = t.value("enabled", cfg.enabled);
    cfg.type = t.value("type", cfg.type);
    cfg.host = t.value("host", cfg.host);
    cfg.port = t.value("port", cfg.port);
    cfg.bind_host = t.value("bind_host", cfg.bind_host);
    cfg.bind_port = t.value("bind_port", cfg.bind_port);
    cfg.connect_timeout_ms = t.value("connect_timeout_ms", cfg.connect_timeout_ms);
    cfg.reconnect_delay_ms = t.value("reconnect_delay_ms", cfg.reconnect_delay_ms);
    cfg.read_timeout_ms = t.value("read_timeout_ms", cfg.read_timeout_ms);
    cfg.write_timeout_ms = t.value("write_timeout_ms", cfg.write_timeout_ms);
    cfg.buffer_size_bytes = t.value("buffer_size_bytes", cfg.buffer_size_bytes);
    cfg.serial_port = t.value("serial_port", cfg.serial_port);
    cfg.serial_baud = t.value("serial_baud", cfg.serial_baud);
    cfg.serial_data_bits = t.value("serial_data_bits", cfg.serial_data_bits);
    cfg.serial_stop_bits = t.value("serial_stop_bits", cfg.serial_stop_bits);
    cfg.serial_parity = t.value("serial_parity", cfg.serial_parity);
    return cfg;
}

static ChannelTransportRef parseTransportRef(const json &t)
{
    ChannelTransportRef ref;
    ref.id = t.value("id", ref.id);
    return ref;
}

static DdsTopicConfig parseDdsTopic(const json &t)
{
    DdsTopicConfig cfg;
    cfg.name = t.value("name", cfg.name);
    cfg.debug = t.value("debug", cfg.debug);
    cfg.publish_interval_ms = t.value("publish_interval_ms", cfg.publish_interval_ms);
    if (cfg.publish_interval_ms > 0 && cfg.publish_interval_ms < LOWEST_PUBLISH_INT_MS)
        cfg.publish_interval_ms = LOWEST_PUBLISH_INT_MS;
    return cfg;
}

static InputChannelConfig parseInputChannel(const json &ch)
{
    InputChannelConfig cfg;
    cfg.enabled = ch.value("enabled", cfg.enabled);
    cfg.debug = ch.value("debug", cfg.debug);
    cfg.data_timeout_ms = ch.value("data_timeout_ms", cfg.data_timeout_ms);
    cfg.format = ch.value("format", cfg.format);
    if (ch.contains("transport"))
        cfg.transport = parseTransportRef(ch["transport"]);
    return cfg;
}

static CommandChannelConfig parseCommandChannel(const json &ch)
{
    CommandChannelConfig cfg;
    cfg.debug = ch.value("debug", cfg.debug);
    cfg.inter_command_delay_ms = ch.value("inter_command_delay_ms", cfg.inter_command_delay_ms);
    cfg.app_start_timeout_ms = ch.value("app_start_timeout_ms", cfg.app_start_timeout_ms);
    if (ch.contains("transport"))
        cfg.transport = parseTransportRef(ch["transport"]);

    if (ch.contains("init_commands"))
    {
        auto &ic = ch["init_commands"];
        cfg.init_commands.enabled = ic.value("enabled", cfg.init_commands.enabled);
        cfg.init_commands.send_on_reconnect = ic.value("send_on_reconnect", cfg.init_commands.send_on_reconnect);
        if (ic.contains("commands") && ic["commands"].is_array())
            for (auto &c : ic["commands"])
                cfg.init_commands.commands.push_back(c.get<std::string>());
    }

    if (ch.contains("periodic_commands"))
    {
        auto &pc = ch["periodic_commands"];
        cfg.periodic_commands.enabled = pc.value("enabled", cfg.periodic_commands.enabled);
        if (pc.contains("commands") && pc["commands"].is_array())
        {
            for (auto &c : pc["commands"])
            {
                PeriodicCommandConfig entry;
                entry.name = c.value("name", entry.name);
                entry.command = c.value("command", entry.command);
                entry.send_interval_ms = c.value("send_interval_ms", entry.send_interval_ms);
                entry.enabled = c.value("enabled", entry.enabled);
                cfg.periodic_commands.commands.push_back(entry);
            }
        }
    }

    return cfg;
}

static DeviceConfig parseDevice(const json &d)
{
    DeviceConfig dev;
    dev.id = d.value("id", dev.id);
    dev.name = d.value("name", dev.name);
    dev.enabled = d.value("enabled", dev.enabled);
    dev.publish_enabled = d.value("publish_enabled", dev.publish_enabled);
    dev.publish_raw_data = d.value("publish_raw_data", dev.publish_raw_data);
    dev.publish_stale_data = d.value("publish_stale_data", dev.publish_stale_data);
    dev.validate_checksum = d.value("validate_checksum", dev.validate_checksum);
    dev.publish_on_data_rx = d.value("publish_on_data_rx", dev.publish_on_data_rx);

    if (d.contains("input_channels") && d["input_channels"].is_object())
        for (auto &[ch_name, ch_j] : d["input_channels"].items())
            dev.input_channels[ch_name] = parseInputChannel(ch_j);

    if (d.contains("output_channels") && d["output_channels"].is_object())
    {
        auto &oc = d["output_channels"];
        if (oc.contains("commands"))
        {
            dev.has_commands = true;
            dev.commands = parseCommandChannel(oc["commands"]);
        }
    }

    return dev;
}

AppConfig AppConfig::fromFile(const std::string &path) { return fromJsonFile(path); }

AppConfig AppConfig::fromJsonFile(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Failed to open config file: " + path);

    json config;
    try
    {
        file >> config;
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("Failed to parse JSON config: " + std::string(e.what()));
    }

    AppConfig cfg;
    cfg.schema_version = config.value("schema_version", cfg.schema_version);
    cfg.sensor = config.value("sensor", cfg.sensor);

    if (config.contains("debug"))
    {
        auto &d = config["debug"];
        cfg.debug.enabled = d.value("enabled", false);
        cfg.debug.level = d.value("level", "warn");
    }

    if (config.contains("dds"))
    {
        auto &m = config["dds"];
        cfg.dds.enabled = m.value("enabled", cfg.dds.enabled);

        if (m.contains("topics") && m["topics"].contains("pub") && m["topics"]["pub"].is_array())
            for (auto &pt : m["topics"]["pub"])
                cfg.dds.pub_topics.push_back(parseDdsTopic(pt));
    }

    if (config.contains("sensor_config"))
    {
        auto &sc = config["sensor_config"];
        if (sc.contains("transport") && sc["transport"].is_array())
            for (auto &t : sc["transport"])
                cfg.sensor_config.transports.push_back(parseTransport(t));
        if (sc.contains("devices") && sc["devices"].is_array())
            for (auto &d : sc["devices"])
                cfg.sensor_config.devices.push_back(parseDevice(d));
    }

    return cfg;
}
