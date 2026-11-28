// 门户账号会话（portal_session）的本地保存。
//
// 门户在 `POST /login` 后下发 HttpOnly 的 `portal_session`（默认 7 天），
// 客户端带着它调 `/api/account/devices` 就能拿到邮箱/会员/设备列表。
// 为了重启后仍能直接显示会员信息，把会话 token 存到本机：
//   - Windows：DPAPI（当前用户作用域）加密，文件本身再是用户私有 ACL；
//   - 其他平台：用户私有文件（0600），没有系统级密钥链可用。
// 失效（接口 401）时由调用方删除该文件。

#ifndef _ACCOUNT_SESSION_H_
#define _ACCOUNT_SESSION_H_

#include <string>

namespace crossdesk::account {

struct StoredSession {
  bool ok = false;
  std::string token;
};

// 文件格式：8 字节魔数 + 平台标记 + 载荷，不兼容旧版直接当作没有会话。
StoredSession LoadSession(const std::string& path);
bool SaveSession(const std::string& path, const std::string& token);
void ClearSession(const std::string& path);

}  // namespace crossdesk::account

#endif  // _ACCOUNT_SESSION_H_
