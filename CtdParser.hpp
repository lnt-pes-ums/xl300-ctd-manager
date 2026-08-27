#pragma once
#include <string>
#include <vector>
#include <sstream>
#include "Logger.hpp"

// VALEPORT Bathy2 CTD data — 14 caret-delimited ASCII fields, one fixed format
// (unlike the SVP unit's 3 auto-detected formats). Ported field-for-field from the
// real running MQTT manager: workspace-mqtt/xl300-ctd-manager/src/CtdParser.hpp /
// docs/payloads.md. Frame carries no checksum byte -- a successfully field-parsed
// line is considered valid.
//
// Format: yyyy-mm-dd^hh:mm:ss.sss^CC.CCC^pppp.ppp^TT.TTT^hh.hhh^cccc.ccc^pppp.ppp^
//         dddd.ddd^dddd.ddd^DDDD.DDD^DDDD.DDDD^s.sss^AA.AAA<CR><LF>
struct CtdData {
    std::string date;                   // yyyy-mm-dd
    std::string time;                   // hh:mm:ss.sss
    std::string raw;                    // original unmodified line

    double conductivity        = 0.0;   // mS/cm
    double pressure_selected   = 0.0;   // dbar
    double water_temp          = 0.0;   // degC
    double altimeter_height    = 0.0;   // m; device sends 0 when no external altimeter is interfaced
    double sound_velocity      = 0.0;   // m/s
    double aux_pressure        = 0.0;   // dbar
    double depth               = 0.0;   // m
    double total_depth         = 0.0;   // m
    double point_density       = 0.0;   // kg/m3
    double profile_density     = 0.0;   // kg/m3
    double salinity            = 0.0;   // PSU
    double barometric_pressure = 0.0;   // bar

    bool   valid                = false;
};

class CtdParser {
public:
    // Splits one caret-delimited line into its 14 fields and converts the numeric
    // fields to double. Returns false (data.valid stays false) if the field count
    // isn't 14 or a numeric field fails to parse; data.raw is set either way.
    static bool parse(const std::string& packet, CtdData& data) {
        std::string s = packet;
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();

        data = CtdData{};
        data.raw = s;
        if (s.empty()) return false;

        auto fields = split(s, '^');
        if (fields.size() != 14) {
            LOG_WRN("CtdParser", "Expected 14 fields, got %zu in: %s", fields.size(), s.c_str());
            return false;
        }

        try {
            data.date                = fields[0];
            data.time                = fields[1];
            data.conductivity        = std::stod(fields[2]);
            data.pressure_selected   = std::stod(fields[3]);
            data.water_temp          = std::stod(fields[4]);
            data.altimeter_height    = std::stod(fields[5]);
            data.sound_velocity      = std::stod(fields[6]);
            data.aux_pressure        = std::stod(fields[7]);
            data.depth               = std::stod(fields[8]);
            data.total_depth         = std::stod(fields[9]);
            data.point_density       = std::stod(fields[10]);
            data.profile_density     = std::stod(fields[11]);
            data.salinity            = std::stod(fields[12]);
            data.barometric_pressure = std::stod(fields[13]);
            data.valid               = true;
            return true;
        } catch (const std::exception& e) {
            LOG_ERR("CtdParser", "Parse error: %s  line: %s", e.what(), s.c_str());
            return false;
        }
    }

private:
    static std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> tokens;
        std::stringstream ss(s);
        std::string tok;
        while (std::getline(ss, tok, delim)) tokens.push_back(tok);
        return tokens;
    }
};
