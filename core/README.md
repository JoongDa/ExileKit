# Core

纯 C++23 Tool / Manifest / Registry / Localization / Result 与配置模型，不依赖 Win32 UI。nlohmann/json 仅用于 parser / locale，UI 不直接使用 JSON。ConfigManager Windows 文件适配实现在 platform/windows/config.cpp，业务编排在 application/。公共接口见 include/poetoolbox/，详细边界见 docs/architecture.md。
