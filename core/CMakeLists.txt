add_library(poetoolbox_core STATIC src/tool.cpp src/manifest.cpp src/registry.cpp src/localization.cpp src/home.cpp)
target_include_directories(poetoolbox_core PUBLIC include "${PROJECT_SOURCE_DIR}/third_party")
