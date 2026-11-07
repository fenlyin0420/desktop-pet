// 平台路径、字体、动态库候选、计时器与日志。
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fx {
namespace platform {

// UTF-8 路径
std::string exeDir();
std::string homeDir();
std::string configHome();  // 已创建
std::string stateHome();   // 已创建

std::string joinPath(const std::string& a, const std::string& b);

// 动态库候选路径（UTF-8）
std::vector<std::string> cubismLibraryCandidates();

// 界面字体文件（UTF-8 路径），找不到返回空
std::string findUiFont();

bool primaryScreenSize(int& w, int& h);

// Windows 用 timeBeginPeriod；Linux 为空
void initTiming();
void shutdownTiming();

// Windows: --console 时把 stdout/stderr 接到控制台；Linux 为空
void attachConsole();

}  // namespace platform

// 写日志到 stateHome()/fxpet.log
void logLine(const char* fmt, ...);

}  // namespace fx
