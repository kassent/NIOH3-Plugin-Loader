#pragma once

#include <cstdint>
#include <windows.h>

constexpr uint32_t NIOH3_PLUGIN_API_VERSION = 1;

struct Nioh3PluginInitializeParam {
	uint32_t loader_api_version;
	uint16_t game_version_major;
	uint16_t game_version_minor;
	uint16_t game_version_patch;
	uint16_t game_version_build;
	const char* game_version_string;
	const char* game_root_dir;
	const char* plugins_dir;
};

using nioh3_plugin_initialize_fn = bool (*)(const Nioh3PluginInitializeParam* initialize_param);

