#pragma once

#include "interfaces/terminal_client.hpp"

namespace thermal_terminal {

class HttpTerminalClient final : public TerminalClient {
public:
  explicit HttpTerminalClient(const NetworkSettings &network);

  ClientResult fetch_models(const std::string &etag,
                            std::vector<ModelInfo> &models,
                            std::string &default_model,
                            std::string &response_etag) override;

  ClientResult fetch_history(const std::string &session_id,
                             const std::string &before,
                             std::vector<HistoryMessage> &messages,
                             std::string &next_cursor) override;

  ClientResult send(const SendRequest &request, tpb1::Sink &bitmap_sink,
                    BinaryResponseMetadata &metadata) override;

  ClientResult retry(const RequestOptions &request, tpb1::Sink &bitmap_sink,
                     BinaryResponseMetadata &metadata) override;

  ClientResult render_message(const std::string &session_id,
                              const std::string &message_id,
                              const RenderSettings &settings,
                              tpb1::Sink &bitmap_sink,
                              BinaryResponseMetadata &metadata) override;

  ClientResult clear_history(const std::string &session_id,
                             std::uint32_t &deleted) override;

private:
  NetworkSettings network_;
};

} // namespace thermal_terminal
