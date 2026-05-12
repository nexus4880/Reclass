#pragma once

#include "rtti.h"
#include <QJsonObject>
#include <QString>
#include <cstdint>

namespace rcx {

class Provider;

QString mcpRttiHex(uint64_t value);
QJsonObject mcpRttiInfoToJson(const RttiInfo& info);
QString mcpValidateRttiAbi(const QString& abi);
bool mcpValidateRttiPointerSize(int pointerSize, QString* error = nullptr);
QJsonObject mcpLookupRtti(const Provider& prov, uint64_t vtableAddress,
                          int pointerSize, const QString& abi,
                          int maxVtableSlots);

} // namespace rcx
