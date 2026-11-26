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
  bool ok = false;
  std::string error;
  std::string email;
  std::string plan;
  long long expires_at = 0;
  std::vector<Device> devices;
};

// 阻塞调用：POST /login 取会话 Cookie，再 GET /api/account/devices。
// 调用方应放在工作线程，不要阻塞 UI 线程。
DevicesResult FetchDevices(const std::string& email, const std::string& password);

}  // namespace crossdesk::account

#endif  // _ACCOUNT_DEVICES_H_
