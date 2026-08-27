# xl300-ctd-manager — multi-stage build (see xl300-svp-manager/Dockerfile for the
# pattern this follows). Consumes uuv_common + uuv_interfaces as git submodules
# (see README.md) -- the HOST must run `git submodule update --init --recursive`
# BEFORE `docker build .`, since Docker's build context is whatever's already on
# disk; there is no in-container submodule fetch step here (these are local-path
# submodules today, unreachable from inside the build container anyway -- see
# each submodule's README.md "Provenance").
#
#   git submodule update --init --recursive
#   docker build --build-arg DEV_BASE=umeshwalkar/xl300-dev-base:0.1.0 -t xl300-ctd-manager:1.0.0 .
ARG DEV_BASE=umeshwalkar/xl300-dev-base:0.1.0

FROM ${DEV_BASE} AS build
WORKDIR /work
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j"$(nproc)" \
 && cmake --install build --prefix /out

FROM ubuntu:24.04 AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends libtinyxml2-9 libssl3 \
 && rm -rf /var/lib/apt/lists/*
COPY --from=build /opt/fastdds/install /opt/fastdds/install
COPY --from=build /out/opt/xl300/bin/ctd_manager /opt/xl300/bin/ctd_manager
COPY --from=build /work/uuv_interfaces/xl300-dds-v2/qos/xl300_profiles.xml /etc/xl300/xl300_profiles.xml
COPY --from=build /work/config/ctd_config.json /etc/xl300/ctd_config.json
ENV LD_LIBRARY_PATH=/opt/fastdds/install/lib
ENV FASTRTPS_DEFAULT_PROFILES_FILE=/etc/xl300/xl300_profiles.xml
ENTRYPOINT ["/opt/xl300/bin/ctd_manager"]
CMD ["/etc/xl300/ctd_config.json"]
