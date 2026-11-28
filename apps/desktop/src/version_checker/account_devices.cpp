#include "account_devices.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace crossdesk::account {
namespace {

constexpr const char* kPortalBase = "https://desk.6glive.com";
constexpr const char* kSessionCookie = "portal_session=";

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

// 从 Set-Cookie 里取出 portal_session 的值（不带 Cookie 名）。
std::string SessionToken(const std::string& set_cookie) {
  const std::string name(kSessionCookie);
  const auto pos = set_cookie.find(name);
  if (pos == std::string::npos) return {};
  const auto start = pos + name.size();
  const auto end = set_cookie.find(';', start);
  return set_cookie.substr(
      start, end == std::string::npos ? std::string::npos : end - start);
}

httplib::Headers CookieHeaders(const std::string& token) {
  return httplib::Headers{{"Cookie", kSessionCookie + token}};
}

void Configure(httplib::Client& client) {
  client.set_connection_timeout(8);
  client.set_read_timeout(12);
  client.set_follow_location(false);
}

void ReadDeviceList(const nlohmann::json& parsed, DevicesResult* result) {
  result->email = parsed.value("email", "");
  result->plan = parsed.value("plan", "free");
  result->expires_at = parsed.value("expires_at", 0LL);
  result->devices.clear();
  if (parsed.contains("devices") && parsed["devices"].is_array()) {
    for (const auto& item : parsed["devices"]) {
      Device device;
      device.id = item.value("device_id", "");
      device.name = item.value("name", "");
      if (!device.id.empty()) result->devices.push_back(std::move(device));
    }
  }
}

// 本机不在账号设备列表里时自动绑定，然后重新拉取一次。
void BindLocalDevice(httplib::Client& client, const httplib::Headers& headers,
                     const std::string& local_id, const std::string& local_name,
                     DevicesResult* result) {
  if (local_id.empty()) return;
  const bool bound =
      std::any_of(result->devices.begin(), result->devices.end(),
                  [&](const Device& device) { return device.id == local_id; });
  if (bound) return;
  const auto form = "device_id=" + UrlEncode(local_id) + "&name=" +
                    UrlEncode(local_name);
  auto bind = client.Post("/account/devices", headers, form,
                          "application/x-www-form-urlencoded");
  if (!bind || bind->status >= 400) return;
  auto refreshed = client.Get("/api/account/devices", headers);
  if (!refreshed || refreshed->status != 200) return;
  auto again = nlohmann::json::parse(refreshed->body, nullptr, false);
  if (!again.is_discarded()) ReadDeviceList(again, result);
}

// 一次 GET /api/account/devices 及错误归类，登录和会话复用共用。
void LoadDevices(httplib::Client& client, const httplib::Headers& headers,
                 const std::string& local_id, const std::string& local_name,
                 DevicesResult* result) {
  auto list = client.Get("/api/account/devices", headers);
  if (!list) {
    result->error_code = DevicesResult::Error::network;
    return;
  }
  if (list->status == 401) {
    result->error_code = DevicesResult::Error::expired;
    return;
  }
  if (list->status != 200) {
    result->error_code = DevicesResult::Error::devices;
    return;
  }
  auto parsed = nlohmann::json::parse(list->body, nullptr, false);
  if (parsed.is_discarded()) {
    result->error_code = DevicesResult::Error::parse;
    return;
  }
  ReadDeviceList(parsed, result);
  BindLocalDevice(client, headers, local_id, local_name, result);
  result->ok = true;
}

}  // namespace

DevicesResult FetchDevices(const std::string& email, const std::string& password,
                           const std::string& local_device_id,
                           const std::string& local_device_name) {
  DevicesResult result;
  httplib::Client client(kPortalBase);
  Configure(client);

  const auto body = "email=" + UrlEncode(email) + "&password=" +
                    UrlEncode(password);
  auto login = client.Post("/login", body, "application/x-www-form-urlencoded");
  if (!login) {
    result.error_code = DevicesResult::Error::network;
    return result;
  }
  if (login->status != 200 && login->status != 302 && login->status != 303) {
    result.error_code = DevicesResult::Error::credentials;
    return result;
  }
  const auto token = SessionToken(login->get_header_value("Set-Cookie"));
  if (token.empty()) {
    result.error_code = DevicesResult::Error::session;
    return result;
  }
  // 交给界面保存，下次启动可以直接刷新会员信息。
  result.session_token = token;
  LoadDevices(client, CookieHeaders(token), local_device_id, local_device_name,
              &result);
  return result;
}

DevicesResult FetchDevicesWithSession(const std::string& session_token,
                                      const std::string& local_device_id,
                                      const std::string& local_device_name) {
  DevicesResult result;
  if (session_token.empty()) {
    result.error_code = DevicesResult::Error::expired;
    return result;
  }
  httplib::Client client(kPortalBase);
  Configure(client);
  LoadDevices(client, CookieHeaders(session_token), local_device_id,
              local_device_name, &result);
  if (result.ok) result.session_token = session_token;
  return result;
}

}  // namespace crossdesk::account
