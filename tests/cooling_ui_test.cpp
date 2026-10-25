#include "backend.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

class CoolingUiTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir directory;
    QLocalServer server;
    QPointer<QLocalSocket> stream;
    QList<QByteArray> requests;
    std::unique_ptr<Backend> backend;
    std::unique_ptr<QQuickView> view;
    const QStringList gearNames{"quiet", "standard", "strong", "overclock"};
    QList<QColor> inactiveGearColors;

    // Repeater delegates belong to the visual tree, not necessarily the QObject tree.
    QQuickItem *item(const QString &name, QQuickItem *parent = nullptr) const
    {
        if (!parent)
            parent = view->contentItem();
        if (!parent)
            return nullptr;
        if (parent->objectName() == name)
            return parent;
        for (auto *child : parent->childItems()) {
            if (auto *found = item(name, child))
                return found;
        }
        return nullptr;
    }

    void report(const QJsonObject &reply)
    {
        QSignalSpy changed(backend.get(), &Backend::changed);
        stream->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
        stream->flush();
        QTRY_COMPARE(changed.count(), 1);
    }

    void checkUi(bool smart, bool manual, const QString &gear, const QString &status)
    {
        auto *automatic = item("automaticMode");
        auto *manualMode = item("manualMode");
        QVERIFY(automatic);
        QVERIFY(manualMode);
        QCOMPARE(automatic->property("selected").toBool(), smart);
        QCOMPARE(manualMode->property("selected").toBool(), manual);
        // Check the rendered rectangle properties too, not just the selection flags.
        QCOMPARE(automatic->property("color").value<QColor>().alpha() > 0, smart);
        QCOMPARE(manualMode->property("color").value<QColor>().alpha() > 0, manual);
        for (int i = 0; i < gearNames.size(); ++i) {
            auto *card = item("gear_" + gearNames[i]);
            QVERIFY(card);
            QCOMPARE(card->property("active").toBool(), gearNames[i] == gear);
            QCOMPARE(card->property("color").value<QColor>() != inactiveGearColors[i],
                     gearNames[i] == gear);
        }
        auto *label = item("coolingStatus");
        QVERIFY(label);
        QCOMPARE(label->property("text").toString(), status);
    }

    void click(const QString &name)
    {
        auto *control = item(name);
        QVERIFY2(control, qPrintable(name));
        QVERIFY(control->isVisible());
        QVERIFY(control->isEnabled());
        const QPoint position = control->mapToScene(
            QPointF(control->width() / 2, control->height() / 2)).toPoint();
        QVERIFY(QRect(QPoint(), view->size()).contains(position));
        QTest::mouseClick(view.get(), Qt::LeftButton, Qt::NoModifier, position);
    }

    void openDraft()
    {
        report({{"reply", "status"}, {"connected", true}, {"fixed_gear", true},
                {"target_rpm", 1700}, {"curve_preset", "quiet"}, {"supply_max_rpm", 2700}});
        click("editCustomCurve");
        QTRY_VERIFY(item("customCurveApply") && item("customCurveApply")->isVisible());
        QVERIFY(!item("customCurveApply")->isEnabled());
        QVERIFY(!item("curveTemperature_8"));
        QVERIFY(!item("curveRpm_8"));
        // Enter through the rendered fields, including a falling RPM segment.
        for (int i = 0; i < 8; ++i) {
            const auto name = QString("curveRpm_%1").arg(i);
            click(name);
            QTest::keyClick(view.get(), Qt::Key_A, Qt::ControlModifier);
            for (const QChar digit : QString::number(i == 0 ? 2700 : 500))
                QTest::keyClick(view.get(), static_cast<Qt::Key>(Qt::Key_0 + digit.digitValue()));
        }
        QTRY_VERIFY(item("customCurveApply")->isEnabled());
        QVERIFY(requests.isEmpty());
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
                        const auto line = socket->readLine();
                        QJsonParseError error;
                        const auto document = QJsonDocument::fromJson(line, &error);
                        QCOMPARE(error.error, QJsonParseError::NoError);
                        QVERIFY(document.isObject());
                        if (document.object().value("request") == QLatin1String("subscribe"))
                            stream = socket;
                        requests.append(line);
                    }
                });
            }
        });
        backend = std::make_unique<Backend>();
        // Set before start: neither command nor subscription can reach a real daemon.
        backend->setSocketPath(server.fullServerName());
        backend->start();
        QTRY_COMPARE(requests.size(), 5);
        QVERIFY(stream);
        QCOMPARE(backend->socketPath(), server.fullServerName());
        requests.clear();

        view = std::make_unique<QQuickView>();
        // Prefer this executable's module over the application's adjacent build module.
        view->engine()->addImportPath("qrc:/qt/qml");
        view->rootContext()->setContextProperty("backend", backend.get());
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->resize(640, 800);
        view->setSource(QUrl("qrc:/qt/qml/FlydigiSpace/qml/Controls.qml"));
        QCOMPARE(view->status(), QQuickView::Ready);
        QVERIFY(view->rootObject());
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));

        report({{"reply", "gears"}, {"gears", QJsonArray{
            QJsonObject{{"name", "quiet"}, {"rpm", 1700}, {"allowed", true}},
            QJsonObject{{"name", "standard"}, {"rpm", 2400}, {"allowed", true}},
            QJsonObject{{"name", "strong"}, {"rpm", 3000}, {"allowed", true}},
            QJsonObject{{"name", "overclock"}, {"rpm", 3700}, {"allowed", true}}
        }}});
        for (const auto &name : gearNames) {
            auto *card = item("gear_" + name);
            QVERIFY(card);
            QVERIFY(!card->property("active").toBool());
            inactiveGearColors.append(card->property("color").value<QColor>());
        }
        checkUi(false, false, {}, "cooling mode not reported as active");
    }

    void cleanup()
    {
        view.reset();
        backend.reset();
        server.close();
        server.disconnect(this);
        const auto sockets = server.findChildren<QLocalSocket *>();
        for (auto *socket : sockets)
            delete socket;
        requests.clear();
        inactiveGearColors.clear();
    }

    void automaticClickWaitsForStatus()
    {
        report({{"reply", "status"}, {"fixed_gear", true}, {"target_rpm", 1700},
                {"curve_preset", "quiet"}});
        checkUi(false, false, "quiet", "fixed gear: 1700 rpm");
        auto *mouse = item("automaticMouseArea");
        QVERIFY(mouse);
        QVERIFY(mouse->isVisible());
        QVERIFY(mouse->isEnabled());
        QVERIFY(mouse->width() > 0 && mouse->height() > 0);
        const QPoint position = mouse->mapToScene(
            QPointF(mouse->width() / 2, mouse->height() / 2)).toPoint();
        QVERIFY(view->geometry().size().isValid());
        QVERIFY(QRect(QPoint(), view->size()).contains(position));
        QSignalSpy changed(backend.get(), &Backend::changed);
        QTest::mouseClick(view.get(), Qt::LeftButton, Qt::NoModifier, position);
        QTRY_COMPARE(requests.size(), 1);
        QCOMPARE(requests.first(), QByteArray("{\"request\":\"set_automatic\"}\n"));
        QCOMPARE(changed.count(), 0);
        checkUi(false, false, "quiet", "fixed gear: 1700 rpm");

        report({{"reply", "status"}, {"smart_mode", true}, {"target_rpm", 1700}});
        checkUi(true, false, {}, "Smart Speed active");
        QCOMPARE(requests.size(), 1);
    }

    void curvePresets()
    {
        report({{"reply", "status"}, {"connected", true}, {"fixed_gear", true},
                {"target_rpm", 1700}, {"curve_preset", "quiet"}});
        QVERIFY(item("curvePreset_quiet"));
        QVERIFY(item("curvePreset_quiet")->property("highlighted").toBool());
        // Preserve the backend's initial index/repeated-selection suppression.
        click("curvePreset_quiet");
        QTest::qWait(20);
        QVERIFY(requests.isEmpty());
        for (const auto &name : {QString("performance"), QString("custom"), QString("quiet")}) {
            requests.clear();
            click("curvePreset_" + name);
            QTRY_COMPARE(requests.size(), 2);
            QCOMPARE(requests[0], QByteArray("{\"preset\":\"") + name.toUtf8()
                     + "\",\"request\":\"set_curve_preset\"}\n");
            QCOMPARE(requests[1], QByteArray("{\"request\":\"set_automatic\"}\n"));
            QVERIFY(item("curvePreset_quiet")->property("highlighted").toBool());
            checkUi(false, false, "quiet", "fixed gear: 1700 rpm");
            click("curvePreset_" + name);
            QTest::qWait(20);
            QCOMPARE(requests.size(), 2);
        }
        report({{"reply", "status"}, {"connected", true}, {"smart_mode", true},
                {"curve_preset", "performance"}});
        QVERIFY(item("curvePreset_performance")->property("highlighted").toBool());
        QVERIFY(!item("curvePreset_quiet")->property("highlighted").toBool());
        checkUi(true, false, {}, "Smart Speed active");
    }

    void customCurveSubmission()
    {
        QVERIFY(item("editCustomCurve"));
        openDraft();
        QSignalSpy changed(backend.get(), &Backend::changed);
        click("customCurveApply");
        QTRY_COMPARE(requests.size(), 1);
        QJsonArray points;
        for (int i = 0; i < 8; ++i)
            points.append(QJsonObject{{"temp_c", 30 + i * 10}, {"rpm", i == 0 ? 2700 : 500}});
        QCOMPARE(requests.first(), QJsonDocument(QJsonObject{{"request", "set_curve_preset"},
            {"preset", "custom"}, {"points", points}}).toJson(QJsonDocument::Compact) + '\n');
        QCOMPARE(changed.count(), 0);
        checkUi(false, false, "quiet", "fixed gear: 1700 rpm");
        QCOMPARE(item("customCurveFeedback")->property("text").toString(),
                 QString("Submitted; awaiting daemon confirmation."));
        report({{"reply", "error"}, {"message", "curve rejected"}});
        QCOMPARE(item("customCurveProblem")->property("text").toString(), QString("curve rejected"));
        checkUi(false, false, "quiet", "fixed gear: 1700 rpm");
        QCOMPARE(requests.size(), 1);
        report({{"reply", "status"}, {"connected", true}, {"smart_mode", true},
                {"curve_preset", "custom"}});
        checkUi(true, false, {}, "Smart Speed active");
        QVERIFY(item("curvePreset_custom")->property("highlighted").toBool());
    }

    void invalidDraft_data()
    {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QString>("text");
        QTest::newRow("empty") << "curveRpm_0" << "";
        QTest::newRow("temp-low") << "curveTemperature_0" << "29";
        QTest::newRow("temp-high") << "curveTemperature_7" << "101";
        QTest::newRow("duplicate") << "curveTemperature_1" << "30";
        QTest::newRow("descending") << "curveTemperature_2" << "39";
        QTest::newRow("fractional-temp") << "curveTemperature_1" << "40.5";
        QTest::newRow("rpm-low") << "curveRpm_0" << "499";
        QTest::newRow("rpm-high") << "curveRpm_0" << "2701";
        QTest::newRow("fractional-rpm") << "curveRpm_0" << "500.5";
        QTest::newRow("nonnumeric") << "curveRpm_0" << "oops";
    }

    void invalidDraft()
    {
        QFETCH(QString, field);
        QFETCH(QString, text);
        QVERIFY(item("editCustomCurve"));
        openDraft();
        QSignalSpy changed(backend.get(), &Backend::changed);
        QVERIFY(item(field)->setProperty("text", text));
        QTRY_VERIFY(!item("customCurveApply")->isEnabled());
        QTest::qWait(20);
        QVERIFY(requests.isEmpty());
        QCOMPARE(changed.count(), 0);
        QCOMPARE(backend->curvePreset(), QString("quiet"));
        checkUi(false, false, "quiet", "fixed gear: 1700 rpm");
    }

    void draftSurvivesSupplyChangeAndDisconnect()
    {
        QVERIFY(item("editCustomCurve"));
        openDraft();
        report({{"reply", "status"}, {"connected", true}, {"supply_max_rpm", 2000}});
        QTRY_VERIFY(!item("customCurveApply")->isEnabled());
        QCOMPARE(item("curveRpm_0")->property("text").toString(), QString("2700"));
        report({{"reply", "status"}, {"connected", true}, {"supply_max_rpm", 4000}});
        QVERIFY(item("curveRpm_0")->setProperty("text", "4000"));
        QTRY_VERIFY(item("customCurveApply")->isEnabled());
        QVERIFY(item("curveRpm_0")->setProperty("text", "4001"));
        QTRY_VERIFY(!item("customCurveApply")->isEnabled());
        QVERIFY(item("curveRpm_0")->setProperty("text", "4000"));
        report({{"reply", "status"}, {"connected", false}});
        QTRY_VERIFY(!item("customCurveApply")->isEnabled());
        QVERIFY(!item("curvePreset_performance")->isEnabled());
        QCOMPARE(item("curveRpm_0")->property("text").toString(), QString("4000"));
        QVERIFY(requests.isEmpty());
    }

    void closingDraftPreservesManualAndGearControls()
    {
        QVERIFY(item("editCustomCurve"));
        openDraft();
        QTest::keyClick(view.get(), Qt::Key_Escape);
        // Closed popups leave the window's visual tree entirely.
        QTRY_VERIFY(!item("customCurveApply") || !item("customCurveApply")->isVisible());
        QVERIFY(requests.isEmpty());
        click("manualMouseArea");
        QTRY_COMPARE(requests.size(), 1);
        QCOMPARE(requests.first(), QByteArray("{\"request\":\"set_manual\",\"rpm\":1700}\n"));
        checkUi(false, false, "quiet", "fixed gear: 1700 rpm");
        report({{"reply", "status"}, {"manual", true}, {"manual_rpm", 1700}});
        checkUi(false, true, {}, "Linux manual override: 1700 rpm");
        click("gear_standard");
        QTRY_COMPARE(requests.size(), 2);
        QCOMPARE(requests.last(), QByteArray("{\"gear\":\"standard\",\"request\":\"set_gear\",\"rpm\":2400}\n"));
        checkUi(false, true, {}, "Linux manual override: 1700 rpm");
        report({{"reply", "status"}, {"fixed_gear", true}, {"target_rpm", 2400}});
        checkUi(false, false, "standard", "fixed gear: 2400 rpm");
    }

    void reportedModes_data()
    {
        QTest::addColumn<QJsonObject>("fields");
        QTest::addColumn<bool>("smart");
        QTest::addColumn<bool>("manual");
        QTest::addColumn<QString>("gear");
        QTest::addColumn<QString>("status");
        QTest::newRow("smart") << QJsonObject{{"smart_mode", true}}
            << true << false << QString() << QString("Smart Speed active");
        QTest::newRow("fixed-matching") << QJsonObject{{"fixed_gear", true}}
            << false << false << QString("quiet") << QString("fixed gear: 1700 rpm");
        QTest::newRow("fixed-other-gear") << QJsonObject{{"fixed_gear", true}, {"target_rpm", 2400}}
            << false << false << QString("standard") << QString("fixed gear: 2400 rpm");
        QTest::newRow("fixed-mismatch") << QJsonObject{{"fixed_gear", true}, {"target_rpm", 2000}}
            << false << false << QString() << QString("fixed gear: 2000 rpm");
        QTest::newRow("matching-rpm-without-fixed") << QJsonObject{{"fixed_gear", false}}
            << false << false << QString() << QString("cooling mode not reported as active");
        QTest::newRow("manual") << QJsonObject{{"manual", true}, {"manual_rpm", 1250}}
            << false << true << QString() << QString("Linux manual override: 1250 rpm");
        QTest::newRow("manual-suppresses-fixed") << QJsonObject{{"manual", true},
            {"manual_rpm", 1250}, {"fixed_gear", true}}
            << false << true << QString() << QString("Linux manual override: 1250 rpm");
        QTest::newRow("smart-suppresses-fixed") << QJsonObject{{"smart_mode", true}, {"fixed_gear", true}}
            << true << false << QString() << QString("Smart Speed active");
        QTest::newRow("preset-only") << QJsonObject{{"curve_preset", "performance"}}
            << false << false << QString() << QString("cooling mode not reported as active");
        QTest::newRow("older-status") << QJsonObject{{"manual", false}}
            << false << false << QString() << QString("cooling mode not reported as active");
    }

    void reportedModes()
    {
        QFETCH(QJsonObject, fields);
        QFETCH(bool, smart);
        QFETCH(bool, manual);
        QFETCH(QString, gear);
        QFETCH(QString, status);
        // Seed Smart first: absent fields must clear a previously active highlight.
        report({{"reply", "status"}, {"smart_mode", true}});
        checkUi(true, false, {}, "Smart Speed active");
        fields.insert("reply", "status");
        if (!fields.contains("target_rpm"))
            fields.insert("target_rpm", 1700);
        report(fields);
        checkUi(smart, manual, gear, status);
        QVERIFY(requests.isEmpty());
    }
};

QTEST_MAIN(CoolingUiTest)
#include "cooling_ui_test.moc"
