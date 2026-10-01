#include "terminal_client/http_terminal_client.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

namespace thermal_terminal {
namespace {

constexpr char kTag[] = "terminal-http";
constexpr std::size_t kMaxJsonResponse = 64 * 1024;
constexpr std::size_t kMaxErrorResponse = 8 * 1024;

struct RequestContext {
  tpb1::Parser *parser{};
  int status_code{};
  std::vector<std::uint8_t> body;
  std::string session_id;
  std::string request_id;
  std::string request_header;
  std::string assistant_header;
  std::string user_header;
  std::string source_user_header;
  std::string etag;
  std::string error_body;
  bool binary{};
};

void append_limited(std::string &output, const char *data, std::size_t length,
                    std::size_t maximum) {
  const std::size_t room =
      maximum > output.size() ? maximum - output.size() : 0;
  output.append(data, std::min(room, length));
}

esp_err_t on_http_event(esp_http_client_event_t *event) {
  auto *context = static_cast<RequestContext *>(event->user_data);
  if (context == nullptr)
    return ESP_OK;

  switch (event->event_id) {
  case HTTP_EVENT_ON_STATUS_CODE:
    if (event->data != nullptr && event->data_len == sizeof(int)) {
      context->status_code = *static_cast<const int *>(event->data);
    }
    break;
  case HTTP_EVENT_ON_HEADER:
    if (event->header_key == nullptr || event->header_value == nullptr)
      break;
    if (strcasecmp(event->header_key, "X-Session-Id") == 0) {
      context->session_id = event->header_value;
    } else if (strcasecmp(event->header_key, "X-Request-Id") == 0) {
      context->request_id = event->header_value;
    } else if (strcasecmp(event->header_key, "X-Assistant-Message-Id") == 0) {
      context->assistant_header = event->header_value;
    } else if (strcasecmp(event->header_key, "X-User-Message-Id") == 0) {
      context->user_header = event->header_value;
    } else if (strcasecmp(event->header_key, "X-Source-User-Message-Id") == 0) {
      context->source_user_header = event->header_value;
    } else if (strcasecmp(event->header_key, "ETag") == 0) {
      context->etag = event->header_value;
    }
    break;
  case HTTP_EVENT_ON_DATA:
    if (event->data == nullptr || event->data_len <= 0)
      break;
    if (context->binary && context->parser != nullptr &&
        context->status_code >= 200 && context->status_code < 300) {
      if (!context->parser->feed(static_cast<const std::uint8_t *>(event->data),
                                 static_cast<std::size_t>(event->data_len))) {
        return ESP_FAIL;
      }
    } else {
      append_limited(
          context->error_body, static_cast<const char *>(event->data),
          static_cast<std::size_t>(event->data_len), kMaxJsonResponse);
    }
    break;
  default:
    break;
  }
  return ESP_OK;
}

std::string join_url(const std::string &base, const char *path) {
  if (base.empty())
    return {};
  if (base.back() == '/')
    return base.substr(0, base.size() - 1) + path;
  return base + path;
}

void add_string(cJSON *object, const char *key, const std::string &value) {
  if (!value.empty())
    cJSON_AddStringToObject(object, key, value.c_str());
}

void add_render_options(cJSON *object, const RenderSettings &settings) {
  cJSON_AddNumberToObject(object, "fontSize", settings.font_size);
  cJSON_AddNumberToObject(object, "lineHeight", settings.line_height);
  cJSON_AddNumberToObject(object, "margin", settings.margin);
  cJSON_AddNumberToObject(object, "bottomFeed", settings.bottom_feed);
  cJSON_AddNumberToObject(object, "threshold", settings.threshold);
  cJSON_AddStringToObject(object, "dither",
                          settings.dither == DitherMode::kBayer4 ? "bayer4"
                                                                 : "threshold");
  cJSON_AddStringToObject(
      object, "compression",
      settings.compression == CompressionMode::kNone ? "none" : "packbits");
  cJSON_AddNumberToObject(object, "bandHeight", settings.band_height);
}

ClientResult result_from_http(int status, const std::string &body,
                              const char *operation) {
  ClientResult result;
  result.http_status = status > 0 ? static_cast<std::uint16_t>(status) : 0;
  result.ok = status >= 200 && status < 300;
  if (result.ok)
    return result;
  result.error_code = "HTTP_ERROR";
  result.error_message = operation;
  cJSON *root = cJSON_ParseWithLength(body.data(), body.size());
  if (root != nullptr) {
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(error, "code");
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(error, "message");
    if (cJSON_IsString(code))
      result.error_code = code->valuestring;
    if (cJSON_IsString(message))
      result.error_message = message->valuestring;
    cJSON_Delete(root);
  }
  return result;
}

ClientResult perform_request(const NetworkSettings &network, const char *path,
                             const std::string &json_body, const char *method,
                             tpb1::Parser *parser, RequestContext &context) {
  if (network.worker_url.empty() || network.terminal_token.empty()) {
    return {false, 0, "NETWORK_NOT_CONFIGURED", "Worker URL or token is empty"};
  }
  const std::string url = join_url(network.worker_url, path);
  esp_http_client_config_t config{};
  config.url = url.c_str();
  config.method = HTTP_METHOD_POST;
  config.event_handler = &on_http_event;
  config.user_data = &context;
  config.timeout_ms = 120000;
  config.buffer_size = 4096;
  config.buffer_size_tx = 4096;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr)
    return {false, 0, "HTTP_INIT_FAILED", "HTTP client init failed"};
  esp_http_client_set_header(client, "Authorization",
                             ("Bearer " + network.terminal_token).c_str());
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "Accept",
                             parser == nullptr ? "application/json"
                                               : "application/octet-stream");
  if (!json_body.empty())
    esp_http_client_set_post_field(client, json_body.data(), json_body.size());
  context.parser = parser;
  context.binary = parser != nullptr;

  esp_err_t error = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  if (error == ESP_OK && parser != nullptr && status >= 200 && status < 300 &&
      !parser->finish()) {
    error = ESP_ERR_INVALID_RESPONSE;
  }
  esp_http_client_cleanup(client);
  if (error != ESP_OK) {
    ClientResult result{false, static_cast<std::uint16_t>(status),
                        "HTTP_REQUEST_FAILED", esp_err_to_name(error)};
    if (parser != nullptr && parser->error() != tpb1::Error::kNone) {
      result.error_code = "TPB1_PARSE_FAILED";
      result.error_message = parser->error_message();
    }
    return result;
  }
  return result_from_http(status, context.error_body, path);
}

std::string print_json(cJSON *root) {
  char *text = cJSON_PrintUnformatted(root);
  if (text == nullptr)
    return {};
  std::string result(text);
  cJSON_free(text);
  return result;
}

void add_common_request(cJSON *root, const RequestOptions &request) {
  add_string(root, "requestId", request.request_id);
  add_string(root, "sessionId", request.session_id);
  add_string(root, "modelSelection", request.model_id);
  cJSON_AddBoolToObject(root, "useContext", request.use_context);
  cJSON_AddNumberToObject(root, "reasoningLevel", request.reasoning_level);
  cJSON *options = cJSON_AddObjectToObject(root, "options");
  add_render_options(options, request.render);
  cJSON_AddStringToObject(root, "format", "tpb");
}

} // namespace

HttpTerminalClient::HttpTerminalClient(const NetworkSettings &network)
    : network_(network) {}

ClientResult HttpTerminalClient::send(const SendRequest &request,
                                      tpb1::Sink &bitmap_sink,
                                      BinaryResponseMetadata &metadata) {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr)
    return {false, 0, "JSON_ALLOC_FAILED", "request JSON allocation failed"};
  add_common_request(root, request);
  add_string(root, "content", request.content);
  const std::string body = print_json(root);
  cJSON_Delete(root);
  RequestContext context;
  tpb1::Parser parser(bitmap_sink);
  const ClientResult result = perform_request(network_, "/api/terminal", body,
                                              "POST", &parser, context);
  metadata.session_id = context.session_id;
  metadata.request_id = context.request_id;
  metadata.user_message_id = context.user_header;
  metadata.assistant_message_id = context.assistant_header;
  return result;
}

ClientResult HttpTerminalClient::retry(const RequestOptions &request,
                                       tpb1::Sink &bitmap_sink,
                                       BinaryResponseMetadata &metadata) {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr)
    return {false, 0, "JSON_ALLOC_FAILED", "request JSON allocation failed"};
  add_common_request(root, request);
  const std::string body = print_json(root);
  cJSON_Delete(root);
  RequestContext context;
  tpb1::Parser parser(bitmap_sink);
  const ClientResult result = perform_request(network_, "/api/terminal/retry",
                                              body, "POST", &parser, context);
  metadata.session_id = context.session_id;
  metadata.request_id = context.request_id;
  metadata.source_user_message_id = context.source_user_header;
  metadata.assistant_message_id = context.assistant_header;
  return result;
}

ClientResult HttpTerminalClient::render_message(
    const std::string &session_id, const std::string &message_id,
    const RenderSettings &settings, tpb1::Sink &bitmap_sink,
    BinaryResponseMetadata &metadata) {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr)
    return {false, 0, "JSON_ALLOC_FAILED", "request JSON allocation failed"};
  add_string(root, "sessionId", session_id);
  add_string(root, "messageId", message_id);
  add_string(root, "requestId", "");
  cJSON_AddStringToObject(root, "format", "tpb");
  cJSON *options = cJSON_AddObjectToObject(root, "options");
  add_render_options(options, settings);
  const std::string body = print_json(root);
  cJSON_Delete(root);
  RequestContext context;
  tpb1::Parser parser(bitmap_sink);
  const ClientResult result = perform_request(
      network_, "/api/terminal/render-message", body, "POST", &parser, context);
  metadata.session_id = context.session_id;
  metadata.request_id = context.request_id;
  metadata.assistant_message_id = context.assistant_header;
  return result;
}

ClientResult HttpTerminalClient::clear_history(const std::string &session_id,
                                               std::uint32_t &deleted) {
  deleted = 0;
  RequestContext context;
  const std::string path = "/api/terminal/history?sessionId=" + session_id;
  const std::string url = join_url(network_.worker_url, path.c_str());
  if (network_.worker_url.empty() || network_.terminal_token.empty()) {
    return {false, 0, "NETWORK_NOT_CONFIGURED", "Worker URL or token is empty"};
  }
  esp_http_client_config_t config{};
  config.url = url.c_str();
  config.method = HTTP_METHOD_DELETE;
  config.event_handler = &on_http_event;
  config.user_data = &context;
  config.timeout_ms = 30000;
  config.buffer_size = 2048;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr)
    return {false, 0, "HTTP_INIT_FAILED", "HTTP client init failed"};
  esp_http_client_set_header(client, "Authorization",
                             ("Bearer " + network_.terminal_token).c_str());
  esp_err_t error = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  if (error != ESP_OK)
    return {false, static_cast<std::uint16_t>(status), "HTTP_REQUEST_FAILED",
            esp_err_to_name(error)};
  const ClientResult result =
      result_from_http(status, context.error_body, "DELETE history");
  if (!result.ok)
    return result;
  cJSON *root = cJSON_ParseWithLength(context.error_body.data(),
                                      context.error_body.size());
  if (root != nullptr) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "deleted");
    if (cJSON_IsNumber(value) && value->valuedouble >= 0)
      deleted = static_cast<std::uint32_t>(value->valuedouble);
    cJSON_Delete(root);
  }
  return result;
}

ClientResult HttpTerminalClient::fetch_models(const std::string &etag,
                                              std::vector<ModelInfo> &models,
                                              std::string &default_model,
                                              std::string &response_etag) {
  models.clear();
  default_model.clear();
  response_etag.clear();
  RequestContext context;
  const std::string url = join_url(network_.worker_url, "/api/terminal/config");
  if (network_.worker_url.empty() || network_.terminal_token.empty())
    return {false, 0, "NETWORK_NOT_CONFIGURED", "Worker URL or token is empty"};
  esp_http_client_config_t config{};
  config.url = url.c_str();
  config.method = HTTP_METHOD_GET;
  config.event_handler = &on_http_event;
  config.user_data = &context;
  config.timeout_ms = 30000;
  config.buffer_size = 2048;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr)
    return {false, 0, "HTTP_INIT_FAILED", "HTTP client init failed"};
  const std::string authorization = "Bearer " + network_.terminal_token;
  esp_http_client_set_header(client, "Authorization", authorization.c_str());
  if (!etag.empty())
    esp_http_client_set_header(client, "If-None-Match", etag.c_str());
  esp_err_t error = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  if (error != ESP_OK)
    return {false, static_cast<std::uint16_t>(status), "HTTP_REQUEST_FAILED",
            esp_err_to_name(error)};
  if (status == 304)
    return {true, 304, {}, {}};
  const ClientResult result =
      result_from_http(status, context.error_body, "GET config");
  if (!result.ok)
    return result;
  cJSON *root = cJSON_ParseWithLength(context.error_body.data(),
                                      context.error_body.size());
  if (root == nullptr)
    return {false, static_cast<std::uint16_t>(status), "INVALID_CONFIG_JSON",
            "invalid model catalog JSON"};
  const cJSON *default_item =
      cJSON_GetObjectItemCaseSensitive(root, "defaultModel");
  if (cJSON_IsString(default_item))
    default_model = default_item->valuestring;
  const cJSON *model_array = cJSON_GetObjectItemCaseSensitive(root, "models");
  if (cJSON_IsArray(model_array)) {
    const cJSON *model = nullptr;
    cJSON_ArrayForEach(model, model_array) {
      const cJSON *id = cJSON_GetObjectItemCaseSensitive(model, "id");
      const cJSON *name = cJSON_GetObjectItemCaseSensitive(model, "name");
      if (!cJSON_IsString(id) || !cJSON_IsString(name))
        continue;
      ModelInfo info;
      info.id = id->valuestring;
      info.name = name->valuestring;
      const cJSON *provider =
          cJSON_GetObjectItemCaseSensitive(model, "provider");
      const cJSON *label = cJSON_GetObjectItemCaseSensitive(model, "label");
      if (cJSON_IsString(provider))
        info.provider = provider->valuestring;
      if (cJSON_IsString(label))
        info.label = label->valuestring;
      const cJSON *levels =
          cJSON_GetObjectItemCaseSensitive(model, "reasoningLevels");
      if (cJSON_IsArray(levels)) {
        const cJSON *level = nullptr;
        cJSON_ArrayForEach(level, levels) {
          const cJSON *level_id = cJSON_GetObjectItemCaseSensitive(level, "id");
          const cJSON *level_label =
              cJSON_GetObjectItemCaseSensitive(level, "label");
          if (cJSON_IsString(level_id) && cJSON_IsString(level_label)) {
            info.reasoning_levels.push_back(
                {level_id->valuestring, level_label->valuestring});
          }
        }
      }
      models.push_back(std::move(info));
    }
  }
  cJSON_Delete(root);
  response_etag = context.etag;
  return result;
}

ClientResult HttpTerminalClient::fetch_history(
    const std::string &session_id, const std::string &before,
    std::vector<HistoryMessage> &messages, std::string &next_cursor) {
  messages.clear();
  next_cursor.clear();
  std::string path =
      "/api/terminal/history?sessionId=" + session_id + "&limit=20";
  if (!before.empty())
    path += "&before=" + before;
  RequestContext context;
  const std::string url = join_url(network_.worker_url, path.c_str());
  if (network_.worker_url.empty() || network_.terminal_token.empty())
    return {false, 0, "NETWORK_NOT_CONFIGURED", "Worker URL or token is empty"};
  esp_http_client_config_t config{};
  config.url = url.c_str();
  config.method = HTTP_METHOD_GET;
  config.event_handler = &on_http_event;
  config.user_data = &context;
  config.timeout_ms = 30000;
  config.buffer_size = 4096;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr)
    return {false, 0, "HTTP_INIT_FAILED", "HTTP client init failed"};
  const std::string authorization = "Bearer " + network_.terminal_token;
  esp_http_client_set_header(client, "Authorization", authorization.c_str());
  esp_err_t error = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  if (error != ESP_OK)
    return {false, static_cast<std::uint16_t>(status), "HTTP_REQUEST_FAILED",
            esp_err_to_name(error)};
  const ClientResult result =
      result_from_http(status, context.error_body, "GET history");
  if (!result.ok)
    return result;
  cJSON *root = cJSON_ParseWithLength(context.error_body.data(),
                                      context.error_body.size());
  if (root == nullptr)
    return {false, static_cast<std::uint16_t>(status), "INVALID_HISTORY_JSON",
            "invalid history JSON"};
  const cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "nextCursor");
  if (cJSON_IsString(cursor))
    next_cursor = cursor->valuestring;
  const cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "messages");
  if (cJSON_IsArray(array)) {
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, array) {
      const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
      const cJSON *content = cJSON_GetObjectItemCaseSensitive(item, "content");
      const cJSON *sequence =
          cJSON_GetObjectItemCaseSensitive(item, "sequence");
      const cJSON *role = cJSON_GetObjectItemCaseSensitive(item, "role");
      if (!cJSON_IsString(id) || !cJSON_IsString(content) ||
          !cJSON_IsNumber(sequence))
        continue;
      HistoryMessage message;
      message.id = id->valuestring;
      message.content = content->valuestring;
      message.sequence = static_cast<std::uint32_t>(sequence->valuedouble);
      message.assistant = cJSON_IsString(role) &&
                          std::strcmp(role->valuestring, "assistant") == 0;
      const cJSON *created =
          cJSON_GetObjectItemCaseSensitive(item, "createdAt");
      if (cJSON_IsNumber(created))
        message.created_at = static_cast<std::uint64_t>(created->valuedouble);
      messages.push_back(std::move(message));
    }
  }
  cJSON_Delete(root);
  return result;
}

} // namespace thermal_terminal
