/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-04
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _GUI_APPLICATION_H_
#define _GUI_APPLICATION_H_

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <remote_action.h>

#include "application/signal_connection_indicator.h"
#include "rendering/video_geometry.h"
#include "runtime/cursor_state_provider.h"
#include "runtime/gui_runtime.h"

namespace crossdesk {

namespace account {
struct DevicesResult;
}

class UpdateChecker;
namespace updater { class Controller; }

// Slint application shell. GuiRuntime remains the owner of transport, media,
// device, clipboard, transfer and settings behavior.
class GuiApplication final : private GuiRuntime {
public:
  GuiApplication();
  ~GuiApplication();

  int Run(bool login_launch = false);
#ifdef _WIN32
  int RunUnattended(void* stop_event);
#endif

private:
  struct SlintUi;
  struct StreamView;
  StreamView* FindStreamView(const std::string& id);
  struct CursorDeliveryState {
    CursorState last_sent{};
    bool has_sent = false;
    bool feedback_pending = false;
    std::chrono::steady_clock::time_point last_sent_time{};
  };

  void InitializeLogger();
  bool InitializeSettings();
  void LoadHistoryWorkspace();
  bool InitializeSDL();
  void InitializeModules();
  void InitializeUi();
  void InitializeSystemTray();
  void ShowMainWindow(const std::string& activation_token = {});
  bool MinimizeMainWindowToTray();
  void BindMainCallbacks(bool gallery_window = false);
  void BindTrustedDeviceCallbacks(bool gallery_window = false);
  void BindStreamCallbacks(StreamView& view);
  void Tick();
#ifdef _WIN32
  void TickLocalHost();
#endif
  void PollUpdateCheck();
  void ExportDiagnosticLogs();
  void PollDiagnosticExport();
  void ShareLocalCursorState();
  bool RequestPasswordChange(const std::string &password);
  void HandlePasswordChangeResult();
  // 临时密码：登录成功后向服务端登记一次性密码，并在界面上显示倒计时。
  void RequestTempPassword();
  // 账号设备列表：登录门户并拉取同账号设备。
  void RequestAccountDevices(const std::string& email, const std::string& password);
  // 用本地保存的门户会话刷新会员信息与设备（启动时与面板打开时调用）。
  void RefreshAccountSession();
  // 把一次门户查询的结果落到界面状态；由工作线程调用，内部切回 UI 线程。
  void ApplyAccountResult(const crossdesk::account::DevicesResult& result,
                          const std::string& fallback_email);
  // 会员区（胶囊 + 头像 + 面板）按当前语言与已保存的账号状态重绘。
  void SyncAccountPanel();
  void HandleTempPasswordState();
  void HandleCredentialRecovery();
  void SyncMainWindow();
  void ShowGalleryWindow();
  void CloseGalleryWindow();
  void SyncWindowGeometry(bool gallery_window);
  void ToggleGalleryFullscreen();
  void RestoreWindowGeometry(bool before_show = false,
                             bool gallery_window = false);
  void SyncGallery();
  void GalleryAction(const std::string& action, const std::string& value,
                     bool gallery_window = true);
  bool AddGalleryDevice(const std::string& id, const std::string& password,
                        const std::string& group, bool remember_password);
  bool ImportGalleryRecent(const std::string& group);
  void DeviceGroupAction(const std::string& action, const std::string& value,
                         bool gallery_window);
  bool SaveDeviceGroups(const std::string& new_group, bool gallery_window);
  bool DeleteGroup(const std::string& name);
  void ReleasePressedInput(bool preserve_local_ui = false);
  void ReleaseControl(bool preserve_local_ui = false);
  void SyncAudioPlayback();
  void SaveGalleryWindowState();
  void SyncAnnouncements();
  void SyncConnectionDialog();
  void SyncPlatformDialogs();
  void SyncStreamWindows();
  void InitializeStreamWindow(StreamView& view);
  void SyncStreamWindow(StreamView& view);
  void ShowStreamWindow(const std::shared_ptr<RemoteSession>& session);
  void SyncStreamVideoFrame(StreamView& view, std::optional<double> timer_lateness_ms);
  void SyncVideoFrameGapStats();
  void ScheduleNextVideoFrame();
  void PresentPendingVideoFrame(std::optional<double> timer_lateness_ms = std::nullopt);
  // Posted from a decoder thread when a frame becomes presentable. Coalesces
  // multiple wakeups into one event-loop task so a busy UI cannot queue work
  // faster than it drains it.
  void RequestVideoPresent();
#if defined(__APPLE__)
  void UpdateVideoFrameScheduler();
#endif
  void SyncStreamKeyboardFocus();
  void SetStreamKeyboardFocus(StreamView& view, bool focused);
  bool AcceptStreamInput(StreamView& view);
  void SyncIncomingControllers();
  void SyncTrustedDevices();
  void SyncFileTransferNotifications();
  void SyncClipboardFileNotification();
  void OpenFileManager(const std::string& remote);
  std::string SessionDisplayName(const std::string& remote);
  void CancelFileTransfer(uint32_t file_id);
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
  void SyncXWaylandWindowActivation();
#endif
  void UpdateLocalization();
  void ResetSettingsUi(bool gallery_window = false);
  bool ValidateSelfHostedSettingsFromUi(bool gallery_window = false);
  bool SaveSettingsFromUi(bool gallery_window = false);
  bool PersistSettingsFromUi(bool gallery_window);
  void ApplySettingsFromConfig(bool reconnect);
#if _WIN32 && CROSSDESK_PORTABLE
  void CheckPortableWindowsService();
  void StartPortableWindowsServiceInstall();
  void JoinPortableWindowsServiceInstallThread();
#endif
  void ConnectFromUi(const std::string &remote_id);
  void DisconnectSession(const std::string &remote_id);
  void CloseDisconnectedSessions();
  void ResetPointerMotion();
  std::optional<VideoPoint> MapRemotePointer(RemoteSession& session, float x, float y);
  void SendPointerInput(int button, int kind, float x, float y);
  void SendScrollInput(float delta_x, float delta_y, float x, float y);
  void SendKeyInput(const std::string &text, bool pressed, bool control,
                    bool alt, bool shift, bool meta);
  void Cleanup();

  void OpenFileDialog(const std::string& title,
                      std::function<void(std::string)> completion);
  bool OpenUrl(const std::string &url);

  std::unique_ptr<SlintUi> ui_;
  std::unique_ptr<UpdateChecker> update_checker_;
#if CROSSDESK_HAS_UPDATER
  std::unique_ptr<updater::Controller> updater_;
#endif
  CursorStateProvider cursor_state_provider_;
  std::unordered_map<std::string, CursorDeliveryState>
      cursor_delivery_states_;
  uint32_t cursor_state_sequence_ = 0;
  std::chrono::steady_clock::time_point next_video_frame_time_{};
  std::chrono::steady_clock::time_point last_video_frame_gap_log_time_{};
  std::atomic<bool> video_present_queued_{false};
  SignalConnectionIndicator signal_connection_indicator_;
  // 会员区状态：plan_code 为空表示未登录；error_key 是待本地化的错误文案键。
  bool account_signed_in_ = false;
  // 一次请求在途时不再发起第二次（启动刷新与手动登录互斥）。
  bool account_busy_ = false;
  // 启动后只尝试恢复一次保存的门户会话。
  bool account_session_restored_ = false;
  std::string account_session_path_;
  std::string account_plan_code_;
  std::string account_email_;
  std::string account_initial_;
  std::string account_expiry_date_;
  std::string account_error_key_;
#if (defined(__linux__) && !defined(__OHOS__)) && !defined(__APPLE__)
  bool use_xwayland_gui_ = false;
  bool use_x11_custom_titlebar_ = false;
#endif
};

} // namespace crossdesk

#endif
