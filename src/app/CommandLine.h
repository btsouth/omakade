#pragma once

#include <QString>
#include <QStringList>

// The value of `--name=value` or `--name value`, or empty when the option is absent.
QString optionValue(const QStringList& arguments, const QString& name);
// Whether `--name` was given, with or without a value.
bool optionSupplied(const QStringList& arguments, const QString& name);
