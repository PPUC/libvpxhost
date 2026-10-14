#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "plugins/LoggingPlugin.h"
#include "plugins/MsgPluginManager.h"
#include "plugins/ScriptablePlugin.h"

namespace vpxhost
{

// Who the application is on the bus. The id is what plugins see as the
// host's plugin id.
struct HostInfo
{
  std::string id = "Host";
  std::string name = "Host";
  std::string description = "VPX plugin host";
};

// A plugin found in the plugin folder.
struct PluginInfo
{
  std::string id;
  std::string name;
  std::string description;
  std::string version;
  bool loaded = false;
};

// The VPX pinball plugin bus, and the parts of it that are not specific to any
// one application.
//
// An application hosts VPX plugins by linking VPX's own MsgPluginManager (this
// library compiles it in) and registering itself as a statically linked plugin.
// PPUC uses this for its runtime, the ColorizingDMD editor for DMD output and
// media; both need the same message API, scriptable-class registry and settings
// answers.
//
// Owned by the application, constructed BEFORE and destroyed AFTER every
// client: at plugin unload the ScriptClassDefs and the code behind every
// plugin-provided function pointer go away together.
//
// NOT thread safe. Everything here runs on the plugin API thread, the thread
// that constructed the bus, normally the application's main loop.
class PluginBus final
{
 public:
  using HostInfo = vpxhost::HostInfo;
  using PluginInfo = vpxhost::PluginInfo;

  // Receives the bus's own messages (source empty) and every plugin's log
  // lines (source is the plugin's name). level is one of LPI_LVL_*.
  using LogHandler = std::function<void(unsigned int level, const std::string& source, const std::string& message)>;

  // Without a log handler, messages go to stdout ("[source:LEVEL] message",
  // the bus's own without a prefix).
  explicit PluginBus(HostInfo host = {}, LogHandler log = {});
  ~PluginBus();

  PluginBus(const PluginBus&) = delete;
  PluginBus& operator=(const PluginBus&) = delete;

  // Registers the host as a plugin, installs the settings handler and the
  // script API, then scans `pluginDir` for plugins (one folder per plugin, each
  // with a plugin.cfg). Returns false and fills `errorMessage` when the
  // directory is missing.
  bool Initialize(const std::string& pluginDir, std::string* errorMessage);

  // Unloads every plugin, host last. Safe to call twice, and when Initialize
  // failed or was never called.
  void Shutdown();

  // The plugins Initialize found, in the order they were found. The host is
  // not one of them.
  std::vector<PluginInfo> Plugins() const;

  // Loads a discovered plugin by its plugin.cfg id. Logs and returns false if
  // no such plugin was found, or it did not load.
  bool LoadPluginById(const std::string& id);
  // Unloads a plugin LoadPluginById loaded. Returns false if it was not loaded.
  bool UnloadPluginById(const std::string& id);
  bool IsPluginLoaded(const std::string& id) const;

  const MsgPluginAPI& Api() const;

  // The host's own endpoint id, as assigned by RegisterPlugin. Captured rather
  // than assumed: every subscription and broadcast of the host uses it.
  uint32_t HostEndpointId() const { return m_hostEndpointId; }

  // Drains RunOnMainThread queues. Must be called from the main loop -- every
  // audio buffer and many display updates a plugin produces are marshalled
  // through here.
  void Process();

  // ---- Settings ---------------------------------------------------------
  // Plugins declare their settings and the host answers once, when they
  // register them. A single handler serves all of them. Anything not
  // overridden takes the plugin's own default; a plugin asking the host to
  // persist a setting is ignored. Set overrides before loading the plugin.
  //
  // Values are text: "1"/"true"/"on"/"yes" and "0"/"false"/"off"/"no" for
  // booleans, numbers for int and float (clamped to the plugin's range), paths
  // as UTF-8 for folder and file settings.
  void SetSettingOverride(const std::string& pluginId, const std::string& propId, const std::string& value);

  // ---- Scriptable classes -----------------------------------------------
  // Some plugins are reachable only through a COM-style override rather than a
  // bus message: B2S registers "B2S.Server", PinMAME registers
  // "VPinMAME.Controller".
  const ScriptClassDef* ComOverride(const char* comClassName) const;
  ScriptClassDef* ClassDef(const char* typeName) const;

  // The last error a plugin reported through the script API, cleared by the
  // read. Callers use it to explain why a member call failed.
  bool TakeLastScriptError(std::string* out);

 private:
  void ConfigureSetting(const std::string& pluginId, MsgPI::MsgPluginManager::SettingAction action,
                        MsgSettingDef* settingDef);
  void LogHost(unsigned int level, const std::string& message) const;

  static void MSGPIAPI OnGetScriptApi(unsigned int msgId, void* context, void* msgData);
  static void MSGPIAPI OnGetLoggingApi(unsigned int msgId, void* context, void* msgData);

  static void MSGPIAPI Log(const char* source, const char* func, int line, unsigned int level,
                           const char* message);
  static void MSGPIAPI RegisterScriptClass(ScriptClassDef* classDef);
  static void MSGPIAPI RegisterScriptTypeAlias(const char*, const char*) {}
  static void MSGPIAPI RegisterScriptArrayType(ScriptArrayDef*) {}
  static void MSGPIAPI SubmitTypeLibrary(unsigned int) {}
  static void MSGPIAPI UnregisterScriptClass(ScriptClassDef* classDef);
  static void MSGPIAPI UnregisterScriptTypeAlias(const char*) {}
  static void MSGPIAPI UnregisterScriptArrayType(ScriptArrayDef*) {}
  static void MSGPIAPI OnScriptError(unsigned int type, const char* message);
  static void MSGPIAPI SetCOMObjectOverride(const char* classname, const ScriptClassDef* classDef);
  static ScriptClassDef* MSGPIAPI GetClassDef(const char* typeName);

  HostInfo m_host;
  LogHandler m_log;
  MsgPI::MsgPluginManager m_manager;
  std::shared_ptr<MsgPI::MsgPlugin> m_hostPlugin;
  std::vector<std::shared_ptr<MsgPI::MsgPlugin>> m_loadedPlugins;
  uint32_t m_hostEndpointId = 0;
  bool m_initialized = false;

  unsigned int m_getScriptApiId = 0;
  unsigned int m_getLoggingApiId = 0;

  std::map<std::pair<std::string, std::string>, std::string> m_settingOverrides;
  std::unordered_map<std::string, ScriptClassDef*> m_scriptClasses;
  std::unordered_map<std::string, const ScriptClassDef*> m_comOverrides;
  std::string m_lastScriptError;

  // The plugin APIs are C structs of function pointers with no context
  // argument, so they need a single instance to reach. The application creates
  // one bus; the constructor asserts it.
  static PluginBus* s_instance;
  static ScriptablePluginAPI s_scriptApi;
  static LoggingPluginAPI s_loggingApi;
};

}  // namespace vpxhost
