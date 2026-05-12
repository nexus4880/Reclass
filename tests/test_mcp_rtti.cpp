#include "mcp/mcp_rtti.h"
#include "providers/buffer_provider.h"
#include "providers/provider.h"

#include <QByteArray>
#include <QJsonArray>
#include <QTest>
#include <cstring>
#include <utility>

using namespace rcx;

namespace {

static constexpr uint64_t kImageBase = 0x10000;

template<class T>
void writeAt(QByteArray& buf, qsizetype at, T value) {
    std::memcpy(buf.data() + at, &value, sizeof(T));
}

void writeCStr(QByteArray& buf, qsizetype at, const char* s) {
    std::memcpy(buf.data() + at, s, std::strlen(s) + 1);
}

QByteArray buildAddressSpaceWithRtti() {
    QByteArray rtti(0x10000, '\0');
    constexpr uint32_t vtableRva = 0x1000;
    constexpr uint32_t tdFooRva  = 0x1100;
    constexpr uint32_t tdBarRva  = 0x1200;
    constexpr uint32_t tdBazRva  = 0x1300;
    constexpr uint32_t chdRva    = 0x1400;
    constexpr uint32_t bcaRva    = 0x1500;
    constexpr uint32_t bcdFooRva = 0x1600;
    constexpr uint32_t bcdBarRva = 0x1700;
    constexpr uint32_t bcdBazRva = 0x1800;
    constexpr uint32_t colRva    = 0x1900;

    writeAt<uint64_t>(rtti, vtableRva - 8, kImageBase + colRva);
    for (int i = 0; i < 5; i++)
        writeAt<uint64_t>(rtti, vtableRva + (qsizetype)i * 8, kImageBase + 0x100 + (uint64_t)i * 0x10);
    writeAt<uint64_t>(rtti, vtableRva + 5 * 8, (uint64_t)0);

    auto writeTd = [&](uint32_t rva, const char* name) {
        writeAt<uint64_t>(rtti, rva + 0, 0xDEADBEEF);
        writeAt<uint64_t>(rtti, rva + 8, 0);
        writeCStr(rtti, rva + 16, name);
    };
    writeTd(tdFooRva, ".?AVFoo@@");
    writeTd(tdBarRva, ".?AVBar@@");
    writeTd(tdBazRva, ".?AVBaz@@");

    writeAt<uint32_t>(rtti, chdRva + 0x08, 3);
    writeAt<uint32_t>(rtti, chdRva + 0x0C, bcaRva);

    writeAt<uint32_t>(rtti, bcaRva + 0, bcdFooRva);
    writeAt<uint32_t>(rtti, bcaRva + 4, bcdBarRva);
    writeAt<uint32_t>(rtti, bcaRva + 8, bcdBazRva);

    writeAt<uint32_t>(rtti, bcdFooRva + 0, tdFooRva);
    writeAt<uint32_t>(rtti, bcdBarRva + 0, tdBarRva);
    writeAt<uint32_t>(rtti, bcdBazRva + 0, tdBazRva);

    writeAt<uint32_t>(rtti, colRva + 0x00, 1);
    writeAt<uint32_t>(rtti, colRva + 0x0C, tdFooRva);
    writeAt<uint32_t>(rtti, colRva + 0x10, chdRva);
    writeAt<uint32_t>(rtti, colRva + 0x14, (uint32_t)kImageBase);

    QByteArray full(kImageBase + rtti.size(), '\0');
    std::memcpy(full.data() + kImageBase, rtti.constData(), rtti.size());
    return full;
}

class FakeModuleProvider : public BufferProvider {
public:
    FakeModuleProvider(QByteArray data)
        : BufferProvider(std::move(data), QStringLiteral("synthetic")) {}

    QVector<ModuleEntry> enumerateModules() const override {
        return { ModuleEntry{ QStringLiteral("synthetic.dll"),
                              QStringLiteral("synthetic.dll"),
                              kImageBase, 0x10000 } };
    }
};

} // namespace

class TestMcpRtti : public QObject {
    Q_OBJECT
private slots:
    void jsonSerializationShape() {
        RttiInfo info;
        info.ok = true;
        info.abi = QStringLiteral("MSVC");
        info.vtableAddress = 0x1234;
        info.imageBase = 0x1000;
        info.moduleName = QStringLiteral("demo.dll");
        info.completeLocator = 0x1500;
        info.offset = 8;
        info.rawName = QStringLiteral(".?AVFoo@@");
        info.demangledName = QStringLiteral("Foo");
        info.bases.append(RttiBaseClass{QStringLiteral(".?AVBase@@"), QStringLiteral("Base"), 1});
        info.vtable.append(RttiVirtualMethod{2, 0x2000, QStringLiteral("demo!Foo::bar")});

        QJsonObject json = mcpRttiInfoToJson(info);
        QCOMPARE(json.value("ok").toBool(), true);
        QCOMPARE(json.value("abi").toString(), QStringLiteral("MSVC"));
        QCOMPARE(json.value("vtableAddress").toString(), QStringLiteral("0x1234"));
        QCOMPARE(json.value("imageBase").toString(), QStringLiteral("0x1000"));
        QCOMPARE(json.value("completeLocator").toString(), QStringLiteral("0x1500"));
        QCOMPARE(json.value("demangledName").toString(), QStringLiteral("Foo"));
        QCOMPARE(json.value("bases").toArray().first().toObject().value("depth").toInt(), 1);
        QCOMPARE(json.value("vtable").toArray().first().toObject().value("symbol").toString(), QStringLiteral("demo!Foo::bar"));
    }

    void invalidPointerSizeRejected() {
        QString error;
        QVERIFY(mcpValidateRttiPointerSize(4, &error));
        QVERIFY(mcpValidateRttiPointerSize(8, &error));
        QVERIFY(!mcpValidateRttiPointerSize(16, &error));
        QVERIFY(error.contains(QStringLiteral("4 or 8")));
    }

    void autoReportsBothFailures() {
        QByteArray data(0x20000, '\0');
        FakeModuleProvider prov(data);
        QJsonObject json = mcpLookupRtti(prov, kImageBase + 0x1000, 8, QStringLiteral("auto"), 64);
        QCOMPARE(json.value("ok").toBool(), false);
        const QString error = json.value("error").toString();
        QVERIFY(error.contains(QStringLiteral("MSVC RTTI failed:")));
        QVERIFY(error.contains(QStringLiteral("Itanium RTTI failed:")));
    }

    void syntheticLookupReturnsNameAndVtable() {
        FakeModuleProvider prov(buildAddressSpaceWithRtti());
        QJsonObject json = mcpLookupRtti(prov, kImageBase + 0x1000, 8, QStringLiteral("auto"), 16);
        QVERIFY2(json.value("ok").toBool(), qPrintable(json.value("error").toString()));
        QCOMPARE(json.value("abi").toString(), QStringLiteral("MSVC"));
        QCOMPARE(json.value("moduleName").toString(), QStringLiteral("synthetic.dll"));
        QCOMPARE(json.value("demangledName").toString(), QStringLiteral("Foo"));
        QCOMPARE(json.value("bases").toArray().size(), 3);
        QCOMPARE(json.value("vtable").toArray().size(), 5);
        QCOMPARE(json.value("vtable").toArray().first().toObject().value("address").toString(), QStringLiteral("0x10100"));
    }
};

QTEST_MAIN(TestMcpRtti)
#include "test_mcp_rtti.moc"
