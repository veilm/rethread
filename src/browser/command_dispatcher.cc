#include "browser/command_dispatcher.h"

#include <cctype>
#include <sstream>
#include <string>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QStringList>
#include <QUrl>
#include <QVariant>

#include "browser/context_menu_binding_manager.h"
#include "browser/key_binding_manager.h"
#include "browser/rules_manager.h"
#include "browser/script_manager.h"
#include "browser/tab_manager.h"
#include "browser/tab_strip_controller.h"

namespace rethread {
namespace {

std::string Trim(const std::string& input) {
  size_t start = input.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return std::string();
  }
  size_t end = input.find_last_not_of(" \t\r\n");
  return input.substr(start, end - start + 1);
}

std::string JsonEscape(const QString& value) {
  const std::string input = value.toStdString();
  std::string escaped;
  escaped.reserve(input.size());
  for (unsigned char c : input) {
    switch (c) {
      case '\"':
        escaped.append("\\\"");
        break;
      case '\\':
        escaped.append("\\\\");
        break;
      case '\b':
        escaped.append("\\b");
        break;
      case '\f':
        escaped.append("\\f");
        break;
      case '\n':
        escaped.append("\\n");
        break;
      case '\r':
        escaped.append("\\r");
        break;
      case '\t':
        escaped.append("\\t");
        break;
      default:
        if (c < 0x20) {
          char buffer[7];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          escaped.append(buffer);
        } else {
          escaped.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  return escaped;
}

int HexCharValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  return -1;
}

bool DecodeHex(const std::string& input, std::string* output) {
  if (!output || input.size() % 2 != 0) {
    return false;
  }
  output->clear();
  output->reserve(input.size() / 2);
  for (size_t i = 0; i + 1 < input.size(); i += 2) {
    int hi = HexCharValue(input[i]);
    int lo = HexCharValue(input[i + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    output->push_back(static_cast<char>((hi << 4) | lo));
  }
  return true;
}

bool ParsePositiveInt(const std::string& text, int* value) {
  if (!value) {
    return false;
  }
  bool ok = false;
  int parsed = QString::fromStdString(text).toInt(&ok);
  if (!ok || parsed <= 0) {
    return false;
  }
  *value = parsed;
  return true;
}

QString VariantToJson(const QVariant& value) {
  QJsonArray wrapper;
  wrapper.append(QJsonValue::fromVariant(value));
  QJsonDocument doc(wrapper);
  QByteArray raw = doc.toJson(QJsonDocument::Compact);
  if (raw.size() >= 2 && raw.front() == '[' && raw.back() == ']') {
    return QString::fromUtf8(raw.mid(1, raw.size() - 2));
  }
  return QStringLiteral("null");
}

QString ParseSwapToken(const QString& token,
                       int active_index,
                       int tab_count,
                       int* index_out) {
  if (!index_out) {
    return QStringLiteral("ERR internal swap error\n");
  }
  const QString trimmed = token.trimmed();
  if (trimmed.isEmpty()) {
    return QStringLiteral("ERR swap requires index arguments\n");
  }
  const QString lower = trimmed.toLower();
  if (lower == QStringLiteral("current") || lower == QStringLiteral("active")) {
    *index_out = active_index;
    return QString();
  }
  if ((trimmed.startsWith('+') || trimmed.startsWith('-')) &&
      trimmed.size() > 1) {
    bool ok = false;
    int delta = trimmed.toInt(&ok);
    if (!ok) {
      return QStringLiteral("ERR invalid swap offset\n");
    }
    if (tab_count <= 0) {
      return QStringLiteral("ERR no tabs available\n");
    }
    int target = (active_index + delta) % tab_count;
    if (target < 0) {
      target += tab_count;
    }
    *index_out = target;
    return QString();
  }
  bool ok = false;
  int parsed = trimmed.toInt(&ok);
  if (!ok) {
    return QStringLiteral("ERR invalid swap index\n");
  }
  if (parsed <= 0 || parsed > tab_count) {
    return QStringLiteral("ERR swap index %1 out of range\n").arg(parsed);
  }
  *index_out = parsed - 1;
  return QString();
}

}  // namespace

CommandDispatcher::CommandDispatcher(TabManager* tab_manager,
                                     KeyBindingManager* key_binding_manager,
                                     ContextMenuBindingManager* context_menu_binding_manager,
                                     RulesManager* rules_manager,
                                     ScriptManager* script_manager,
                                     TabStripController* tab_strip_controller)
    : tab_manager_(tab_manager),
      key_binding_manager_(key_binding_manager),
      context_menu_binding_manager_(context_menu_binding_manager),
      rules_manager_(rules_manager),
      script_manager_(script_manager),
      tab_strip_controller_(tab_strip_controller) {}

QString CommandDispatcher::Execute(const QString& command) const {
  const std::string trimmed = Trim(command.toStdString());
  if (trimmed.empty()) {
    return QStringLiteral("ERR empty command\n");
  }

  std::istringstream stream(trimmed);
  std::string op;
  stream >> op;

  if (op == "get" || op == "list") {
    return HandleList();
  }
  if (op == "switch") {
    int target_id = 0;
    stream >> target_id;
    return HandleSwitch(target_id);
  }
  if (op == "cycle") {
    int delta = 0;
    stream >> delta;
    return HandleCycle(delta);
  }
  if (op == "history-back") {
    return HandleHistoryBack();
  }
  if (op == "history-forward") {
    return HandleHistoryForward();
  }
  if (op == "close") {
    std::string rest;
    std::getline(stream, rest);
    return HandleClose(QString::fromStdString(rest));
  }
  if (op == "open") {
    std::string rest;
    std::getline(stream, rest);
    return HandleOpen(QString::fromStdString(Trim(rest)));
  }
  if (op == "swap") {
    std::string rest;
    std::getline(stream, rest);
    return HandleSwap(QString::fromStdString(rest));
  }
  if (op == "bind") {
    std::string rest;
    std::getline(stream, rest);
    return HandleBind(QString::fromStdString(rest));
  }
  if (op == "unbind") {
    std::string rest;
    std::getline(stream, rest);
    return HandleUnbind(QString::fromStdString(rest));
  }
  if (op == "rules") {
    std::string rest;
    std::getline(stream, rest);
    return HandleRules(QString::fromStdString(rest));
  }
  if (op == "scripts") {
    std::string rest;
    std::getline(stream, rest);
    return HandleScripts(QString::fromStdString(rest));
  }
  if (op == "devtools") {
    std::string rest;
    std::getline(stream, rest);
    return HandleDevTools(QString::fromStdString(rest));
  }
  if (op == "devtools-id") {
    std::string rest;
    std::getline(stream, rest);
    return HandleDevToolsId(QString::fromStdString(rest));
  }
  if (op == "tabstrip") {
    std::string rest;
    std::getline(stream, rest);
    return HandleTabStrip(QString::fromStdString(rest));
  }
  if (op == "eval") {
    std::string rest;
    std::getline(stream, rest);
    return HandleEval(QString::fromStdString(rest));
  }

  return QStringLiteral("ERR unknown command\n");
}

QString CommandDispatcher::HandleList() const {
  if (!tab_manager_) {
    return QStringLiteral("ERR tab manager unavailable\n");
  }
  auto tabs = tab_manager_->snapshot();
  std::ostringstream out;
  out << "{\n  \"tabs\": [";
  for (int i = 0; i < tabs.size(); ++i) {
    const auto& tab = tabs.at(i);
    if (i == 0) {
      out << "\n";
    }
    out << "    {\"id\": " << tab.id << ", \"active\": "
        << (tab.active ? "true" : "false") << ", \"url\": \""
        << JsonEscape(tab.url) << "\", \"title\": \""
        << JsonEscape(tab.title) << "\"}";
    if (i + 1 < tabs.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ]\n}\n";
  return QString::fromStdString(out.str());
}

QString CommandDispatcher::HandleSwitch(int id) const {
  if (id <= 0 || !tab_manager_) {
    return QStringLiteral("ERR missing tab id\n");
  }
  if (!tab_manager_->activateTab(id)) {
    return QStringLiteral("ERR failed to switch tab\n");
  }
  return QString();
}

QString CommandDispatcher::HandleCycle(int delta) const {
  if (!tab_manager_) {
    return QStringLiteral("ERR failed to cycle tab\n");
  }
  if (!tab_manager_->cycleActiveTab(delta)) {
    return QStringLiteral("ERR failed to cycle tab\n");
  }
  return QString();
}

QString CommandDispatcher::HandleClose(const QString& args) const {
  if (!tab_manager_) {
    return QStringLiteral("ERR failed to close tab\n");
  }
  const QString trimmed = args.trimmed();
  if (trimmed.isEmpty()) {
    return tab_manager_->closeActiveTab() ? QString()
                                          : QStringLiteral("ERR failed to close tab\n");
  }
  QStringList pieces = trimmed.split(QChar(' '), Qt::SkipEmptyParts);
  if (pieces.size() > 1) {
    return QStringLiteral("ERR close accepts at most one index\n");
  }
  bool ok = false;
  int index = pieces.first().toInt(&ok);
  if (!ok || index <= 0) {
    return QStringLiteral("ERR close requires a positive tab index\n");
  }
  return tab_manager_->closeTabAtIndex(index - 1) ? QString()
                                                  : QStringLiteral("ERR failed to close tab\n");
}

QString CommandDispatcher::HandleOpen(const QString& url) const {
  if (!tab_manager_) {
    return QStringLiteral("ERR failed to open tab\n");
  }
  std::istringstream stream(Trim(url.toStdString()));
  std::string token;
  bool open_at_end = false;
  bool saw_separator = false;
  std::string url_text;
  while (stream >> token) {
    if (token == "--at-end") {
      open_at_end = true;
      continue;
    }
    if (token == "--") {
      saw_separator = true;
      break;
    }
    url_text = token;
    std::string rest;
    std::getline(stream, rest);
    if (!rest.empty()) {
      url_text += rest;
    }
    break;
  }
  if (saw_separator) {
    std::string rest;
    std::getline(stream, rest);
    url_text = Trim(rest);
  }
  if (url_text.empty()) {
    url_text = Trim(url.toStdString());
  }
  const QString normalized = QString::fromStdString(url_text).trimmed();
  if (normalized.isEmpty()) {
    return QStringLiteral("ERR missing URL\n");
  }
  int id = tab_manager_->openTab(QUrl::fromUserInput(normalized), true,
                                 open_at_end);
  if (id <= 0) {
    return QStringLiteral("ERR failed to open tab\n");
  }
  return QString();
}

QString CommandDispatcher::HandleSwap(const QString& args) const {
  if (!tab_manager_) {
    return QStringLiteral("ERR tabs unavailable\n");
  }
  auto tabs = tab_manager_->snapshot();
  if (tabs.isEmpty()) {
    return QStringLiteral("ERR no tabs to swap\n");
  }
  const QString trimmed = args.trimmed();
  if (trimmed.isEmpty()) {
    return QStringLiteral("ERR swap requires one or two indexes\n");
  }
  const QStringList tokens = trimmed.split(QChar(' '), Qt::SkipEmptyParts);
  if (tokens.size() < 1 || tokens.size() > 2) {
    return QStringLiteral("ERR swap expects one or two indexes\n");
  }
  int active_index = 0;
  for (int i = 0; i < tabs.size(); ++i) {
    if (tabs.at(i).active) {
      active_index = i;
      break;
    }
  }
  int first_index = active_index;
  int second_index = -1;
  if (tokens.size() == 1) {
    QString error =
        ParseSwapToken(tokens.first(), active_index, tabs.size(), &second_index);
    if (!error.isEmpty()) {
      return error;
    }
  } else {
    QString error =
        ParseSwapToken(tokens.at(0), active_index, tabs.size(), &first_index);
    if (!error.isEmpty()) {
      return error;
    }
    error =
        ParseSwapToken(tokens.at(1), active_index, tabs.size(), &second_index);
    if (!error.isEmpty()) {
      return error;
    }
  }
  if (second_index < 0) {
    return QStringLiteral("ERR swap target missing\n");
  }
  if (!tab_manager_->SwapTabs(first_index, second_index)) {
    return QStringLiteral("ERR failed to swap tabs\n");
  }
  return QString();
}

QString CommandDispatcher::HandleHistoryBack() const {
  if (!tab_manager_) {
    return QStringLiteral("ERR failed to go back\n");
  }
  if (!tab_manager_->historyBack()) {
    return QStringLiteral("ERR no page to go back to\n");
  }
  return QString();
}

QString CommandDispatcher::HandleHistoryForward() const {
  if (!tab_manager_) {
    return QStringLiteral("ERR failed to go forward\n");
  }
  if (!tab_manager_->historyForward()) {
    return QStringLiteral("ERR no page to go forward to\n");
  }
  return QString();
}

QString CommandDispatcher::HandleBind(const QString& args) const {
  std::istringstream stream(args.toStdString());
  KeyBindingManager::Binding binding;
  std::string token;
  std::string command_text;
  bool context_menu = false;
  while (stream >> token) {
    if (token == "--") {
      std::string rest;
      std::getline(stream, rest);
      command_text = Trim(rest);
      break;
    }
    if (token == "--alt") {
      binding.alt = true;
      continue;
    }
    if (token == "--ctrl") {
      binding.ctrl = true;
      continue;
    }
    if (token == "--shift") {
      binding.shift = true;
      continue;
    }
    if (token == "--command" || token == "--meta") {
      binding.command = true;
      continue;
    }
    if (token == "--no-consume") {
      binding.consume = false;
      continue;
    }
    if (token == "--context-menu" || token == "--right-click") {
      context_menu = true;
      continue;
    }
    const std::string key_prefix = "--key=";
    if (token.rfind(key_prefix, 0) == 0) {
      binding.key = QString::fromStdString(token.substr(key_prefix.size()));
      continue;
    }
    if (token == "--key") {
      std::string key_value;
      if (stream >> key_value) {
        binding.key = QString::fromStdString(key_value);
        continue;
      }
    }
    command_text = token;
    std::string rest;
    std::getline(stream, rest);
    if (!rest.empty()) {
      command_text += " " + Trim(rest);
    }
    break;
  }

  if (context_menu) {
    if (!context_menu_binding_manager_) {
      return QStringLiteral("ERR context menu bindings unavailable\n");
    }
    if (command_text.empty()) {
      return QStringLiteral("ERR bind requires a command after --\n");
    }
    context_menu_binding_manager_->SetBinding(
        QString::fromStdString(command_text));
    return QString();
  }

  if (!key_binding_manager_) {
    return QStringLiteral("ERR bindings unavailable\n");
  }
  if (binding.key.trimmed().isEmpty()) {
    return QStringLiteral("ERR bind requires --key\n");
  }
  if (command_text.empty()) {
    return QStringLiteral("ERR bind requires a command after --\n");
  }
  binding.command_line = QString::fromStdString(command_text);
  if (!key_binding_manager_->AddBinding(binding)) {
    return QStringLiteral("ERR failed to add binding\n");
  }
  return QString();
}

QString CommandDispatcher::HandleUnbind(const QString& args) const {
  std::istringstream stream(args.toStdString());
  KeyBindingManager::Binding binding;
  std::string token;
  bool context_menu = false;
  while (stream >> token) {
    if (token == "--alt") {
      binding.alt = true;
      continue;
    }
    if (token == "--ctrl") {
      binding.ctrl = true;
      continue;
    }
    if (token == "--shift") {
      binding.shift = true;
      continue;
    }
    if (token == "--command" || token == "--meta") {
      binding.command = true;
      continue;
    }
    if (token == "--context-menu" || token == "--right-click") {
      context_menu = true;
      continue;
    }
    const std::string key_prefix = "--key=";
    if (token.rfind(key_prefix, 0) == 0) {
      binding.key = QString::fromStdString(token.substr(key_prefix.size()));
      continue;
    }
    if (token == "--key") {
      std::string key_value;
      if (stream >> key_value) {
        binding.key = QString::fromStdString(key_value);
        continue;
      }
    }
    return QStringLiteral("ERR unknown unbind flag\n");
  }
  if (context_menu) {
    if (!context_menu_binding_manager_) {
      return QStringLiteral("ERR context menu bindings unavailable\n");
    }
    context_menu_binding_manager_->ClearBinding();
    return QString();
  }

  if (!key_binding_manager_) {
    return QStringLiteral("ERR bindings unavailable\n");
  }
  if (binding.key.trimmed().isEmpty()) {
    return QStringLiteral("ERR unbind requires --key\n");
  }
  if (!key_binding_manager_->RemoveBinding(binding)) {
    return QStringLiteral("ERR failed to remove binding\n");
  }
  return QString();
}

QString CommandDispatcher::HandleRules(const QString& args) const {
  std::istringstream stream(args.toStdString());
  std::string action;
  stream >> action;
  if (action.empty()) {
    return QStringLiteral("ERR missing rules target\n");
  }
  if (action != "js" && action != "iframes") {
    return QStringLiteral("ERR unknown rules target\n");
  }
  std::string token;
  std::string mode_text;
  std::string data_hex;
  bool append = false;
  while (stream >> token) {
    if (token == "--mode") {
      if (!(stream >> mode_text)) {
        return QStringLiteral("ERR missing rules mode\n");
      }
      continue;
    }
    const std::string mode_prefix = "--mode=";
    if (token.rfind(mode_prefix, 0) == 0) {
      mode_text = token.substr(mode_prefix.size());
      continue;
    }
    if (token == "--data") {
      if (!(stream >> data_hex)) {
        return QStringLiteral("ERR missing rules data\n");
      }
      continue;
    }
    const std::string data_prefix = "--data=";
    if (token.rfind(data_prefix, 0) == 0) {
      data_hex = token.substr(data_prefix.size());
      continue;
    }
    if (token == "--append") {
      append = true;
      continue;
    }
    if (!token.empty()) {
      return QStringLiteral("ERR unknown rules flag\n");
    }
  }
  if (mode_text.empty()) {
    return QStringLiteral("ERR missing rules mode\n");
  }
  if (data_hex.empty()) {
    return QStringLiteral("ERR missing rules data\n");
  }
  RulesManager::ListMode mode;
  if (mode_text == "whitelist") {
    mode = RulesManager::ListMode::kAllowlist;
  } else if (mode_text == "blacklist") {
    mode = RulesManager::ListMode::kBlacklist;
  } else {
    return QStringLiteral("ERR unknown rules mode\n");
  }
  std::string decoded;
  if (!DecodeHex(data_hex, &decoded)) {
    return QStringLiteral("ERR invalid rules payload\n");
  }
  if (!rules_manager_) {
    return QStringLiteral("ERR rules unavailable\n");
  }
  const QString text =
      QString::fromUtf8(decoded.data(), static_cast<int>(decoded.size()));
  int count = 0;
  bool ok = false;
  if (action == "js") {
    ok = rules_manager_->LoadJavaScriptRules(mode, text, append, &count);
  } else if (action == "iframes") {
    ok = rules_manager_->LoadIframeRules(mode, text, append, &count);
  }
  if (!ok) {
    return QStringLiteral("ERR failed to load rules\n");
  }
  return QStringLiteral("Loaded %1 host(s)\n").arg(count);
}

QString CommandDispatcher::HandleDevTools(const QString& args) const {
  Q_UNUSED(args);
  if (!tab_manager_) {
    return QStringLiteral("ERR devtools unavailable\n");
  }
  if (!tab_manager_->OpenDevToolsForActiveTab()) {
    return QStringLiteral("ERR failed to open devtools\n");
  }
  return QString();
}

QString CommandDispatcher::HandleDevToolsId(const QString& args) const {
  if (!tab_manager_) {
    return QStringLiteral("ERR devtools unavailable\n");
  }
  const QString trimmed = args.trimmed();
  if (trimmed.isEmpty()) {
    return QStringLiteral("ERR missing tab id\n");
  }
  QStringList pieces = trimmed.split(QChar(' '), Qt::SkipEmptyParts);
  if (pieces.size() != 1) {
    return QStringLiteral("ERR devtools-id expects one tab id\n");
  }
  bool ok = false;
  int tab_id = pieces.first().toInt(&ok);
  if (!ok || tab_id <= 0) {
    return QStringLiteral("ERR invalid tab id\n");
  }
  const QString devtools_id = tab_manager_->DevToolsIdForTab(tab_id);
  if (devtools_id.isEmpty()) {
    return QStringLiteral("ERR devtools id unavailable\n");
  }
  return devtools_id + QLatin1Char('\n');
}

QString CommandDispatcher::HandleTabStrip(const QString& args) const {
  if (!tab_strip_controller_) {
    return QStringLiteral("ERR tab strip unavailable\n");
  }
  std::istringstream stream(args.toStdString());
  std::string action;
  stream >> action;
  if (action.empty()) {
    return QStringLiteral("ERR missing tabstrip action\n");
  }
  if (action == "show") {
    tab_strip_controller_->Show();
    return QString();
  }
  if (action == "hide") {
    tab_strip_controller_->Hide();
    return QString();
  }
  if (action == "toggle") {
    tab_strip_controller_->Toggle();
    return QString();
  }
  if (action == "peek") {
    int duration_ms = 0;
    stream >> duration_ms;
    if (duration_ms <= 0) {
      return QStringLiteral("ERR tabstrip peek requires duration in ms\n");
    }
    tab_strip_controller_->Peek(duration_ms);
    return QString();
  }
  if (action == "message") {
    std::string token;
    std::string duration_text;
    std::string data_hex;
    while (stream >> token) {
      if (token == "--duration") {
        stream >> duration_text;
        continue;
      }
      const std::string duration_prefix = "--duration=";
      if (token.rfind(duration_prefix, 0) == 0) {
        duration_text = token.substr(duration_prefix.size());
        continue;
      }
      if (token == "--data") {
        stream >> data_hex;
        continue;
      }
      const std::string data_prefix = "--data=";
      if (token.rfind(data_prefix, 0) == 0) {
        data_hex = token.substr(data_prefix.size());
        continue;
      }
      if (!token.empty()) {
        return QStringLiteral("ERR unknown tabstrip message flag\n");
      }
    }
    if (duration_text.empty()) {
      return QStringLiteral("ERR tabstrip message requires --duration\n");
    }
    bool ok = false;
    int duration_ms = QString::fromStdString(duration_text).toInt(&ok);
    if (!ok || duration_ms < 0) {
      return QStringLiteral("ERR invalid tabstrip message duration\n");
    }
    if (data_hex.empty()) {
      return QStringLiteral("ERR tabstrip message missing data\n");
    }
    std::string decoded;
    if (!DecodeHex(data_hex, &decoded)) {
      return QStringLiteral("ERR invalid tabstrip message payload\n");
    }
    const QString payload =
        QString::fromUtf8(decoded.data(), static_cast<int>(decoded.size()));
    const QStringList lines = payload.split(QChar('\n'));
    tab_strip_controller_->ShowMessage(lines, duration_ms);
    return QString();
  }
  return QStringLiteral("ERR unknown tabstrip action\n");
}

QString CommandDispatcher::HandleEval(const QString& args) const {
  if (!tab_manager_) {
    return QStringLiteral("ERR tabs unavailable\n");
  }
  std::istringstream stream(args.toStdString());
  std::string token;
  int tab_id = 0;
  int tab_index = 0;
  std::string code_hex;
  while (stream >> token) {
    if (token == "--tab-id") {
      std::string value;
      if (!(stream >> value) || !ParsePositiveInt(value, &tab_id)) {
        return QStringLiteral("ERR invalid --tab-id value\n");
      }
      continue;
    }
    const std::string tab_id_prefix = "--tab-id=";
    if (token.rfind(tab_id_prefix, 0) == 0) {
      if (!ParsePositiveInt(token.substr(tab_id_prefix.size()), &tab_id)) {
        return QStringLiteral("ERR invalid --tab-id value\n");
      }
      continue;
    }
    if (token == "--tab-index") {
      std::string value;
      if (!(stream >> value) || !ParsePositiveInt(value, &tab_index)) {
        return QStringLiteral("ERR invalid --tab-index value\n");
      }
      continue;
    }
    const std::string tab_index_prefix = "--tab-index=";
    if (token.rfind(tab_index_prefix, 0) == 0) {
      if (!ParsePositiveInt(token.substr(tab_index_prefix.size()),
                            &tab_index)) {
        return QStringLiteral("ERR invalid --tab-index value\n");
      }
      continue;
    }
    if (token == "--code") {
      if (!(stream >> code_hex)) {
        return QStringLiteral("ERR missing --code value\n");
      }
      continue;
    }
    const std::string code_prefix = "--code=";
    if (token.rfind(code_prefix, 0) == 0) {
      code_hex = token.substr(code_prefix.size());
      continue;
    }
    if (token.empty()) {
      continue;
    }
    return QStringLiteral("ERR unknown eval flag\n");
  }

  if (code_hex.empty()) {
    return QStringLiteral("ERR missing eval payload\n");
  }
  if (tab_id > 0 && tab_index > 0) {
    return QStringLiteral("ERR specify only one tab selector\n");
  }

  std::string decoded;
  if (!DecodeHex(code_hex, &decoded)) {
    return QStringLiteral("ERR invalid eval payload encoding\n");
  }

  QVariant result;
  QString error_message;
  if (!tab_manager_->EvaluateJavaScript(
          QString::fromUtf8(decoded.c_str(), static_cast<int>(decoded.size())),
          tab_id, tab_index, &result, &error_message)) {
    if (!error_message.isEmpty()) {
      return QStringLiteral("ERR %1\n").arg(error_message);
    }
    return QStringLiteral("ERR failed to evaluate script\n");
  }
  return VariantToJson(result) + QChar('\n');
}

QString CommandDispatcher::HandleScripts(const QString& args) const {
  if (!script_manager_) {
    return QStringLiteral("ERR scripts unavailable\n");
  }
  const std::string trimmed = Trim(args.toStdString());
  if (trimmed.empty()) {
    return QStringLiteral("ERR missing scripts command\n");
  }
  std::istringstream stream(trimmed);
  std::string action;
  stream >> action;
  std::string rest;
  std::getline(stream, rest);
  rest = Trim(rest);
  if (action == "list") {
    if (!rest.empty()) {
      return QStringLiteral("ERR scripts list takes no arguments\n");
    }
    const auto scripts = script_manager_->ListScripts();
    std::ostringstream out;
    out << "{\n  \"scripts\": [";
    for (size_t i = 0; i < scripts.size(); ++i) {
      const auto& entry = scripts[i];
      if (i == 0) {
        out << "\n";
      }
      out << "    {\"id\": \"" << JsonEscape(entry.id) << "\", "
          << "\"path\": \"" << JsonEscape(entry.path) << "\"}";
      if (i + 1 < scripts.size()) {
        out << ",";
      }
      out << "\n";
    }
    out << "  ]\n}\n";
    return QString::fromStdString(out.str());
  }
  if (action == "rm") {
    QString id;
    std::istringstream rm_stream(rest);
    std::string token;
    while (rm_stream >> token) {
      const std::string id_prefix = "--id=";
      if (token == "--id") {
        if (!(rm_stream >> token)) {
          return QStringLiteral("ERR scripts rm requires a value after --id\n");
        }
        id = QString::fromStdString(token);
        continue;
      }
      if (token.rfind(id_prefix, 0) == 0) {
        id = QString::fromStdString(token.substr(id_prefix.size()));
        continue;
      }
      if (!token.empty()) {
        return QStringLiteral("ERR unknown scripts rm flag\n");
      }
    }
    if (id.isEmpty()) {
      return QStringLiteral("ERR scripts rm requires --id\n");
    }
    QString error;
    if (!script_manager_->RemoveScript(id, &error)) {
      if (error.isEmpty()) {
        return QStringLiteral("ERR failed to remove script\n");
      }
      return QStringLiteral("ERR %1\n").arg(error);
    }
    return QString();
  }
  if (action == "add") {
    QString id;
    QString match;
    QString run_at;
    bool stylesheet = false;
    std::string code_hex;
    std::istringstream add_stream(rest);
    std::string token;
    while (add_stream >> token) {
      if (token == "--stylesheet") {
        stylesheet = true;
        continue;
      }
      const std::string id_prefix = "--id=";
      if (token == "--id") {
        if (!(add_stream >> token)) {
          return QStringLiteral("ERR scripts add requires a value after --id\n");
        }
        id = QString::fromStdString(token);
        continue;
      }
      if (token.rfind(id_prefix, 0) == 0) {
        id = QString::fromStdString(token.substr(id_prefix.size()));
        continue;
      }
      const std::string match_prefix = "--match=";
      if (token == "--match") {
        if (!(add_stream >> token)) {
          return QStringLiteral("ERR scripts add requires a value after --match\n");
        }
        match = QString::fromStdString(token);
        continue;
      }
      if (token.rfind(match_prefix, 0) == 0) {
        match = QString::fromStdString(token.substr(match_prefix.size()));
        continue;
      }
      const std::string run_at_prefix = "--run-at=";
      if (token == "--run-at") {
        if (!(add_stream >> token)) {
          return QStringLiteral("ERR scripts add requires a value after --run-at\n");
        }
        run_at = QString::fromStdString(token);
        continue;
      }
      if (token.rfind(run_at_prefix, 0) == 0) {
        run_at = QString::fromStdString(token.substr(run_at_prefix.size()));
        continue;
      }
      const std::string code_prefix = "--code=";
      if (token == "--code") {
        if (!(add_stream >> token)) {
          return QStringLiteral("ERR scripts add requires a value after --code\n");
        }
        code_hex = token;
        continue;
      }
      if (token.rfind(code_prefix, 0) == 0) {
        code_hex = token.substr(code_prefix.size());
        continue;
      }
      if (!token.empty()) {
        return QStringLiteral("ERR unknown scripts add flag\n");
      }
    }
    if (id.isEmpty()) {
      return QStringLiteral("ERR scripts add requires --id\n");
    }
    if (code_hex.empty()) {
      return QStringLiteral("ERR scripts add is missing payload\n");
    }
    std::string decoded;
    if (!DecodeHex(code_hex, &decoded)) {
      return QStringLiteral("ERR invalid script payload\n");
    }
    QString error;
    if (!script_manager_->AddScript(
            id, QByteArray::fromStdString(decoded), stylesheet, match, run_at,
            &error)) {
      if (error.isEmpty()) {
        return QStringLiteral("ERR failed to add script\n");
      }
      return QStringLiteral("ERR %1\n").arg(error);
    }
    return QString();
  }
  return QStringLiteral("ERR unknown scripts action\n");
}

}  // namespace rethread
