#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>
#include <QVariantList>

/// Client for the flydigictld daemon.
///
/// The daemon speaks newline-delimited JSON over a unix socket: one connection
/// for requests, another one subscribed to the live status stream. Nothing here
/// owns the cooler; the daemon does, and this only asks it things.
class Backend : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString socketPath READ socketPath NOTIFY changed)
    Q_PROPERTY(QString model READ model NOTIFY changed)
    Q_PROPERTY(int currentRpm READ currentRpm NOTIFY changed)
    Q_PROPERTY(int targetRpm READ targetRpm NOTIFY changed)
    Q_PROPERTY(bool manual READ manual NOTIFY changed)
    Q_PROPERTY(int manualRpm READ manualRpm NOTIFY changed)
    Q_PROPERTY(int supplyMaxRpm READ supplyMaxRpm NOTIFY changed)
    Q_PROPERTY(QString supply READ supply NOTIFY changed)
    Q_PROPERTY(QString transport READ transport NOTIFY changed)
    Q_PROPERTY(QString leading READ leading NOTIFY changed)
    Q_PROPERTY(QString standby READ standby NOTIFY changed)
    Q_PROPERTY(bool stripOn READ stripOn NOTIFY changed)
    Q_PROPERTY(QString lightMode READ lightMode NOTIFY changed)
    Q_PROPERTY(QString lightColor READ lightColor NOTIFY changed)
    Q_PROPERTY(int lightEffect READ lightEffect NOTIFY changed)
    Q_PROPERTY(int lightBrightness READ lightBrightness NOTIFY changed)
    Q_PROPERTY(bool lightIndicators READ lightIndicators NOTIFY changed)
    Q_PROPERTY(bool configWritable READ configWritable NOTIFY changed)
    Q_PROPERTY(QVariantList temps READ temps NOTIFY changed)
    Q_PROPERTY(QVariantList gears READ gears NOTIFY changed)
    Q_PROPERTY(QVariantList demands READ demands NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)

public:
    explicit Backend(QObject *parent = nullptr);

    void setSocketPath(const QString &path);
    void start();

    QString socketPath() const { return m_socketPath; }
    bool connected() const { return m_connected; }
    QString model() const { return m_model; }
    int currentRpm() const { return m_currentRpm; }
    int targetRpm() const { return m_targetRpm; }
    bool manual() const { return m_manual; }
    int manualRpm() const { return m_manualRpm; }
    int supplyMaxRpm() const { return m_supplyMaxRpm; }
    QString supply() const { return m_supply; }
    QString transport() const { return m_transport; }
    QString leading() const { return m_leading; }
    QString standby() const { return m_standby; }
    bool stripOn() const { return m_stripOn; }
    QString lightMode() const { return m_lightMode; }
    QString lightColor() const { return m_lightColor; }
    int lightEffect() const { return m_lightEffect; }
    int lightBrightness() const { return m_lightBrightness; }
    bool lightIndicators() const { return m_lightIndicators; }
    bool configWritable() const { return m_configWritable; }
    QVariantList temps() const { return m_temps; }
    QVariantList gears() const { return m_gears; }
    QVariantList demands() const { return m_demands; }
    QString problem() const { return m_problem; }

public slots:
    /// Ask the daemon for everything a fresh window needs.
    void refreshAll();

    /// Hold a speed by hand. Zero is a stop, not "release".
    void holdSpeed(int rpm);

    /// Give the cooler back to its fan curves.
    void releaseManual();

    /// Rewrite one of the four speeds stored in the cooler.
    void writeGear(const QString &name, int rpm);

    /// off, instant or delayed.
    void setStandby(const QString &mode);

    /// Light or dark the side strip. Turning it back on uses a static colour,
    /// because the cooler cannot be asked what it was showing.
    void setStrip(bool on);

    /// off, static or effect: what the side strip should do.
    void setLightMode(const QString &mode);

    /// A solid colour, given as "#RRGGBB". Also switches to static mode.
    void setLightColor(const QString &color);

    /// One of the five presets burned into the firmware (1-5).
    void setLightEffect(int effect);

    /// Strip brightness, 0-100. The gear lights keep their own setting.
    void setLightBrightness(int percent);

    /// The small LEDs behind the gear marks, which are a separate light.
    void setLightIndicators(bool on);

signals:
    void changed();

private:
    void tryConnect();
    void send(const QJsonObject &request);
    void dispatch(const QByteArray &line);
    void applyStatus(const QJsonObject &status);
    void applySensors(const QJsonObject &reply);
    void applyGears(const QJsonObject &reply);
    void applyConfig(const QJsonObject &reply);
    void pushLighting();
    void sendLighting();
    void fail(const QString &message);
    void readLines(QLocalSocket *socket, QByteArray &buffer);

    QLocalSocket m_command;
    QLocalSocket m_stream;
    QTimer m_reconnect;
    /// Lighting is written to the device a third of a second at a time, so a
    /// dragged slider coalesces into one write instead of thirty.
    QTimer m_lightTimer;

    QString m_socketPath;
    QByteArray m_commandBuffer;
    QByteArray m_streamBuffer;

    bool m_connected = false;
    QString m_model;
    int m_currentRpm = 0;
    int m_targetRpm = 0;
    bool m_manual = false;
    int m_manualRpm = 0;
    int m_supplyMaxRpm = 4000;
    QString m_supply;
    QString m_transport;
    QString m_leading;
    QString m_standby = QStringLiteral("delayed");
    bool m_stripOn = true;
    QString m_lightMode = QStringLiteral("off");
    QString m_lightColor = QStringLiteral("#22D3EE");
    int m_lightEffect = 5;
    int m_lightBrightness = 100;
    bool m_lightIndicators = true;
    bool m_configWritable = true;
    QVariantList m_temps;
    QVariantList m_gears;
    QVariantList m_demands;
    QString m_problem;
};
