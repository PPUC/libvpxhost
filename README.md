# libvpxhost

Hosts [VPX](https://github.com/vpinball/vpinball) message plugins in an
application that is not VPX: the plugin bus (`MsgPluginManager` of the VPX
plugin SDK), plugin discovery and loading, the settings answers, the script
class registry and the logging API.

It is shared by the PPUC applications:

- [ppuc](https://github.com/PPUC/ppuc): runs PinMAME, PUP, AltSound, B2S and
  DMD output as VPX plugins on a real machine.
- [ColorizingDMD](https://github.com/PPUC/ColorizingDMD): the Serum
  colorization editor, for DMD output, PuP testing and video import.

## Use

```cpp
#include "vpxhost/PluginBus.h"

vpxhost::PluginBus bus({"MyApp", "My application", "Hosts VPX plugins"});
std::string error;
if (!bus.Initialize("/path/to/plugins", &error)) { /* error says why */ }
bus.SetSettingOverride("PUP", "PUPFolder", "/media/pupvideos");
bus.LoadPluginById("PUP");
// In the main loop:
bus.Process();
```

- One bus per process; it is not thread safe. Construct it on the thread that
  runs the main loop and keep it alive longer than everything that uses a
  plugin.
- The plugin folder holds one folder per plugin with its `plugin.cfg` and
  library, as VPX ships them.
- Settings: plugins get the host's override, or their own default. Every
  setting type of the SDK takes an override (string, folder, file, bool, int,
  float). Set overrides before the plugin is loaded.
- Logging: the second constructor argument receives the bus's own messages and
  every plugin's log lines; without it they go to stdout.

## Build

The plugin SDK comes from a vpinball checkout, the one the application builds
its plugins from:

```sh
cmake -S . -B build -DVPXHOST_VPINBALL_DIR=/path/to/vpinball
cmake --build build
ctest --test-dir build --output-on-failure
```

Only `plugins/plugins/` and a few headers of `third-party/include/` (mINI,
plog, unordered_dense, stl.h) are used; see `.github/workflows/libvpxhost.yml`
for a sparse checkout of just those. CI uses PPUC's vpinball fork at the commit
ppuc pins (`VPINBALL_SHA` in `ppuc/platforms/config.sh`).

The result is a static library, `vpxhost::vpxhost`, which carries the SDK's
include folders. As a subproject:

```cmake
set(VPXHOST_VPINBALL_DIR "${VPINBALL_DIR}")
add_subdirectory(libvpxhost)
target_link_libraries(myapp PRIVATE vpxhost::vpxhost)
```

## License

GPLv3 or later, as the VPX plugin SDK it compiles in.
