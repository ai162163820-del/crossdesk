// 账号会话落盘的往返测试（DPAPI 加密 + 私有文件）。
//
// 覆盖：空 token 拒绝、不存在文件、写入后读回、重复保存覆盖、退出登录删除。

#include <cstdio>
#include <filesystem>
#include <string>

#include "account_session.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", what);
  ++g_failures;
}

bool HasValue(const std::string& value) { return !value.empty(); }

}  // namespace

int main() {
  std::error_code error;
  const auto directory = std::filesystem::temp_directory_path(error) /
                         ("crossdesk-account-session-test-" +
                          std::to_string(static_cast<long long>(
                              std::filesystem::file_time_type::clock::now()
                                  .time_since_epoch()
                                  .count())));
  if (error) {
    std::fprintf(stderr, "FAIL: temp directory unavailable\n");
    return 1;
  }
  std::filesystem::create_directories(directory, error);
  if (error) {
    std::fprintf(stderr, "FAIL: cannot create temp directory\n");
    return 1;
  }
  const auto path = (directory / "account_session.bin").string();

  // 没有文件时不算“有会话”。
  Check(!crossdesk::account::LoadSession(path).ok, "missing file has no session");

  // 空 token 不写盘。
  Check(!crossdesk::account::SaveSession(path, ""), "empty token is rejected");
  Check(!std::filesystem::exists(path), "empty token writes nothing");

  // 往返：加密写入、读回同一个 token。
  const std::string token = "6glive-test-token-0123456789abcdef";
  Check(crossdesk::account::SaveSession(path, token), "session is saved");
  Check(std::filesystem::exists(path), "session file exists");
  const auto loaded = crossdesk::account::LoadSession(path);
  Check(loaded.ok, "saved session loads");
  Check(loaded.token == token, "loaded token matches");

  // 覆盖保存。
  const std::string rotated = "6glive-test-token-rotated-abcdef0123456789";
  Check(crossdesk::account::SaveSession(path, rotated), "session is rotated");
  Check(crossdesk::account::LoadSession(path).token == rotated,
        "rotated token matches");

  // 退出登录删除文件，且重复删除不抛异常。
  crossdesk::account::ClearSession(path);
  Check(!std::filesystem::exists(path), "sign-out removes the file");
  Check(!crossdesk::account::LoadSession(path).ok, "cleared session is gone");
  crossdesk::account::ClearSession(path);

  // 损坏/异种文件按“没有会话”处理，不能崩溃。
  {
    const auto foreign = (directory / "foreign.bin").string();
    std::FILE* file = std::fopen(foreign.c_str(), "wb");
    if (file) {
      const char junk[] = "not-a-session-file";
      std::fwrite(junk, 1, sizeof(junk) - 1, file);
      std::fclose(file);
    }
    Check(!crossdesk::account::LoadSession(foreign).ok, "foreign file is rejected");
    std::filesystem::remove(foreign, error);
  }

  std::filesystem::remove_all(directory, error);
  if (g_failures == 0) std::printf("account session tests passed\n");
  return g_failures == 0 ? 0 : 1;
}
