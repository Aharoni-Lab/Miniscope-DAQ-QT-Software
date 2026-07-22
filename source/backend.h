#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QThread>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QStandardItemModel>
#include <QStandardItem>

#include "miniscope.h"
#include "behaviorcam.h"
#include "controlpanel.h"
#include "datasaver.h"
#include "behaviortracker.h"
#include "tracedisplay.h"


class backEnd : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString userConfigFileName READ userConfigFileName WRITE setUserConfigFileName NOTIFY userConfigFileNameChanged)
    Q_PROPERTY(QString userConfigDisplay READ userConfigDisplay WRITE setUserConfigDisplay NOTIFY userConfigDisplayChanged)
    Q_PROPERTY(bool userConfigOK READ userConfigOK WRITE setUserConfigOK NOTIFY userConfigOKChanged)
    Q_PROPERTY(bool hasDevices READ hasDevices NOTIFY hasDevicesChanged)
    Q_PROPERTY(QString availableCodecList READ availableCodecList WRITE setAvailableCodecList NOTIFY availableCodecListChanged)
    Q_PROPERTY(QStringList availableCodecs READ availableCodecs CONSTANT)
    Q_PROPERTY(QStringList availableLUTs READ availableLUTs CONSTANT)
    Q_PROPERTY(QString versionNumber READ versionNumber WRITE setVersionNumber NOTIFY versionNumberChanged)
    Q_PROPERTY(QString buildInfo READ buildInfo WRITE setBuildInfo NOTIFY buildInfoChanged)

    Q_PROPERTY(QStandardItemModel* jsonTreeModel READ jsonTreeModel WRITE setJsonTreeModel NOTIFY jsonTreeModelChanged)

public:
    explicit backEnd(QObject *parent = nullptr);

    QString userConfigFileName() {return m_userConfigFileName;}
    void setUserConfigFileName(const QString &input);

    bool userConfigOK() {return m_userConfigOK;}
    void setUserConfigOK(bool userConfigOK) {m_userConfigOK = userConfigOK;}

    // True when the config has at least one device (miniscope or camera). The Run
    // button is gated on this so you can't run a config with nothing to record.
    bool hasDevices() const { return m_hasDevices; }

    QString userConfigDisplay(){ return m_userConfigDisplay; }
    void setUserConfigDisplay(const QString &input);

    QString availableCodecList(){ return m_availableCodecList; }
    void setAvailableCodecList(const QString &input);

    // List of host-supported codecs, for the compression dropdown in the tree editor.
    QStringList availableCodecs() const { return QStringList(m_availableCodec.begin(), m_availableCodec.end()); }

    // Display LUTs (colormaps) offered in the tree editor's "lut" dropdown. Must
    // stay in sync with the lutMode mapping in VideoDevice::createView and the
    // shader. "None" = grayscale.
    QStringList availableLUTs() const { return {"None", "Green", "Red", "Inferno"}; }

    QString versionNumber() { return m_versionNumber; }
    void setVersionNumber(const QString &input) { m_versionNumber = input; emit versionNumberChanged(); }

    QString buildInfo() { return m_buildInfo; }
    void setBuildInfo(const QString &input) { m_buildInfo = input; emit buildInfoChanged(); }

    QStandardItemModel* jsonTreeModel() { return m_jsonTreeModel; }
    void setJsonTreeModel(QStandardItemModel* model) { m_jsonTreeModel = model; }

    void constructJsonTreeModel();
    Q_INVOKABLE void treeViewTextChanged(const QModelIndex &index, QString text);
    // Convert a file:// URL from a QML folder/file dialog to a native path, so
    // the path-browse buttons in the config tree editor can store a plain path.
    Q_INVOKABLE QString urlToLocalFile(const QUrl &url) const { return url.toLocalFile(); }
    // Inverse of urlToLocalFile: build a file:// URL to seed the Save-As dialog.
    Q_INVOKABLE QUrl localFileToUrl(const QString &path) const { return QUrl::fromLocalFile(path); }
    // Folder the user-config open/save dialogs should default to. Driven by
    // MINISCOPE_USERCONFIG_DIR (set by the Linux AppImage's first-run prompt);
    // empty URL when unset, in which case QML leaves the dialog default alone.
    Q_INVOKABLE QUrl defaultUserConfigFolderUrl() const {
        const QString dir = qEnvironmentVariable("MINISCOPE_USERCONFIG_DIR");
        return dir.isEmpty() ? QUrl() : QUrl::fromLocalFile(dir);
    }
    QStandardItem *handleJsonObject(QStandardItem* parent, QJsonObject obj, QJsonObject objProps);
    QStandardItem *handleJsonArray(QStandardItem* parent, QJsonArray arry, QString type);
    void generateUserConfigFromModel();
    QJsonObject getObjectFromModel(QModelIndex index);
    QJsonArray getArrayFromModel(QModelIndex index);
    Q_INVOKABLE void saveConfigObject();
    // Save the (edited) user config to a user-chosen path from the Save-As dialog.
    Q_INVOKABLE void saveConfigObjectAs(const QString &filePath);
    // Enumerate connected video devices as "deviceID N: <name>" lines so the user
    // can see which deviceID maps to which camera. Dispatches at compile time to a
    // per-OS implementation (DirectShow on Windows, V4L2 on Linux).
    Q_INVOKABLE QString scanVideoDevices();

    // --- User-config generator -----------------------------------------------
    // Device types available to add for the given category ("miniscopes" or
    // "cameras"), for the Add-Device dialog's type dropdown. Categorization is
    // data-driven from each catalog entry's qmlFile: camera-class devices (WebCam
    // variants, Minicam) share behaviorCam.qml, while miniscopes use a
    // Miniscope_*.qml; anything not identified as a camera counts as a miniscope.
    Q_INVOKABLE QStringList deviceTypesForCategory(const QString &category) const;
    // Device IDs not already used by another device in the config, each labelled with
    // the connected-device name when known. Drives the Add-Device dialog's ID
    // dropdown so two devices can't be assigned the same deviceID.
    Q_INVOKABLE QStringList availableDeviceIDs();
    // Seed a fresh, valid user config from the schema (no example file needed) and
    // show it in the tree editor.
    Q_INVOKABLE void newUserConfig();
    // Add a device of the given catalog type under devices.<category> (category is
    // "miniscopes" or "cameras"), with sensible catalog-derived defaults, then
    // rebuild the tree. Names must be non-empty and unique within their category.
    Q_INVOKABLE void addDevice(const QString &category, const QString &deviceType, const QString &deviceName, int deviceID);


    void loadUserConfigFile();
    // Recompute m_hasDevices and emit hasDevicesChanged() if it changed; call after
    // the config is mutated/loaded. Kept out of checkUserConfigForIssues() so that
    // stays purely a validity check.
    void updateHasDevices();
    bool checkUserConfigForIssues();
    void constructUserConfigGUI();
    void parseUserConfig();

    void setupBehaviorTracker();

    bool checkForUniqueDeviceNames();
    bool checkForCompression();


    void testLibusb();

signals:
    void userConfigFileNameChanged();
    void userConfigDisplayChanged();
    void userConfigOKChanged();
    void hasDevicesChanged();
    void availableCodecListChanged();
    void versionNumberChanged();
    void buildInfoChanged();
    void jsonTreeModelChanged();

    void closeAll();
    void showErrorMessage();
    void showErrorMessageCompression();
    void sendMessage(QString);

public slots:
    void onRunClicked();
    void onRecordClicked();
    void exitClicked();
    void handleUserConfigFileNameChanged();

//    Q_INVOKABLE void treeViewclicked();
//    void onStopClicked();

private:
    void connectSnS();
    void setupDataSaver();

    void testCodecSupport();

    // User-config generator helpers. defaultFromProps walks a userConfigProps.json
    // node and returns a default value matching its declared types; defaultForType
    // maps a single type string to its zero value; enrichDeviceDefaults fills a
    // freshly-templated device with sensible, catalog-derived starting values.
    QJsonValue defaultFromProps(const QJsonValue &propNode);
    QJsonValue defaultForType(const QString &type);
    void enrichDeviceDefaults(QJsonObject &device, const QString &category, const QString &deviceType);

    // Per-OS implementations behind scanVideoDevices(); each is defined only on its
    // platform (calls to the other are #ifdef'd out, so it's never odr-used there).
    // enumerateVideoDevices() lists the connected DirectShow device names indexed by
    // deviceID (order == OpenCV's CAP_DSHOW index) and is also used by
    // availableDeviceIDs(); it exists on Windows only.
    QStringList enumerateVideoDevices();
    QString scanVideoDevicesWindows();
    QString scanVideoDevicesLinux();

    QString m_versionNumber;
    QString m_buildInfo;
    QString m_userConfigFileName;
    QString m_userConfigDisplay;
    bool m_userConfigOK;
    bool m_hasDevices = false;
    QJsonObject m_userConfig;
    QJsonObject m_configProps;
    QJsonObject m_deviceCatalog;   // deviceConfigs/videoDevices.json (device types + defaults)

    // Break down of different types in user config file
    // 'uc' stands for userConfig
    QString researcherName;
    QString dataDirectory;
    QJsonArray dataStructureOrder;
    QString experimentName;
    QString animalName;

    QJsonObject ucExperiment;
    QJsonObject ucMiniscopes;
    QJsonObject ucBehaviorCams;
    QJsonObject ucBehaviorTracker;
    QJsonObject ucTraceDisplay;

    QVector<Miniscope*> miniscope;
    QVector<BehaviorCam*> behavCam;
    ControlPanel *controlPanel;
    TraceDisplayBackend *traceDisplay;

    DataSaver *dataSaver;
    QThread *dataSaverThread;

    BehaviorTracker *behavTracker;

    QVector<QString> m_availableCodec;
    QString m_availableCodecList;
    QVector<QString> unAvailableCodec;

    qint64 m_softwareStartTime;

    QHash <int,QByteArray> roles;
    QStandardItemModel* m_jsonTreeModel;
    QVector<QStandardItem*> m_standardItem;

};

#endif // BACKEND_H
