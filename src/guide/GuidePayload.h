#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

namespace GuidePayload {
constexpr int kVersion = 1;
// Missing data stays absent. Process identities remain in the native service.
QJsonObject build(const QVariantMap& session, const QVariantMap& game, const QString& output,
                  const QString& pad, bool pauseWhileOpen, bool paused);
bool parse(const QByteArray& json, QJsonObject* payload);
QString padFamily(const QString& name);
} // namespace GuidePayload
