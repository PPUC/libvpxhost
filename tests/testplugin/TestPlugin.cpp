// A plugin that reports what the host gave it, through the host's logging API.
//
// It registers one setting of every type the SDK has, a script class and a COM
// override, then logs each setting's value as "<propId>=<value>", the API
// versions it was handed, and "unloaded" when it goes. The tests read those
// lines from the bus's log handler.

#include <cstdint>
#include <string>

#include "plugins/LoggingPlugin.h"
#include "plugins/MsgPlugin.h"
#include "plugins/ScriptablePlugin.h"

namespace
{

const MsgPluginAPI* g_api = nullptr;
uint32_t g_endpointId = 0;
LoggingPluginAPI* g_log = nullptr;
ScriptablePluginAPI* g_script = nullptr;

void Report(const std::string& line)
{
  if (g_log != nullptr)
  {
    g_log->Log("VpxHostTest", "", 0, LPI_LVL_INFO, line.c_str());
  }
}

template <typename Api>
Api* GetApi(const char* nameSpace, const char* name)
{
  Api* api = nullptr;
  const unsigned int msgId = g_api->GetMsgID(nameSpace, name);
  g_api->BroadcastMsg(g_endpointId, msgId, &api);
  g_api->ReleaseMsgID(msgId);
  return api;
}

void MSGPIAPI SetText(const char* value) { Report(std::string("Text=") + value); }
void MSGPIAPI SetFolder(const char* value) { Report(std::string("Folder=") + value); }
void MSGPIAPI SetFile(const char* value) { Report(std::string("File=") + value); }
void MSGPIAPI SetFlag(int value) { Report("Flag=" + std::to_string(value)); }
void MSGPIAPI SetCount(int value) { Report("Count=" + std::to_string(value)); }
void MSGPIAPI SetRatio(float value) { Report("Ratio=" + std::to_string(value)); }

MsgSettingDef g_textSetting{.propId = "Text",
                            .name = "Text",
                            .description = "",
                            .isUserEditable = 1,
                            .type = MSGPI_SETTING_TYPE_STRING,
                            .stringDef = {"default text", nullptr, &SetText}};
MsgSettingDef g_folderSetting{.propId = "Folder",
                              .name = "Folder",
                              .description = "",
                              .isUserEditable = 1,
                              .type = MSGPI_SETTING_TYPE_FOLDER,
                              .folderDef = {"default/folder", nullptr, &SetFolder}};
MsgSettingDef g_fileSetting{.propId = "File",
                            .name = "File",
                            .description = "",
                            .isUserEditable = 1,
                            .type = MSGPI_SETTING_TYPE_FILE,
                            .fileDef = {"default.txt", "txt", nullptr, &SetFile}};
MsgSettingDef g_flagSetting{.propId = "Flag",
                            .name = "Flag",
                            .description = "",
                            .isUserEditable = 1,
                            .type = MSGPI_SETTING_TYPE_BOOL,
                            .boolDef = {1, nullptr, &SetFlag}};
MsgSettingDef g_countSetting{.propId = "Count",
                             .name = "Count",
                             .description = "",
                             .isUserEditable = 1,
                             .type = MSGPI_SETTING_TYPE_INT,
                             .intDef = {0, 10, 5, nullptr, nullptr, &SetCount}};
MsgSettingDef g_ratioSetting{.propId = "Ratio",
                             .name = "Ratio",
                             .description = "",
                             .isUserEditable = 1,
                             .type = MSGPI_SETTING_TYPE_FLOAT,
                             .floatDef = {0.0f, 1.0f, 0.0f, 0.5f, nullptr, &SetRatio}};

ScriptClassDef g_class{.name = {.name = "VpxHostTest.Object", .id = 0}, .CreateObject = nullptr, .nMembers = 0};

}  // namespace

MSGPI_EXPORT void MSGPIAPI VpxHostTestPluginLoad(const uint32_t endpointId, const MsgPluginAPI* api)
{
  g_api = api;
  g_endpointId = endpointId;
  g_log = GetApi<LoggingPluginAPI>(LOGPI_NAMESPACE, LOGPI_MSG_GET_API);
  g_script = GetApi<ScriptablePluginAPI>(SCRIPTPI_NAMESPACE, SCRIPTPI_MSG_GET_API);
  Report("versions=" + std::to_string(api->version) + "," + std::to_string(g_log ? g_log->version : -1) + "," +
         std::to_string(g_script ? g_script->version : -1));

  for (MsgSettingDef* setting :
       {&g_textSetting, &g_folderSetting, &g_fileSetting, &g_flagSetting, &g_countSetting, &g_ratioSetting})
  {
    api->RegisterSetting(endpointId, setting);
  }

  if (g_script != nullptr)
  {
    g_script->RegisterScriptClass(&g_class);
    g_script->SetCOMObjectOverride("VpxHostTest.Server", &g_class);
  }
}

MSGPI_EXPORT void MSGPIAPI VpxHostTestPluginUnload()
{
  if (g_script != nullptr)
  {
    g_script->UnregisterScriptClass(&g_class);
  }
  Report("unloaded");
  g_script = nullptr;
  g_log = nullptr;
  g_api = nullptr;
}
