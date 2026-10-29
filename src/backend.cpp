#include "backend.h"

#include <QColor>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace {

/// Resolve the packaged user-service socket first, then retain the legacy
/// system-wide and per-user flydigictl paths for development compatibility.
QString resolveSocket()
{
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty()) {
        const QString packaged = runtime + QStringLiteral("/flydigi-bs2pro-linux.sock");
        if (QFile::exists(packaged))
            return packaged;
    }

    const QString system = QStringLiteral("/run/flydigictl/flydigictl.sock");
    if (QFile::exists(system))
        return system;

    if (!runtime.isEmpty()) {
        const QString legacy = runtime + QStringLiteral("/flydigictl.sock");
        if (QFile::exists(legacy))
            return legacy;
    }

    return system;
}

/// The rows worth showing on a dashboard: the package, the disk, the card.
/// Per-core values are noise at a glance and the cooler's own numbers already
/// come with the status.
QString rowName(const QJsonObject &sensor)
{
    const QString hwmon = sensor.value("hwmon").toString();
    const QString label = sensor.value("label").toString();

    if (label == QLatin1String("Package id 0"))
        return QStringLiteral("CPU");
    if ((hwmon == QLatin1String("nvme") || sensor.value("kind").toString() == QLatin1String("nvme")) && label == QLatin1String("Composite"))
        return QStringLiteral("NVMe");
    if (hwmon == QLatin1String("acpitz") || sensor.value("kind").toString() == QLatin1String("chassis"))
        return QStringLiteral("Chassis");
    if (sensor.value("kind").toString() == QLatin1String("gpu"))
        return label.isEmpty() ? QStringLiteral("GPU") : QStringLiteral("GPU ") + label;
    if (label.isEmpty())
        return hwmon;

    return label;
}

int rowRank(const QJsonObject &sensor)
{
    const QString hwmon = sensor.value("hwmon").toString();
    const QString label = sensor.value("label").toString();

    if (label == QLatin1String("Package id 0"))
        return 0;
    if ((hwmon == QLatin1String("nvme") || sensor.value("kind").toString() == QLatin1String("nvme")) && label == QLatin1String("Composite"))
        return 1;
    if (sensor.value("kind").toString() == QLatin1String("gpu"))
        return 2;
    if (hwmon == QLatin1String("acpitz") || sensor.value("kind").toString() == QLatin1String("chassis"))
        return 3;
    if (hwmon == QLatin1String("coretemp") || hwmon == QLatin1String("k10temp"))
        return 100; // a single core, only reached when nothing better exists

    return 4;
}

/// Chips worth a place on the dashboard. A wifi card or a RAM module reports a
/// temperature too, and neither tells anyone anything about the cooler.
bool worthShowing(const QJsonObject &sensor)
{
    const QString hwmon = sensor.value("hwmon").toString();
    const QString label = sensor.value("label").toString();

    if (sensor.value("kind").toString() == QLatin1String("gpu"))
        return true;
    if (sensor.value("kind").toString() == QLatin1String("cpu") && label == QLatin1String("Package id 0"))
        return true;
    if (hwmon == QLatin1String("coretemp") || hwmon == QLatin1String("k10temp"))
        return label == QLatin1String("Package id 0");
    if (hwmon == QLatin1String("nvme") || sensor.value("kind").toString() == QLatin1String("nvme"))
        return label == QLatin1String("Composite");
    if (hwmon == QLatin1String("acpitz") || sensor.value("kind").toString() == QLatin1String("chassis") || sensor.value("kind").toString() == QLatin1String("gpu"))
        return true;

    return false;
}

/// Two identical labels from the same chip are the same reading twice.
QString rowKey(const QJsonObject &sensor)
{
    return sensor.value("hwmon").toString() + QLatin1Char('/')
        + sensor.value("device").toString() + QLatin1Char('/')
        + sensor.value("label").toString();
}

} // namespace

Backend::Backend(QObject *parent)
    : QObject(parent)
{
    m_reconnect.setInterval(1000);
    m_reconnect.setSingleShot(false);
    connect(&m_reconnect, &QTimer::timeout, this, &Backend::tryConnect);

    m_statusTimer.setInterval(1000);
    m_statusTimer.setSingleShot(false);
    connect(&m_statusTimer, &QTimer::timeout, this, [this] {
        if (m_command.state() == QLocalSocket::ConnectedState)
            send(QJsonObject{{QStringLiteral("request"), QStringLiteral("status")} });
    });

    m_lightTimer.setInterval(140);
    m_lightTimer.setSingleShot(true);
    connect(&m_lightTimer, &QTimer::timeout, this, &Backend::sendLighting);

    connect(&m_command, &QLocalSocket::connected, this, [this] {
        m_problem.clear();
        QJsonArray pending;
        emit changed();
        refreshAll();
    });

    connect(&m_stream, &QLocalSocket::connected, this, [this] {
        m_stream.write(QByteArrayLiteral("{\"request\":\"subscribe\"}\n"));
        m_stream.flush();
        m_connected = true;
        emit changed();
    });

    connect(&m_command, &QLocalSocket::disconnected, this, [this] {
        m_connected = false;
        emit changed();
    });
    connect(&m_stream, &QLocalSocket::disconnected, this, [this] {
        m_connected = false;
        emit changed();
    });

    connect(&m_command, &QLocalSocket::readyRead, this, [this] {
        readLines(&m_command, m_commandBuffer);
    });
    connect(&m_stream, &QLocalSocket::readyRead, this, [this] {
        readLines(&m_stream, m_streamBuffer);
    });

    connect(&m_command, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        m_connected = false;
        emit changed();
    });
    connect(&m_stream, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        m_connected = false;
        emit changed();
    });
}

void Backend::setSocketPath(const QString &path)
{
    m_socketPath = path;
    emit changed();
}

void Backend::start()
{
    if (m_socketPath.isEmpty())
        m_socketPath = resolveSocket();

    tryConnect();
    m_reconnect.start();
    m_statusTimer.start();
}

void Backend::tryConnect()
{
    if (m_socketPath.isEmpty())
        m_socketPath = resolveSocket();

    if (m_command.state() == QLocalSocket::UnconnectedState)
        m_command.connectToServer(m_socketPath);
    if (m_stream.state() == QLocalSocket::UnconnectedState)
        m_stream.connectToServer(m_socketPath);
}

void Backend::refreshAll()
{
    send(QJsonObject{{QStringLiteral("request"), QStringLiteral("status")}});
    send(QJsonObject{{QStringLiteral("request"), QStringLiteral("sensors")}});
    send(QJsonObject{{QStringLiteral("request"), QStringLiteral("gears")}});
    send(QJsonObject{{QStringLiteral("request"), QStringLiteral("get_config")}});
}

void Backend::send(const QJsonObject &request)
{
    if (m_command.state() != QLocalSocket::ConnectedState) {
        // The daemon is not there yet; the reconnect timer will bring it back
        // and refreshAll runs again on connect.
        return;
    }

    m_command.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    m_command.flush();
    qInfo().noquote() << "->" << QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void Backend::holdSpeed(int rpm)
{
    send(QJsonObject{{QStringLiteral("request"), QStringLiteral("set_manual")},
                 {QStringLiteral("rpm"), rpm}});
}

void Backend::releaseManual()
{
    send(QJsonObject{{QStringLiteral("request"), QStringLiteral("set_manual")},
                 {QStringLiteral("rpm"), QJsonValue()}});
}

/// Explicitly request Smart Speed; status confirms the resulting mode.
void Backend::setAutomatic()
{
    if (m_smartMode)
        return;
    m_smartMode = true;
    send(QJsonObject{{QStringLiteral("request"), QStringLiteral("set_automatic")} });
}

/// Rewrite one of the four speeds stored in the cooler.
void Backend::writeGear(const QString &name, int rpm)
{
    // Map UI gear names to daemon gear names
    static const QMap<QString, QString> gearAlias = {
        {"Silent", QStringLiteral("quiet")},
        {"Standard", QStringLiteral("standard")},
        {"Strong", QStringLiteral("strong")},
        {"Overclock", QStringLiteral("overclock")}
    };
    QString gearName = gearAlias.value(name, name);

    // Map UI RPM arrays to daemon gear names' expected RPM levels
    static const QMap<QString, QVector<int>> officialGears = {
        {"quiet", {1300, 1700, 1900}},
        {"standard", {2100, 2400, 2700}},
        {"strong", {2800, 3000, 3300}},
        {"overclock", {3500, 3700, 4000}}
    };

    if (!officialGears.contains(gearName)) {
        qWarning() << "Unknown gear" << gearName;
        return;
    }
    const auto &levels = officialGears[gearName];
    // Find closest RPM level or clamp to max
    int target = std::find(levels.begin(), levels.end(), rpm) != levels.end() ? rpm :
                   (rpm < levels.first() ? levels.first() : levels.last());
    // Supply gating: clamp to supply_max_rpm from daemon status
    int clamped = qBound(500, target, m_supplyMaxRpm);
    send(QJsonObject{{QStringLiteral("request"), QStringLiteral("set_gear")},
                 {QStringLiteral("gear"), gearName},
                 {QStringLiteral("rpm"), clamped}});
}

void Backend::setStandby(const QString &mode)
{
    if (mode == m_standby)
        return;

    send(QJsonObject{{QStringLiteral("request"), QStringLiteral("set_standby")},
                 {QStringLiteral("standby"), mode}});
    m_standby = mode;
    emit changed();
}

void Backend::setStrip(bool on)
{
    setLightMode(on ? QStringLiteral("static") : QStringLiteral("off"));
}

void Backend::setLightMode(const QString &mode)
{
    if (mode != QLatin1String("off") && mode != QLatin1String("static")
        && mode != QLatin1String("effect"))
        return;

    if (mode == m_lightMode)
        return;

    m_lightMode = mode;
    pushLighting();
}

void Backend::setLightColor(const QString &color)
{
    const QColor parsed(color);
    if (!parsed.isValid())
        return;

    m_lightColor = color.toUpper();
    m_lightMode = QStringLiteral("static");
    pushLighting();
}

QStringList Backend::lightColors() const
{
    // Keep the legacy QColor input forms while exposing canonical RGB entries.
    return {QColor(m_lightColor).name(QColor::HexRgb).toUpper()};
}

bool Backend::setLightColors(const QStringList &colors)
{
    // Never silently truncate a sequence the daemon cannot represent.
    if (colors.size() != lightColorLimit())
        return false;

    const QString &color = colors.first();
    if (color.size() != 7 || !color.startsWith(QLatin1Char('#')) || !QColor(color).isValid())
        return false;

    setLightColor(color);
    return true;
}

void Backend::setLightEffect(int effect)
{
    m_lightEffect = qBound(1, effect, 5);
    m_lightMode = QStringLiteral("effect");
    pushLighting();
}

void Backend::setLightBrightness(int percent)
{
    const int clamped = qBound(0, percent, 100);
    if (clamped == m_lightBrightness)
        return;

    m_lightBrightness = clamped;
    pushLighting();
}

void Backend::setLightSpeed(int speed)
{
    const int clamped = qBound(10, speed, 2550);
    if (clamped == m_lightSpeed)
        return;

    m_lightSpeed = clamped;
    pushLighting();
}

void Backend::setLightIndicators(bool on)
{
    if (on == m_lightIndicators)
        return;

    m_lightIndicators = on;
    pushLighting();
}

void Backend::pushLighting()
{
    emit changed();
    m_lightTimer.start();
}

void Backend::sendLighting()
{
    // LightMode is an internally tagged enum *inside* a Lighting struct, so the
    // wire shape nests one level deeper than it looks: the mode object carries
    // its own "mode" tag. Flattening it makes the daemon reject the request.
    QJsonObject mode;
    if (m_lightMode == QLatin1String("off")) {
        mode.insert(QStringLiteral("mode"), QStringLiteral("off"));
    } else if (m_lightMode == QLatin1String("static")) {
        const QColor color(m_lightColor);
        mode.insert(QStringLiteral("mode"), QStringLiteral("static"));
        mode.insert(QStringLiteral("color"),
                    QJsonObject{{QStringLiteral("r"), color.red()},
                                {QStringLiteral("g"), color.green()},
                                {QStringLiteral("b"), color.blue()}});
    } else if (m_lightMode == QLatin1String("effect")) {
        mode.insert(QStringLiteral("mode"), QStringLiteral("effect"));
        mode.insert(QStringLiteral("effect"), m_lightEffect);
    } else {
        mode.insert(QStringLiteral("mode"), QStringLiteral("off"));
    }

    QJsonObject lighting;
    lighting.insert(QStringLiteral("mode"), mode);
    lighting.insert(QStringLiteral("brightness"), m_lightBrightness);
    lighting.insert(QStringLiteral("indicators"), m_lightIndicators);
    if (m_lightMode == QLatin1String("effect")) {
        QJsonArray colors;
        const QColor color(m_lightColor);
        colors.append(QJsonObject{{QStringLiteral("r"), color.red()},
                                  {QStringLiteral("g"), color.green()},
                                  {QStringLiteral("b"), color.blue()}});
        lighting.insert(QStringLiteral("colors"), colors);
        lighting.insert(QStringLiteral("speed"), m_lightSpeed);
        lighting.insert(QStringLiteral("ledCount"), 6);
    }

    send(QJsonObject{{QStringLiteral("request"), QStringLiteral("set_lighting")},
                 {QStringLiteral("lighting"), lighting}});
}

void Backend::readLines(QLocalSocket *socket, QByteArray &buffer)
{
    Q_UNUSED(socket)
    buffer.append(socket->readAll());

    qsizetype newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(newline).trimmed();
        buffer.remove(0, newline + 1);
        if (!line.isEmpty())
            dispatch(line);
    }
}

void Backend::dispatch(const QByteArray &line)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return;

    const QJsonObject object = document.object();
    const QString reply = object.value(QStringLiteral("reply")).toString();

    if (reply == QLatin1String("status"))
        applyStatus(object);
    else if (reply == QLatin1String("sensors"))
        applySensors(object);
    else if (reply == QLatin1String("gears"))
        applyGears(object);
    else if (reply == QLatin1String("config"))
        applyConfig(object);
    else if (reply == QLatin1String("ok")) {
        const QJsonValue status = object.value(QStringLiteral("status"));
        if (status.isObject())
            applyStatus(status.toObject());
        // Mutation readback is authoritative, then refresh both persistent
        // snapshots and gear metadata so the visual state never waits for the
        // next user click.
        refreshAll();
    } else if (reply == QLatin1String("error"))
        fail(object.value(QStringLiteral("message")).toString());
}

void Backend::applyStatus(const QJsonObject &status)
{
    m_connected = status.value(QStringLiteral("connected")).toBool(false);
    m_model = status.value(QStringLiteral("model")).toString();
    m_currentRpm = status.value(QStringLiteral("current_rpm")).toInt(0);
    m_targetRpm = status.value(QStringLiteral("target_rpm")).toInt(0);
    m_manual = status.value(QStringLiteral("manual")).toBool(false);
    m_manualRpm = status.value(QStringLiteral("manual_rpm")).toInt(0);
    m_supply = status.value(QStringLiteral("supply")).toString();
    m_transport = status.value(QStringLiteral("transport")).toString();

    const QJsonValue ceiling = status.value(QStringLiteral("supply_max_rpm"));
    m_supplyMaxRpm = ceiling.isDouble() ? ceiling.toInt() : 4000;

    // The daemon tells us what it last told the strip, which is the only way to
    // know what is playing: the cooler cannot be asked. Skip it while a local
    // edit is still on its way out, or the answer would undo the click.
    if (!m_lightTimer.isActive()) {
        const QJsonValue reported = status.value(QStringLiteral("lighting"));
        if (reported.isObject()) {
            const QJsonObject lighting = reported.toObject();
            const QJsonObject modeObject = lighting.value(QStringLiteral("mode")).toObject();
            const QString mode = modeObject.value(QStringLiteral("mode")).toString();
            if (mode == QLatin1String("off") || mode == QLatin1String("static")
                || mode == QLatin1String("effect"))
                m_lightMode = mode;

            const QJsonObject color = modeObject.value(QStringLiteral("color")).toObject();
            if (!color.isEmpty()) {
                const QColor rgb(color.value(QStringLiteral("r")).toInt(),
                                 color.value(QStringLiteral("g")).toInt(),
                                 color.value(QStringLiteral("b")).toInt());
                if (rgb.isValid())
                    m_lightColor = rgb.name(QColor::HexRgb).toUpper();
            }

            const QJsonValue effect = modeObject.value(QStringLiteral("effect"));
            if (effect.isDouble())
                m_lightEffect = qBound(1, effect.toInt(), 5);

            const QJsonValue brightness = lighting.value(QStringLiteral("brightness"));
            if (brightness.isDouble())
                m_lightBrightness = qBound(0, brightness.toInt(), 100);

            const QJsonValue indicators = lighting.value(QStringLiteral("indicators"));
            if (indicators.isBool())
                m_lightIndicators = indicators.toBool();
        }
    }

    m_demands.clear();
    for (const QJsonValue &value : status.value(QStringLiteral("demands")).toArray()) {
        const QJsonObject demand = value.toObject();
        m_demands.append(QVariantMap{
            {QStringLiteral("name"), demand.value(QStringLiteral("name")).toString()},
            {QStringLiteral("temp"), demand.value(QStringLiteral("temp_c")).toInt(0)},
            {QStringLiteral("rpm"), demand.value(QStringLiteral("rpm")).toInt(0)},
            {QStringLiteral("panic"), demand.value(QStringLiteral("panic")).toBool(false)},
        });
    }

    // Read gear-aware state from daemon
    m_smartMode = status.value(QStringLiteral("smart_mode")).toBool(false);
    m_fixedGear = status.value(QStringLiteral("fixed_gear")).toBool(false);
    m_curvePreset = status.value(QStringLiteral("curve_preset")).toString();
    // Status may omit curves even when get_config reports them. Only an explicit
    // field replaces the readback; never infer which curve is the Custom preset.
    if (status.contains(QStringLiteral("curves")))
        m_curves = status.value(QStringLiteral("curves")).toArray().toVariantList();

    emit changed();
}

void Backend::applySensors(const QJsonObject &reply)
{
    struct Row {
        int rank;
        QVariantMap row;
    };

    QList<Row> rows;
    QSet<QString> seen;

    for (const QJsonValue &value : reply.value(QStringLiteral("sensors")).toArray()) {
        const QJsonObject sensor = value.toObject();
        if (!worthShowing(sensor))
            continue;

        const QJsonValue reading = sensor.value(QStringLiteral("temp_c"));
        if (!reading.isDouble())
            continue;

        const QString key = rowKey(sensor);
        if (seen.contains(key))
            continue;
        seen.insert(key);

        rows.append(Row{
            rowRank(sensor),
            QVariantMap{
                {QStringLiteral("name"), rowName(sensor)},
                {QStringLiteral("value"), reading.toInt()},
                {QStringLiteral("chip"), sensor.value(QStringLiteral("hwmon")).toString()},
            },
        });
    }

    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        return a.rank < b.rank;
    });

    m_temps.clear();
    for (int i = 0; i < rows.size() && i < 4; ++i)
        m_temps.append(rows.at(i).row);

    emit changed();
}

void Backend::applyGears(const QJsonObject &reply)
{
    m_gears.clear();
    for (const QJsonValue &value : reply.value(QStringLiteral("gears")).toArray()) {
        const QJsonObject gear = value.toObject();
        m_gears.append(QVariantMap{
            {QStringLiteral("name"), gear.value(QStringLiteral("name")).toString()},
            {QStringLiteral("rpm"), gear.value(QStringLiteral("rpm")).toInt(0)},
            {QStringLiteral("allowed"), gear.value(QStringLiteral("allowed")).toBool(false)},
        });
    }

    emit changed();
}

void Backend::applyConfig(const QJsonObject &reply)
{
    m_configWritable = reply.value(QStringLiteral("writable")).toBool(true);

    const QJsonObject config = reply.value(QStringLiteral("config")).toObject();
    m_curvePreset = config.value(QStringLiteral("curve_preset")).toString();
    m_curves = config.value(QStringLiteral("curves")).toArray().toVariantList();
    const QString standby = config.value(QStringLiteral("standby")).toString();
    if (!standby.isEmpty())
        m_standby = standby;

    const QJsonValue smartVal = config.value(QStringLiteral("smart_mode"));
    if (smartVal.isBool()) {
        m_smartMode = smartVal.toBool();
    }
    const QJsonValue curveVal = config.value(QStringLiteral("current_curve"));
    if (curveVal.isDouble()) {
        m_currentCurve = curveVal.toInt();
    }

    emit changed();
}

bool Backend::setCustomCurve(const QVariantList &points)
{
    if (points.size() != 8 || m_command.state() != QLocalSocket::ConnectedState)
        return false;

    const int maximum = qMin(4000, m_supplyMaxRpm);
    QJsonArray encoded;
    int previousTemperature = 29;
    for (const QVariant &value : points) {
        const QVariantMap point = value.toMap();
        const QJsonValue temperature = QJsonValue::fromVariant(point.value(QStringLiteral("temp_c")));
        const QJsonValue speed = QJsonValue::fromVariant(point.value(QStringLiteral("rpm")));
        if (!temperature.isDouble() || !speed.isDouble())
            return false;

        const double temp = temperature.toDouble();
        const double rpm = speed.toDouble();
        if (!std::isfinite(temp) || !std::isfinite(rpm)
            || temp != std::floor(temp) || rpm != std::floor(rpm)
            || temp < 30 || temp > 100 || temp <= previousTemperature
            || rpm < 500 || rpm > maximum)
            return false;

        previousTemperature = static_cast<int>(temp);
        encoded.append(QJsonObject{{QStringLiteral("temp_c"), previousTemperature},
                                   {QStringLiteral("rpm"), static_cast<int>(rpm)}});
    }

    // Validate the whole request before sending. Do not manufacture readback or
    // activate Smart separately: a rejected curve must not change cooling mode.
    send(QJsonObject{{QStringLiteral("request"), QStringLiteral("set_curve_preset")},
                    {QStringLiteral("preset"), QStringLiteral("custom")},
                    {QStringLiteral("points"), encoded}});
    return true;
}

void Backend::fail(const QString &message)
{
    m_problem = message;
    emit changed();
}

/// Select a curve preset: 0=Quiet, 1=Performance, 2=Custom
void Backend::setCurvePreset(int preset)
{
    // Clamp to valid presets: 0=Quiet, 1=Performance, 2=Custom
    preset = qBound(0, preset, 2);
    static const QString names[]{QStringLiteral("quiet"), QStringLiteral("performance"),
                                 QStringLiteral("custom")};
    const bool changedPreset = preset != m_currentCurve;
    const bool resumeAuto = !m_smartMode;
    if (!changedPreset && !resumeAuto)
        return;
    m_currentCurve = preset;
    if (changedPreset) {
        send(QJsonObject{{QStringLiteral("request"), QStringLiteral("set_curve_preset")},
                         {QStringLiteral("preset"), names[preset]}});
    }
    // Selecting the current curve is still an explicit request to resume Auto.
    if (resumeAuto)
        setAutomatic();
    emit changed();
}