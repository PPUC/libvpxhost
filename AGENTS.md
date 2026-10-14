# AGENTS.md

## Scope

`libvpxhost` is the VPX plugin host shared by `ppuc` and the ColorizingDMD
editor. It holds what both need to host VPX message plugins and nothing
application-specific: no SDL, no Qt, no PinMAME, no media handling of its own.

- `include/vpxhost/PluginBus.h`, `src/PluginBus.cpp`: the bus. Started as
  `ppuc/src/PluginBus.*`; ppuc moves to this library.
- `tests/`: `vpxhost_tests` drives the bus against `tests/testplugin`, a plugin
  that reports through the host's logging API what it was given.

## Rules

- Keep it free of application dependencies. Something only one application
  needs stays in that application; something both need moves here.
- The plugin SDK is not vendored. It comes from the vpinball checkout passed as
  `VPXHOST_VPINBALL_DIR`. Keep `VPINBALL_SHA` in the workflow equal to the one
  `ppuc/platforms/config.sh` pins.
- Every behaviour of the bus a plugin can observe gets a check in
  `tests/plugin_bus_tests.cpp`, through the test plugin: real plugins fail
  silently when a host gets it wrong.
- One bus per process (the SDK's C APIs have no context pointer).

## Build And Validation

```sh
cmake -S . -B build -DVPXHOST_VPINBALL_DIR=<vpinball checkout>
cmake --build build
ctest --test-dir build --output-on-failure
```

In the PPUC workspace a checkout is at `../ppuc/external/vpinball/vpinball`.
