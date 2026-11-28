#include "application/gui_application.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <SDL3/SDL.h>

#include "rd_log.h"
#include "unattended_config.h"
#include "shared_identity.h"
#include "platform/windows/winrt_apartment.h"

namespace crossdesk {

int GuiApplication::RunUnattended(void* stop_event) {
  // 诊断：不依赖日志系统，在宿主入口最前面留下痕迹（运营排查用，保留）。
  {
    std::error_code ec;
    std::filesystem::create_directories("C:\\ProgramData\\6glive", ec);
    std::ofstream trace("C:\\ProgramData\\6glive\\host-start.log", std::ios::app);
    if (trace) trace << "host entry pid=" << GetCurrentProcessId()
                     << " system=" << IsLocalSystemProcess()
                     << " unattended=" << IsUnattendedEnabled()
                     << " gui_present=" << SharedHostIdentity::GuiPresent() << "\n";
  }
  auto host_trace = [](const char* stage) {
    std::error_code ec;
    std::filesystem::create_directories("C:\\ProgramData\\6glive", ec);
    std::ofstream trace("C:\\ProgramData\\6glive\\host-start.log", std::ios::app);
    if (trace) trace << "stage=" << stage << " pid=" << GetCurrentProcessId() << std::endl;
  };
  host_trace("entry");
  if (!stop_event) { host_trace("no-stop-event"); return 1; }
  if (!IsLocalSystemProcess()) { host_trace("not-system"); return 1; }
  if (!RepairUnattendedDirectory(false)) { host_trace("prepare-dir-failed"); return 1; }
  host_trace("prepare-dir-ok");
  if (!IsUnattendedEnabled() && !SharedHostIdentity::GuiPresent()) {
    host_trace("no-unattended-no-gui");
    return 1;
  }
  unattended_host_ = true;
  host_trace("unattended-enabled");
  DWORD session = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session) || session == 0) {
    host_trace("bad-session");
    return 1;
  }

  path_manager_ = std::make_unique<PathManager>("CrossDesk");
  std::string identity_error;
  shared_identity_ = SharedHostIdentity::AcquireHost(identity_error);
  if (!shared_identity_) {
    LOG_ERROR("Cannot acquire the unattended host identity: {}", identity_error);
    host_trace("identity-failed");
    return 1;
  }
  cache_path_ = shared_identity_->CachePath().string();
  const auto log_directory = UnattendedLogPath();
  if (log_directory.empty()) return 1;
  exec_log_path_ = log_directory.string();
  dll_log_path_ = exec_log_path_;
  InitializeLogger();
  LOG_INFO("Unattended host initialized in session {}", session);
  // Reconnect starts WGC on this main thread, whereas the first unlock can
  // start it on the capture recovery thread. Both need their own WinRT scope.
  ScopedWinRtApartment apartment;
  if (!apartment.ready()) return 1;
  config_center_ = std::make_unique<ConfigCenter>(cache_path_ + "/config.ini");
  InitializeTrustedDevices(shared_identity_->TrustedDevicesPath());
  if (config_center_->SetPrivacyScreen(false) != 0 ||
      config_center_->SetSaveRemotePreviews(false) != 0)
    return 1;
  if (!InitializeSettings()) return 1;

  // The transport and device controllers do not need a Slint window, an SDL
  // video driver, a tray icon, or a logged-in user's audio/clipboard session.
  if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
  devices_.Initialize();
  local_host_ = std::make_shared<LocalHostChannel>(true);
  ULONGLONG last_status = 0;
  ULONGLONG next_peer_attempt = 0;
  host_trace("loop-enter");
  while (WaitForSingleObject(stop_event, 16) == WAIT_TIMEOUT) {
    HandleSessionCleanup();
    HandlePasswordChangeResult();
    HandleCredentialRecovery();
    TickLocalHost();
    const auto now = GetTickCount64();
    if (!peer_ && now >= next_peer_attempt) {
      next_peer_attempt = now + 3000;
      if (CreateConnectionPeer() != 0) CloseConnectionPeer("unattended-failed");
      else signal_login_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    } else if (peer_ && !signal_connected_.load()) {
      // 宿主自己的登录看门狗：超时未连上就重建，避免永久离线。
      if (signal_login_deadline_ == std::chrono::steady_clock::time_point{}) {
        signal_login_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      } else if (std::chrono::steady_clock::now() > signal_login_deadline_ &&
                 signal_login_attempts_ < 3) {
        ++signal_login_attempts_;
        LOG_WARN("Host signal login timed out, retry {}/3", signal_login_attempts_);
        if (signal_login_attempts_ >= 3) {
          LOG_WARN("Resetting local identity after repeated login failures");
          force_empty_identity_ = true;
          signal_login_attempts_ = 0;
        }
        signal_login_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        CloseConnectionPeer("host-login-timeout");
      }
    } else if (signal_connected_.load()) {
      signal_login_attempts_ = 0;
      signal_login_deadline_ = {};
    }
    TickControllerTrust();
    // The GUI owns presence/announcement subscriptions on this transport.
    // Publishing an empty headless subscription would replace its device list.
    HandleScreenLock();
    HandleWindowsServiceIntegration();
    devices_.UpdateInteractions();
    ShareLocalCursorState();
    if (now - last_status >= 1000) {
      last_status = now;
      std::string diagnostic;
      if (!WriteUnattendedStatus(settings_.ActiveIdentity(),
                                 signal_connected_.load(), session, &diagnostic))
        LOG_WARN("Could not publish unattended host status: {}", diagnostic);
      else if (!diagnostic.empty())
        LOG_WARN("Repaired machine identity directory permissions: {}", diagnostic);
    }
    // Pump native device messages without constructing an application window.
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }

  // Quiesce network callbacks before destroying the controllers they use.
  remote_input_.Stop();
  CloseAllRemoteSessions();
  WaitForSessionCleanup();
  local_host_.reset();
  keyboard_.ReleaseAllRemotePressedKeys("unattended_host_exit");
  devices_.DestroyDevices();
  devices_.DestroyFactories();
  privacy_.Shutdown();
  HandleScreenLock();
  WriteUnattendedStatus(settings_.ActiveIdentity(), false, session);
  SDL_Quit();
  return 0;
}
}  // namespace crossdesk
