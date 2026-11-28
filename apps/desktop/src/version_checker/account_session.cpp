#include "account_session.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>

#include "private_file.h"

#if defined(_WIN32)
#include <windows.h>
// CryptProtectData / CryptUnprotectData
#include <dpapi.h>
#endif

namespace crossdesk::account {
namespace {

// "6glive-a" 会话文件魔数，避免把别的缓存文件当成会话解析。
constexpr char kMagic[8] = {'6', 'g', 'l', 's', 'e', 's', 's', '1'};
constexpr std::uint8_t kKindDpapi = 1;
constexpr std::uint8_t kKindPlain = 2;
constexpr std::size_t kMaxSessionBytes = 4096;

#if defined(_WIN32)
bool ProtectForUser(const std::string& plain, std::string* sealed) {
  DATA_BLOB input{};
  input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
  input.cbData = static_cast<DWORD>(plain.size());
  DATA_BLOB output{};
  const BOOL ok = CryptProtectData(&input, L"6glive account session", nullptr,
                                   nullptr, nullptr,
                                   CRYPTPROTECT_UI_FORBIDDEN, &output);
  if (!ok || output.pbData == nullptr) return false;
  sealed->assign(reinterpret_cast<const char*>(output.pbData), output.cbData);
  LocalFree(output.pbData);
  return true;
}

bool UnprotectForUser(const std::string& sealed, std::string* plain) {
  DATA_BLOB input{};
  input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(sealed.data()));
  input.cbData = static_cast<DWORD>(sealed.size());
  DATA_BLOB output{};
  const BOOL ok = CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                                     CRYPTPROTECT_UI_FORBIDDEN, &output);
  if (!ok || output.pbData == nullptr) return false;
  plain->assign(reinterpret_cast<const char*>(output.pbData), output.cbData);
  LocalFree(output.pbData);
  return true;
}
#endif

}  // namespace

StoredSession LoadSession(const std::string& path) {
  StoredSession session;
  std::string raw;
  try {
    if (!std::filesystem::exists(path)) return session;
    raw = private_file::Read(path);
  } catch (const std::exception&) {
    return session;
  }
  if (raw.size() <= sizeof(kMagic) + 1 ||
      raw.compare(0, sizeof(kMagic), kMagic, sizeof(kMagic)) != 0)
    return session;
  const auto kind = static_cast<std::uint8_t>(raw[sizeof(kMagic)]);
  const std::string payload = raw.substr(sizeof(kMagic) + 1);
  if (payload.empty() || payload.size() > kMaxSessionBytes) return session;
  switch (kind) {
    case kKindPlain:
      session.token = payload;
      break;
#if defined(_WIN32)
    case kKindDpapi:
      if (!UnprotectForUser(payload, &session.token)) return session;
      break;
#endif
    default:
      return session;
  }
  session.ok = !session.token.empty();
  return session;
}

bool SaveSession(const std::string& path, const std::string& token) {
  if (token.empty() || token.size() > kMaxSessionBytes) return false;
  std::string payload;
  std::uint8_t kind = kKindPlain;
#if defined(_WIN32)
  if (!ProtectForUser(token, &payload)) return false;
  kind = kKindDpapi;
#else
  payload = token;
#endif
  std::string raw(kMagic, sizeof(kMagic));
  raw.push_back(static_cast<char>(kind));
  raw += payload;
  try {
    private_file::Write(path, raw);
  } catch (const std::exception&) {
    return false;
  }
  return true;
}

void ClearSession(const std::string& path) {
  std::error_code error;
  std::filesystem::remove(path, error);
}

}  // namespace crossdesk::account
