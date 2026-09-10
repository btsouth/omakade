#pragma once
#include "saves/SaveSetStore.h"
SaveLayout resolveSaveLayout(const QJsonObject& context, const QString& home,
                             const QString& retroArchConfig);
