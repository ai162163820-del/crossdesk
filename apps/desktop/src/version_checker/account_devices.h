// 账号设备列表：登录 6glive 门户并拉取同账号设备。
//
// 放在 version_checker 目标里，因为该目标已经链接 cpp-httplib + OpenSSL；
// 直接给 gui 目标加 cpp-httplib 会让 xmake 输出含中文的绝对包含路径，
// 导致 cl.exe 在中文代码页下解析参数失败（D9024 / 找不到头文件）。

#ifndef _ACCOUNT_DEVICES_H_
#define _ACCOUNT_DEVICES_H_

#include <string>
#include <vector>

namespace crossdesk::account {

struct Device {
  std::string id;
  std::string name;
};

struct DevicesResult {
  // 界面按错误码本地化，不再直接显示这里的中文串。
  enum class Error {
    none = 0,
    network,
    credentials,
    session,
    devices,
    parse,
    expired,
  };
  bool ok = false;
  Error error_code = Error::none;
  std::string error;
  std::string email;
  std::string plan;
  // 门户会话 token：登录成功时会下发，界面负责持久化（见 account_session.h）。
  std::string session_token;
  long long expires_at = 0;
  std::vector<Device> devices;
};

// 阻塞调用：POST /login 取会话 Cookie，再 GET /api/account/devices。
// 调用方应放在工作线程，不要阻塞 UI 线程。
// 登录成功后，如果本机不在账号设备列表里，会自动把它绑定到账号。
DevicesResult FetchDevices(const std::string& email, const std::string& password,
                           const std::string& local_device_id = "",
                           const std::string& local_device_name = "");

// 用已保存的会话 token 刷新会员信息与设备列表；会话失效返回 Error::expired。
DevicesResult FetchDevicesWithSession(const std::string& session_token,
                                      const std::string& local_device_id = "",
                                      const std::string& local_device_name = "");

}  // namespace crossdesk::account

#endif  // _ACCOUNT_DEVICES_H_
