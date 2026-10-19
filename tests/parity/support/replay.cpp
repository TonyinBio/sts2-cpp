#include "replay.hpp"
#include "exact_json.hpp"
#include <openssl/evp.h>
#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sts2::parity {
namespace {
using Json = nlohmann::json;
std::string sha256(std::string_view bytes) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int size = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), digest.data(), &size, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("SHA-256 failed");
    std::ostringstream out;
    for (unsigned int i = 0; i < size; ++i)
        out << std::hex << std::setw(2) << std::setfill('0') << unsigned(digest[i]);
    return out.str();
}
// Re-emit numeric tokens from the exact envelope parse, never through double.
std::string wire(const Json& value, const std::string& path,
                 const std::map<std::string, std::string>& numbers) {
    if (value.is_number_float()) return numbers.at(path);
    if (!value.is_structured()) return value.dump();
    std::string result = value.is_array() ? "[" : "{";
    bool first = true;
    for (const auto& [key, item] : value.items()) {
        if (!first) result += ',';
        first = false;
        if (value.is_object()) result += Json(key).dump() + ':';
        result += wire(item, path + "/" + detail::pointer_token(key), numbers);
    }
    return result + (value.is_array() ? "]" : "}");
}
}
std::vector<ReplayFailure> replay_parity(
    std::string_view action_bytes, std::string_view state_bytes,
    const std::set<std::string>& features, const Json& reference,
    const ReplayBackend& backend) {
    const auto input = detail::ExactDocument::parse(action_bytes);
    const auto actions = ActionRecording::parse(input.value, features);
    const auto states = detail::ExactDocument::parse(state_bytes);
    validate_state_recording(states.value, actions, features);
    if (states.value.at("actions_sha256") != sha256(action_bytes))
        throw std::invalid_argument("/actions_sha256: action file digest mismatch");
    if (states.value.at("reference") != reference)
        throw std::invalid_argument("/reference: reference provenance mismatch");
    if (!backend.initialize || !backend.apply || !backend.snapshot)
        throw std::invalid_argument("Incomplete replay backend");
    // Validate exact snapshots before executing anything; envelope validation above
    // also enforces action association and terminal-state rules.
    auto expected = [&](const std::string& path) {
        return Snapshot::parse_text(wire(states.value.at(Json::json_pointer(path)), path, states.numbers));
    };
    const auto initial = expected("/initial");
    std::vector<Snapshot> checkpoints;
    for (std::size_t i = 0; i < states.value.at("transitions").size(); ++i)
        checkpoints.push_back(expected("/transitions/" + std::to_string(i) + "/state"));
    std::string boundary = "initial";
    try {
        backend.initialize(actions.json().at("setup"), initial);
        auto diff = compare(initial, backend.snapshot());
        if (!diff.empty()) return {{boundary, std::move(diff)}};
        std::size_t next = 0;
        const auto& transitions = states.value.at("transitions");
        for (const auto& entry : actions.json().at("actions")) {
            boundary = entry.at("id").get<std::string>();
            backend.apply(entry.at("action"));
            if (next < transitions.size() && transitions[next].at("action_id") == boundary) {
                diff = compare(checkpoints[next++], backend.snapshot());
                if (!diff.empty()) return {{boundary, std::move(diff)}};
            }
        }
    } catch (const std::exception& error) {
        throw std::runtime_error("Replay at " + boundary + ": " + error.what());
    }
    return {};
}
}
