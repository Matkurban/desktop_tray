// ---------------------------------------------------------------------------
// desktop_tray – Windows native implementation
//
// Uses Shell_NotifyIcon / NOTIFYICONDATA for the tray icon and Win32
// HMENU for the context menu.
// ---------------------------------------------------------------------------

#include "include/desktop_tray/desktop_tray_plugin.h"

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <strsafe.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <codecvt>
#include <memory>
#include <string>

#define WM_DESKTOP_TRAY (WM_USER + 100)

namespace {

const flutter::EncodableValue *ValueOrNull(const flutter::EncodableMap &map,
                                           const char *key) {
  auto it = map.find(flutter::EncodableValue(key));
  if (it == map.end())
    return nullptr;
  return &(it->second);
}

// Shared channel (set once during registration).
std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> g_channel;

class DesktopTrayPlugin : public flutter::Plugin {
public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows *registrar);

  explicit DesktopTrayPlugin(flutter::PluginRegistrarWindows *registrar);
  ~DesktopTrayPlugin() override;

private:
  flutter::PluginRegistrarWindows *registrar_;
  NOTIFYICONDATA nid_{};
  NOTIFYICONIDENTIFIER niif_{};
  HMENU hMenu_ = CreatePopupMenu();
  bool icon_set_ = false;
  int window_proc_id_ = -1;
  UINT taskbar_created_msg_ = 0;

  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter_;

  HWND GetMainWindow();
  void ApplyIcon();
  void BuildMenu(HMENU menu, const flutter::EncodableMap &args);

  std::optional<LRESULT> HandleWindowProc(HWND hwnd, UINT message,
                                          WPARAM wparam, LPARAM lparam);

  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue> &call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  void Destroy(
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void SetIcon(
      const flutter::EncodableMap &args,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void SetToolTip(
      const flutter::EncodableMap &args,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void SetContextMenu(
      const flutter::EncodableMap &args,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void PopUpContextMenu(
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
};

// Guard against double registration in multi-window scenarios.
static bool g_registered = false;

void DesktopTrayPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows *registrar) {
  if (g_registered)
    return;
  g_registered = true;

  g_channel = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
      registrar->messenger(), "desktop_tray",
      &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<DesktopTrayPlugin>(registrar);

  g_channel->SetMethodCallHandler(
      [p = plugin.get()](const auto &call, auto result) {
        p->HandleMethodCall(call, std::move(result));
      });

  registrar->AddPlugin(std::move(plugin));
}

DesktopTrayPlugin::DesktopTrayPlugin(flutter::PluginRegistrarWindows *registrar)
    : registrar_(registrar) {
  window_proc_id_ = registrar->RegisterTopLevelWindowProcDelegate(
      [this](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        return HandleWindowProc(hwnd, msg, wp, lp);
      });
  taskbar_created_msg_ = RegisterWindowMessage(L"TaskbarCreated");
}

DesktopTrayPlugin::~DesktopTrayPlugin() {
  registrar_->UnregisterTopLevelWindowProcDelegate(window_proc_id_);
  if (icon_set_) {
    Shell_NotifyIcon(NIM_DELETE, &nid_);
    if (nid_.hIcon)
      DestroyIcon(nid_.hIcon);
  }
  if (hMenu_)
    DestroyMenu(hMenu_);
}

HWND DesktopTrayPlugin::GetMainWindow() {
  return ::GetAncestor(registrar_->GetView()->GetNativeWindow(), GA_ROOT);
}

void DesktopTrayPlugin::ApplyIcon() {
  if (icon_set_) {
    Shell_NotifyIcon(NIM_MODIFY, &nid_);
  } else {
    HICON hIconBackup = nid_.hIcon;
    WCHAR szTipBackup[128];
    StringCchCopy(szTipBackup, _countof(szTipBackup), nid_.szTip);

    ZeroMemory(&nid_, sizeof(NOTIFYICONDATA));
    nid_.cbSize = sizeof(NOTIFYICONDATA);
    nid_.hWnd = GetMainWindow();
    nid_.uID = 1;
    nid_.hIcon = hIconBackup;
    StringCchCopy(nid_.szTip, _countof(nid_.szTip), szTipBackup);
    nid_.uCallbackMessage = WM_DESKTOP_TRAY;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON;
    if (nid_.szTip[0] != L'\0') {
      nid_.uFlags |= NIF_TIP;
    }
    Shell_NotifyIcon(NIM_ADD, &nid_);
  }

  niif_.cbSize = sizeof(NOTIFYICONIDENTIFIER);
  niif_.hWnd = nid_.hWnd;
  niif_.uID = nid_.uID;
  niif_.guidItem = GUID_NULL;

  icon_set_ = true;
}

void DesktopTrayPlugin::BuildMenu(HMENU menu,
                                  const flutter::EncodableMap &args) {
  auto items = std::get<flutter::EncodableList>(
      args.at(flutter::EncodableValue("items")));

  // Clear existing items.
  int count = GetMenuItemCount(menu);
  for (int i = 0; i < count; i++) {
    RemoveMenu(menu, 0, MF_BYPOSITION);
  }

  for (const auto &item_value : items) {
    auto item_map = std::get<flutter::EncodableMap>(item_value);
    int id = std::get<int>(item_map.at(flutter::EncodableValue("id")));
    auto type =
        std::get<std::string>(item_map.at(flutter::EncodableValue("type")));
    auto label =
        std::get<std::string>(item_map.at(flutter::EncodableValue("label")));
    bool disabled =
        std::get<bool>(item_map.at(flutter::EncodableValue("disabled")));
    auto *checked = std::get_if<bool>(ValueOrNull(item_map, "checked"));

    UINT_PTR item_id = id;
    UINT uFlags = MF_STRING;

    if (disabled)
      uFlags |= MF_GRAYED;

    if (type == "separator") {
      AppendMenuW(menu, MF_SEPARATOR, item_id, NULL);
    } else {
      if (type == "checkbox") {
        if (checked != nullptr) {
          uFlags |= (*checked ? MF_CHECKED : MF_UNCHECKED);
        }
      } else if (type == "submenu") {
        uFlags |= MF_POPUP;
        HMENU sub_menu = ::CreatePopupMenu();
        BuildMenu(sub_menu, std::get<flutter::EncodableMap>(item_map.at(
                                flutter::EncodableValue("submenu"))));
        item_id = reinterpret_cast<UINT_PTR>(sub_menu);
      }
      AppendMenuW(menu, uFlags, item_id, converter_.from_bytes(label).c_str());
    }
  }
}

std::optional<LRESULT> DesktopTrayPlugin::HandleWindowProc(HWND hWnd,
                                                           UINT message,
                                                           WPARAM wParam,
                                                           LPARAM lParam) {
  if (message == WM_DESTROY) {
    if (icon_set_) {
      Shell_NotifyIcon(NIM_DELETE, &nid_);
      if (nid_.hIcon)
        DestroyIcon(nid_.hIcon);
      icon_set_ = false;
    }
  } else if (message == WM_COMMAND) {
    flutter::EncodableMap data;
    data[flutter::EncodableValue("id")] =
        flutter::EncodableValue(static_cast<int>(wParam));
    g_channel->InvokeMethod("onTrayMenuItemClick",
                            std::make_unique<flutter::EncodableValue>(data));
  } else if (message == WM_DESKTOP_TRAY) {
    switch (lParam) {
    case WM_LBUTTONDOWN:
      g_channel->InvokeMethod("onTrayIconMouseDown",
                              std::make_unique<flutter::EncodableValue>());
      break;
    case WM_LBUTTONUP:
      g_channel->InvokeMethod("onTrayIconMouseUp",
                              std::make_unique<flutter::EncodableValue>());
      break;
    case WM_RBUTTONDOWN:
      g_channel->InvokeMethod("onTrayIconRightMouseDown",
                              std::make_unique<flutter::EncodableValue>());
      break;
    case WM_RBUTTONUP:
      g_channel->InvokeMethod("onTrayIconRightMouseUp",
                              std::make_unique<flutter::EncodableValue>());
      break;
    default:
      return DefWindowProc(hWnd, message, wParam, lParam);
    }
  } else if (message == taskbar_created_msg_) {
    if (taskbar_created_msg_ != 0 && icon_set_) {
      icon_set_ = false;
      ApplyIcon();
    }
  } else if (message == WM_POWERBROADCAST) {
    if ((wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) &&
        icon_set_) {
      icon_set_ = false;
      ApplyIcon();
    }
  }
  return std::nullopt;
}

// ---- Method handlers -------------------------------------------------------

void DesktopTrayPlugin::Destroy(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (icon_set_) {
    Shell_NotifyIcon(NIM_DELETE, &nid_);
    if (nid_.hIcon) {
      DestroyIcon(nid_.hIcon);
      nid_.hIcon = nullptr;
    }
    icon_set_ = false;
  }
  result->Success(flutter::EncodableValue(true));
}

void DesktopTrayPlugin::SetIcon(
    const flutter::EncodableMap &args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  auto iconPath =
      std::get<std::string>(args.at(flutter::EncodableValue("iconPath")));

  if (nid_.hIcon != nullptr) {
    DestroyIcon(nid_.hIcon);
  }

  nid_.hIcon = static_cast<HICON>(
      LoadImage(nullptr, converter_.from_bytes(iconPath).c_str(), IMAGE_ICON,
                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                LR_LOADFROMFILE));

  ApplyIcon();
  result->Success(flutter::EncodableValue(true));
}

void DesktopTrayPlugin::SetToolTip(
    const flutter::EncodableMap &args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  auto toolTip =
      std::get<std::string>(args.at(flutter::EncodableValue("toolTip")));

  nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  StringCchCopy(nid_.szTip, _countof(nid_.szTip),
                converter_.from_bytes(toolTip).c_str());
  Shell_NotifyIcon(NIM_MODIFY, &nid_);

  result->Success(flutter::EncodableValue(true));
}

void DesktopTrayPlugin::SetContextMenu(
    const flutter::EncodableMap &args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  BuildMenu(hMenu_, std::get<flutter::EncodableMap>(
                        args.at(flutter::EncodableValue("menu"))));
  result->Success(flutter::EncodableValue(true));
}

void DesktopTrayPlugin::PopUpContextMenu(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  HWND hWnd = GetMainWindow();
  POINT cursorPos;
  GetCursorPos(&cursorPos);
  SetForegroundWindow(hWnd);
  TrackPopupMenu(hMenu_, TPM_BOTTOMALIGN | TPM_LEFTALIGN, cursorPos.x,
                 cursorPos.y, 0, hWnd, NULL);
  result->Success(flutter::EncodableValue(true));
}

void DesktopTrayPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  const auto &method = call.method_name();

  if (method == "destroy") {
    Destroy(std::move(result));
  } else if (method == "setIcon") {
    SetIcon(std::get<flutter::EncodableMap>(*call.arguments()),
            std::move(result));
  } else if (method == "setToolTip") {
    SetToolTip(std::get<flutter::EncodableMap>(*call.arguments()),
               std::move(result));
  } else if (method == "setContextMenu") {
    SetContextMenu(std::get<flutter::EncodableMap>(*call.arguments()),
                   std::move(result));
  } else if (method == "popUpContextMenu") {
    PopUpContextMenu(std::move(result));
  } else {
    result->NotImplemented();
  }
}

} // namespace

void DesktopTrayPluginRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  DesktopTrayPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
