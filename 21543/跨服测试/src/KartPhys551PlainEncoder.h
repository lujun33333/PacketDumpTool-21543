#pragma once

#include <string>
#include <vector>

#include "yaml-cpp/yaml.h"

namespace QQSpeedKartPhys551
{
	bool HasPlain551(const YAML::Node& config);
	bool BuildCompactDataFromPlainYaml(const YAML::Node& config, std::vector<unsigned char>& outData, std::string* error = nullptr);
}
