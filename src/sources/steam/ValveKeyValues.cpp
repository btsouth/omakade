#include "sources/steam/ValveKeyValues.h"

#include <cstring>

#include <QFile>
#include <QtEndian>

#include <cctype>

namespace {
constexpr qint64 kMaximumFileBytes = 64LL * 1024 * 1024;
constexpr int kMaximumNestingDepth = 128;

QString matchingKey(const auto& values, const QString& wanted) {
  for (auto iterator = values.cbegin(); iterator != values.cend(); ++iterator) {
    if (iterator.key().compare(wanted, Qt::CaseInsensitive) == 0) {
      return iterator.key();
    }
  }
  return {};
}

class Tokenizer final {
public:
  explicit Tokenizer(const QByteArray& input) : m_input(input) {}

  bool next(QString* result) {
    skipSpaceAndComments();
    if (m_position >= m_input.size()) {
      return false;
    }

    const char first = m_input.at(m_position++);
    if (first == '{' || first == '}') {
      *result = QString(first);
      return true;
    }
    if (first != '"') {
      QByteArray token(1, first);
      while (m_position < m_input.size()) {
        const char current = m_input.at(m_position);
        if (current == '{' || current == '}' || std::isspace(static_cast<unsigned char>(current))) {
          break;
        }
        token.append(current);
        ++m_position;
      }
      *result = QString::fromUtf8(token);
      return true;
    }

    QByteArray token;
    while (m_position < m_input.size()) {
      const char current = m_input.at(m_position++);
      if (current == '"') {
        *result = QString::fromUtf8(token);
        return true;
      }
      if (current == '\\' && m_position < m_input.size()) {
        const char escaped = m_input.at(m_position++);
        if (escaped == 'n') {
          token.append('\n');
        } else if (escaped == 't') {
          token.append('\t');
        } else {
          token.append(escaped);
        }
      } else {
        token.append(current);
      }
    }
    m_error = QStringLiteral("Unterminated quoted string");
    return false;
  }

  [[nodiscard]] QString error() const { return m_error; }

private:
  void skipSpaceAndComments() {
    while (m_position < m_input.size()) {
      const char current = m_input.at(m_position);
      if (std::isspace(static_cast<unsigned char>(current))) {
        ++m_position;
        continue;
      }
      if (current == '/' && m_position + 1 < m_input.size() && m_input.at(m_position + 1) == '/') {
        m_position += 2;
        while (m_position < m_input.size() && m_input.at(m_position) != '\n') {
          ++m_position;
        }
        continue;
      }
      break;
    }
  }

  QByteArray m_input;
  qsizetype m_position = 0;
  QString m_error;
};

bool parseObject(Tokenizer* tokenizer, ValveKeyValues* result, int depth, QString* error) {
  if (depth > kMaximumNestingDepth) {
    *error = QStringLiteral("Object nesting is too deep");
    return false;
  }
  while (true) {
    QString key;
    if (!tokenizer->next(&key)) {
      if (!tokenizer->error().isEmpty()) {
        *error = tokenizer->error();
        return false;
      }
      if (depth > 0) {
        *error = QStringLiteral("Object is missing a closing brace");
        return false;
      }
      return true;
    }
    if (key == QStringLiteral("}")) {
      if (depth == 0) {
        *error = QStringLiteral("Unexpected closing brace");
        return false;
      }
      return true;
    }

    QString value;
    if (!tokenizer->next(&value) || value == QStringLiteral("}")) {
      *error = QStringLiteral("Missing value for key '%1'").arg(key);
      return false;
    }
    if (value == QStringLiteral("{")) {
      ValveKeyValues child;
      if (!parseObject(tokenizer, &child, depth + 1, error)) {
        return false;
      }
      result->objects.insert(key, child);
    } else {
      result->values.insert(key, value);
    }
  }
}
} // namespace

QString ValveKeyValues::value(const QString& key) const {
  return values.value(matchingKey(values, key));
}

const ValveKeyValues* ValveKeyValues::object(const QString& key) const {
  const auto iterator = objects.constFind(matchingKey(objects, key));
  return iterator == objects.cend() ? nullptr : &iterator.value();
}

bool ValveKeyValuesParser::parse(const QByteArray& contents, ValveKeyValues* result,
                                 QString* error) {
  if (result == nullptr) {
    return false;
  }
  *result = {};
  if (contents.size() > kMaximumFileBytes) {
    if (error != nullptr) {
      *error = QStringLiteral("File is too large");
    }
    return false;
  }
  QString localError;
  Tokenizer tokenizer(contents);
  const bool success = parseObject(&tokenizer, result, 0, &localError);
  if (error != nullptr) {
    *error = localError;
  }
  return success;
}

bool ValveKeyValuesParser::parseFile(const QString& path, ValveKeyValues* result, QString* error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    return false;
  }
  if (file.size() > kMaximumFileBytes) {
    if (error != nullptr) {
      *error = QStringLiteral("File is too large");
    }
    return false;
  }
  return parse(file.readAll(), result, error);
}

namespace {
bool readCString(const QByteArray& input, qsizetype* position, QString* result, QString* error) {
  const qsizetype start = *position;
  while (*position < input.size()) {
    if (input.at(*position) == '\0') {
      *result = QString::fromUtf8(input.constData() + start, *position - start);
      ++(*position);
      return true;
    }
    ++(*position);
  }
  *error = QStringLiteral("Unterminated binary string");
  return false;
}

bool parseBinaryObject(const QByteArray& input, qsizetype* position, ValveKeyValues* result,
                       int depth, QString* error) {
  if (depth > kMaximumNestingDepth) {
    *error = QStringLiteral("Object nesting is too deep");
    return false;
  }
  while (true) {
    if (*position >= input.size()) {
      *error = QStringLiteral("Object is missing a closing marker");
      return false;
    }
    const auto type = static_cast<unsigned char>(input.at((*position)++));
    if (type == 0x08) {
      return true;
    }
    QString key;
    if (!readCString(input, position, &key, error)) {
      return false;
    }
    if (type == 0x00) {
      ValveKeyValues child;
      if (!parseBinaryObject(input, position, &child, depth + 1, error)) {
        return false;
      }
      result->objects.insert(key, child);
      continue;
    }
    if (type == 0x01) {
      QString value;
      if (!readCString(input, position, &value, error)) {
        return false;
      }
      result->values.insert(key, value);
      continue;
    }
    if (type == 0x02) {
      if (*position + 4 > input.size()) {
        *error = QStringLiteral("Truncated 32-bit value");
        return false;
      }
      const auto value =
          qFromLittleEndian<quint32>(input.constData() + *position);
      *position += 4;
      result->values.insert(key, QString::number(value));
      continue;
    }
    if (type == 0x03) {
      // A float, as Steam's stats files may store a stat.
      if (*position + 4 > input.size()) {
        *error = QStringLiteral("Truncated float value");
        return false;
      }
      const quint32 bits = qFromLittleEndian<quint32>(input.constData() + *position);
      float value = 0;
      std::memcpy(&value, &bits, sizeof(value));
      *position += 4;
      result->values.insert(key, QString::number(value));
      continue;
    }
    if (type == 0x07) {
      if (*position + 8 > input.size()) {
        *error = QStringLiteral("Truncated 64-bit value");
        return false;
      }
      const auto value =
          qFromLittleEndian<quint64>(input.constData() + *position);
      *position += 8;
      result->values.insert(key, QString::number(value));
      continue;
    }
    *error = QStringLiteral("Unsupported binary type %1").arg(type);
    return false;
  }
}
} // namespace

bool ValveKeyValuesParser::parseBinary(const QByteArray& contents, ValveKeyValues* result,
                                       QString* error) {
  if (result == nullptr) {
    return false;
  }
  *result = {};
  if (contents.size() > kMaximumFileBytes) {
    if (error != nullptr) {
      *error = QStringLiteral("File is too large");
    }
    return false;
  }
  if (contents.isEmpty()) {
    return true;
  }

  QString localError;
  qsizetype position = 0;
  bool success = false;
  if (static_cast<unsigned char>(contents.at(0)) == 0x00) {
    ++position;
    QString rootKey;
    success = readCString(contents, &position, &rootKey, &localError);
    if (success) {
      ValveKeyValues child;
      success = parseBinaryObject(contents, &position, &child, 1, &localError);
      if (success) {
        result->objects.insert(rootKey, child);
      }
    }
  } else {
    success = parseBinaryObject(contents, &position, result, 0, &localError);
  }
  if (success && position < contents.size() &&
      static_cast<unsigned char>(contents.at(position)) == 0x08) {
    ++position;
  }
  if (success && position != contents.size()) {
    localError = QStringLiteral("Unexpected trailing data");
    *result = {};
    success = false;
  }
  if (error != nullptr) {
    *error = localError;
  }
  return success;
}

bool ValveKeyValuesParser::parseBinaryFile(const QString& path, ValveKeyValues* result,
                                           QString* error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    return false;
  }
  const QByteArray contents = file.read(kMaximumFileBytes + 1);
  if (contents.size() > kMaximumFileBytes) {
    if (error != nullptr) {
      *error = QStringLiteral("File is too large");
    }
    return false;
  }
  return parseBinary(contents, result, error);
}
