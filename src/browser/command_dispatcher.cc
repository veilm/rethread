#include "browser/command_dispatcher.h"

#include <cctype>
#include <sstream>
#include <string>

#include <QUrl>
#include <QStringList>

#include "browser/key_binding_manager.h"
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

}  // namespace

CommandDispatcher::CommandDispatcher(TabManager* tab_manager,
                                     KeyBindingManager* key_binding_manager,
                                     TabStripController* tab_strip_controller)
    : tab_manager_(tab_manager),
      key_binding_manager_(key_binding_manager),
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
  if (op == "tabstrip") {
    std::string rest;
    std::getline(stream, rest);
    return HandleTabStrip(QString::fromStdString(rest));
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
  if (url.trimmed().isEmpty()) {
    return QStringLiteral("ERR missing URL\n");
  }
  int id = tab_manager_->openTab(QUrl::fromUserInput(url), true);
  if (id <= 0) {
    return QStringLiteral("ERR failed to open tab\n");
  }
  return QString();
}

QString CommandDispatcher::HandleBind(const QString& args) const {
  if (!key_binding_manager_) {
    return QStringLiteral("ERR bindings unavailable\n");
  }

  std::istringstream stream(args.toStdString());
  KeyBindingManager::Binding binding;
  std::string token;
  std::string command_text;
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
  if (!key_binding_manager_) {
    return QStringLiteral("ERR bindings unavailable\n");
  }
  std::istringstream stream(args.toStdString());
  KeyBindingManager::Binding binding;
  std::string token;
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
  if (binding.key.trimmed().isEmpty()) {
    return QStringLiteral("ERR unbind requires --key\n");
  }
  if (!key_binding_manager_->RemoveBinding(binding)) {
    return QStringLiteral("ERR failed to remove binding\n");
  }
  return QString();
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
  return QStringLiteral("ERR unknown tabstrip action\n");
}

}  // namespace rethread
