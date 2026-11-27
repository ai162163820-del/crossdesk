// 客户端可执行文件名：品牌化后主程序改名为 6glive-desk.exe。
// 服务、会话助手、更新器和 RDP 交接都必须用同一个名字，否则会出现
// “宿主找不到主程序 → 没人登录信令 → 界面一直显示未连接服务器”。
#ifndef _CLIENT_EXECUTABLE_H_
#define _CLIENT_EXECUTABLE_H_

// 主程序文件名（宽字符字面量，不含目录）。
#define CROSSDESK_CLIENT_EXE L"6glive-desk.exe"

// 主程序文件名（窄字符，用于比较进程名等场景）。
#define CROSSDESK_CLIENT_EXE_NARROW "6glive-desk.exe"

#endif  // _CLIENT_EXECUTABLE_H_
