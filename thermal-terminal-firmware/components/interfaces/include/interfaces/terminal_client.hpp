#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "settings/user_settings.hpp"
#include "tpb1/parser.hpp"

namespace thermal_terminal {

struct ReasoningLevel {
  std::string id;
  std::string label;
};

struct ModelInfo {
  std::string id;
  std::string provider;
  std::string name;
  std::string label;
  std::vector<ReasoningLevel> reasoning_levels;
};

struct HistoryMessage {
  std::string id;
  std::uint32_t sequence{};
  bool assistant{};
  std::string content;
  std::uint64_t created_at{};
};

struct RequestOptions {
  std::string request_id;
  std::string session_id;
  std::string model_id;
  std::uint8_t reasoning_level{};
  bool use_context{true};
  RenderSettings render;
};

struct SendRequest : RequestOptions {
  std::string content;
};

struct BinaryResponseMetadata {
  std::string session_id;
  std::string request_id;
  std::string user_message_id;
  std::string assistant_message_id;
  std::string source_user_message_id;
};

struct ClientResult {
  bool ok{};
  std::uint16_t http_status{};
  std::string error_code;
  std::string error_message;
};

class TerminalClient {
public:
  virtual ~TerminalClient() = default;

  virtual ClientResult fetch_models(const std::string &etag,
                                    std::vector<ModelInfo> &models,
                                    std::string &default_model,
                                    std::string &response_etag) = 0;

  virtual ClientResult fetch_history(const std::string &session_id,
                                     const std::string &before,
                                     std::vector<HistoryMessage> &messages,
                                     std::string &next_cursor) = 0;

  virtual ClientResult send(const SendRequest &request, tpb1::Sink &bitmap_sink,
                            BinaryResponseMetadata &metadata) = 0;

  virtual ClientResult retry(const RequestOptions &request,
                             tpb1::Sink &bitmap_sink,
                             BinaryResponseMetadata &metadata) = 0;

  virtual ClientResult render_message(const std::string &session_id,
                                      const std::string &message_id,
                                      const RenderSettings &settings,
                                      tpb1::Sink &bitmap_sink,
                                      BinaryResponseMetadata &metadata) = 0;

  virtual ClientResult clear_history(const std::string &session_id,
                                     std::uint32_t &deleted) = 0;
};

} // namespace thermal_terminal
