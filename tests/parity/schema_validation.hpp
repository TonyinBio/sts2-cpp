#pragma once
#include <nlohmann/json.hpp>
#include <string>
namespace sts2::parity::detail {
[[noreturn]] void invalid(const std::string& path, const std::string& reason);
std::string pointer_token(const std::string& key);
void validate(const nlohmann::json& value, const nlohmann::json& rule,
              const nlohmann::json& root, const std::string& path);
}
