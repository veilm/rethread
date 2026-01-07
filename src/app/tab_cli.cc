#include "app/tab_cli.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <netdb.h>

#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <cstdlib>
#include <limits>
#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <regex>
#include <vector>
#include <csignal>
#include <ctime>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QUrl>

#include "app/user_dirs.h"

namespace rethread {
namespace {

void PrintTabUsage() {
  std::cerr
      << "Usage: rethread tabs [--user-data-dir=PATH] [--profile=NAME] <command>\n"
         "Commands:\n"
         "  get|list              List open tabs.\n"
         "  switch <id>           Activate the tab with the given id.\n"
         "  cycle <delta>         Move relative tab focus.\n"
         "  swap <target> [peer]  Swap/move tabs by index or +/- offset (wraps around).\n"
         "  open [--at-end] <url> Open a new tab (default inserts after the active tab).\n"
         "  history-back          Navigate back in the active tab.\n"
         "  history-forward       Navigate forward in the active tab.\n"
         "  close [index]         Close the tab at 1-based index or the active "
         "tab if omitted.\n"
         "\n"
         "Use `rethread bind ...` / `rethread unbind ...` for key bindings and\n"
         "`rethread tabstrip ...` to control the overlay.\n";
}

void PrintBindUsage() {
  std::cerr
      << "Usage: rethread bind [--user-data-dir=PATH] [--profile=NAME]\n"
      << "                     [mods] [--no-consume]\n"
      << "                      --key=K -- command...\n"
      << "Mods:\n"
      << "  --alt --ctrl --shift --command/--meta\n"
      << "Other flags:\n"
      << "  --context-menu       Bind right-clicks to run `command`\n"
      << "  --no-consume          Allow the key event to pass through to the page\n"
      << "  --user-data-dir PATH  Target a specific profile/socket\n";
}

void PrintUnbindUsage() {
  std::cerr
      << "Usage: rethread unbind [--user-data-dir=PATH] [--profile=NAME]\n"
      << "                       [mods] --key=K\n"
      << "Mods:\n"
      << "  --alt --ctrl --shift --command/--meta\n"
      << "Other flags:\n"
      << "  --context-menu       Clear the right-click binding\n";
}

void PrintTabStripUsage() {
  std::cerr
      << "Usage: rethread tabstrip [--user-data-dir=PATH] [--profile=NAME]\n"
      << "       show|hide|toggle|peek <ms>\n"
      << "       message --duration=MS [--stdin] <text>\n";
}

void PrintRulesUsage() {
  std::cerr
      << "Usage: rethread rules [--user-data-dir=PATH] [--profile=NAME]\n"
      << "                      (js|iframes) (--whitelist|--blacklist)\n"
      << "                      [--append]\n"
      << "  Provide newline-delimited hostnames via stdin "
         "(e.g. `rethread rules js --blacklist < hosts.txt`).\n";
}

void PrintScriptsUsage() {
  std::cerr
      << "Usage: rethread scripts [--user-data-dir=PATH] [--profile=NAME]\n"
      << "       add --id=ID [--match=PATTERN] [--run-at=TYPE] [--stylesheet] < script\n"
      << "       list\n"
      << "       rm --id=ID\n";
}

void PrintDevToolsUsage() {
  std::cerr
      << "Usage: rethread devtools [--user-data-dir=PATH] [--profile=NAME] open\n";
}
void PrintEvalUsage() {
  std::cerr
      << "Usage: rethread eval [--user-data-dir=PATH] [--profile=NAME] [--stdin]\n"
      << "                     [--tab-id=N|--tab-index=N] <script>\n"
      << "Options:\n"
      << "  --stdin              Read the script from stdin instead of argv\n"
      << "  --tab-id=N           Target a specific tab id (default: active tab)\n"
      << "  --tab-index=N        Target the 1-based tab index\n";
}
void PrintNetworkLogUsage() {
  std::cerr
      << "Usage: rethread network-log [--user-data-dir=PATH] [--profile=NAME]\n"
      << "                           --id=N [--dir PATH]\n"
      << "                           [--url REGEX] [--method REGEX]\n"
      << "                           [--status REGEX] [--mime REGEX]\n"
      << "                           [--cdp-port PORT]\n";
}

struct BindingOptions {
  bool alt = false;
  bool ctrl = false;
  bool shift = false;
  bool command = false;
  bool consume = true;
  std::string key;
  bool context_menu = false;
};

bool ParseBindingOptions(int argc,
                         char* argv[],
                         int* index,
                         BindingOptions* options,
                         bool allow_consume,
                         bool* encountered_separator) {
  if (!options) {
    return false;
  }
  bool separator = false;
  while (*index < argc) {
    std::string arg = argv[*index];
    if (arg == "--") {
      separator = true;
      ++(*index);
      break;
    }
    if (arg == "--alt") {
      options->alt = true;
      ++(*index);
      continue;
    }
    if (arg == "--ctrl") {
      options->ctrl = true;
      ++(*index);
      continue;
    }
    if (arg == "--shift") {
      options->shift = true;
      ++(*index);
      continue;
    }
    if (arg == "--command" || arg == "--meta") {
      options->command = true;
      ++(*index);
      continue;
    }
    if (allow_consume && arg == "--no-consume") {
      options->consume = false;
      ++(*index);
      continue;
    }
    if (arg == "--context-menu" || arg == "--right-click") {
      options->context_menu = true;
      ++(*index);
      continue;
    }
    const std::string key_prefix = "--key=";
    if (arg.rfind(key_prefix, 0) == 0) {
      options->key = arg.substr(key_prefix.size());
      ++(*index);
      continue;
    }
    if (arg == "--key") {
      if (*index + 1 >= argc) {
        std::cerr << "--key requires a value\n";
        return false;
      }
      options->key = argv[*index + 1];
      *index += 2;
      continue;
    }
    break;
  }
  if (encountered_separator) {
    *encountered_separator = separator;
  }
  return true;
}

bool ParseUserDataDir(int argc,
                      char* argv[],
                      const std::string& default_root,
                      std::string* user_data_dir,
                      int* index) {
  if (!user_data_dir || !index) {
    return false;
  }
  bool user_data_override = false;
  bool profile_specified = false;
  std::string profile_name = rethread::kDefaultProfileName;
  const char* env_dir = std::getenv("RETHREAD_USER_DATA_DIR");
  const std::string env_user_data_dir =
      (env_dir && env_dir[0] != '\0') ? std::string(env_dir) : std::string();
  for (; *index < argc; ++(*index)) {
    std::string arg = argv[*index];
    const std::string prefix = "--user-data-dir=";
    if (arg.rfind(prefix, 0) == 0) {
      *user_data_dir = arg.substr(prefix.size());
      user_data_override = true;
      continue;
    }
    if (arg == "--user-data-dir") {
      if (*index + 1 >= argc) {
        std::cerr << "Missing value after --user-data-dir\n";
        return false;
      }
      *user_data_dir = argv[++(*index)];
      user_data_override = true;
      continue;
    }
    const std::string profile_prefix = "--profile=";
    if (arg.rfind(profile_prefix, 0) == 0) {
      profile_name = arg.substr(profile_prefix.size());
      profile_specified = true;
      continue;
    }
    if (arg == "--profile") {
      if (*index + 1 >= argc) {
        std::cerr << "Missing value after --profile\n";
        return false;
      }
      profile_name = argv[++(*index)];
      profile_specified = true;
      continue;
    }
    break;
  }
  if (!user_data_override) {
    if (profile_specified) {
      std::string profile = profile_name.empty() ? rethread::kDefaultProfileName
                                                 : profile_name;
      if (default_root.empty()) {
        *user_data_dir = profile;
      } else if (default_root.back() == '/' || default_root.back() == '\\') {
        *user_data_dir = default_root + profile;
      } else {
        *user_data_dir = default_root + "/" + profile;
      }
    } else if (!env_user_data_dir.empty()) {
      *user_data_dir = env_user_data_dir;
    } else {
      std::string profile = rethread::kDefaultProfileName;
      if (default_root.empty()) {
        *user_data_dir = profile;
      } else if (default_root.back() == '/' || default_root.back() == '\\') {
        *user_data_dir = default_root + profile;
      } else {
        *user_data_dir = default_root + "/" + profile;
      }
    }
  }
  return true;
}

bool SendCommand(const std::string& socket_path, const std::string& payload) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    std::cerr << "Failed to create socket: " << std::strerror(errno) << "\n";
    return false;
  }

  sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s",
                socket_path.c_str());

  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::cerr << "Failed to connect to " << socket_path << ": "
              << std::strerror(errno) << "\n";
    close(fd);
    return false;
  }

  if (write(fd, payload.data(), payload.size()) < 0) {
    std::cerr << "Failed to send command: " << std::strerror(errno) << "\n";
    close(fd);
    return false;
  }

  char buffer[512];
  ssize_t n = 0;
  while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
    std::cout.write(buffer, n);
  }
  close(fd);
  return true;
}

bool ParsePositiveInt(const std::string& text, int* value) {
  if (!value) {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  long parsed = std::strtol(text.c_str(), &end, 10);
  if (errno != 0 || !end || *end != '\0' || parsed <= 0 ||
      parsed > std::numeric_limits<int>::max()) {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

bool IsValidScriptId(const std::string& id) {
  if (id.empty()) {
    return false;
  }
  for (char c : id) {
    if (std::isalnum(static_cast<unsigned char>(c)) ||
        c == '-' || c == '_' || c == '.') {
      continue;
    }
    if (c == '/' || c == '\\') {
      return false;
    }
    return false;
  }
  return true;
}

std::string HexEncode(const std::string& input) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string output;
  output.reserve(input.size() * 2);
  for (unsigned char c : input) {
    output.push_back(kHex[(c >> 4) & 0xF]);
    output.push_back(kHex[c & 0xF]);
  }
  return output;
}

bool ReadHttpHeaders(int fd, std::string* header_out,
                     std::string* remainder_out);
std::string TrimWhitespace(const std::string& input);
std::string ToLower(std::string value);

std::string TrimWhitespace(const std::string& input) {
  const size_t start = input.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return std::string();
  }
  const size_t end = input.find_last_not_of(" \t\r\n");
  return input.substr(start, end - start + 1);
}

bool SendCommandCapture(const std::string& socket_path,
                        const std::string& payload,
                        std::string* response) {
  if (response) {
    response->clear();
  }
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    std::cerr << "Failed to create socket: " << std::strerror(errno) << "\n";
    return false;
  }

  sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s",
                socket_path.c_str());

  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::cerr << "Failed to connect to " << socket_path << ": "
              << std::strerror(errno) << "\n";
    close(fd);
    return false;
  }

  if (write(fd, payload.data(), payload.size()) < 0) {
    std::cerr << "Failed to send command: " << std::strerror(errno) << "\n";
    close(fd);
    return false;
  }

  char buffer[512];
  ssize_t n = 0;
  while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
    if (response) {
      response->append(buffer, static_cast<size_t>(n));
    }
  }
  close(fd);
  return true;
}

std::optional<int> ReadCdpPortFile(const std::string& user_data_dir) {
  const std::string path = rethread::CdpPortPath(user_data_dir);
  std::ifstream file(path);
  if (!file.is_open()) {
    return std::nullopt;
  }
  int port = 0;
  file >> port;
  if (!file || port <= 0 || port >= 65536) {
    return std::nullopt;
  }
  return port;
}

int ConnectTcp(const std::string& host, int port) {
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;

  struct addrinfo* res = nullptr;
  const std::string port_str = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0) {
    return -1;
  }
  int fd = -1;
  for (struct addrinfo* p = res; p; p = p->ai_next) {
    fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) {
      continue;
    }
    if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
      break;
    }
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  return fd;
}

bool ReadExact(int fd, void* buffer, size_t length) {
  char* out = static_cast<char*>(buffer);
  size_t remaining = length;
  while (remaining > 0) {
    ssize_t n = recv(fd, out, remaining, 0);
    if (n <= 0) {
      return false;
    }
    out += n;
    remaining -= static_cast<size_t>(n);
  }
  return true;
}

bool ReadExactWithBuffer(int fd, void* buffer, size_t length,
                         std::string* prefetch) {
  char* out = static_cast<char*>(buffer);
  size_t remaining = length;
  if (prefetch && !prefetch->empty()) {
    const size_t take = std::min(remaining, prefetch->size());
    std::memcpy(out, prefetch->data(), take);
    prefetch->erase(0, take);
    out += take;
    remaining -= take;
  }
  while (remaining > 0) {
    ssize_t n = recv(fd, out, remaining, 0);
    if (n <= 0) {
      return false;
    }
    out += n;
    remaining -= static_cast<size_t>(n);
  }
  return true;
}

bool SendAll(int fd, const void* data, size_t length) {
  const char* buf = static_cast<const char*>(data);
  size_t remaining = length;
  while (remaining > 0) {
    ssize_t n = send(fd, buf, remaining, 0);
    if (n <= 0) {
      return false;
    }
    buf += n;
    remaining -= static_cast<size_t>(n);
  }
  return true;
}

bool ReadHttpResponse(int fd, std::string* header_out, std::string* body_out) {
  if (header_out) {
    header_out->clear();
  }
  if (body_out) {
    body_out->clear();
  }
  std::string headers;
  std::string remainder;
  if (!ReadHttpHeaders(fd, &headers, &remainder)) {
    return false;
  }
  if (header_out) {
    *header_out = headers;
  }

  size_t content_length = 0;
  bool has_length = false;
  bool chunked = false;
  std::istringstream stream(headers);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const std::string lower = ToLower(line);
    const std::string length_prefix = "content-length:";
    const std::string chunked_prefix = "transfer-encoding:";
    if (lower.rfind(length_prefix, 0) == 0) {
      std::string value = line.substr(length_prefix.size());
      value = TrimWhitespace(value);
      try {
        content_length = static_cast<size_t>(std::stoul(value));
        has_length = true;
      } catch (...) {
      }
    } else if (lower.rfind(chunked_prefix, 0) == 0) {
      if (lower.find("chunked") != std::string::npos) {
        chunked = true;
      }
    }
  }

  std::string body;
  if (chunked) {
    body = remainder;
    while (true) {
      size_t pos = body.find("\r\n");
      while (pos == std::string::npos) {
        char buffer[4096];
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
          break;
        }
        body.append(buffer, static_cast<size_t>(n));
        pos = body.find("\r\n");
      }
      if (pos == std::string::npos) {
        break;
      }
      std::string len_text = body.substr(0, pos);
      size_t chunk_len = 0;
      try {
        chunk_len = static_cast<size_t>(std::stoul(len_text, nullptr, 16));
      } catch (...) {
        break;
      }
      if (body.size() < pos + 2 + chunk_len + 2) {
        char buffer[4096];
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
          break;
        }
        body.append(buffer, static_cast<size_t>(n));
        continue;
      }
      const size_t chunk_start = pos + 2;
      std::string chunk = body.substr(chunk_start, chunk_len);
      std::string rest = body.substr(chunk_start + chunk_len + 2);
      if (chunk_len == 0) {
        body = rest;
        break;
      }
      body = chunk + rest;
    }
  } else if (has_length) {
    body = remainder;
    while (body.size() < content_length) {
      char buffer[4096];
      ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
      if (n <= 0) {
        break;
      }
      body.append(buffer, static_cast<size_t>(n));
    }
    if (body.size() > content_length) {
      body.resize(content_length);
    }
  } else {
    body = remainder;
    char buffer[4096];
    ssize_t n = 0;
    while ((n = recv(fd, buffer, sizeof(buffer), 0)) > 0) {
      body.append(buffer, static_cast<size_t>(n));
    }
  }

  if (body_out) {
    *body_out = std::move(body);
  }
  return true;
}

bool ReadHttpHeaders(int fd, std::string* header_out,
                     std::string* remainder_out) {
  if (header_out) {
    header_out->clear();
  }
  if (remainder_out) {
    remainder_out->clear();
  }
  std::string data;
  char buffer[4096];
  ssize_t n = 0;
  while ((n = recv(fd, buffer, sizeof(buffer), 0)) > 0) {
    data.append(buffer, static_cast<size_t>(n));
    const size_t split = data.find("\r\n\r\n");
    if (split != std::string::npos) {
      if (header_out) {
        header_out->assign(data.data(), split);
      }
      if (remainder_out) {
        remainder_out->assign(data.data() + split + 4,
                               data.size() - split - 4);
      }
      return true;
    }
  }
  return false;
}

bool HttpGetJson(const std::string& host, int port, const std::string& path,
                 QJsonDocument* doc_out, std::string* error_out) {
  if (error_out) {
    error_out->clear();
  }
  int fd = ConnectTcp(host, port);
  if (fd < 0) {
    if (error_out) {
      *error_out = "Failed to connect to CDP port";
    }
    return false;
  }
  std::ostringstream request;
  request << "GET " << path << " HTTP/1.1\r\n"
          << "Host: " << host << ":" << port << "\r\n"
          << "Connection: close\r\n\r\n";
  const std::string payload = request.str();
  if (!SendAll(fd, payload.data(), payload.size())) {
    if (error_out) {
      *error_out = "Failed to send HTTP request";
    }
    close(fd);
    return false;
  }
  std::string headers;
  std::string body;
  if (!ReadHttpResponse(fd, &headers, &body)) {
    if (error_out) {
      *error_out = "Failed to read HTTP response";
    }
    close(fd);
    return false;
  }
  close(fd);

  if (headers.find("200") == std::string::npos) {
    if (error_out) {
      *error_out = "Unexpected HTTP response";
    }
    return false;
  }

  QJsonParseError parse_error;
  QJsonDocument doc = QJsonDocument::fromJson(
      QByteArray::fromStdString(body), &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    if (error_out) {
      *error_out = "Failed to parse JSON response";
    }
    return false;
  }
  if (doc_out) {
    *doc_out = doc;
  }
  return true;
}

std::string Base64Encode(const std::string& data) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  size_t i = 0;
  while (i + 2 < data.size()) {
    const unsigned char a = static_cast<unsigned char>(data[i++]);
    const unsigned char b = static_cast<unsigned char>(data[i++]);
    const unsigned char c = static_cast<unsigned char>(data[i++]);
    out.push_back(kAlphabet[a >> 2]);
    out.push_back(kAlphabet[((a & 0x03) << 4) | (b >> 4)]);
    out.push_back(kAlphabet[((b & 0x0F) << 2) | (c >> 6)]);
    out.push_back(kAlphabet[c & 0x3F]);
  }
  if (i < data.size()) {
    const unsigned char a = static_cast<unsigned char>(data[i++]);
    out.push_back(kAlphabet[a >> 2]);
    if (i < data.size()) {
      const unsigned char b = static_cast<unsigned char>(data[i]);
      out.push_back(kAlphabet[((a & 0x03) << 4) | (b >> 4)]);
      out.push_back(kAlphabet[(b & 0x0F) << 2]);
      out.push_back('=');
    } else {
      out.push_back(kAlphabet[(a & 0x03) << 4]);
      out.push_back('=');
      out.push_back('=');
    }
  }
  return out;
}

bool Base64Decode(const std::string& input, std::string* output) {
  if (!output) {
    return false;
  }
  const QByteArray decoded =
      QByteArray::fromBase64(QByteArray::fromStdString(input));
  output->assign(decoded.constData(),
                 static_cast<size_t>(decoded.size()));
  return true;
}

std::string GenerateWebSocketKey() {
  std::array<unsigned char, 16> data;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(0, 255);
  for (auto& byte : data) {
    byte = static_cast<unsigned char>(dist(gen));
  }
  return Base64Encode(std::string(
      reinterpret_cast<const char*>(data.data()), data.size()));
}

bool WebSocketHandshake(int fd, const std::string& host, int port,
                        const std::string& path, std::string* error_out,
                        std::string* prefetch_out) {
  if (error_out) {
    error_out->clear();
  }
  const std::string key = GenerateWebSocketKey();
  std::ostringstream request;
  request << "GET " << path << " HTTP/1.1\r\n"
          << "Host: " << host << ":" << port << "\r\n"
          << "Upgrade: websocket\r\n"
          << "Connection: Upgrade\r\n"
          << "Sec-WebSocket-Key: " << key << "\r\n"
          << "Sec-WebSocket-Version: 13\r\n\r\n";
  const std::string payload = request.str();
  if (!SendAll(fd, payload.data(), payload.size())) {
    if (error_out) {
      *error_out = "Failed to send WebSocket handshake";
    }
    return false;
  }
  std::string headers;
  std::string remainder;
  if (!ReadHttpHeaders(fd, &headers, &remainder)) {
    if (error_out) {
      *error_out = "Failed to read WebSocket handshake";
    }
    return false;
  }
  if (headers.find("101") == std::string::npos) {
    if (error_out) {
      *error_out = "WebSocket handshake rejected";
    }
    return false;
  }
  if (prefetch_out) {
    *prefetch_out = remainder;
  }
  return true;
}

bool SendWebSocketText(int fd, const std::string& payload) {
  std::vector<unsigned char> frame;
  frame.reserve(payload.size() + 14);
  frame.push_back(0x81);
  const size_t len = payload.size();
  if (len < 126) {
    frame.push_back(static_cast<unsigned char>(0x80 | len));
  } else if (len <= 0xFFFF) {
    frame.push_back(0x80 | 126);
    frame.push_back(static_cast<unsigned char>((len >> 8) & 0xFF));
    frame.push_back(static_cast<unsigned char>(len & 0xFF));
  } else {
    frame.push_back(0x80 | 127);
    for (int i = 7; i >= 0; --i) {
      frame.push_back(static_cast<unsigned char>((len >> (8 * i)) & 0xFF));
    }
  }
  std::array<unsigned char, 4> mask;
  std::random_device rd;
  for (auto& byte : mask) {
    byte = static_cast<unsigned char>(rd());
  }
  frame.insert(frame.end(), mask.begin(), mask.end());
  for (size_t i = 0; i < payload.size(); ++i) {
    frame.push_back(static_cast<unsigned char>(
        payload[i] ^ mask[i % mask.size()]));
  }
  return SendAll(fd, frame.data(), frame.size());
}

bool ReadWebSocketMessage(int fd, std::string* payload_out,
                          unsigned char* opcode_out) {
  if (payload_out) {
    payload_out->clear();
  }
  unsigned char header[2];
  if (!ReadExact(fd, header, sizeof(header))) {
    return false;
  }
  const bool fin = (header[0] & 0x80) != 0;
  const unsigned char opcode = header[0] & 0x0F;
  const bool masked = (header[1] & 0x80) != 0;
  uint64_t length = header[1] & 0x7F;
  if (length == 126) {
    unsigned char ext[2];
    if (!ReadExact(fd, ext, sizeof(ext))) {
      return false;
    }
    length = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
  } else if (length == 127) {
    unsigned char ext[8];
    if (!ReadExact(fd, ext, sizeof(ext))) {
      return false;
    }
    length = 0;
    for (int i = 0; i < 8; ++i) {
      length = (length << 8) | ext[i];
    }
  }
  std::array<unsigned char, 4> mask = {0, 0, 0, 0};
  if (masked) {
    if (!ReadExact(fd, mask.data(), mask.size())) {
      return false;
    }
  }
  if (!fin && opcode == 0x1) {
    return false;
  }
  std::string payload;
  payload.resize(static_cast<size_t>(length));
  if (!payload.empty()) {
    if (!ReadExact(fd, payload.data(), payload.size())) {
      return false;
    }
    if (masked) {
      for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>(
            payload[i] ^ mask[i % mask.size()]);
      }
    }
  }
  if (opcode == 0x9) {
    std::vector<unsigned char> pong;
    pong.push_back(0x8A);
    pong.push_back(0x00);
    SendAll(fd, pong.data(), pong.size());
    return ReadWebSocketMessage(fd, payload_out, opcode_out);
  }
  if (opcode_out) {
    *opcode_out = opcode;
  }
  if (payload_out) {
    *payload_out = std::move(payload);
  }
  return opcode != 0x8;
}

bool ReadWebSocketMessageWithBuffer(int fd, std::string* payload_out,
                                    unsigned char* opcode_out,
                                    std::string* prefetch) {
  if (payload_out) {
    payload_out->clear();
  }
  std::string assembled;
  bool assembling = false;
  while (true) {
    unsigned char header[2];
    if (!ReadExactWithBuffer(fd, header, sizeof(header), prefetch)) {
      return false;
    }
    const bool fin = (header[0] & 0x80) != 0;
    const unsigned char opcode = header[0] & 0x0F;
    const bool masked = (header[1] & 0x80) != 0;
    uint64_t length = header[1] & 0x7F;
    if (length == 126) {
      unsigned char ext[2];
      if (!ReadExactWithBuffer(fd, ext, sizeof(ext), prefetch)) {
        return false;
      }
      length = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (length == 127) {
      unsigned char ext[8];
      if (!ReadExactWithBuffer(fd, ext, sizeof(ext), prefetch)) {
        return false;
      }
      length = 0;
      for (int i = 0; i < 8; ++i) {
        length = (length << 8) | ext[i];
      }
    }
    std::array<unsigned char, 4> mask = {0, 0, 0, 0};
    if (masked) {
      if (!ReadExactWithBuffer(fd, mask.data(), mask.size(), prefetch)) {
        return false;
      }
    }
    std::string payload;
    payload.resize(static_cast<size_t>(length));
    if (!payload.empty()) {
      if (!ReadExactWithBuffer(fd, payload.data(), payload.size(), prefetch)) {
        return false;
      }
      if (masked) {
        for (size_t i = 0; i < payload.size(); ++i) {
          payload[i] = static_cast<char>(
              payload[i] ^ mask[i % mask.size()]);
        }
      }
    }

    if (opcode == 0x8) {
      return false;
    }
    if (opcode == 0x9) {
      std::vector<unsigned char> pong;
      pong.push_back(0x8A);
      pong.push_back(0x00);
      SendAll(fd, pong.data(), pong.size());
      continue;
    }
    if (opcode == 0xA) {
      continue;
    }

    if (opcode == 0x1) {
      if (fin) {
        if (opcode_out) {
          *opcode_out = opcode;
        }
        if (payload_out) {
          *payload_out = std::move(payload);
        }
        return true;
      }
      assembling = true;
      assembled = std::move(payload);
      continue;
    }
    if (opcode == 0x0) {
      if (!assembling) {
        continue;
      }
      assembled.append(payload);
      if (fin) {
        if (opcode_out) {
          *opcode_out = 0x1;
        }
        if (payload_out) {
          *payload_out = std::move(assembled);
        }
        return true;
      }
      continue;
    }
  }
}

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string FormatTimestamp(std::chrono::system_clock::time_point tp) {
  using namespace std::chrono;
  const auto seconds = time_point_cast<std::chrono::seconds>(tp);
  const auto nanos = duration_cast<std::chrono::nanoseconds>(tp - seconds);
  std::time_t tt = system_clock::to_time_t(seconds);
  std::tm tm;
  gmtime_r(&tt, &tm);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &tm);
  std::ostringstream out;
  out << buffer << "." << std::setw(9) << std::setfill('0')
      << nanos.count() << "Z";
  return out.str();
}

std::string SanitizePathFragment(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (unsigned char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('_');
    }
  }
  size_t start = out.find_first_not_of('_');
  if (start == std::string::npos) {
    return "tab";
  }
  size_t end = out.find_last_not_of('_');
  return out.substr(start, end - start + 1);
}

std::string NormalizeUrlFragment(const std::string& raw) {
  std::string clean = TrimWhitespace(raw);
  if (clean.empty()) {
    return "url";
  }
  QUrl url(QString::fromStdString(clean));
  if (url.isValid() && !url.host().isEmpty()) {
    clean = url.host().toStdString() + url.path().toStdString();
  } else {
    const size_t scheme = clean.find("://");
    if (scheme != std::string::npos) {
      clean = clean.substr(scheme + 3);
    }
    const size_t cut = clean.find_first_of("?#");
    if (cut != std::string::npos) {
      clean = clean.substr(0, cut);
    }
  }
  const size_t query = clean.find('?');
  if (query != std::string::npos) {
    clean = clean.substr(0, query);
  }
  const size_t hash = clean.find('#');
  if (hash != std::string::npos) {
    clean = clean.substr(0, hash);
  }
  while (!clean.empty() && clean.front() == '/') {
    clean.erase(clean.begin());
  }
  while (!clean.empty() && clean.back() == '/') {
    clean.pop_back();
  }
  std::replace(clean.begin(), clean.end(), '/', '-');
  clean = ToLower(clean);

  std::string out;
  out.reserve(clean.size());
  for (unsigned char c : clean) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('-');
    }
  }
  size_t start = out.find_first_not_of("-_.");
  if (start == std::string::npos) {
    return "url";
  }
  size_t end = out.find_last_not_of("-_.");
  out = out.substr(start, end - start + 1);
  return out.empty() ? "url" : out;
}

std::string ShortenUrlFragment(const std::string& raw, size_t limit) {
  std::string fragment = NormalizeUrlFragment(raw);
  if (limit == 0 || fragment.size() <= limit) {
    return fragment;
  }
  if (limit <= 6) {
    return fragment.substr(0, limit);
  }
  const size_t head = (limit - 3) / 2;
  const size_t tail = limit - 3 - head;
  return fragment.substr(0, head) + "..." +
         fragment.substr(fragment.size() - tail);
}

bool NetworkLogDebugEnabled() {
  const char* value = std::getenv("RETHREAD_NETWORK_LOG_DEBUG");
  return value && value[0] != '\0';
}

void NetworkLogDebug(const std::string& message) {
  if (!NetworkLogDebugEnabled()) {
    return;
  }
  std::cerr << "[network-log] " << message << "\\n";
}

volatile sig_atomic_t g_stop_requested = 0;

void HandleStopSignal(int) {
  g_stop_requested = 1;
}

struct NetworkFilters {
  std::optional<std::regex> url;
  std::optional<std::regex> method;
  std::optional<std::regex> status;
  std::optional<std::regex> mime;

  bool Match(const std::string& url_value,
             const std::string& method_value,
             const std::string& status_value,
             const std::string& mime_value) const {
    if (url && !std::regex_search(url_value, *url)) {
      return false;
    }
    if (method && !std::regex_search(method_value, *method)) {
      return false;
    }
    if (status && !std::regex_search(status_value, *status)) {
      return false;
    }
    if (mime && !std::regex_search(mime_value, *mime)) {
      return false;
    }
    return true;
  }
};

bool BuildNetworkFilters(const std::string& url_pattern,
                         const std::string& method_pattern,
                         const std::string& status_pattern,
                         const std::string& mime_pattern,
                         NetworkFilters* out,
                         std::string* error) {
  if (!out) {
    return false;
  }
  if (error) {
    error->clear();
  }
  try {
    if (!url_pattern.empty()) {
      out->url = std::regex(url_pattern);
    }
    if (!method_pattern.empty()) {
      out->method = std::regex(method_pattern);
    }
    if (!status_pattern.empty()) {
      out->status = std::regex(status_pattern);
    }
    if (!mime_pattern.empty()) {
      out->mime = std::regex(mime_pattern);
    }
  } catch (const std::regex_error& err) {
    if (error) {
      *error = err.what();
    }
    return false;
  }
  return true;
}

std::map<std::string, std::string> NormalizeHeaderList(
    const QJsonArray& headers) {
  std::map<std::string, std::string> result;
  for (const QJsonValue& entry : headers) {
    const QJsonObject obj = entry.toObject();
    const QString name = obj.value(QStringLiteral("name")).toString();
    const QString value = obj.value(QStringLiteral("value")).toString();
    if (!name.isEmpty()) {
      result[ToLower(name.toStdString())] = value.toStdString();
    }
  }
  return result;
}

std::map<std::string, std::string> NormalizeHeaderMap(
    const QJsonObject& headers) {
  std::map<std::string, std::string> result;
  for (auto it = headers.begin(); it != headers.end(); ++it) {
    if (it.key().isEmpty()) {
      continue;
    }
    const QString value = it.value().toVariant().toString();
    result[it.key().toStdString()] = value.toStdString();
  }
  return result;
}

bool WriteJsonFile(const std::string& path, const QJsonObject& payload) {
  QJsonDocument doc(payload);
  QByteArray data = doc.toJson(QJsonDocument::Indented);
  if (data.isEmpty() || data.back() != '\n') {
    data.append('\n');
  }
  std::ofstream out(path, std::ios::binary);
  if (!out.is_open()) {
    return false;
  }
  out.write(data.constData(), data.size());
  return out.good();
}

bool WriteJsonFile(const std::string& path,
                   const std::map<std::string, std::string>& payload) {
  QJsonObject obj;
  for (const auto& [key, value] : payload) {
    obj.insert(QString::fromStdString(key),
               QString::fromStdString(value));
  }
  return WriteJsonFile(path, obj);
}

bool WriteResponseBodyJson(const std::string& path,
                           const std::string& body) {
  QJsonParseError parse_error;
  QJsonDocument doc =
      QJsonDocument::fromJson(QByteArray::fromStdString(body), &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    return true;
  }
  QByteArray data = doc.toJson(QJsonDocument::Indented);
  if (data.isEmpty() || data.back() != '\n') {
    data.append('\n');
  }
  std::ofstream out(path, std::ios::binary);
  if (!out.is_open()) {
    return false;
  }
  out.write(data.constData(), data.size());
  return out.good();
}

std::string FormatCaptureDirName(
    const std::chrono::system_clock::time_point& timestamp,
    const std::string& method,
    const std::string& url) {
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      timestamp.time_since_epoch()).count();
  std::string verb = TrimWhitespace(method);
  if (verb.empty()) {
    verb = "REQ";
  } else {
    verb = ToLower(verb);
    std::transform(verb.begin(), verb.end(), verb.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  }
  const std::string url_fragment = ShortenUrlFragment(url, 96);
  std::ostringstream out;
  out << ms << "-" << verb << "-" << url_fragment;
  return out.str();
}

bool WriteNetworkCapture(const std::string& base_dir,
                         const std::chrono::system_clock::time_point& timestamp,
                         const std::string& request_id,
                         const std::string& url,
                         const std::string& method,
                         const std::string& stage,
                         const std::string& status,
                         const std::string& content_type,
                         const std::map<std::string, std::string>& request_headers,
                         const std::map<std::string, std::string>& response_headers,
                         const std::string& request_body,
                         const std::string& response_body,
                         const std::string& response_body_error) {
  const std::string dir_name =
      FormatCaptureDirName(timestamp, method, url);
  const std::filesystem::path capture_dir =
      std::filesystem::path(base_dir) / dir_name;
  std::error_code ec;
  std::filesystem::create_directories(capture_dir, ec);
  if (ec) {
    return false;
  }

  QJsonObject metadata;
  metadata.insert(QStringLiteral("timestamp"),
                  QString::fromStdString(FormatTimestamp(timestamp)));
  metadata.insert(QStringLiteral("requestId"),
                  QString::fromStdString(request_id));
  metadata.insert(QStringLiteral("url"), QString::fromStdString(url));
  metadata.insert(QStringLiteral("method"),
                  QString::fromStdString(method));
  metadata.insert(QStringLiteral("stage"),
                  QString::fromStdString(stage));
  metadata.insert(QStringLiteral("status"),
                  QString::fromStdString(status));
  if (!content_type.empty()) {
    metadata.insert(QStringLiteral("contentType"),
                    QString::fromStdString(content_type));
  }
  if (!response_body_error.empty()) {
    metadata.insert(QStringLiteral("responseBodyError"),
                    QString::fromStdString(response_body_error));
  }

  const std::string metadata_path =
      (capture_dir / "metadata.json").string();
  if (!WriteJsonFile(metadata_path, metadata)) {
    return false;
  }

  if (!WriteJsonFile((capture_dir / "request-headers.json").string(),
                     request_headers)) {
    return false;
  }
  if (!WriteJsonFile((capture_dir / "response-headers.json").string(),
                     response_headers)) {
    return false;
  }

  if (!request_body.empty()) {
    std::ofstream out((capture_dir / "request-body.bin").string(),
                      std::ios::binary);
    if (!out.is_open()) {
      return false;
    }
    out.write(request_body.data(), request_body.size());
  }
  if (!response_body.empty()) {
    std::ofstream out((capture_dir / "response-body.bin").string(),
                      std::ios::binary);
    if (!out.is_open()) {
      return false;
    }
    out.write(response_body.data(), response_body.size());
    if (!WriteResponseBodyJson(
            (capture_dir / "response-body.json").string(), response_body)) {
      return false;
    }
  }
  return true;
}

bool SendCdpRequest(int fd,
                    int* next_id,
                    const QString& method,
                    const QJsonObject& params,
                    int* request_id_out) {
  if (!next_id) {
    return false;
  }
  const int request_id = (*next_id)++;
  QJsonObject payload;
  payload.insert(QStringLiteral("id"), request_id);
  payload.insert(QStringLiteral("method"), method);
  if (!params.isEmpty()) {
    payload.insert(QStringLiteral("params"), params);
  }
  QJsonDocument doc(payload);
  const std::string text = doc.toJson(QJsonDocument::Compact).toStdString();
  if (!SendWebSocketText(fd, text)) {
    return false;
  }
  if (request_id_out) {
    *request_id_out = request_id;
  }
  return true;
}

bool ReadCdpMessage(int fd, QJsonObject* message_out,
                    std::string* prefetch) {
  std::string payload;
  unsigned char opcode = 0;
  if (!ReadWebSocketMessageWithBuffer(fd, &payload, &opcode, prefetch)) {
    return false;
  }
  if (opcode != 0x1) {
    return true;
  }
  QJsonParseError parse_error;
  QJsonDocument doc = QJsonDocument::fromJson(
      QByteArray::fromStdString(payload), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
    return true;
  }
  if (message_out) {
    *message_out = doc.object();
  }
  return true;
}

bool WaitForCdpResponse(int fd,
                        int target_id,
                        QJsonObject* response_out,
                        std::deque<QJsonObject>* pending_events,
                        std::string* prefetch) {
  while (!g_stop_requested) {
    QJsonObject message;
    if (!ReadCdpMessage(fd, &message, prefetch)) {
      return false;
    }
    if (message.isEmpty()) {
      continue;
    }
    if (message.contains(QStringLiteral("id"))) {
      const int id = message.value(QStringLiteral("id")).toInt();
      if (id == target_id) {
        if (response_out) {
          *response_out = message;
        }
        return true;
      }
      continue;
    }
    if (pending_events) {
      pending_events->push_back(message);
    }
  }
  return false;
}

}  // namespace

std::string TabSocketPath(const std::string& user_data_dir) {
  if (user_data_dir.empty()) {
    return std::string("tabs.sock");
  }
  return user_data_dir + "/tabs.sock";
}

int RunTabCli(int argc, char* argv[], const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir, &index)) {
    return 1;
  }
  if (index < argc) {
    std::string maybe_help = argv[index];
    if (maybe_help == "--help" || maybe_help == "-h") {
      PrintTabUsage();
      return 0;
    }
  }
  if (index >= argc) {
    PrintTabUsage();
    return 1;
  }

  std::string cmd = argv[index++];
  std::ostringstream payload;

  if (cmd == "get" || cmd == "list") {
    payload << "list\n";
  } else if (cmd == "switch") {
    if (index >= argc) {
      std::cerr << "switch requires a tab id\n";
      return 1;
    }
    payload << "switch " << argv[index++] << "\n";
  } else if (cmd == "cycle") {
    if (index >= argc) {
      std::cerr << "cycle requires a delta\n";
      return 1;
    }
    payload << "cycle " << argv[index++] << "\n";
  } else if (cmd == "swap") {
    if (index >= argc) {
      std::cerr << "swap requires at least one index or offset\n";
      return 1;
    }
    payload << "swap";
    for (int i = index; i < argc; ++i) {
      payload << " " << argv[i];
    }
    payload << "\n";
  } else if (cmd == "open") {
    bool open_at_end = false;
    while (index < argc) {
      std::string arg = argv[index];
      if (arg == "--at-end") {
        open_at_end = true;
        ++index;
        continue;
      }
      if (arg == "--") {
        ++index;
        break;
      }
      break;
    }
    if (index >= argc) {
      std::cerr << "open requires a URL\n";
      return 1;
    }
    std::ostringstream url_stream;
    for (int i = index; i < argc; ++i) {
      if (i > index) {
        url_stream << " ";
      }
      url_stream << argv[i];
    }
    const std::string url_text = url_stream.str();
    if (url_text.empty()) {
      std::cerr << "open requires a URL\n";
      return 1;
    }
    payload << "open";
    if (open_at_end) {
      payload << " --at-end";
    }
    payload << " -- " << url_text << "\n";
  } else if (cmd == "history-back") {
    payload << "history-back\n";
  } else if (cmd == "history-forward") {
    payload << "history-forward\n";
  } else if (cmd == "close") {
    payload << "close";
    if (index < argc) {
      payload << " " << argv[index++];
      if (index < argc) {
        std::cerr << "close accepts at most one tab index\n";
        return 1;
      }
    }
    payload << "\n";
  } else {
    std::cerr << "Unknown tabs command: " << cmd << "\n";
    PrintTabUsage();
    return 1;
  }

  const std::string socket_path = TabSocketPath(user_data_dir);
  if (!SendCommand(socket_path, payload.str())) {
    return 1;
  }
  return 0;
}

int RunBindCli(int argc,
               char* argv[],
               const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir, &index)) {
    return 1;
  }
  if (index < argc) {
    std::string maybe_help = argv[index];
    if (maybe_help == "--help" || maybe_help == "-h") {
      PrintBindUsage();
      return 0;
    }
  }

  BindingOptions options;
  if (!ParseBindingOptions(argc, argv, &index, &options, true, nullptr)) {
    PrintBindUsage();
    return 1;
  }

  if (options.context_menu) {
    if (!options.key.empty() || !options.consume || options.alt ||
        options.ctrl || options.shift || options.command) {
      std::cerr << "--context-menu cannot be combined with key or modifier flags\n";
      PrintBindUsage();
      return 1;
    }
  } else {
    if (options.key.empty()) {
      std::cerr << "bind requires --key\n";
      PrintBindUsage();
      return 1;
    }
  }
  if (index >= argc) {
    std::cerr << "bind requires a command\n";
    PrintBindUsage();
    return 1;
  }

  std::ostringstream command_stream;
  for (int i = index; i < argc; ++i) {
    if (i > index) {
      command_stream << " ";
    }
    command_stream << argv[i];
  }

  std::ostringstream payload;
  payload << "bind";
  if (options.context_menu) {
    payload << " --context-menu";
  } else {
    if (options.alt) {
      payload << " --alt";
    }
    if (options.ctrl) {
      payload << " --ctrl";
    }
    if (options.shift) {
      payload << " --shift";
    }
    if (options.command) {
      payload << " --command";
    }
    if (!options.consume) {
      payload << " --no-consume";
    }
    payload << " --key=" << options.key;
  }
  payload << " -- " << command_stream.str() << "\n";

  if (!SendCommand(TabSocketPath(user_data_dir), payload.str())) {
    return 1;
  }
  return 0;
}

int RunUnbindCli(int argc,
                 char* argv[],
                 const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir, &index)) {
    return 1;
  }
  if (index < argc) {
    std::string maybe_help = argv[index];
    if (maybe_help == "--help" || maybe_help == "-h") {
      PrintUnbindUsage();
      return 0;
    }
  }

  BindingOptions options;
  bool saw_separator = false;
  if (!ParseBindingOptions(argc, argv, &index, &options, false,
                           &saw_separator)) {
    PrintUnbindUsage();
    return 1;
  }
  if (saw_separator) {
    std::cerr << "unbind does not accept a command\n";
    PrintUnbindUsage();
    return 1;
  }
  if (options.context_menu) {
    if (!options.key.empty() || options.alt || options.ctrl ||
        options.shift || options.command) {
      std::cerr << "--context-menu cannot be combined with key or modifier flags\n";
      PrintUnbindUsage();
      return 1;
    }
  } else {
    if (options.key.empty()) {
      std::cerr << "unbind requires --key\n";
      PrintUnbindUsage();
      return 1;
    }
  }
  if (index < argc) {
    std::cerr << "unbind does not accept extra arguments\n";
    PrintUnbindUsage();
    return 1;
  }

  std::ostringstream payload;
  payload << "unbind";
  if (options.context_menu) {
    payload << " --context-menu";
  } else {
    if (options.alt) {
      payload << " --alt";
    }
    if (options.ctrl) {
      payload << " --ctrl";
    }
    if (options.shift) {
      payload << " --shift";
    }
    if (options.command) {
      payload << " --command";
    }
    payload << " --key=" << options.key;
  }
  payload << "\n";

  if (!SendCommand(TabSocketPath(user_data_dir), payload.str())) {
    return 1;
  }
  return 0;
}

int RunEvalCli(int argc,
               char* argv[],
               const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir, &index)) {
    return 1;
  }

  bool use_stdin = false;
  int tab_id = 0;
  int tab_index = 0;
  while (index < argc) {
    std::string arg = argv[index];
    if (arg == "--help" || arg == "-h") {
      PrintEvalUsage();
      return 0;
    }
    if (arg == "--stdin") {
      use_stdin = true;
      ++index;
      continue;
    }
    if (arg == "--tab-id") {
      if (index + 1 >= argc) {
        std::cerr << "--tab-id requires a value\n";
        return 1;
      }
      if (!ParsePositiveInt(argv[index + 1], &tab_id)) {
        std::cerr << "Invalid --tab-id value\n";
        return 1;
      }
      index += 2;
      continue;
    }
    const std::string tab_id_prefix = "--tab-id=";
    if (arg.rfind(tab_id_prefix, 0) == 0) {
      if (!ParsePositiveInt(arg.substr(tab_id_prefix.size()), &tab_id)) {
        std::cerr << "Invalid --tab-id value\n";
        return 1;
      }
      ++index;
      continue;
    }
    if (arg == "--tab-index") {
      if (index + 1 >= argc) {
        std::cerr << "--tab-index requires a value\n";
        return 1;
      }
      if (!ParsePositiveInt(argv[index + 1], &tab_index)) {
        std::cerr << "Invalid --tab-index value\n";
        return 1;
      }
      index += 2;
      continue;
    }
    const std::string tab_index_prefix = "--tab-index=";
    if (arg.rfind(tab_index_prefix, 0) == 0) {
      if (!ParsePositiveInt(arg.substr(tab_index_prefix.size()), &tab_index)) {
        std::cerr << "Invalid --tab-index value\n";
        return 1;
      }
      ++index;
      continue;
    }
    if (arg == "--") {
      ++index;
      break;
    }
    break;
  }

  if (tab_id > 0 && tab_index > 0) {
    std::cerr << "Specify at most one tab selector (--tab-id or --tab-index)\n";
    return 1;
  }

  std::string script;
  if (use_stdin) {
    if (index < argc) {
      std::cerr << "--stdin cannot be combined with a script argument\n";
      return 1;
    }
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    script = buffer.str();
  } else {
    if (index >= argc) {
      std::cerr << "eval requires a script argument\n";
      PrintEvalUsage();
      return 1;
    }
    std::ostringstream buffer;
    for (int i = index; i < argc; ++i) {
      if (i > index) {
        buffer << " ";
      }
      buffer << argv[i];
    }
    script = buffer.str();
  }

  if (script.empty()) {
    std::cerr << "eval requires a non-empty script\n";
    return 1;
  }

  const std::string encoded = HexEncode(script);
  std::ostringstream payload;
  payload << "eval";
  if (tab_id > 0) {
    payload << " --tab-id=" << tab_id;
  }
  if (tab_index > 0) {
    payload << " --tab-index=" << tab_index;
  }
  payload << " --code=" << encoded << "\n";

  if (!SendCommand(TabSocketPath(user_data_dir), payload.str())) {
    return 1;
  }
  return 0;
}

int RunTabStripCli(int argc,
                   char* argv[],
                   const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir, &index)) {
    return 1;
  }
  if (index >= argc) {
    PrintTabStripUsage();
    return 1;
  }
  if (index < argc) {
    std::string maybe_help = argv[index];
    if (maybe_help == "--help" || maybe_help == "-h") {
      PrintTabStripUsage();
      return 0;
    }
  }

  std::string action = argv[index++];
  std::ostringstream payload;
  if (action == "show" || action == "hide" || action == "toggle") {
    if (index < argc) {
      std::cerr << "tabstrip " << action << " does not take extra arguments\n";
      PrintTabStripUsage();
      return 1;
    }
    payload << "tabstrip " << action << "\n";
  } else if (action == "peek") {
    if (index >= argc) {
      std::cerr << "tabstrip peek requires a duration in ms\n";
      PrintTabStripUsage();
      return 1;
    }
    payload << "tabstrip peek " << argv[index++] << "\n";
    if (index < argc) {
      std::cerr << "tabstrip peek only accepts a single duration\n";
      PrintTabStripUsage();
      return 1;
    }
  } else if (action == "message") {
    bool use_stdin = false;
    int duration_ms = -1;
    while (index < argc) {
      std::string arg = argv[index];
      if (arg == "--stdin") {
        use_stdin = true;
        ++index;
        continue;
      }
      if (arg == "--duration") {
        if (index + 1 >= argc) {
          std::cerr << "--duration requires a value\n";
          return 1;
        }
        std::string value = argv[index + 1];
        int parsed = 0;
        if (value != "0" && !ParsePositiveInt(value, &parsed)) {
          std::cerr << "Invalid --duration value\n";
          return 1;
        }
        duration_ms = (value == "0") ? 0 : parsed;
        index += 2;
        continue;
      }
      const std::string duration_prefix = "--duration=";
      if (arg.rfind(duration_prefix, 0) == 0) {
        std::string value = arg.substr(duration_prefix.size());
        int parsed = 0;
        if (value != "0" && !ParsePositiveInt(value, &parsed)) {
          std::cerr << "Invalid --duration value\n";
          return 1;
        }
        duration_ms = (value == "0") ? 0 : parsed;
        ++index;
        continue;
      }
      if (arg == "--") {
        ++index;
        break;
      }
      break;
    }
    if (duration_ms < 0) {
      std::cerr << "tabstrip message requires --duration\n";
      PrintTabStripUsage();
      return 1;
    }
    std::string message;
    if (use_stdin) {
      if (index < argc) {
        std::cerr << "--stdin cannot be combined with inline text\n";
        return 1;
      }
      std::ostringstream buffer;
      buffer << std::cin.rdbuf();
      message = buffer.str();
    } else {
      if (index >= argc) {
        std::cerr << "tabstrip message requires text after --duration\n";
        PrintTabStripUsage();
        return 1;
      }
      std::ostringstream buffer;
      for (int i = index; i < argc; ++i) {
        if (i > index) {
          buffer << " ";
        }
        buffer << argv[i];
      }
      message = buffer.str();
    }
    if (message.empty()) {
      std::cerr << "tabstrip message requires non-empty text\n";
      return 1;
    }
    payload << "tabstrip message --duration=" << duration_ms
            << " --data=" << HexEncode(message) << "\n";
  } else {
    std::cerr << "Unknown tabstrip action: " << action << "\n";
    PrintTabStripUsage();
    return 1;
  }

  if (!SendCommand(TabSocketPath(user_data_dir), payload.str())) {
    return 1;
  }
  return 0;
}

int RunRulesCli(int argc,
                char* argv[],
                const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir,
                        &index)) {
    return 1;
  }
  if (index < argc) {
    std::string maybe_help = argv[index];
    if (maybe_help == "--help" || maybe_help == "-h") {
      PrintRulesUsage();
      return 0;
    }
  }
  if (index >= argc) {
    PrintRulesUsage();
    return 1;
  }
  std::string action = argv[index++];
  if (action != "js" && action != "iframes") {
    std::cerr << "Unknown rules target: " << action << "\n";
    PrintRulesUsage();
    return 1;
  }

  bool whitelist = false;
  bool blacklist = false;
  bool append = false;
  while (index < argc) {
    std::string arg = argv[index];
    if (arg == "--whitelist") {
      whitelist = true;
      ++index;
      continue;
    }
    if (arg == "--blacklist") {
      blacklist = true;
      ++index;
      continue;
    }
    if (arg == "--append") {
      append = true;
      ++index;
      continue;
    }
    if (arg == "--help" || arg == "-h") {
      PrintRulesUsage();
      return 0;
    }
    std::cerr << "Unknown rules flag: " << arg << "\n";
    PrintRulesUsage();
    return 1;
  }
  if (whitelist == blacklist) {
    std::cerr << "Specify exactly one of --whitelist or --blacklist\n";
    PrintRulesUsage();
    return 1;
  }

  std::ostringstream buffer;
  buffer << std::cin.rdbuf();
  const std::string data = buffer.str();
  if (data.empty()) {
    std::cerr << "rules requires host data via stdin\n";
    return 1;
  }
  const std::string encoded = HexEncode(data);
  std::ostringstream payload;
  payload << "rules " << action << " --mode="
          << (whitelist ? "whitelist" : "blacklist") << " --data=" << encoded
          << (append ? " --append" : "") << "\n";
  if (!SendCommand(TabSocketPath(user_data_dir), payload.str())) {
    return 1;
  }
  return 0;
}

int RunScriptsCli(int argc,
                  char* argv[],
                  const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir,
                        &index)) {
    return 1;
  }
  if (index < argc) {
    std::string maybe_help = argv[index];
    if (maybe_help == "--help" || maybe_help == "-h") {
      PrintScriptsUsage();
      return 0;
    }
  }
  if (index >= argc) {
    PrintScriptsUsage();
    return 1;
  }
  std::string action = argv[index++];

  std::ostringstream payload;
  if (action == "list") {
    if (index < argc) {
      std::cerr << "scripts list does not take extra arguments\n";
      PrintScriptsUsage();
      return 1;
    }
    payload << "scripts list\n";
  } else if (action == "rm") {
    std::string id;
    while (index < argc) {
      std::string arg = argv[index];
      if (arg == "--id") {
        if (index + 1 >= argc) {
          std::cerr << "--id requires a value\n";
          return 1;
        }
        id = argv[index + 1];
        index += 2;
        continue;
      }
      const std::string id_prefix = "--id=";
      if (arg.rfind(id_prefix, 0) == 0) {
        id = arg.substr(id_prefix.size());
        ++index;
        continue;
      }
      if (arg == "--help" || arg == "-h") {
        PrintScriptsUsage();
        return 0;
      }
      std::cerr << "Unknown scripts rm flag: " << arg << "\n";
      PrintScriptsUsage();
      return 1;
    }
    if (!IsValidScriptId(id)) {
      std::cerr << "scripts rm requires a valid --id\n";
      return 1;
    }
    payload << "scripts rm --id=" << id << "\n";
  } else if (action == "add") {
    std::string id;
    std::string match;
    std::string run_at;
    bool stylesheet = false;
    while (index < argc) {
      std::string arg = argv[index];
      if (arg == "--stylesheet") {
        stylesheet = true;
        ++index;
        continue;
      }
      if (arg == "--id") {
        if (index + 1 >= argc) {
          std::cerr << "--id requires a value\n";
          return 1;
        }
        id = argv[index + 1];
        index += 2;
        continue;
      }
      const std::string id_prefix = "--id=";
      if (arg.rfind(id_prefix, 0) == 0) {
        id = arg.substr(id_prefix.size());
        ++index;
        continue;
      }
      if (arg == "--match") {
        if (index + 1 >= argc) {
          std::cerr << "--match requires a value\n";
          return 1;
        }
        match = argv[index + 1];
        index += 2;
        continue;
      }
      const std::string match_prefix = "--match=";
      if (arg.rfind(match_prefix, 0) == 0) {
        match = arg.substr(match_prefix.size());
        ++index;
        continue;
      }
      if (arg == "--run-at") {
        if (index + 1 >= argc) {
          std::cerr << "--run-at requires a value\n";
          return 1;
        }
        run_at = argv[index + 1];
        index += 2;
        continue;
      }
      const std::string run_prefix = "--run-at=";
      if (arg.rfind(run_prefix, 0) == 0) {
        run_at = arg.substr(run_prefix.size());
        ++index;
        continue;
      }
      if (arg == "--help" || arg == "-h") {
        PrintScriptsUsage();
        return 0;
      }
      std::cerr << "Unknown scripts add flag: " << arg << "\n";
      PrintScriptsUsage();
      return 1;
    }
    if (!IsValidScriptId(id)) {
      std::cerr << "scripts add requires a valid --id\n";
      return 1;
    }
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    const std::string script = buffer.str();
    if (script.empty()) {
      std::cerr << "scripts add requires script data via stdin\n";
      return 1;
    }
    payload << "scripts add"
            << " --id=" << id;
    if (stylesheet) {
      payload << " --stylesheet";
    }
    if (!match.empty()) {
      payload << " --match=" << match;
    }
    if (!run_at.empty()) {
      payload << " --run-at=" << run_at;
    }
    payload << " --code=" << HexEncode(script) << "\n";
  } else {
    std::cerr << "Unknown scripts command: " << action << "\n";
    PrintScriptsUsage();
    return 1;
  }

  if (!SendCommand(TabSocketPath(user_data_dir), payload.str())) {
    return 1;
  }
  return 0;
}

int RunDevToolsCli(int argc,
                   char* argv[],
                   const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir,
                        &index)) {
    return 1;
  }
  if (index < argc) {
    std::string maybe_help = argv[index];
    if (maybe_help == "--help" || maybe_help == "-h") {
      PrintDevToolsUsage();
      return 0;
    }
  }
  if (index >= argc) {
    PrintDevToolsUsage();
    return 1;
  }
  std::string action = argv[index++];
  if (action != "open") {
    std::cerr << "Unknown devtools action: " << action << "\n";
    PrintDevToolsUsage();
    return 1;
  }
  if (index < argc) {
    std::cerr << "devtools open does not take additional arguments\n";
    PrintDevToolsUsage();
    return 1;
  }
  if (!SendCommand(TabSocketPath(user_data_dir), "devtools open\n")) {
    return 1;
  }
  return 0;
}

int RunNetworkLogCli(int argc,
                     char* argv[],
                     const std::string& default_user_data_dir) {
  std::string user_data_dir;
  int index = 0;
  if (!ParseUserDataDir(argc, argv, default_user_data_dir, &user_data_dir,
                        &index)) {
    return 1;
  }
  if (index < argc) {
    std::string maybe_help = argv[index];
    if (maybe_help == "--help" || maybe_help == "-h") {
      PrintNetworkLogUsage();
      return 0;
    }
  }

  int tab_id = 0;
  std::string output_dir;
  std::string url_pattern;
  std::string method_pattern;
  std::string status_pattern;
  std::string mime_pattern;
  int cdp_port = 0;
  bool cdp_port_set = false;

  for (; index < argc; ++index) {
    std::string arg = argv[index];
    if (arg == "--help" || arg == "-h") {
      PrintNetworkLogUsage();
      return 0;
    }
    const std::string id_prefix = "--id=";
    if (arg.rfind(id_prefix, 0) == 0) {
      if (!ParsePositiveInt(arg.substr(id_prefix.size()), &tab_id)) {
        std::cerr << "Invalid --id value\n";
        return 1;
      }
      continue;
    }
    if (arg == "--id") {
      if (index + 1 >= argc ||
          !ParsePositiveInt(argv[index + 1], &tab_id)) {
        std::cerr << "--id requires a positive tab id\n";
        return 1;
      }
      ++index;
      continue;
    }
    const std::string dir_prefix = "--dir=";
    if (arg.rfind(dir_prefix, 0) == 0) {
      output_dir = arg.substr(dir_prefix.size());
      continue;
    }
    if (arg == "--dir") {
      if (index + 1 >= argc) {
        std::cerr << "--dir requires a value\n";
        return 1;
      }
      output_dir = argv[++index];
      continue;
    }
    const std::string url_prefix = "--url=";
    if (arg.rfind(url_prefix, 0) == 0) {
      url_pattern = arg.substr(url_prefix.size());
      continue;
    }
    if (arg == "--url") {
      if (index + 1 >= argc) {
        std::cerr << "--url requires a value\n";
        return 1;
      }
      url_pattern = argv[++index];
      continue;
    }
    const std::string method_prefix = "--method=";
    if (arg.rfind(method_prefix, 0) == 0) {
      method_pattern = arg.substr(method_prefix.size());
      continue;
    }
    if (arg == "--method") {
      if (index + 1 >= argc) {
        std::cerr << "--method requires a value\n";
        return 1;
      }
      method_pattern = argv[++index];
      continue;
    }
    const std::string status_prefix = "--status=";
    if (arg.rfind(status_prefix, 0) == 0) {
      status_pattern = arg.substr(status_prefix.size());
      continue;
    }
    if (arg == "--status") {
      if (index + 1 >= argc) {
        std::cerr << "--status requires a value\n";
        return 1;
      }
      status_pattern = argv[++index];
      continue;
    }
    const std::string mime_prefix = "--mime=";
    if (arg.rfind(mime_prefix, 0) == 0) {
      mime_pattern = arg.substr(mime_prefix.size());
      continue;
    }
    if (arg == "--mime") {
      if (index + 1 >= argc) {
        std::cerr << "--mime requires a value\n";
        return 1;
      }
      mime_pattern = argv[++index];
      continue;
    }
    const std::string cdp_port_prefix = "--cdp-port=";
    if (arg.rfind(cdp_port_prefix, 0) == 0) {
      if (!ParsePositiveInt(arg.substr(cdp_port_prefix.size()), &cdp_port)) {
        std::cerr << "Invalid --cdp-port value\n";
        return 1;
      }
      if (cdp_port >= 65536) {
        std::cerr << "--cdp-port out of range\n";
        return 1;
      }
      cdp_port_set = true;
      continue;
    }
    if (arg == "--cdp-port") {
      if (index + 1 >= argc ||
          !ParsePositiveInt(argv[index + 1], &cdp_port)) {
        std::cerr << "--cdp-port requires a positive value\n";
        return 1;
      }
      if (cdp_port >= 65536) {
        std::cerr << "--cdp-port out of range\n";
        return 1;
      }
      ++index;
      cdp_port_set = true;
      continue;
    }
    std::cerr << "Unknown network-log option: " << arg << "\n";
    PrintNetworkLogUsage();
    return 1;
  }

  if (tab_id <= 0) {
    std::cerr << "network-log requires --id\n";
    PrintNetworkLogUsage();
    return 1;
  }

  if (output_dir.empty()) {
    output_dir = "rethread-tab-" + std::to_string(tab_id) + "-network-log";
  }

  NetworkFilters filters;
  std::string filter_error;
  if (!BuildNetworkFilters(url_pattern, method_pattern, status_pattern,
                           mime_pattern, &filters, &filter_error)) {
    std::cerr << "Invalid filter regex: " << filter_error << "\n";
    return 1;
  }

  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  if (ec) {
    std::cerr << "Failed to create output directory: " << output_dir << "\n";
    return 1;
  }
  std::cout << "Logging to " << output_dir << "\n";

  const std::string socket_path = TabSocketPath(user_data_dir);
  std::ostringstream payload;
  payload << "devtools-id " << tab_id << "\n";
  std::string response;
  if (!SendCommandCapture(socket_path, payload.str(), &response)) {
    return 1;
  }
  response = TrimWhitespace(response);
  if (response.rfind("ERR", 0) == 0) {
    std::cerr << response << "\n";
    return 1;
  }
  const std::string devtools_id = response;
  if (devtools_id.empty()) {
    std::cerr << "Failed to resolve devtools id\n";
    return 1;
  }

  if (!cdp_port_set) {
    const auto port_from_file = ReadCdpPortFile(user_data_dir);
    if (port_from_file) {
      cdp_port = *port_from_file;
    }
  }
  if (cdp_port <= 0) {
    cdp_port = 9222;
  }

  QJsonDocument target_doc;
  std::string http_error;
  NetworkLogDebug("fetching targets");
  if (!HttpGetJson("127.0.0.1", cdp_port, "/json/list", &target_doc,
                   &http_error)) {
    std::cerr << "Failed to fetch CDP targets: " << http_error << "\n";
    return 1;
  }
  if (!target_doc.isArray()) {
    std::cerr << "Unexpected CDP target response\n";
    return 1;
  }
  QString ws_url;
  const QJsonArray targets = target_doc.array();
  for (const QJsonValue& entry : targets) {
    const QJsonObject obj = entry.toObject();
    if (obj.value(QStringLiteral("id")).toString().toStdString() ==
        devtools_id) {
      ws_url = obj.value(QStringLiteral("webSocketDebuggerUrl")).toString();
      break;
    }
  }
  if (ws_url.isEmpty()) {
    std::cerr << "CDP target not found for tab id " << tab_id << "\n";
    return 1;
  }

  QUrl url(ws_url);
  if (!url.isValid() || url.scheme() != QStringLiteral("ws")) {
    std::cerr << "Invalid WebSocket URL: " << ws_url.toStdString() << "\n";
    return 1;
  }
  const std::string host = url.host().toStdString();
  const int port = url.port(cdp_port);
  std::string path = url.path().toStdString();
  if (!url.query().isEmpty()) {
    path += "?" + url.query().toStdString();
  }

  int fd = ConnectTcp(host.empty() ? "127.0.0.1" : host, port);
  if (fd < 0) {
    std::cerr << "Failed to connect to CDP WebSocket\n";
    return 1;
  }
  NetworkLogDebug("connected websocket");
  std::string ws_error;
  std::string ws_prefetch;
  if (!WebSocketHandshake(fd, host.empty() ? "127.0.0.1" : host, port, path,
                          &ws_error, &ws_prefetch)) {
    std::cerr << "WebSocket handshake failed: " << ws_error << "\n";
    close(fd);
    return 1;
  }

  int next_id = 1;
  QJsonObject empty_params;
  if (!SendCdpRequest(fd, &next_id, QStringLiteral("Network.enable"),
                      empty_params, nullptr)) {
    std::cerr << "Failed to enable Network domain\n";
    close(fd);
    return 1;
  }

  g_stop_requested = 0;
  std::signal(SIGINT, HandleStopSignal);
  std::signal(SIGTERM, HandleStopSignal);

  struct CaptureEntry {
    std::chrono::system_clock::time_point timestamp;
    std::string request_id;
    std::string url;
    std::string method;
    std::string status;
    std::string content_type;
    std::map<std::string, std::string> request_headers;
    std::map<std::string, std::string> response_headers;
    std::string request_body;
    bool has_response = false;
  };

  std::map<std::string, CaptureEntry> pending;
  std::deque<QJsonObject> pending_events;
  while (!g_stop_requested) {
    QJsonObject event;
    if (!pending_events.empty()) {
      event = pending_events.front();
      pending_events.pop_front();
    } else {
      QJsonObject message;
      if (!ReadCdpMessage(fd, &message, &ws_prefetch)) {
        break;
      }
      if (message.isEmpty()) {
        continue;
      }
      if (!message.contains(QStringLiteral("method"))) {
        continue;
      }
      event = message;
    }

    const QString method = event.value(QStringLiteral("method")).toString();
    const QJsonObject params = event.value(QStringLiteral("params")).toObject();
    if (method == QStringLiteral("Network.requestWillBeSent")) {
      const QString request_id =
          params.value(QStringLiteral("requestId")).toString();
      const QJsonObject request =
          params.value(QStringLiteral("request")).toObject();
      CaptureEntry entry;
      entry.request_id = request_id.toStdString();
      entry.url = request.value(QStringLiteral("url")).toString().toStdString();
      entry.method =
          request.value(QStringLiteral("method")).toString().toStdString();
      entry.request_headers =
          NormalizeHeaderMap(request.value(QStringLiteral("headers")).toObject());
      entry.request_body =
          request.value(QStringLiteral("postData")).toString().toStdString();
      entry.timestamp = std::chrono::system_clock::now();
      pending[entry.request_id] = std::move(entry);
      NetworkLogDebug("request " + request_id.toStdString());
      continue;
    }
    if (method == QStringLiteral("Network.responseReceived")) {
      const QString request_id =
          params.value(QStringLiteral("requestId")).toString();
      CaptureEntry& entry = pending[request_id.toStdString()];
      entry.request_id = request_id.toStdString();
      const QJsonObject response =
          params.value(QStringLiteral("response")).toObject();
      entry.url = response.value(QStringLiteral("url")).toString().toStdString();
      entry.status = std::to_string(
          response.value(QStringLiteral("status")).toInt());
      entry.response_headers =
          NormalizeHeaderMap(response.value(QStringLiteral("headers")).toObject());
      entry.content_type =
          ToLower(response.value(QStringLiteral("mimeType")).toString().toStdString());
      auto content_it = entry.response_headers.find("content-type");
      if (content_it != entry.response_headers.end()) {
        entry.content_type = ToLower(content_it->second);
      }
      entry.timestamp = std::chrono::system_clock::now();
      entry.has_response = true;
      NetworkLogDebug("response " + request_id.toStdString());
      continue;
    }
    if (method == QStringLiteral("Network.loadingFailed")) {
      const QString request_id =
          params.value(QStringLiteral("requestId")).toString();
      pending.erase(request_id.toStdString());
      NetworkLogDebug("failed " + request_id.toStdString());
      continue;
    }
    if (method != QStringLiteral("Network.loadingFinished")) {
      continue;
    }

    const QString request_id =
        params.value(QStringLiteral("requestId")).toString();
    auto it = pending.find(request_id.toStdString());
    if (it == pending.end()) {
      NetworkLogDebug("finished missing " + request_id.toStdString());
      continue;
    }
    CaptureEntry entry = std::move(it->second);
    pending.erase(it);

    std::string response_body;
    std::string response_body_error;
    QJsonObject body_params;
    body_params.insert(QStringLiteral("requestId"), request_id);
    int body_request_id = 0;
    if (SendCdpRequest(fd, &next_id, QStringLiteral("Network.getResponseBody"),
                       body_params, &body_request_id)) {
      QJsonObject body_response;
      if (WaitForCdpResponse(fd, body_request_id, &body_response,
                             &pending_events, &ws_prefetch)) {
        if (body_response.contains(QStringLiteral("error"))) {
          const QJsonObject err_obj =
              body_response.value(QStringLiteral("error")).toObject();
          response_body_error =
              err_obj.value(QStringLiteral("message")).toString().toStdString();
        } else {
          const QJsonObject result =
              body_response.value(QStringLiteral("result")).toObject();
          const std::string body_text =
              result.value(QStringLiteral("body")).toString().toStdString();
          const bool base64_encoded =
              result.value(QStringLiteral("base64Encoded")).toBool();
          if (!body_text.empty()) {
            if (base64_encoded) {
              std::string decoded;
              if (Base64Decode(body_text, &decoded)) {
                response_body = std::move(decoded);
              } else {
                response_body_error = "decode body: invalid base64";
              }
            } else {
              response_body = body_text;
            }
          }
        }
      }
    }

    if (!filters.Match(entry.url, entry.method, entry.status,
                       entry.content_type)) {
      continue;
    }

    if (!WriteNetworkCapture(output_dir, entry.timestamp, entry.request_id,
                             entry.url, entry.method, "Response",
                             entry.status.empty() ? "<pending>" : entry.status,
                             entry.content_type, entry.request_headers,
                             entry.response_headers, entry.request_body,
                             response_body, response_body_error)) {
      std::cerr << "Failed to write capture for " << entry.url << "\n";
    }
  }
  close(fd);
  return g_stop_requested ? 0 : 1;
}

}  // namespace rethread
