#include "account_devices.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace crossdesk::account {
namespace {

std::string UrlEncode(const std::string& value) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size());
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 15];
    }
  }
  return out;
}

std::string SessionCookie(const std::string& set_cookie) {
  const auto pos = set_cookie.find("portal_session=");
  if (pos == std::string::npos) return {};
  const auto end = set_cookie.find(';', pos);
  return set_cookie.substr(
      pos, end == std::string::npos ? std::string::npos : end - pos);
}

}  // namespace

DevicesResult FetchDevices(const std::string& email, const std::string& password,
                           const std::string& local_device_id,
                           const std::string& local_device_name) {
  DevicesResult result;
  httplib::Client client("https://desk.6glive.com");
  client.set_connection_timeout(8);
  client.set_read_timeout(12);
  client.set_follow_location(false);

  const auto body = "email=" + UrlEncode(email) + "&password=" +
                    UrlEncode(password);
  auto login = client.Post("/login", body, "application/x-www-form-urlencoded");
  if (!login) {
    result.error = "无法连接账号服务器";
    return result;
  }
  if (login->status != 200 && login->status != 302 && login->status != 303) {
    result.error = "邮箱或密码不正确";
    return result;
  }
  const auto cookie = SessionCookie(login->get_header_value("Set-Cookie"));
  if (cookie.empty()) {
    result.error = "登录响应缺少会话";
    return result;
  }

  httplib::Headers headers{{"Cookie", cookie}};
  auto list = client.Get("/api/account/devices", headers);
  if (!list) {
    result.error = "无法获取设备列表";
    return result;
  }
  if (list->status == 401) {
    result.error = "登录已失效，请重试";
    return result;
  }
  if (list->status != 200) {
    result.error = "获取设备列表失败";
    return result;
  }
  auto parsed = nlohmann::json::parse(list->body, nullptr, false);
  if (parsed.is_discarded()) {
    result.error = "设备列表解析失败";
    return result;
  }
  result.email = parsed.value("email", "");
  result.plan = parsed.value("plan", "free");
  result.expires_at = parsed.value("expires_at", 0LL);
  if (parsed.contains("devices") && parsed["devices"].is_array()) {
    for (const auto& item : parsed["devices"]) {
      Device device;
      device.id = item.value("device_id", "");
      device.name = item.value("name", "");
      if (!device.id.empty()) result.devices.push_back(std::move(device));
    }
  }
  const bool already_bound =
      !local_device_id.empty() &&
      std::any_of(result.devices.begin(), result.devices.end(),
                  [&](const Device& device) { return device.id == local_device_id; });
  if (!local_device_id.empty() && !already_bound) {
    // 自动把本机绑定到账号，用户无需手工输入 9 位 ID。
    const auto form = "device_id=" + UrlEncode(local_device_id) + "&name=" +
                      UrlEncode(local_device_name);
    auto bind = client.Post("/account/devices", headers, form,
                            "application/x-www-form-urlencoded");
    if (bind && bind->status < 400) {
      auto refreshed = client.Get("/api/account/devices", headers);
      if (refreshed && refreshed->status == 200) {
        auto again = nlohmann::json::parse(refreshed->body, nullptr, false);
        if (!again.is_discarded() && again.contains("devices") &&
            again["devices"].is_array()) {
          result.devices.clear();
          for (const auto& item : again["devices"]) {
            Device device;
            device.id = item.value("device_id", "");
            device.name = item.value("name", "");
            if (!device.id.empty()) result.devices.push_back(std::move(device));
          }
        }
      }
    }
  }
  result.ok = true;
  return result;
}

}  // namespace crossdesk::account
