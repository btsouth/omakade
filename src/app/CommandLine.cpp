#include "app/CommandLine.h"

#include <algorithm>

QString optionValue(const QStringList& arguments, const QString& name) {
  const QString prefix = name + QLatin1Char('=');
  for (qsizetype index = 0; index < arguments.size(); ++index) {
    const QString& argument = arguments.at(index);
    if (argument.startsWith(prefix)) {
      return argument.mid(prefix.size());
    }
    if (argument == name && index + 1 < arguments.size() &&
        !arguments.at(index + 1).startsWith(QStringLiteral("--"))) {
      return arguments.at(index + 1);
    }
  }
  return {};
}

bool optionSupplied(const QStringList& arguments, const QString& name) {
  return std::ranges::any_of(arguments, [&name](const QString& argument) {
    return argument == name || argument.startsWith(name + QLatin1Char('='));
  });
}
