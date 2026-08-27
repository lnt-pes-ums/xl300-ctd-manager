# DDS payload reference — `sensors/ctd` and `health/<node>`

Field reference for the two DDS topics `ctd_manager` publishes. The measurement
fields are copied field-for-field from the real, currently-running MQTT
manager's payload — see
`workspace-mqtt/xl300-ctd-manager/docs/payloads.md` for the original MQTT
`uuv/ctd/data` JSON this was ported from, and `../CtdParser.hpp` for the parser.

## `sensors/ctd` — `xl300::CtdSample` (SENSOR QoS, `mission` partition)

| Field | Type | Unit | Meaning |
|---|---|---|---|
| `header.ts` | uint64 (epoch ms) | — | Wall-clock time this sample was published. |
| `header.data_ts` | uint64 (epoch ms) | — | Wall-clock time the underlying line was received (may lag `ts`). |
| `header.sender` | string<64> | — | `"ctd_manager"`. |
| `header.schema_version` | uint16 | — | `1`. |
| `device_id` | uint16 | — | Configured device id (`sensor_config.devices[].id`). |
| `device_name` | string<32> | — | Configured device name. |
| `valid` | bool | — | Always `true` when published — `main.cpp` skips the write entirely for an invalid/stale reading (see [Publish conditions](#publish-conditions) below). |
| `date` | string<16> | yyyy-mm-dd | Device-supplied date. |
| `time` | string<16> | hh:mm:ss.sss | Device-supplied time. |
| `conductivity` | double | mS/cm | Water conductivity. |
| `pressure` | double | dbar (see note) | Selected pressure sensor. |
| `water_temp` | double | °C | Water temperature. |
| `altimeter_height` | double | m | Height above seabed; device reports `0` when no external altimeter is interfaced. |
| `sound_vel` | double | m/s | Speed of sound in water. |
| `pressure_aux` | double | dbar (see note) | Auxiliary pressure sensor. |
| `depth` | double | m | Device-calculated depth. |
| `total_depth` | double | m | Total water column depth. |
| `point_density` | double | kg/m³ | In-situ (point) water density. |
| `profile_density` | double | kg/m³ | Profile-averaged density. |
| `salinity` | double | PSU | Practical salinity. |
| `pressure_baro` | double | bar | Atmospheric / tare pressure. |

**Unit note:** `xl300-dds-v2/idl/sensors/ctd.idl` comments `pressure` and
`pressure_aux` as `// bar`, but the real VALEPORT Bathy2 wire fields ("Pressure
Selected", "AUX Pressure") are dbar per the MQTT manager's payload docs and the
VALEPORT spec. This manager writes the raw device value through unconverted
either way, so the published double is correct regardless of the comment — see
`CLAUDE.md`'s "Unit-comment discrepancy" note.

### Publish conditions
`sensors/ctd` is published every `publish_interval_ms` (default 1000ms, config:
`dds.topics.pub[name="sensors_ctd"]`), and only when:
- `sensor_config.devices[0].publish_enabled` is `true`, and
- a valid frame has been received at least once, and
- (unless `devices[0].publish_stale_data` is `true`) the latest reading is younger
  than that input channel's `data_timeout_ms`.

No sample is written otherwise (unlike the MQTT original, there's no separate
"stale" flag on the sample itself — a stale reading is simply not published).

## `health/<node>` — `xl300::Heartbeat` (HEALTH QoS, `diagnostics` partition)

One literal shared topic, published by every subsystem/manager, keyed by `node` —
**not** one topic per subsystem. See `xl300-dds-v2/config/topic_registry.yaml`'s
`health/<node>` entry.

| Field | Type | Meaning |
|---|---|---|
| `header.ts` | uint64 (epoch ms) | Wall-clock time this heartbeat was published. |
| `header.data_ts` | uint64 (epoch ms) | `0` — not applicable to a heartbeat. |
| `header.sender` | string<64> | `"ctd_manager"`. |
| `header.schema_version` | uint16 | `1`. |
| `node` (**@key**) | string<32> | `"ctd_manager"` — filter on this to isolate this manager's heartbeats. |
| `status` | `HealthState` enum | `HEALTH_OK` \| `HEALTH_DEGRADED` \| `HEALTH_DISCONNECTED` \| `HEALTH_DISABLED` — see mapping below. `HEALTH_ERROR` is defined but never emitted (no local fault condition maps to it, same as the MQTT original's `"error"` value). |
| `seq` | uint32 | Monotonically increasing per-process counter, wraps at 2³²; resets on restart. |
| `data_age_ms` | int32 | Milliseconds since the last valid CTD reading; `-1` if none has ever been received (MQTT's null→-1 sentinel convention, per `common.idl`'s header comment). |

### `status` mapping (`CtdManager::health()` → `HealthState`)
| `CtdManager::health()` | `HealthState` | Meaning |
|---|---|---|
| `"ok"` | `HEALTH_OK` | Connected, data fresher than `data_timeout_ms`. |
| `"degraded"` | `HEALTH_DEGRADED` | Connected but no data yet, or data has gone stale. |
| `"disconnected"` | `HEALTH_DISCONNECTED` | `ctd_rx` transport not open. |
| `"disabled"` | `HEALTH_DISABLED` | Device turned off in config (`enabled: false`). |

Published every `publish_interval_ms` (default 1000ms, config:
`dds.topics.pub[name="health"]`) regardless of device state — a heartbeat is
always emitted, unlike `sensors/ctd`.
