#include "vpxhost/PluginBus.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace vpxhost
{

namespace
{

#if defined(_WIN32)
std::wstring WideFromUtf8(const std::string& text)
{
  if (text.empty())
  {
    return {};
  }
  const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
  return wide;
}
#endif

// Loads plugin shared objects with the platform's own loader. Paths are UTF-8,
// as MsgPluginManager passes them.
class NativeModuleLoader final : public MsgPI::MsgModuleLoader
{
 public:
  void* Link(const std::string& directory, const std::string& file) override
  {
#if defined(_WIN32)
    // The plugin's own folder is searched for the libraries it depends on
    // (ffmpeg next to the PUP plugin, libdmdutil next to DMDUtil).
    SetDllDirectoryW(WideFromUtf8(directory).c_str());
    HMODULE module = LoadLibraryW(WideFromUtf8(file).c_str());
    SetDllDirectoryW(nullptr);
    return reinterpret_cast<void*>(module);
#else
    (void)directory;
    return dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
  }

  void Unlink(void* dynamicModule) override
  {
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(dynamicModule));
#else
    dlclose(dynamicModule);
#endif
  }

  void* GetFunction(void* dynamicModule, const std::string& functionName) override
  {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(dynamicModule), functionName.c_str()));
#else
    return dlsym(dynamicModule, functionName.c_str());
#endif
  }
};

void MSGPIAPI HostPluginLoad(const uint32_t, const MsgPluginAPI*) {}
void MSGPIAPI HostPluginUnload() {}

std::filesystem::path PathFromUtf8(const std::string& text)
{
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

const char* LevelName(unsigned int level)
{
  if (level >= LPI_LVL_ERROR)
  {
    return "ERROR";
  }
  if (level >= LPI_LVL_WARN)
  {
    return "WARN";
  }
  if (level <= LPI_LVL_DEBUG)
  {
    return "DEBUG";
  }
  return "INFO";
}

void PrintLog(unsigned int level, const std::string& source, const std::string& message)
{
  if (source.empty())
  {
    std::printf("%s\n", message.c_str());
  }
  else
  {
    std::printf("[%s:%s] %s\n", source.c_str(), LevelName(level), message.c_str());
  }
}

bool ParseSettingBool(const std::string& text, bool fallback)
{
  if (text == "1" || text == "true" || text == "on" || text == "yes")
  {
    return true;
  }
  if (text == "0" || text == "false" || text == "off" || text == "no")
  {
    return false;
  }
  return fallback;
}

bool ParseSettingInt(const std::string& text, int* pValue)
{
  try
  {
    size_t consumed = 0;
    const int parsed = std::stoi(text, &consumed);
    if (consumed != text.size())
    {
      return false;
    }
    *pValue = parsed;
    return true;
  }
  catch (const std::exception&)
  {
    return false;
  }
}

bool ParseSettingFloat(const std::string& text, float* pValue)
{
  try
  {
    size_t consumed = 0;
    const float parsed = std::stof(text, &consumed);
    if (consumed != text.size())
    {
      return false;
    }
    *pValue = parsed;
    return true;
  }
  catch (const std::exception&)
  {
    return false;
  }
}

}  // namespace

PluginBus* PluginBus::s_instance = nullptr;

// version must be 1, as the SDK requires; plugins may check it.
ScriptablePluginAPI PluginBus::s_scriptApi = {
    .version = 1,
    .RegisterScriptClass = &PluginBus::RegisterScriptClass,
    .RegisterScriptTypeAlias = &PluginBus::RegisterScriptTypeAlias,
    .RegisterScriptArrayType = &PluginBus::RegisterScriptArrayType,
    .SubmitTypeLibrary = &PluginBus::SubmitTypeLibrary,
    .UnregisterScriptClass = &PluginBus::UnregisterScriptClass,
    .UnregisterScriptTypeAlias = &PluginBus::UnregisterScriptTypeAlias,
    .UnregisterScriptArrayType = &PluginBus::UnregisterScriptArrayType,
    .OnError = &PluginBus::OnScriptError,
    .SetCOMObjectOverride = &PluginBus::SetCOMObjectOverride,
    .GetClassDef = &PluginBus::GetClassDef,
};

LoggingPluginAPI PluginBus::s_loggingApi = {
    .version = 1,
    .Log = &PluginBus::Log,
};

PluginBus::PluginBus(HostInfo host, LogHandler log)
    : m_host(std::move(host)), m_log(log ? std::move(log) : LogHandler(PrintLog))
{
  // The plugin APIs are C structs with no context pointer, so their statics
  // have to reach one instance. The application creates exactly one.
  assert(s_instance == nullptr && "only one PluginBus may exist");
  s_instance = this;
}

PluginBus::~PluginBus()
{
  Shutdown();
  if (s_instance == this)
  {
    s_instance = nullptr;
  }
}

const MsgPluginAPI& PluginBus::Api() const { return m_manager.GetMsgAPI(); }

void PluginBus::LogHost(unsigned int level, const std::string& message) const { m_log(level, std::string(), message); }

bool PluginBus::Initialize(const std::string& pluginDir, std::string* errorMessage)
{
  if (m_initialized)
  {
    return true;
  }

  // Checked before anything is registered: a bus that fails here is destroyed
  // without Shutdown() having anything to undo.
  std::error_code error;
  if (!std::filesystem::is_directory(PathFromUtf8(pluginDir), error))
  {
    if (errorMessage != nullptr)
    {
      *errorMessage = "plugin directory does not exist: " + pluginDir;
    }
    return false;
  }

  m_hostPlugin = m_manager.RegisterPlugin(m_host.id, m_host.name, m_host.description, "", "", "", HostPluginLoad,
                                          HostPluginUnload);
  m_manager.LoadPlugin(*m_hostPlugin);
  m_hostEndpointId = m_hostPlugin->m_endpointId;
  assert(m_hostEndpointId != 0 && "RegisterPlugin must assign an endpoint id");

  m_manager.SetSettingsHandler([this](const std::string& pluginId, MsgPI::MsgPluginManager::SettingAction action,
                                      MsgSettingDef* settingDef) { ConfigureSetting(pluginId, action, settingDef); });

  const MsgPluginAPI& api = Api();
  m_getScriptApiId = api.GetMsgID(SCRIPTPI_NAMESPACE, SCRIPTPI_MSG_GET_API);
  m_getLoggingApiId = api.GetMsgID(LOGPI_NAMESPACE, LOGPI_MSG_GET_API);
  api.SubscribeMsg(m_hostEndpointId, m_getScriptApiId, OnGetScriptApi, this);
  api.SubscribeMsg(m_hostEndpointId, m_getLoggingApiId, OnGetLoggingApi, this);

  LogHost(LPI_LVL_INFO, "Plugin directory: " + pluginDir);
  m_manager.ScanPluginFolder(std::make_shared<NativeModuleLoader>(), PathFromUtf8(pluginDir),
                             [](MsgPI::MsgPlugin&) {});

  m_initialized = true;
  return true;
}

void PluginBus::Shutdown()
{
  if (!m_initialized)
  {
    return;
  }

  const MsgPluginAPI& api = Api();
  for (auto it = m_loadedPlugins.rbegin(); it != m_loadedPlugins.rend(); ++it)
  {
    if (*it && (*it)->IsLoaded())
    {
      m_manager.UnloadPlugin(**it);
    }
  }
  m_loadedPlugins.clear();

  api.UnsubscribeMsg(m_getScriptApiId, OnGetScriptApi, this);
  api.UnsubscribeMsg(m_getLoggingApiId, OnGetLoggingApi, this);
  api.ReleaseMsgID(m_getScriptApiId);
  api.ReleaseMsgID(m_getLoggingApiId);

  // Host last: unloading it tears down the endpoint every other subscription
  // was registered against.
  if (m_hostPlugin && m_hostPlugin->IsLoaded())
  {
    m_manager.UnloadPlugin(*m_hostPlugin);
  }
  m_hostPlugin.reset();
  m_manager.SetSettingsHandler(nullptr);

  m_scriptClasses.clear();
  m_comOverrides.clear();
  m_initialized = false;
}

std::vector<PluginBus::PluginInfo> PluginBus::Plugins() const
{
  std::vector<PluginInfo> plugins;
  for (const auto& plugin : m_manager.GetPlugins())
  {
    if (plugin == m_hostPlugin)
    {
      continue;
    }
    plugins.push_back({plugin->m_id, plugin->m_name, plugin->m_description, plugin->m_version, plugin->IsLoaded()});
  }
  return plugins;
}

bool PluginBus::LoadPluginById(const std::string& id)
{
  for (const auto& plugin : m_manager.GetPlugins())
  {
    if (plugin->m_id == id && plugin != m_hostPlugin)
    {
      if (plugin->IsLoaded())
      {
        return true;
      }
      m_manager.LoadPlugin(*plugin);
      if (!plugin->IsLoaded())
      {
        LogHost(LPI_LVL_ERROR, "Plugin did not load: " + id);
        return false;
      }
      m_loadedPlugins.push_back(plugin);
      return true;
    }
  }
  LogHost(LPI_LVL_ERROR, "Plugin not found: " + id);
  return false;
}

bool PluginBus::UnloadPluginById(const std::string& id)
{
  const auto it = std::find_if(m_loadedPlugins.begin(), m_loadedPlugins.end(),
                               [&id](const std::shared_ptr<MsgPI::MsgPlugin>& plugin) { return plugin->m_id == id; });
  if (it == m_loadedPlugins.end())
  {
    return false;
  }
  if ((*it)->IsLoaded())
  {
    m_manager.UnloadPlugin(**it);
  }
  m_loadedPlugins.erase(it);
  return true;
}

bool PluginBus::IsPluginLoaded(const std::string& id) const
{
  return std::any_of(m_loadedPlugins.begin(), m_loadedPlugins.end(),
                     [&id](const std::shared_ptr<MsgPI::MsgPlugin>& plugin)
                     { return plugin->m_id == id && plugin->IsLoaded(); });
}

void PluginBus::Process() { m_manager.ProcessAsyncCallbacks(); }

void PluginBus::SetSettingOverride(const std::string& pluginId, const std::string& propId, const std::string& value)
{
  m_settingOverrides[{pluginId, propId}] = value;
}

void PluginBus::ConfigureSetting(const std::string& pluginId, MsgPI::MsgPluginManager::SettingAction action,
                                 MsgSettingDef* settingDef)
{
  if (action != MsgPI::MsgPluginManager::SettingAction::Load || settingDef == nullptr)
  {
    return;
  }

  // An override applies to every type. One that reaches only some types is
  // accepted and then silently replaced by the plugin's default -- which looks
  // exactly like the setting having no effect.
  const std::string* override = nullptr;
  if (settingDef->propId != nullptr)
  {
    const auto it = m_settingOverrides.find({pluginId, settingDef->propId});
    if (it != m_settingOverrides.end())
    {
      override = &it->second;
    }
  }

  switch (settingDef->type)
  {
    case MSGPI_SETTING_TYPE_STRING:
      if (settingDef->stringDef.Set != nullptr)
      {
        const char* value = override != nullptr ? override->c_str() : settingDef->stringDef.defVal;
        settingDef->stringDef.Set(value ? value : "");
      }
      break;
    case MSGPI_SETTING_TYPE_FOLDER:
      if (settingDef->folderDef.Set != nullptr)
      {
        const char* value = override != nullptr ? override->c_str() : settingDef->folderDef.defVal;
        settingDef->folderDef.Set(value ? value : "");
      }
      break;
    case MSGPI_SETTING_TYPE_FILE:
      if (settingDef->fileDef.Set != nullptr)
      {
        const char* value = override != nullptr ? override->c_str() : settingDef->fileDef.defVal;
        settingDef->fileDef.Set(value ? value : "");
      }
      break;
    case MSGPI_SETTING_TYPE_BOOL:
      if (settingDef->boolDef.Set != nullptr)
      {
        settingDef->boolDef.Set(override != nullptr ? ParseSettingBool(*override, settingDef->boolDef.defVal)
                                                    : settingDef->boolDef.defVal);
      }
      break;
    case MSGPI_SETTING_TYPE_INT:
      if (settingDef->intDef.Set != nullptr)
      {
        int value = settingDef->intDef.defVal;
        if (override != nullptr && ParseSettingInt(*override, &value))
        {
          // Clamped rather than rejected: the plugin declares the range it can
          // accept, and passing something outside it through would be a worse
          // failure than quietly using the nearest legal value.
          value = std::clamp(value, settingDef->intDef.minVal, settingDef->intDef.maxVal);
        }
        settingDef->intDef.Set(value);
      }
      break;
    case MSGPI_SETTING_TYPE_FLOAT:
      if (settingDef->floatDef.Set != nullptr)
      {
        float value = settingDef->floatDef.defVal;
        if (override != nullptr && ParseSettingFloat(*override, &value))
        {
          value = std::clamp(value, settingDef->floatDef.minVal, settingDef->floatDef.maxVal);
        }
        settingDef->floatDef.Set(value);
      }
      break;
    default:
      if (override != nullptr)
      {
        LogHost(LPI_LVL_WARN, "Setting " + pluginId + "." + settingDef->propId + " has a type the host does not know (" +
                                  std::to_string(settingDef->type) + "); the override is ignored");
      }
      break;
  }
}

const ScriptClassDef* PluginBus::ComOverride(const char* comClassName) const
{
  if (comClassName == nullptr)
  {
    return nullptr;
  }
  const auto it = m_comOverrides.find(comClassName);
  return it == m_comOverrides.end() ? nullptr : it->second;
}

ScriptClassDef* PluginBus::ClassDef(const char* typeName) const
{
  if (typeName == nullptr)
  {
    return nullptr;
  }
  const auto it = m_scriptClasses.find(typeName);
  return it == m_scriptClasses.end() ? nullptr : it->second;
}

bool PluginBus::TakeLastScriptError(std::string* out)
{
  if (m_lastScriptError.empty())
  {
    return false;
  }
  if (out != nullptr)
  {
    *out = m_lastScriptError;
  }
  m_lastScriptError.clear();
  return true;
}

// ---- Static API trampolines ---------------------------------------------

void MSGPIAPI PluginBus::OnGetScriptApi(unsigned int, void*, void* msgData)
{
  if (msgData != nullptr)
  {
    *static_cast<ScriptablePluginAPI**>(msgData) = &s_scriptApi;
  }
}

void MSGPIAPI PluginBus::OnGetLoggingApi(unsigned int, void*, void* msgData)
{
  if (msgData != nullptr)
  {
    *static_cast<LoggingPluginAPI**>(msgData) = &s_loggingApi;
  }
}

void MSGPIAPI PluginBus::Log(const char* source, const char*, int, unsigned int level, const char* message)
{
  const std::string sourceName = source != nullptr && source[0] != '\0' ? source : "plugin";
  const std::string text = message != nullptr ? message : "";
  if (s_instance != nullptr)
  {
    s_instance->m_log(level, sourceName, text);
  }
  else
  {
    PrintLog(level, sourceName, text);
  }
}

void MSGPIAPI PluginBus::RegisterScriptClass(ScriptClassDef* classDef)
{
  if (s_instance == nullptr || classDef == nullptr || classDef->name.name == nullptr)
  {
    return;
  }
  s_instance->m_scriptClasses[classDef->name.name] = classDef;
}

void MSGPIAPI PluginBus::UnregisterScriptClass(ScriptClassDef* classDef)
{
  if (s_instance == nullptr || classDef == nullptr || classDef->name.name == nullptr)
  {
    return;
  }
  auto it = s_instance->m_scriptClasses.find(classDef->name.name);
  if (it != s_instance->m_scriptClasses.end() && it->second == classDef)
  {
    s_instance->m_scriptClasses.erase(it);
  }
  // A COM override naming this class is now dangling; drop it with the class.
  for (auto o = s_instance->m_comOverrides.begin(); o != s_instance->m_comOverrides.end();)
  {
    o = (o->second == classDef) ? s_instance->m_comOverrides.erase(o) : std::next(o);
  }
}

void MSGPIAPI PluginBus::OnScriptError(unsigned int type, const char* message)
{
  const std::string text = message != nullptr ? message : "";
  if (s_instance != nullptr)
  {
    s_instance->m_lastScriptError = text;
    s_instance->m_log(LPI_LVL_ERROR, "plugin-script:" + std::to_string(type), text);
  }
}

void MSGPIAPI PluginBus::SetCOMObjectOverride(const char* classname, const ScriptClassDef* classDef)
{
  if (s_instance == nullptr || classname == nullptr || classname[0] == '\0')
  {
    return;
  }
  if (classDef == nullptr)
  {
    s_instance->m_comOverrides.erase(classname);
    return;
  }
  s_instance->m_comOverrides[classname] = classDef;
}

ScriptClassDef* MSGPIAPI PluginBus::GetClassDef(const char* typeName)
{
  return s_instance == nullptr ? nullptr : s_instance->ClassDef(typeName);
}

}  // namespace vpxhost
