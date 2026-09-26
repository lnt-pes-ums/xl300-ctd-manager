# xl300-ctd-manager

**Identity:** dumb adaptor for the CTD (Conductivity/Temperature/Depth) sensor
(VALEPORT Bathy2). Reads the device over its configured transport, parses the real
14-field caret-delimited ASCII wire format, and publishes `sensors/ctd` for MPS
(primary consumer) and NSS (optional aiding vote) to consume.

**Grounding:** `CtdSample`'s fields, the wire format, and the 14-field mapping are
copied field-for-field from the real, currently-running MQTT manager at
`D:\Projects\LnT\XL300\workspace-mqtt\xl300-ctd-manager\` (`docs/payloads.md`,
`src/CtdParser.hpp`) — not invented. See `docs/payloads.md` in this repo for the
DDS-side field reference and `references/` in the MQTT repo for the VALEPORT device
spec.

**Contract:** this manager is built against `xl300-dds-v2`
(`D:\Projects\LnT\XL300\workspace-dds\xl300-dds-v2\`), the current normative DDS
contract — NOT the older, superseded `xl300-dds` that `xl300-svp-manager`/
`xl300-ins-manager` still point their `dds/` submodule at. Do not copy those two
managers' `.gitmodules`/topic names/field names verbatim; their `dds/` submodule is
stale relative to `xl300-dds-v2`.

**This is the reference app for the shared-repo pattern (2026-08-27):** the first
manager migrated off vendored/copy-pasted shared code onto `uuv_common` (Logger/
Transport/TransportConfig/DdsNode) + `uuv_interfaces` (generated DDS types for the
FULL `xl300-dds-v2` contract, which it submodules directly) as git submodules.
When building or migrating another manager, follow *this* repo's structure —
`xl300-svp-manager`'s vendored-copy-of-3-files approach is what this replaced, not
what to imitate.

**Also the reference for `main()` shape (2026-08-28):** `main.cpp` is orchestration
only (construct `CtdApp` → `initialize()` → `start()` → idle-wait on
`ShutdownToken` → `stop()`) — every line of DDS setup and the entire publish loop
that used to live in `main()` moved into `CtdApp`. `CtdManager::receiveLoop()` got
the same treatment: a thin driver over four named steps operating on a
`ReceiveState` struct, not one long loop body. Follow this shape, not a `main()`
that does the actual work itself — see README.md's "Why main.cpp looks the way it
does".

## DDS I/O (per `xl300-dds-v2/DDS_Topic_Contract.md` §5/§6)
| Direction | Topic | Type | QoS | Partition |
|---|---|---|---|---|
| **publishes →** | `sensors/ctd` | `xl300::CtdSample` | SENSOR | `mission` |
| **publishes →** | `health/<node>` (literal shared topic, keyed by `node`) | `xl300::Heartbeat` | HEALTH | `diagnostics` |
| **subscribes ←** | *(none — pure sensor-in, publish-only)* | | | |
| **physical** | CTD device/simulator over the configured transport (default: UDP, `ctd_config.json`) | | | |

Domain **10** (`xl300-dds-v2/config/dds_domain.yaml`), participant profile
`xl300_domain10` (`deps/uuv_interfaces/xl300-dds-v2/qos/xl300_profiles.xml`).

`sensors/ctd` is on `mission`, **not** `nss-internal` — MPS is the primary consumer,
NSS is a secondary/optional aiding vote (`sensor_catalog.yaml`,
`config/sensors/ctd.yaml`, `DDS_Topic_Contract.md` §1.2/§2). Don't move it to a
private partition by analogy with `sensors/ins` — the contract explicitly calls out
CTD as public for this reason.

`health/<node>` is ONE literal topic name shared by every subsystem/manager, keyed
by the `Heartbeat.node` field — not `health/ctd_manager` or `health/ctd-manager`.
This manager's node name is `"ctd_manager"` (`message_ownership.yaml`,
`config/sensors/ctd.yaml`'s `mqtt_manager` field, matching the real MQTT process
name).

## uuv_common / uuv_interfaces are git submodules, not vendored copies
`uuv_common` (Logger, ITransport, TransportConfig, DdsNode — participant/writer/
reader-with-fallback helpers) and `uuv_interfaces` (generated type support for
the **entire** `xl300-dds-v2` `idl/` tree, which it submodules directly) replaced
this repo's earlier flat-copied `dds/` directory and local `Logger.*`/
`Transport.*` (2026-08-27). Both live under `deps/` (moved there 2026-09-08 so
`git status`/repo listings stay uncluttered by dependency checkouts). Never
hand-edit anything inside either submodule from this repo — bump the pinned
commit deliberately instead
(`cd deps/uuv_common && git checkout <ref> && cd ../.. && git add deps/uuv_common`),
same as any other dependency version bump. Never edit `xl300-dds-v2` from here either —
fix it there, bump `uuv_interfaces`' own submodule pointer, then bump this repo's
`uuv_interfaces` pointer.

**A real fastddsgen gotcha found and fixed via this migration, worth knowing if
you touch any `.idl` file:** an IDL `#include "../x.idl"` (relative path) gets
mirrored literally into the generated header's `#include "../x.h"` line AND
physically placed one directory above `-d` — breaking any consumer that only
compiles what's flat inside the output dir (i.e. every consumer). Fixed in
`xl300-dds-v2@v0.1.1` by switching every cross-file include to a bare filename
(`#include "common.idl"`, relying purely on `-I`). If you ever see a `fatal
error: ../something.h: No such file or directory` while building
`xl300_dds_types`, this is why — check `xl300-dds-v2/idl/**/*.idl` for a
reintroduced `../` include, don't just add the missing file.

## Contract facts come from code, not JSON (2026-08-27)
Domain id, topic name strings, QoS profile names, and partition names are
**not** in `ctd_config.json` — they're `xl300::contract` constants
(`deps/uuv_interfaces/generated/contract_constants.hpp`, generated by
`deps/uuv_interfaces/gen_contract_constants.py` from `xl300-dds-v2`'s own
`config/dds_domain.yaml` + `config/topic_registry.yaml`). The reasoning: these
must be byte-identical across every participant on the bus — a JSON typo in
one app's `domain_id` or a topic's `"topic"` string would silently desync that
app from everyone else's understanding of the bus, with zero error at
runtime. `ctd_config.json`'s `dds.topics.pub[].name` is only a local
correlation key (`"sensors_ctd"` -> `topics::kSensorsCtd`) for the genuinely
per-app settings (`publish_interval_ms`, `debug`) — never the topic string
itself. If you add a new topic to this app, add it via `xl300-dds-v2`'s
`config/topic_registry.yaml` (bump `uuv_interfaces`'s pointer, regenerate),
not by inventing a topic string in this app's JSON.

## Config-driven, not env-var-driven
Same `Config.hpp` template as `xl300-svp-manager` (uniform sensor-manager schema
shared across every UUV sensor manager) — `TransportConfig`/`ChannelTransportRef`
now come from `deps/uuv_common/TransportConfig.hpp` rather than being redefined here.
All transport/port/publish-rate/debug settings live in `ctd_config.json`.

## Known simplification vs the MQTT original
The real MQTT `ctd_manager` publishes three MQTT topics (`data`/`status`/
`diagnostic`) with request/response and health-change-triggered publishing. This
DDS port collapses that to the two DDS topics above, published periodically at
their own `publish_interval_ms` (see `CtdApp::publishLoop()`) — DDS's own QoS (durability,
deadline) covers what the MQTT version's request/response and reconnect-triggered
publishing existed to work around. No functional gap; different mechanism.

## Unit-comment discrepancy in `xl300-dds-v2/idl/sensors/ctd.idl` (not this repo's bug)
The contract's `CtdSample.pressure` / `pressure_aux` fields are commented `// bar`
but the real VALEPORT Bathy2 wire format reports them in **dbar**
(`pressure_baro` genuinely is bar — see workspace-mqtt's `docs/payloads.md`). This
manager writes the raw device values through unconverted (same as every other
sensor manager), so the wire *values* published on `sensors/ctd` are correct
regardless of the comment. Fix the unit comment in `xl300-dds-v2` itself
(`idl/sensors/ctd.idl`), not in `uuv_interfaces`' generated copy.

## Build & test (devcontainer)
"Reopen in Container" → `git submodule update --init --recursive` runs
automatically (`onCreateCommand`). Then:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
export FASTRTPS_DEFAULT_PROFILES_FILE=$PWD/deps/uuv_interfaces/xl300-dds-v2/qos/xl300_profiles.xml
./build/ctd_manager config/ctd_config.json
```
Verified end-to-end against the real toolchain 2026-08-27 (not just reviewed):
`fastddsgen` clean on the full `xl300-dds-v2` `idl/` tree, `xl300_dds_types` +
`uuv_common` + `ctd_manager` + `probe` all compile/link, and `ctd_manager`
actually starts, binds its UDP transport, and shuts down clean on signal.

See [README.md](README.md) for the config schema and [docs/payloads.md](docs/payloads.md)
for the `CtdSample`/`Heartbeat` field reference.
