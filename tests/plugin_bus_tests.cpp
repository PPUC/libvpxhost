// Drives PluginBus against the VpxHostTest plugin and asserts on what the
// plugin reports through the host's logging API.
//
// Usage: vpxhost_tests <plugin folder>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "vpxhost/PluginBus.h"

namespace
{

std::vector<std::string> g_lines;
int g_failures = 0;

void Expect(bool condition, const std::string& message)
{
  if (!condition)
  {
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    ++g_failures;
  }
}

bool Saw(const std::string& line) { return std::find(g_lines.begin(), g_lines.end(), line) != g_lines.end(); }

void Dump()
{
  for (const std::string& line : g_lines)
  {
    std::fprintf(stderr, "  saw: %s\n", line.c_str());
  }
}

}  // namespace

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    std::fprintf(stderr, "usage: %s <plugin folder>\n", argv[0]);
    return 2;
  }

  {
    vpxhost::PluginBus missing({}, [](unsigned int, const std::string&, const std::string&) {});
    std::string error;
    Expect(!missing.Initialize(std::string(argv[1]) + "/does-not-exist", &error) && !error.empty(),
           "a missing plugin folder is refused");
  }

  vpxhost::PluginBus bus({"TestHost", "Test host", "libvpxhost tests"},
                         [](unsigned int, const std::string& source, const std::string& message)
                         {
                           if (source == "VpxHostTest")
                           {
                             g_lines.push_back(message);
                           }
                         });
  std::string error;
  Expect(bus.Initialize(argv[1], &error), "the bus initializes: " + error);
  Expect(bus.HostEndpointId() != 0, "the host has an endpoint");

  const std::vector<vpxhost::PluginBus::PluginInfo> plugins = bus.Plugins();
  Expect(plugins.size() == 1 && plugins[0].id == "VpxHostTest" && plugins[0].version == "1.0" && !plugins[0].loaded,
         "the plugin is found, not loaded, and the host is not listed");

  // Defaults first: nothing overridden.
  Expect(bus.LoadPluginById("VpxHostTest"), "the plugin loads");
  Expect(bus.IsPluginLoaded("VpxHostTest") && bus.Plugins()[0].loaded, "and is reported loaded");
  Expect(Saw("versions=1,1,1"), "the APIs it is handed are version 1");
  Expect(Saw("Text=default text") && Saw("Folder=default/folder") && Saw("File=default.txt") && Saw("Flag=1") &&
             Saw("Count=5") && Saw("Ratio=0.500000"),
         "every setting gets its default");
  Expect(bus.ClassDef("VpxHostTest.Object") != nullptr, "the script class is registered");
  Expect(bus.ComOverride("VpxHostTest.Server") == bus.ClassDef("VpxHostTest.Object"), "the COM override is set");
  Expect(bus.UnloadPluginById("VpxHostTest") && !bus.IsPluginLoaded("VpxHostTest"), "the plugin unloads");
  Expect(Saw("unloaded"), "and saw its unload");
  Expect(bus.ClassDef("VpxHostTest.Object") == nullptr && bus.ComOverride("VpxHostTest.Server") == nullptr,
         "its script class and COM override went with it");
  Expect(!bus.UnloadPluginById("VpxHostTest"), "unloading it again does nothing");

  // Every type takes an override, folder and file included.
  g_lines.clear();
  bus.SetSettingOverride("VpxHostTest", "Text", "some text");
  bus.SetSettingOverride("VpxHostTest", "Folder", "/media/pup");
  bus.SetSettingOverride("VpxHostTest", "File", "/media/scene.txt");
  bus.SetSettingOverride("VpxHostTest", "Flag", "off");
  bus.SetSettingOverride("VpxHostTest", "Count", "42");
  bus.SetSettingOverride("VpxHostTest", "Ratio", "0.25");
  Expect(bus.LoadPluginById("VpxHostTest"), "the plugin loads again");
  Expect(Saw("Text=some text") && Saw("Folder=/media/pup") && Saw("File=/media/scene.txt") && Saw("Flag=0") &&
             Saw("Count=10") && Saw("Ratio=0.250000"),
         "every override reaches the plugin, an int clamped to its range");

  Expect(!bus.LoadPluginById("NoSuchPlugin"), "an unknown plugin is not loaded");
  bus.Shutdown();
  Expect(Saw("unloaded"), "shutdown unloads the plugin");
  bus.Shutdown();

  if (g_failures != 0)
  {
    Dump();
    return 1;
  }
  std::printf("vpxhost_tests passed\n");
  return 0;
}
