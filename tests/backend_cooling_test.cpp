#include "backend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <limits>

class BackendCoolingTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir directory;
    QLocalServer server;
    QPointer<QLocalSocket> stream;
    QList<QJsonObject> requests;
    QList<QByteArray> wireRequests;
    std::unique_ptr<Backend> backend;

    static QVariantList curve(int maximum = 4000)
    {
        QVariantList points;
        for (int i = 0; i < 8; ++i)
            points.append(QVariantMap{{"temp_c", 30 + 10 * i},
                                      {"rpm", i == 7 ? maximum : 500}});
        return points;
    }

    bool submitCurve(const QVariantList &points)
    {
        bool accepted = false;
        const bool invoked = QMetaObject::invokeMethod(backend.get(), "setCustomCurve",
            Q_RETURN_ARG(bool, accepted), Q_ARG(QVariantList, points));
        if (!invoked)
            QTest::qFail("setCustomCurve must be a public Qt slot", __FILE__, __LINE__);
        return accepted;
    }

    void report(const QJsonObject &status)
    {
        QSignalSpy changed(backend.get(), &Backend::changed);
        stream->write(QJsonDocument(status).toJson(QJsonDocument::Compact) + '\n');
        stream->flush();
        QTRY_COMPARE(changed.count(), 1);
    }

private slots:
    void init()
    {
        QVERIFY(directory.isValid());
        QVERIFY(server.listen(directory.filePath("daemon.sock")));
        connect(&server, &QLocalServer::newConnection, this, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                    while (socket->canReadLine()) {
                        QJsonParseError error;
                        const auto line = socket->readLine();
                        const auto document = QJsonDocument::fromJson(line, &error);
                        wireRequests.append(line);
                        QCOMPARE(error.error, QJsonParseError::NoError);
                        QVERIFY(document.isObject());
                        const auto request = document.object();
                        if (request.value("request") == QLatin1String("subscribe"))
                            stream = socket;
                        requests.append(request);
                    }
                });
            }
        });
        backend = std::make_unique<Backend>();
        backend->setSocketPath(server.fullServerName());
        backend->start();
        QTRY_COMPARE(requests.size(), 5);
        QVERIFY(stream);
        requests.clear();
        wireRequests.clear();
    }

    void cleanup()
    {
        backend.reset();
        server.close();
        server.disconnect(this);
        const auto sockets = server.findChildren<QLocalSocket *>();
        for (auto *socket : sockets)
            delete socket;
        requests.clear();
        wireRequests.clear();
    }

    void customCurveValid_data()
    {
        QTest::addColumn<int>("supplyMax");
        QTest::addColumn<int>("maximum");
        QTest::newRow("full-boundaries") << 4000 << 4000;
        QTest::newRow("supply-boundary") << 2700 << 2700;
        QTest::newRow("hardware-ceiling") << 5000 << 4000;
        QTest::newRow("minimum-supply") << 500 << 500;
    }

    void customCurveValid()
    {
        QFETCH(int, supplyMax);
        QFETCH(int, maximum);
        report({{"reply", "status"}, {"supply_max_rpm", supplyMax},
                {"fixed_gear", true}, {"curve_preset", "quiet"}});
        const auto points = curve(maximum);
        QSignalSpy changed(backend.get(), &Backend::changed);
        QVERIFY(submitCurve(points));
        QTRY_COMPARE(requests.size(), 1);
        const QByteArray expected =
            "{\"points\":[{\"rpm\":500,\"temp_c\":30},{\"rpm\":500,\"temp_c\":40},"
            "{\"rpm\":500,\"temp_c\":50},{\"rpm\":500,\"temp_c\":60},"
            "{\"rpm\":500,\"temp_c\":70},{\"rpm\":500,\"temp_c\":80},"
            "{\"rpm\":500,\"temp_c\":90},{\"rpm\":" + QByteArray::number(maximum) +
            ",\"temp_c\":100}],\"preset\":\"custom\",\"request\":\"set_curve_preset\"}\n";
        QCOMPARE(wireRequests, QList<QByteArray>{expected});
        QCOMPARE(changed.count(), 0);
        QCOMPARE(backend->curvePreset(), QString("quiet"));
        QCOMPARE(backend->currentCurve(), 0);
        QVERIFY(backend->fixedGear());
        QVERIFY(!backend->smartMode());
        QVERIFY(backend->property("curves").toList().isEmpty());
    }

    void customCurveInvalid_data()
    {
        QTest::addColumn<QVariantList>("points");
        QTest::addColumn<int>("supplyMax");
        QTest::newRow("empty") << QVariantList{} << 4000;
        auto points = curve();
        points.removeLast();
        QTest::newRow("seven") << points << 4000;
        points = curve();
        points.append(QVariantMap{{"temp_c", 100}, {"rpm", 4000}});
        QTest::newRow("nine") << points << 4000;
        QTest::newRow("supply-exceeded") << curve(2701) << 2700;
        QTest::newRow("supply-below-minimum") << curve(500) << 499;
        const QList<QPair<QString, QVariant>> invalidValues{
            {"temp_c", 29}, {"temp_c", 101}, {"temp_c", 90}, {"temp_c", 80},
            {"temp_c", 99.5}, {"temp_c", QString("100")}, {"temp_c", true},
            {"temp_c", QVariant()}, {"temp_c", std::numeric_limits<double>::infinity()},
            {"temp_c", std::numeric_limits<double>::quiet_NaN()},
            {"rpm", 499}, {"rpm", 4001}, {"rpm", 500.5},
            {"rpm", QString("500")}, {"rpm", true}, {"rpm", QVariant()},
            {"rpm", std::numeric_limits<double>::infinity()},
            {"rpm", std::numeric_limits<double>::quiet_NaN()}
        };
        for (int i = 0; i < invalidValues.size(); ++i) {
            points = curve();
            auto point = points.last().toMap();
            point[invalidValues[i].first] = invalidValues[i].second;
            points.last() = point;
            QTest::newRow(qPrintable(QString("invalid-field-%1").arg(i))) << points << 4000;
        }
        for (const auto &key : {QString("temp_c"), QString("rpm")}) {
            points = curve();
            auto point = points.last().toMap();
            point.remove(key);
            points.last() = point;
            QTest::newRow(qPrintable("missing-" + key)) << points << 4000;
        }
        points = curve();
        points.last() = QString("not a point");
        QTest::newRow("not-a-map") << points << 4000;
    }

    void customCurveInvalid()
    {
        QFETCH(QVariantList, points);
        QFETCH(int, supplyMax);
        const QJsonArray reported{QJsonObject{{"name", "unmapped"},
            {"points", QJsonArray::fromVariantList(curve())}}};
        report({{"reply", "status"}, {"supply_max_rpm", supplyMax},
                {"curve_preset", "performance"}, {"smart_mode", true}, {"curves", reported}});
        const auto before = backend->property("curves");
        const auto problem = backend->problem();
        QSignalSpy changed(backend.get(), &Backend::changed);
        QVERIFY(!submitCurve(points));
        QTest::qWait(30);
        QVERIFY(requests.isEmpty());
        QVERIFY(wireRequests.isEmpty());
        QCOMPARE(changed.count(), 0);
        QCOMPARE(backend->property("curves"), before);
        QCOMPARE(backend->curvePreset(), QString("performance"));
        QCOMPARE(backend->currentCurve(), 0);
        QVERIFY(backend->smartMode());
        QCOMPARE(backend->problem(), problem);
    }

    void customCurveAllowsFallingRpm()
    {
        auto points = curve(500);
        auto point = points.first().toMap();
        point["rpm"] = 4000.0; // QML numbers may be integral doubles.
        point["temp_c"] = 30.0;
        points.first() = point;
        QVERIFY(submitCurve(points));
        QTRY_COMPARE(requests.size(), 1);
        QCOMPARE(requests.first(), (QJsonObject{{"request", "set_curve_preset"},
            {"preset", "custom"}, {"points", QJsonArray::fromVariantList(points)}}));
    }

    void customCurveDaemonError()
    {
        report({{"reply", "status"}, {"fixed_gear", true}, {"curve_preset", "quiet"}});
        QVERIFY(submitCurve(curve()));
        QTRY_COMPARE(requests.size(), 1);
        const QString message = "preset must be quiet, performance, or custom with valid points";
        report({{"reply", "error"}, {"message", message}});
        QCOMPARE(backend->problem(), message);
        QCOMPARE(backend->curvePreset(), QString("quiet"));
        QVERIFY(backend->fixedGear());
        QVERIFY(!backend->smartMode());
        QVERIFY(backend->property("curves").toList().isEmpty());
        QCOMPARE(requests.size(), 1);
    }

    void customCurveDisconnected()
    {
        // A fresh Backend never starts and cannot contact the real daemon.
        backend = std::make_unique<Backend>();
        QSignalSpy changed(backend.get(), &Backend::changed);
        QVERIFY(!submitCurve(curve()));
        QCOMPARE(changed.count(), 0);
        QVERIFY(requests.isEmpty());
        QVERIFY(backend->property("curves").toList().isEmpty());
        QVERIFY(backend->problem().isEmpty());
    }

    void curveReadback()
    {
        QVERIFY(backend->property("curves").isValid());
        QVERIFY(backend->property("curves").toList().isEmpty());
        const QJsonArray reported{QJsonObject{{"name", "cpu"},
            {"points", QJsonArray::fromVariantList(curve())}},
            QJsonObject{{"name", "gpu"}, {"points", QJsonArray::fromVariantList(curve(2700))}}};
        report({{"reply", "config"}, {"writable", false}, {"config", QJsonObject{
            {"curve_preset", "custom"}, {"curves", reported}, {"standby", "instant"},
            {"smart_mode", true}, {"current_curve", 2}}}});
        QCOMPARE(backend->property("curves").toList(), reported.toVariantList());
        QCOMPARE(backend->curvePreset(), QString("custom"));
        QCOMPARE(backend->currentCurve(), 2);
        QVERIFY(!backend->configWritable());
        QCOMPARE(backend->standby(), QString("instant"));
        QVERIFY(backend->smartMode());
        // A status without curves must not erase a configuration readback.
        report({{"reply", "status"}, {"curve_preset", "quiet"}});
        QCOMPARE(backend->property("curves").toList(), reported.toVariantList());
        report({{"reply", "status"}, {"curves", QJsonArray{reported.last()}}});
        QCOMPARE(backend->property("curves").toList(), QJsonArray{reported.last()}.toVariantList());
        report({{"reply", "config"}, {"config", QJsonObject{}}});
        QVERIFY(backend->property("curves").toList().isEmpty());
        QVERIFY(backend->curvePreset().isEmpty());
        QVERIFY(requests.isEmpty());
    }

    void presetRequests_data()
    {
        QTest::addColumn<int>("preset");
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("smart");
        QTest::newRow("quiet") << 0 << QString("quiet") << false;
        QTest::newRow("performance") << 1 << QString("performance") << false;
        QTest::newRow("custom") << 2 << QString("custom") << false;
        QTest::newRow("already-smart") << 1 << QString("performance") << true;
        QTest::newRow("clamp-low") << -1 << QString("quiet") << false;
        QTest::newRow("clamp-high") << 3 << QString("custom") << false;
    }

    void presetRequests()
    {
        QFETCH(int, preset);
        QFETCH(QString, name);
        QFETCH(bool, smart);
        const int initial = preset == 1 ? 0 : 1;
        report({{"reply", "config"}, {"config", QJsonObject{
            {"current_curve", initial}, {"smart_mode", smart}}}});
        QSignalSpy changed(backend.get(), &Backend::changed);
        backend->setCurvePreset(preset);
        QTRY_COMPARE(requests.size(), smart ? 1 : 2);
        QCOMPARE(wireRequests.first(), "{\"preset\":\"" + name.toUtf8() +
            "\",\"request\":\"set_curve_preset\"}\n");
        if (!smart)
            QCOMPARE(wireRequests.last(), QByteArray("{\"request\":\"set_automatic\"}\n"));
        QCOMPARE(backend->currentCurve(), qBound(0, preset, 2));
        QCOMPARE(backend->smartMode(), smart);
        QCOMPARE(changed.count(), 1);
        backend->setCurvePreset(preset);
        QTest::qWait(30);
        QCOMPARE(requests.size(), smart ? 1 : 2);
        QCOMPARE(changed.count(), 1);
    }

    void initialState()
    {
        QCOMPARE(backend->property("smartMode"), QVariant(false));
        QCOMPARE(backend->property("fixedGear"), QVariant(false));
        QCOMPARE(backend->property("curvePreset"), QVariant(QString()));
    }

    void statusReadback_data()
    {
        QTest::addColumn<QJsonObject>("fields");
        QTest::addColumn<bool>("smart");
        QTest::addColumn<bool>("fixed");
        QTest::addColumn<bool>("manual");
        QTest::addColumn<QString>("preset");
        QTest::newRow("smart") << QJsonObject{{"smart_mode", true}, {"fixed_gear", false},
            {"manual", false}, {"curve_preset", "performance"}}
            << true << false << false << QString("performance");
        QTest::newRow("fixed-with-persisted-preset") << QJsonObject{{"smart_mode", false},
            {"fixed_gear", true}, {"manual", false}, {"curve_preset", "quiet"}}
            << false << true << false << QString("quiet");
        QTest::newRow("manual") << QJsonObject{{"smart_mode", false}, {"fixed_gear", false},
            {"manual", true}, {"curve_preset", QJsonValue()}}
            << false << false << true << QString();
        QTest::newRow("older-daemon") << QJsonObject{{"manual", false}}
            << false << false << false << QString();
        QTest::newRow("preset-only") << QJsonObject{{"curve_preset", "custom"}}
            << false << false << false << QString("custom");
        QTest::newRow("invalid-types") << QJsonObject{{"smart_mode", "true"},
            {"fixed_gear", 1}, {"curve_preset", false}}
            << false << false << false << QString();
    }

    void statusReadback()
    {
        QFETCH(QJsonObject, fields);
        QFETCH(bool, smart);
        QFETCH(bool, fixed);
        QFETCH(bool, manual);
        QFETCH(QString, preset);
        // Every reply replaces stale fields, including when an older daemon omits them.
        report({{"reply", "status"}, {"smart_mode", true}, {"fixed_gear", true},
                {"curve_preset", "stale"}, {"manual", true}});
        fields.insert("reply", "status");
        fields.insert("target_rpm", 1700);
        fields.insert("manual_rpm", 1200);
        report(fields);
        QCOMPARE(backend->property("smartMode"), QVariant(smart));
        QCOMPARE(backend->property("fixedGear"), QVariant(fixed));
        QCOMPARE(backend->property("curvePreset"), QVariant(preset));
        QCOMPARE(backend->manual(), manual);
        QCOMPARE(backend->targetRpm(), 1700);
        QCOMPARE(backend->manualRpm(), 1200);
        QVERIFY(requests.isEmpty());
    }

    void coolingRequests_data()
    {
        QTest::addColumn<int>("operation");
        QTest::addColumn<QJsonObject>("expected");
        QTest::newRow("automatic") << 0 << QJsonObject{{"request", "set_automatic"}};
        QTest::newRow("stop") << 1 << QJsonObject{{"request", "set_manual"}, {"rpm", 0}};
        QTest::newRow("hold") << 1 << QJsonObject{{"request", "set_manual"}, {"rpm", 1800}};
        QTest::newRow("release") << 2 << QJsonObject{{"request", "set_manual"}, {"rpm", QJsonValue()}};
        QTest::newRow("quiet") << 3 << QJsonObject{{"request", "set_gear"}, {"gear", "quiet"}, {"rpm", 1700}};
        QTest::newRow("standard") << 3 << QJsonObject{{"request", "set_gear"}, {"gear", "standard"}, {"rpm", 2400}};
        QTest::newRow("strong") << 3 << QJsonObject{{"request", "set_gear"}, {"gear", "strong"}, {"rpm", 3000}};
        QTest::newRow("overclock") << 3 << QJsonObject{{"request", "set_gear"}, {"gear", "overclock"}, {"rpm", 3700}};
    }

    void coolingRequests()
    {
        QFETCH(int, operation);
        QFETCH(QJsonObject, expected);
        report({{"reply", "status"}, {"smart_mode", false}, {"fixed_gear", true},
                {"manual", false}, {"target_rpm", 1700}, {"curve_preset", "quiet"}});
        const auto smart = backend->property("smartMode");
        const auto fixed = backend->property("fixedGear");
        const auto preset = backend->property("curvePreset");
        QSignalSpy changed(backend.get(), &Backend::changed);
        switch (operation) {
        case 0: QVERIFY(QMetaObject::invokeMethod(backend.get(), "setAutomatic")); break;
        case 1: backend->holdSpeed(expected.value("rpm").toInt()); break;
        case 2: backend->releaseManual(); break;
        case 3: backend->writeGear(expected.value("gear").toString(), expected.value("rpm").toInt()); break;
        }
        QTRY_COMPARE(requests.size(), 1);
        QCOMPARE(requests.first(), expected);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(backend->property("smartMode"), smart);
        QCOMPARE(backend->property("fixedGear"), fixed);
        QCOMPARE(backend->property("curvePreset"), preset);
        QVERIFY(!backend->manual());
        QCOMPARE(backend->targetRpm(), 1700);
    }

    void lightingColors()
    {
        QCOMPARE(backend->property("lightColorLimit"), QVariant(1));
        QCOMPARE(backend->property("lightColors").toStringList(), QStringList{"#22D3EE"});
        bool accepted = false;
        QSignalSpy changed(backend.get(), &Backend::changed);
        QVERIFY(QMetaObject::invokeMethod(backend.get(), "setLightColors",
            Q_RETURN_ARG(bool, accepted), Q_ARG(QStringList, QStringList{"#a1b2c3"})));
        QVERIFY(accepted);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(backend->lightColor(), QString("#A1B2C3"));
        QCOMPARE(backend->property("lightColors").toStringList(), QStringList{"#A1B2C3"});
        QCOMPARE(backend->lightMode(), QString("static"));
        QVERIFY(backend->stripOn());
        QTRY_COMPARE(requests.size(), 1);
        QCOMPARE(wireRequests, (QList<QByteArray>{
            "{\"lighting\":{\"brightness\":100,\"indicators\":true,\"mode\":{\"color\":{\"b\":195,\"g\":178,\"r\":161},\"mode\":\"static\"}},\"request\":\"set_lighting\"}\n"}));
        report({{"reply", "status"}, {"lighting", QJsonObject{
            {"mode", QJsonObject{{"mode", "static"}, {"color", QJsonObject{
                {"r", 18}, {"g", 52}, {"b", 86}}}}}, {"brightness", 65}, {"indicators", false}}}});
        QCOMPARE(backend->property("lightColors").toStringList(), QStringList{"#123456"});
        QCOMPARE(backend->lightBrightness(), 65);
        QVERIFY(!backend->lightIndicators());
        QCOMPARE(requests.size(), 1);
    }

    void lightingColorsRejected_data()
    {
        QTest::addColumn<QStringList>("colors");
        QTest::newRow("empty") << QStringList{};
        QTest::newRow("multi-color-unsupported") << QStringList{"#123456", "#ABCDEF"};
        QTest::newRow("invalid-last-color") << QStringList{"#123456", "invalid"};
        QTest::newRow("not-hex") << QStringList{"#GGHHII"};
        QTest::newRow("short") << QStringList{"#123"};
        QTest::newRow("alpha") << QStringList{"#FF123456"};
        QTest::newRow("named") << QStringList{"red"};
        QTest::newRow("missing-hash") << QStringList{"123456"};
    }

    void lightingColorsRejected()
    {
        QFETCH(QStringList, colors);
        report({{"reply", "status"}, {"lighting", QJsonObject{
            {"mode", QJsonObject{{"mode", "effect"}, {"effect", 3}}},
            {"brightness", 65}, {"indicators", false}}}});
        const auto before = backend->property("lightColors");
        const auto problem = backend->problem();
        const bool strip = backend->stripOn();
        QSignalSpy changed(backend.get(), &Backend::changed);
        bool accepted = true;
        QVERIFY(QMetaObject::invokeMethod(backend.get(), "setLightColors",
            Q_RETURN_ARG(bool, accepted), Q_ARG(QStringList, colors)));
        QVERIFY(!accepted);
        QTest::qWait(200);
        QVERIFY(wireRequests.isEmpty());
        QVERIFY(requests.isEmpty());
        QCOMPARE(changed.count(), 0);
        QCOMPARE(backend->property("lightColors"), before);
        QCOMPARE(backend->lightColor(), QString("#22D3EE"));
        QCOMPARE(backend->lightMode(), QString("effect"));
        QCOMPARE(backend->lightEffect(), 3);
        QCOMPARE(backend->lightBrightness(), 65);
        QVERIFY(!backend->lightIndicators());
        QCOMPARE(backend->stripOn(), strip);
        QCOMPARE(backend->problem(), problem);
    }

    void lightingEnabledReadback()
    {
        QVERIFY(!backend->stripOn()); // No reported configuration yet: default mode is off.
        for (const auto &mode : {QString("effect"), QString("off"), QString("static"), QString("off")}) {
            report({{"reply", "status"}, {"lighting", QJsonObject{
                {"mode", QJsonObject{{"mode", mode}}}}}});
            QCOMPARE(backend->stripOn(), mode != "off");
            QCOMPARE(backend->lightMode(), mode);
        }
        report({{"reply", "status"}});
        QVERIFY(!backend->stripOn());
        report({{"reply", "status"}, {"lighting", QJsonObject{
            {"mode", QJsonObject{{"mode", "unrecognized"}}}}}});
        QVERIFY(!backend->stripOn());
        QVERIFY(requests.isEmpty());
    }

    void lightingLegacyRequests_data()
    {
        QTest::addColumn<int>("operation");
        QTest::addColumn<int>("value");
        QTest::addColumn<QByteArray>("modeJson");
        QTest::addColumn<int>("brightness");
        QTest::addColumn<bool>("indicators");
        const QByteArray color = "{\"color\":{\"b\":86,\"g\":52,\"r\":18},\"mode\":\"static\"}";
        QTest::newRow("mode") << 0 << 0 << QByteArray("{\"mode\":\"off\"}") << 65 << false;
        QTest::newRow("single-color") << 1 << 0 << QByteArray("{\"color\":{\"b\":0,\"g\":0,\"r\":255},\"mode\":\"static\"}") << 65 << false;
        QTest::newRow("effect-low") << 2 << 0 << QByteArray("{\"effect\":1,\"mode\":\"effect\"}") << 65 << false;
        QTest::newRow("effect-high") << 2 << 6 << QByteArray("{\"effect\":5,\"mode\":\"effect\"}") << 65 << false;
        QTest::newRow("brightness-low") << 3 << -1 << color << 0 << false;
        QTest::newRow("brightness-high") << 3 << 101 << color << 100 << false;
        QTest::newRow("indicators") << 4 << 1 << color << 65 << true;
        QTest::newRow("strip-off") << 5 << 0 << QByteArray("{\"mode\":\"off\"}") << 65 << false;
    }

    void lightingLegacyRequests()
    {
        QFETCH(int, operation);
        QFETCH(int, value);
        QFETCH(QByteArray, modeJson);
        QFETCH(int, brightness);
        QFETCH(bool, indicators);
        report({{"reply", "status"}, {"lighting", QJsonObject{
            {"mode", QJsonObject{{"mode", "static"}, {"color", QJsonObject{
                {"r", 18}, {"g", 52}, {"b", 86}}}}}, {"brightness", 65}, {"indicators", false}}}});
        switch (operation) {
        case 0: backend->setLightMode("off"); break;
        case 1: backend->setLightColor("red"); break; // Legacy QColor names remain accepted.
        case 2: backend->setLightEffect(value); break;
        case 3: backend->setLightBrightness(value); break;
        case 4: backend->setLightIndicators(value); break;
        case 5: backend->setStrip(value); break;
        }
        QTRY_COMPARE(requests.size(), 1);
        QJsonObject sent = QJsonDocument::fromJson(wireRequests.first()).object();
        QJsonObject lighting = sent.value("lighting").toObject();
        QCOMPARE(lighting.value("brightness").toInt(), brightness);
        QCOMPARE(lighting.value("indicators").toBool(), indicators);
        QCOMPARE(lighting.value("mode").toObject(), QJsonDocument::fromJson(modeJson).object());
        if (operation == 2) {
            QCOMPARE(lighting.value("speed").toInt(), 500);
            QCOMPARE(lighting.value("ledCount").toInt(), 6);
            QCOMPARE(lighting.value("colors").toArray().size(), 1);
        } else {
            QVERIFY(!lighting.contains("speed"));
            QVERIFY(!lighting.contains("ledCount"));
            QVERIFY(!lighting.contains("colors"));
        }
        QCOMPARE(backend->stripOn(), backend->lightMode() != "off");
        QCOMPARE(backend->property("lightColors").toStringList(),
            QStringList{operation == 1 ? "#FF0000" : "#123456"});
    }

    void lightingPendingAndEnablePreserved()
    {
        backend->setLightColor("#123456");
        backend->setLightEffect(4);
        report({{"reply", "status"}, {"lighting", QJsonObject{
            {"mode", QJsonObject{{"mode", "off"}}}, {"brightness", 0}}}});
        QCOMPARE(backend->lightMode(), QString("effect"));
        QVERIFY(backend->stripOn());
        QCOMPARE(backend->lightBrightness(), 100);
        QTRY_COMPARE(requests.size(), 1); // Edits coalesce and pending state ignores stale readback.
        QJsonObject effectLighting = QJsonDocument::fromJson(wireRequests.first()).object().value("lighting").toObject();
        QCOMPARE(effectLighting.value("brightness").toInt(), 100);
        QCOMPARE(effectLighting.value("indicators").toBool(), true);
        QCOMPARE(effectLighting.value("mode").toObject().value("effect").toInt(), 4);
        QCOMPARE(effectLighting.value("speed").toInt(), 500);
        QCOMPARE(effectLighting.value("ledCount").toInt(), 6);
        QCOMPARE(effectLighting.value("colors").toArray().size(), 1);
        backend->setStrip(true); // Legacy enable selects static, not the previous effect.
        QTRY_COMPARE(requests.size(), 2);
        QCOMPARE(wireRequests.last(), QByteArray(
            "{\"lighting\":{\"brightness\":100,\"indicators\":true,\"mode\":{\"color\":{\"b\":86,\"g\":52,\"r\":18},\"mode\":\"static\"}},\"request\":\"set_lighting\"}\n"));
        backend->setLightMode("invalid");
        backend->setLightColor("invalid");
        backend->setStrip(true);
        QTest::qWait(200);
        QCOMPARE(requests.size(), 2);
        QCOMPARE(backend->lightMode(), QString("static"));
        QCOMPARE(backend->lightColor(), QString("#123456"));
    }

    void lightingReadbackPreserved()
    {
        report({{"reply", "status"}, {"strip_on", true}, {"lighting", QJsonObject{
            {"mode", QJsonObject{{"mode", "static"}, {"color", QJsonObject{
                {"r", 18}, {"g", 52}, {"b", 86}}}}}, {"brightness", 65}, {"indicators", false}}}});
        QCOMPARE(backend->lightMode(), QString("static"));
        QCOMPARE(backend->lightColor(), QString("#123456"));
        QCOMPARE(backend->lightBrightness(), 65);
        QVERIFY(!backend->lightIndicators());
        QVERIFY(backend->stripOn());
        backend->setLightBrightness(70);
        QTRY_COMPARE(requests.size(), 1);
        QCOMPARE(requests.first(), (QJsonObject{{"request", "set_lighting"},
            {"lighting", QJsonObject{{"mode", QJsonObject{{"mode", "static"},
                {"color", QJsonObject{{"r", 18}, {"g", 52}, {"b", 86}}}}},
                {"brightness", 70}, {"indicators", false}}}}));
    }
};

QTEST_GUILESS_MAIN(BackendCoolingTest)
#include "backend_cooling_test.moc"
