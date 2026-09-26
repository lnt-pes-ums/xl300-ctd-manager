#include "CtdManager.hpp"
#include "Logger.hpp"

#define MOD "CtdManager"

// ── Construction ──────────────────────────────────────────────────────────────

CtdManager::CtdManager(const AppConfig &cfg) : cfg_(cfg)
{
    for (const auto &dev : cfg_.sensor_config.devices)
    {
        if (dev.enabled)
        {
            active_device_ = &dev;
            break;
        }
    }
    if (!active_device_)
    {
        LOG_WRN(MOD, "No enabled devices found in config — running idle");
        return;
    }
    LOG_INF(MOD, "Device '%s' (id=%d) activated", active_device_->name.c_str(), active_device_->id);

    for (const auto &entry : cfg_.sensor_config.transports)
    {
        if (!entry.enabled)
        {
            LOG_INF(MOD, "Transport '%s' disabled — skipping", entry.id.c_str());
            continue;
        }
        transport_pool_[entry.id] = makeTransport(entry, entry.id);
        LOG_INF(MOD, "Transport '%s' instantiated (%s)", entry.id.c_str(), entry.type.c_str());
    }

    auto rxIt = active_device_->input_channels.find("ctd_rx");
    if (rxIt != active_device_->input_channels.end() && rxIt->second.enabled)
    {
        const auto &ich = rxIt->second;
        auto it = transport_pool_.find(ich.transport.id);
        if (it != transport_pool_.end())
        {
            rx_transport_ = it->second;
            for (const auto &entry : cfg_.sensor_config.transports)
                if (entry.id == ich.transport.id)
                {
                    rx_tr_cfg_ = &entry;
                    break;
                }
            LOG_INF(MOD, "ctd_rx -> transport '%s'", ich.transport.id.c_str());
        }
        else
        {
            LOG_WRN(MOD, "ctd_rx transport '%s' not found or disabled", ich.transport.id.c_str());
        }
    }
    else
    {
        LOG_WRN(MOD, "No 'ctd_rx' input channel configured — receive loop will not start");
    }

    if (active_device_->has_commands)
    {
        auto it = transport_pool_.find(active_device_->commands.transport.id);
        if (it != transport_pool_.end())
        {
            cmd_transport_ = it->second;
            LOG_INF(MOD, "commands -> transport '%s' (init=%zu periodic=%zu)",
                    active_device_->commands.transport.id.c_str(),
                    active_device_->commands.init_commands.commands.size(),
                    active_device_->commands.periodic_commands.commands.size());
        }
        else
        {
            LOG_WRN(MOD, "commands transport '%s' not found or disabled",
                    active_device_->commands.transport.id.c_str());
        }
    }
}

CtdManager::~CtdManager() { stop(); }

// ── start()/stop() ────────────────────────────────────────────────────────────

void CtdManager::start()
{
    if (running_)
        return;
    if (!active_device_)
    {
        LOG_WRN(MOD, "No active device — nothing to do");
        return;
    }
    running_ = true;
    start_time_ = std::chrono::steady_clock::now();
    receive_thread_ = std::thread(&CtdManager::receiveLoop, this);
    LOG_INF(MOD, "receiveLoop thread started");
}

void CtdManager::stop()
{
    if (!running_)
        return;
    running_ = false;
    cv_.notify_all();
    if (receive_thread_.joinable())
        receive_thread_.join();

    for (auto &[id, tr] : transport_pool_)
        if (tr && tr->isOpen())
        {
            LOG_INF(MOD, "Closing transport '%s'", id.c_str());
            tr->close();
        }

    LOG_INF(MOD, "Stopped pkt_rx=%llu pkt_errors=%llu cmd_sent=%llu cmd_errors=%llu", (unsigned long long)stats_.pkt_rx,
            (unsigned long long)stats_.pkt_errors, (unsigned long long)stats_.cmd_sent,
            (unsigned long long)stats_.cmd_errors);
}

// ── accessors ─────────────────────────────────────────────────────────────────

CtdSnapshot CtdManager::latestSnapshot()
{
    std::lock_guard<std::mutex> lk(snapshot_mutex_);
    return latest_snapshot_;
}

CtdStats CtdManager::stats()
{
    std::lock_guard<std::mutex> lk(snapshot_mutex_);
    return stats_;
}

bool CtdManager::isConnected() const { return rx_transport_ && rx_transport_->isOpen(); }

std::string CtdManager::health() const
{
    if (!active_device_ || !active_device_->enabled)
        return "disabled";
    if (!isConnected())
        return "disconnected";
    CtdSnapshot snap;
    {
        std::lock_guard<std::mutex> lk(snapshot_mutex_);
        snap = latest_snapshot_;
    }
    if (!snap.is_valid)
        return "degraded"; // connected, never received valid data
    auto rxIt = active_device_->input_channels.find("ctd_rx");
    int timeout_ms = (rxIt != active_device_->input_channels.end()) ? rxIt->second.data_timeout_ms : 2000;
    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - snap.timestamp)
                   .count();
    if (age >= timeout_ms)
        return "degraded"; // stale
    return "ok";
}

// ── commands ──────────────────────────────────────────────────────────────────

void CtdManager::sendCommand(ITransport *tx, const std::string &cmd, int inter_command_delay_ms)
{
    if (!tx)
        return;
    std::string msg = cmd + "\r\n";
    bool ok = tx->send(msg);
    {
        std::lock_guard<std::mutex> lk(snapshot_mutex_);
        if (ok)
        {
            stats_.cmd_sent++;
            LOG_DBG(MOD, "TX cmd: %s", cmd.c_str());
        }
        else
        {
            stats_.cmd_errors++;
            LOG_ERR(MOD, "TX cmd FAILED: %s", cmd.c_str());
        }
    }
    std::unique_lock<std::mutex> lock(cv_mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(inter_command_delay_ms),
                 [this]
                 {
                     return !running_.load();
                 });
}

void CtdManager::sendInitCommands(ITransport *tx, const DeviceConfig &dev)
{
    const auto &commands = dev.commands.init_commands.commands;
    if (commands.empty())
        return;
    LOG_INF(MOD, "Sending %zu init command(s) via transport '%s'", commands.size(), dev.commands.transport.id.c_str());
    for (const auto &cmd : commands)
    {
        sendCommand(tx, cmd, dev.commands.inter_command_delay_ms);
        if (!running_)
            break;
    }
}

// ── receiveLoop ───────────────────────────────────────────────────────────────

void CtdManager::receiveLoop()
{
    LOG_INF(MOD, "receiveLoop started");

    if (!rx_transport_)
    {
        LOG_ERR(MOD, "No ctd_rx transport — receiveLoop exiting");
        return;
    }

    ReceiveState st;
    st.dev = active_device_;
    st.rx = rx_transport_.get();
    st.tx = cmd_transport_ ? cmd_transport_.get() : nullptr;

    auto rxIt = st.dev->input_channels.find("ctd_rx");
    st.data_timeout_ms = rxIt != st.dev->input_channels.end() ? rxIt->second.data_timeout_ms : 2000;
    st.read_timeout_ms = rx_tr_cfg_ ? rx_tr_cfg_->read_timeout_ms : 1000;
    st.reconnect_ms = rx_tr_cfg_ ? rx_tr_cfg_->reconnect_delay_ms : 3000;
    st.last_rx_time = std::chrono::steady_clock::now();
    st.periodic_last.assign(st.dev->has_commands ? st.dev->commands.periodic_commands.commands.size() : 0,
                            std::chrono::steady_clock::time_point::min());

    LOG_INF(MOD, "receiveLoop: device '%s' read_timeout=%dms data_timeout=%dms", st.dev->name.c_str(),
            st.read_timeout_ms, st.data_timeout_ms);

    while (running_)
    {
        if (!openTransportsIfNeeded(st))
            continue;

        sendDueInitCommands(st);
        if (!running_)
            break;

        sendDuePeriodicCommands(st);
        receiveOneFrame(st);
    }

    LOG_INF(MOD, "receiveLoop stopped");
}

// Ensures the RX transport is open (retrying after reconnect_ms if not) and
// opens the commands transport if it's a separate one. Returns false if RX
// still isn't open after trying -- the caller should skip straight to the
// next tick rather than attempt commands/read on a closed transport.
bool CtdManager::openTransportsIfNeeded(ReceiveState &st)
{
    if (!st.rx->isOpen())
    {
        LOG_INF(MOD, "Connecting ctd_rx transport ...");
        if (!st.rx->open())
        {
            LOG_WRN(MOD, "ctd_rx transport open failed — retrying in %dms", st.reconnect_ms);
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(st.reconnect_ms),
                         [this]
                         {
                             return !running_.load();
                         });
            return false;
        }
        st.last_rx_time = std::chrono::steady_clock::now();
        st.timeout_warned = false;
        if (st.dev->has_commands && st.dev->commands.init_commands.send_on_reconnect)
            st.init_sent = false;
    }

    if (st.tx && st.tx != st.rx && !st.tx->isOpen())
    {
        LOG_INF(MOD, "Opening commands transport '%s'", st.dev->commands.transport.id.c_str());
        if (!st.tx->open())
            LOG_ERR(MOD, "commands transport open failed");
    }

    return true;
}

// Sends the device's init_commands exactly once per connection (gated by
// st.init_sent, which openTransportsIfNeeded() resets on a reconnect if
// send_on_reconnect is set), honouring app_start_timeout_ms on the very
// first connection.
void CtdManager::sendDueInitCommands(ReceiveState &st)
{
    if (st.init_sent)
        return;

    if (st.dev->has_commands && st.dev->commands.init_commands.enabled)
    {
        ITransport *cmd_tr = st.tx ? st.tx : st.rx;
        if (!st.app_start_waited && st.dev->commands.app_start_timeout_ms > 0)
        {
            int64_t since_start =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time_)
                    .count();
            int64_t wait_ms = st.dev->commands.app_start_timeout_ms - since_start;
            if (wait_ms > 0)
            {
                std::unique_lock<std::mutex> lock(cv_mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(wait_ms),
                             [this]
                             {
                                 return !running_.load();
                             });
            }
        }
        sendInitCommands(cmd_tr, *st.dev);
    }
    st.app_start_waited = true;
    st.init_sent = true;
}

// Sends whichever periodic_commands entries are due this tick (each on its
// own send_interval_ms), if the device has any configured and enabled.
void CtdManager::sendDuePeriodicCommands(ReceiveState &st)
{
    if (!(st.dev->has_commands && st.dev->commands.periodic_commands.enabled))
        return;

    ITransport *cmd_tr = st.tx ? st.tx : st.rx;
    auto now = std::chrono::steady_clock::now();
    const auto &pcmds = st.dev->commands.periodic_commands.commands;
    for (size_t i = 0; i < pcmds.size(); ++i)
    {
        if (!pcmds[i].enabled)
            continue;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - st.periodic_last[i]).count();
        if (elapsed >= pcmds[i].send_interval_ms)
        {
            sendCommand(cmd_tr, pcmds[i].command, st.dev->commands.inter_command_delay_ms);
            st.periodic_last[i] = std::chrono::steady_clock::now();
        }
    }
}

// Reads and parses one line from the RX transport, updating latest_snapshot_/
// stats_ on success. Logs (once per silence) if no data has arrived within
// data_timeout_ms, and reschedules init commands on an unexpected close.
void CtdManager::receiveOneFrame(ReceiveState &st)
{
    std::string line = st.rx->readLine(st.read_timeout_ms);
    auto now_steady = std::chrono::steady_clock::now();

    if (line.empty())
    {
        if (!st.rx->isOpen())
        {
            LOG_WRN(MOD, "ctd_rx transport closed — scheduling reconnect");
            if (st.dev->has_commands && st.dev->commands.init_commands.send_on_reconnect)
                st.init_sent = false;
            return;
        }
        auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_steady - st.last_rx_time).count();
        if (!st.timeout_warned && age_ms > st.data_timeout_ms)
        {
            LOG_WRN(MOD, "No data on ctd_rx for %lldms (timeout=%dms) — device silent?", (long long)age_ms,
                    st.data_timeout_ms);
            st.timeout_warned = true;
        }
        return;
    }

    st.last_rx_time = now_steady;
    st.timeout_warned = false;

    // VALEPORT Bathy2 frames carry no checksum byte -- a successfully
    // field-parsed line (all 14 fields) is considered valid; see CtdParser.hpp.
    CtdData cd;
    std::lock_guard<std::mutex> lk(snapshot_mutex_);
    latest_snapshot_.timestamp = std::chrono::system_clock::now();
    if (CtdParser::parse(line, cd))
    {
        latest_snapshot_.data = cd;
        latest_snapshot_.is_valid = true;
        stats_.pkt_rx++;
        LOG_DBG(MOD, "T=%.3f degC P=%.3f dbar D=%.3f m Sal=%.3f PSU (pkt#%llu)", cd.water_temp, cd.pressure_selected,
                cd.depth, cd.salinity, (unsigned long long)stats_.pkt_rx);
    }
    else
    {
        stats_.pkt_errors++;
        LOG_WRN(MOD, "Parse error #%llu on: %s", (unsigned long long)stats_.pkt_errors, line.c_str());
    }
}
