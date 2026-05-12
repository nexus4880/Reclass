#include "mcp_rtti.h"
#include "providers/provider.h"

#include <QJsonArray>

namespace rcx {

QString mcpRttiHex(uint64_t value) {
    return QStringLiteral("0x") + QString::number(value, 16).toUpper();
}

QJsonObject mcpRttiInfoToJson(const RttiInfo& info) {
    QJsonObject out;
    out["ok"] = info.ok;
    if (!info.ok)
        out["error"] = info.error;
    if (!info.abi.isEmpty())
        out["abi"] = info.abi;
    out["vtableAddress"] = mcpRttiHex(info.vtableAddress);
    out["imageBase"] = mcpRttiHex(info.imageBase);
    out["moduleName"] = info.moduleName;
    out["completeLocator"] = mcpRttiHex(info.completeLocator);
    out["offset"] = info.offset;
    out["rawName"] = info.rawName;
    out["demangledName"] = info.demangledName;

    QJsonArray bases;
    for (const auto& base : info.bases) {
        bases.append(QJsonObject{
            {"rawName", base.rawName},
            {"demangledName", base.demangledName},
            {"depth", base.depth}
        });
    }
    out["bases"] = bases;

    QJsonArray vtable;
    for (const auto& method : info.vtable) {
        vtable.append(QJsonObject{
            {"slot", method.slot},
            {"address", mcpRttiHex(method.address)},
            {"symbol", method.symbol}
        });
    }
    out["vtable"] = vtable;
    return out;
}

QString mcpValidateRttiAbi(const QString& abi) {
    const QString normalized = abi.trimmed().toLower();
    if (normalized == QLatin1String("auto") ||
        normalized == QLatin1String("msvc") ||
        normalized == QLatin1String("itanium")) {
        return normalized;
    }
    return {};
}

bool mcpValidateRttiPointerSize(int pointerSize, QString* error) {
    if (pointerSize == 4 || pointerSize == 8)
        return true;
    if (error)
        *error = QStringLiteral("pointerSize must be 4 or 8");
    return false;
}

QJsonObject mcpLookupRtti(const Provider& prov, uint64_t vtableAddress,
                          int pointerSize, const QString& abi,
                          int maxVtableSlots) {
    const QString normalizedAbi = mcpValidateRttiAbi(abi.isEmpty() ? QStringLiteral("auto") : abi);
    if (normalizedAbi.isEmpty()) {
        RttiInfo info;
        info.vtableAddress = vtableAddress;
        info.error = QStringLiteral("abi must be 'auto', 'msvc', or 'itanium'");
        return mcpRttiInfoToJson(info);
    }

    QString pointerError;
    if (!mcpValidateRttiPointerSize(pointerSize, &pointerError)) {
        RttiInfo info;
        info.vtableAddress = vtableAddress;
        info.error = pointerError;
        return mcpRttiInfoToJson(info);
    }

    if (normalizedAbi == QLatin1String("msvc"))
        return mcpRttiInfoToJson(walkRtti(prov, vtableAddress, pointerSize, maxVtableSlots));

    if (normalizedAbi == QLatin1String("itanium"))
        return mcpRttiInfoToJson(walkRttiItanium(prov, vtableAddress, pointerSize, maxVtableSlots));

    RttiInfo msvc = walkRtti(prov, vtableAddress, pointerSize, maxVtableSlots);
    if (msvc.ok)
        return mcpRttiInfoToJson(msvc);

    RttiInfo itanium = walkRttiItanium(prov, vtableAddress, pointerSize, maxVtableSlots);
    if (itanium.ok)
        return mcpRttiInfoToJson(itanium);

    RttiInfo failed;
    failed.vtableAddress = vtableAddress;
    failed.error = QStringLiteral("MSVC RTTI failed: %1; Itanium RTTI failed: %2")
        .arg(msvc.error.isEmpty() ? QStringLiteral("unknown error") : msvc.error,
             itanium.error.isEmpty() ? QStringLiteral("unknown error") : itanium.error);
    return mcpRttiInfoToJson(failed);
}

} // namespace rcx
