# ctd_manager — CTD sensor DDS adaptor

Pure sensor-in DDS publisher: reads the VALEPORT Bathy2 CTD device over its
configured transport, parses the real 14-field caret-delimited ASCII frame, and
publishes `sensors/ctd` (`CtdSample`) + `health/<node>` (`Heartbeat`) per
`xl300-dds-v2`'s `DDS_Topic_Contract.md`. No subscriptions, no failover gate — if
it dies, whatever schedules it (k3s, systemd, a plain restart loop) just restarts
it; there's no peer replica or aiding-write path to coordinate.

**This is the reference app for the shared-repo pattern** (2026-08-27): the first
manager migrated off vendored/copy-pasted shared code onto `uuv_common` +
`uuv_interfaces` as git submodules. Future managers (`xl300-svp-manager`,
`xl300-ins-manager`, and anything new) should follow this repo's structure, not
`xl300-svp-manager`'s older vendored-copy one.

**Config-driven, transport-abstracted design**, copied from the real, currently
running MQTT prototype at
`D:\Projects\LnT\XL300\workspace-mqtt\xl300-ctd-manager\` (same JSON config shape,
same `ITransport` pool, same real VALEPORT Bathy2 frame parser). The one
structural swap: DDS pub replaces MQTT pub, and the three MQTT topics
(`data`/`status`/`diagnostic`) collapse into two DDS topics.

```
main.cpp             DDS wiring only: publish sensors/ctd + health/<node>, at each
                      topic's own publish_interval_ms, via uuv_common::DdsNode
                      helpers. No subscriptions and no sd_notify.
Config.hpp/.cpp       JSON config: debug, dds{} (domain/topics), sensor_config{}
                      (transport pool + device + input channel + optional commands
                      output channel). TransportConfig/ChannelTransportRef come
                      from uuv_common/TransportConfig.hpp, not defined here.
CtdParser.hpp         Real VALEPORT Bathy2 parser: one fixed 14-field caret-
                      delimited format, no checksum byte. App-specific -- not
                      shared (only two data points exist; see uuv_common/README.md).
CtdManager.hpp/.cpp   Owns: transport pool, receive loop (parse + optional device
                      startup/periodic commands), health computation.
ctd_config.json       Default config: ctd_rx on udp_server:9095 (matches the real
                      manager's shipped default); commands channel present but
                      disabled (see TODO below).
uuv_common/           Git submodule -- Logger, ITransport, TransportConfig,
                      DdsNode (participant/writer/reader-with-fallback helpers).
                      See uuv_common/README.md.
uuv_interfaces/       Git submodule -- generated DDS type support for the FULL
                      xl300-dds-v2 contract (xl300_dds_types library), which
                      itself submodules xl300-dds-v2. See uuv_interfaces/README.md.
```

See [CLAUDE.md](CLAUDE.md) for the DDS I/O table and the domain/partition/QoS
values.

## Build
```bash
git submodule update --init --recursive   # pulls uuv_common + uuv_interfaces
                                            # (which itself pulls xl300-dds-v2)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# -> build/ctd_manager   (install target: /opt/xl300/bin/ctd_manager)
```
No `fastddsgen`/JDK needed here — `uuv_interfaces` ships its generated code
committed. Requires Fast-DDS 2.14.x + `fastddsgen`'s runtime libs (for linking,
not codegen) — provided by the `umeshwalkar/xl300-dev-base:0.1.0` image, built
from `D:\Projects\LnT\XL300\workspace-dds\xl300-dev-base`. **Not** `uuv-dev-base`
— that's built from `workspace-mqtt\xl300-dev-base` and only has MQTT deps
(`libmosquitto`), no Fast-DDS at all. Also requires `nlohmann-json3-dev`
(installed by the DDS image; CMake falls back to `FetchContent` if it's missing).

### Docker
```bash
git submodule update --init --recursive   # MUST run on the host first -- Docker's
                                            # build context is whatever's already
                                            # on disk, there's no in-container fetch
docker build --build-arg DEV_BASE=umeshwalkar/xl300-dev-base:0.1.0 -t xl300-ctd-manager:1.0.0 .
docker run --rm --network host xl300-ctd-manager:1.0.0
```

## Run
```bash
export FASTRTPS_DEFAULT_PROFILES_FILE=$PWD/uuv_interfaces/xl300-dds-v2/qos/xl300_profiles.xml
./build/ctd_manager config/ctd_config.json
```
Edit `ctd_config.json`'s `sensor_config.transport[0]` for your actual CTD
host/port. The shipped default binds a UDP server on `0.0.0.0:9095` (matching the
real MQTT manager's shipped default and its `tests/ctd_simulator.py` once pointed
at UDP, or switch both to `tcp_client`/a TCP simulator port).

Verified end-to-end 2026-08-27 (real toolchain, real config): starts, binds the
UDP transport, logs correctly, publishes the periodic loop, shuts down clean on
signal — see `docs/payloads.md` for what actually goes on the wire.

## Verify (independent of whether the CTD device/simulator is reachable)
```bash
./build/probe
```
`health/<node>` heartbeats (filtered to `node == "ctd_manager"`) prove the DDS
pub/sub pipeline works end-to-end; `sensors/ctd` only appears once the transport is
actually connected and parsing valid frames.

## Config schema
Same uniform sensor-manager schema as `xl300-svp-manager`/the MQTT original:
`schema_version`, `sensor`, `debug`, `dds{domain_id, profile_file, topics.pub[]}`,
`sensor_config{transport[], devices[]}`. See
`workspace-mqtt/xl300-ctd-manager/README.md`'s "Configuration" section for the
full field-by-field reference (same shapes; `mqtt.*` there maps to `dds.*` here).

## Submodule versioning
`uuv_common` and `uuv_interfaces` are pinned to specific commits, not floating
branches — `git submodule status` shows exactly which. To pick up a change in
either (or in `xl300-dds-v2` via `uuv_interfaces`), bump the pinned commit
deliberately and rebuild/retest, the same discipline as any other dependency
version bump:
```bash
cd uuv_common && git checkout <new-commit-or-tag> && cd ..
git add uuv_common
```

## Building this pattern into a new manager
1. `git submodule add <uuv_common-url> uuv_common`
2. `git submodule add <uuv_interfaces-url> uuv_interfaces` (brings `xl300-dds-v2`
   along recursively)
3. `#include "TransportConfig.hpp"` in your `Config.hpp` instead of redefining
   `TransportConfig`/`ChannelTransportRef`
4. `add_subdirectory(uuv_common)` + `add_subdirectory(uuv_interfaces)` in
   `CMakeLists.txt`; link `uuv_common` + `xl300_dds_types`
5. `#include "DdsNode.hpp"`, use `uuv_common::createParticipant/createWriter/
   createReader/epochMs` instead of reimplementing the profile-with-fallback
   pattern
6. Point `FASTRTPS_DEFAULT_PROFILES_FILE`/`dds.profile_file` at
   `uuv_interfaces/xl300-dds-v2/qos/xl300_profiles.xml`

## TODO markers
- **`init_commands.commands`** — left empty and disabled in `ctd_config.json`: the
  real MQTT manager also ships an empty init-commands list for CTD (unlike the SVP
  unit, the VALEPORT Bathy2 CTD device isn't documented there as needing a startup
  configuration sequence). Add one if a real device turns out to need it.
- **`ctd_rx` transport host/port** — shipped as `udp_server` on `0.0.0.0:9095`,
  matching the real manager's default. Point at the real device or run
  `workspace-mqtt/xl300-ctd-manager/tests/ctd_simulator.py` (switch it or the
  config to matching transport types — the simulator speaks TCP) for local testing.
- **Pressure unit comment** — see CLAUDE.md's "Unit-comment discrepancy" note;
  not something to fix in this repo.
- **Migrate `xl300-svp-manager`/`xl300-ins-manager`** onto this same pattern —
  not done yet; they still vendor their own copies and point their `dds/`
  submodule at the old, superseded `xl300-dds` contract.

## Version note
Same Fast DDS 2.14.x DDS-PIM API notes as `xl300-svp-manager/README.md` (generated
header extension, `RETCODE_OK` path, enum scoping, link target name) apply here.
