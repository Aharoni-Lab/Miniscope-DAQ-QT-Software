#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QThread>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QUrl>

#include "miniscope.h"
#include "behaviorcam.h"
#include "controlpanel.h"
#include "datasaver.h"
#include "behaviortracker.h"
#include "tracedisplay.h"
#include "commutator.h"


class backEnd : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString userConfigFileName READ userConfigFileName WRITE setUserConfigFileName NOTIFY userConfigFileNameChanged)
    Q_PROPERTY(QString userConfigDisplay READ userConfigDisplay WRITE setUserConfigDisplay NOTIFY userConfigDisplayChanged)
    Q_PROPERTY(QString configCheckNotes READ configCheckNotes NOTIFY configCheckNotesChanged)
    Q_PROPERTY(bool configDirty READ configDirty NOTIFY configDirtyChanged)
    Q_PROPERTY(bool userConfigOK READ userConfigOK WRITE setUserConfigOK NOTIFY userConfigOKChanged)
    Q_PROPERTY(bool hasDevices READ hasDevices NOTIFY hasDevicesChanged)
    Q_PROPERTY(QString availableCodecList READ availableCodecList WRITE setAvailableCodecList NOTIFY availableCodecListChanged)
    Q_PROPERTY(QStringList availableCodecs READ availableCodecs CONSTANT)
    Q_PROPERTY(QStringList availableLUTs READ availableLUTs CONSTANT)
    Q_PROPERTY(bool sessionActive READ sessionActive NOTIFY sessionActiveChanged)
    // Run is synchronous: every device opens its camera on the GUI thread, which
    // takes seconds and used to leave the app looking frozen with no feedback.
    // These two drive the shell's "Starting session" overlay - the backend
    // narrates each step and pumps the event loop between them so it paints.
    Q_PROPERTY(bool starting READ starting NOTIFY startingChanged)
    Q_PROPERTY(QString startupStage READ startupStage NOTIFY startupStageChanged)
    // The running session's windows as pane descriptors for the Acquire view:
    // [{name, type ("video"/"panel"/"trace"), window (QWindow*), aspect}].
    // Rebuilt on Run, cleared (and emitted) FIRST during session teardown so
    // QML releases its WindowContainers before the windows are destroyed.
    Q_PROPERTY(QVariantList sessionPanes READ sessionPanes NOTIFY sessionPanesChanged)
    // The running session's controller (recording state machine + message
    // log) for the QML session bar. Null outside a session.
    Q_PROPERTY(QObject *sessionControl READ sessionControl NOTIFY sessionControlChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    // Folder the current (or most recent) recording of this session writes into.
    // Empty until the first recording starts. Pushed by the saver thread, so it
    // is the real path including the date/time/name folders the config's
    // directoryStructure produced - not just the configured dataDirectory.
    Q_PROPERTY(QString recordDirectory READ recordDirectory NOTIFY recordDirectoryChanged)
    Q_PROPERTY(QString versionNumber READ versionNumber WRITE setVersionNumber NOTIFY versionNumberChanged)
    Q_PROPERTY(QString buildInfo READ buildInfo WRITE setBuildInfo NOTIFY buildInfoChanged)

    // The loaded user config as a QML value (maps/lists), for the form editor.
    // Re-read on userConfigJsonChanged; mutate ONLY via setConfigValue /
    // removeConfigKey / addDevice / removeDevice so unknown and COMMENT_* keys
    // survive round-trips (the config object itself is the single source of truth).
    Q_PROPERTY(QVariantMap userConfigJson READ userConfigJson NOTIFY userConfigJsonChanged)

public:
    explicit backEnd(QObject *parent = nullptr);

    QString userConfigFileName() {return m_userConfigFileName;}
    void setUserConfigFileName(const QString &input);

    bool userConfigOK() {return m_userConfigOK;}
    void setUserConfigOK(bool userConfigOK) {m_userConfigOK = userConfigOK;}

    // True when the config has at least one device (miniscope or camera). The Run
    // button is gated on this so you can't run a config with nothing to record.
    bool hasDevices() const { return m_hasDevices; }

    // True between a successful Run and the matching endSession(): the
    // acquisition windows/threads exist. Lets QML flip between the config
    // (Setup) view and the running-session (Acquire) view.
    bool sessionActive() const { return m_sessionActive; }

    // True from the moment Run is pressed until the session is up (or failed).
    bool starting() const { return m_starting; }
    // What Run is doing right now, e.g. "Opening Miniscope \"My Miniscope\"…".
    QString startupStage() const { return m_startupStage; }

    // True while a recording is in progress in the active session. Gates
    // endSession() (ending the session mid-recording would trash an
    // experiment); the quit path force-stops the recording cleanly instead.
    bool recording() const { return m_recording; }

    QString recordDirectory() const { return m_recordDirectory; }
    // Show a folder in the OS file manager. False when the path is empty or no
    // longer exists (moved/deleted drive), so QML can say so instead of
    // silently doing nothing.
    Q_INVOKABLE bool openDirectory(const QString &path) const;

    // Live session object counts. Used by the session-lifecycle test to pin the
    // Run -> endSession -> Run cycle (no leftover or doubled devices); will move
    // to the Session object when backEnd is split.
    int sessionMiniscopeCount() const { return miniscope.size(); }
    int sessionCameraCount() const { return behavCam.size(); }

    QString userConfigDisplay(){ return m_userConfigDisplay; }
    void setUserConfigDisplay(const QString &input);

    // True when the in-memory config differs from what was last loaded from /
    // saved to disk. Drives the Save button, the "edited" chip in the header,
    // and the save-before-Run/Open/New prompts.
    bool configDirty() const { return m_configDirty; }

    // Migration notes + schema warnings from the last config load, newline-
    // separated; empty when the config is clean. Shown as a banner above the
    // config tree (the raw-text display is hidden once a config loads).
    QString configCheckNotes() const { return m_configCheckNotes; }

    QString availableCodecList(){ return m_availableCodecList; }
    void setAvailableCodecList(const QString &input);

    // List of host-supported codecs, for the form editor's compression dropdowns.
    QStringList availableCodecs() const { return QStringList(m_availableCodec.begin(), m_availableCodec.end()); }

    // Display LUTs (colormaps) offered in the form editor's "lut" dropdown. Must
    // stay in sync with the lutMode mapping in VideoDevice::createView and the
    // shader. "None" = grayscale.
    QStringList availableLUTs() const { return {"None", "Green", "Red", "Inferno"}; }

    QString versionNumber() { return m_versionNumber; }
    void setVersionNumber(const QString &input) { m_versionNumber = input; emit versionNumberChanged(); }

    QString buildInfo() { return m_buildInfo; }
    void setBuildInfo(const QString &input) { m_buildInfo = input; emit buildInfoChanged(); }

    // --- Form-editor API -------------------------------------------------------
    QVariantMap userConfigJson() const { return m_userConfig.toVariantMap(); }
    // Set/remove a value at a key path (e.g. ["devices","cameras","Cam1","gain"]).
    // Path elements are strings; integral doubles are stored as JSON ints.
    // Re-checks the config and emits userConfigJsonChanged.
    Q_INVOKABLE void setConfigValue(const QVariantList &path, const QVariant &value);
    Q_INVOKABLE void removeConfigKey(const QVariantList &path);
    // Remove a device from devices.<category> (category "miniscopes"/"cameras").
    Q_INVOKABLE void removeDevice(const QString &category, const QString &deviceName);
    // Per-key editor metadata (types + tips) and the device catalog (control
    // ranges/choices), for building form widgets. Constant per run.
    Q_INVOKABLE QVariantMap configPropsJson() const { return m_configProps.toVariantMap(); }
    Q_INVOKABLE QVariantMap deviceCatalogJson() const { return m_deviceCatalog.toVariantMap(); }
    // Raw-JSON tab: current config text, and apply-edited-text (returns "" on
    // success or a human-readable parse error; nothing changes on error).
    Q_INVOKABLE QString rawConfigJson() const;
    Q_INVOKABLE QString applyRawConfigJson(const QString &text);
    // Serial ports for the commutator's port picker: [{name, label}, ...].
    Q_INVOKABLE QVariantList availableSerialPorts() const;

    // --- Acquire pane host -------------------------------------------------------
    QVariantList sessionPanes() const { return m_sessionPanes; }
    QObject *sessionControl() const { return controlPanel; }
    // One polling snapshot for the session bar's telemetry chips:
    // {diskFreeBytes, devices: [{name, frames, dropped, bufferUsed,
    // bufferSize}]}. QML polls ~1 Hz and differentiates frame counts to FPS.
    Q_INVOKABLE QVariantMap sessionTelemetry() const;
    // Switch a pane window between container-embedded (aspect handled by the
    // QML letterbox, free window resize) and top-level floating (re-shown with
    // its interactive aspect lock restored).
    Q_INVOKABLE void setPaneEmbedded(QObject *paneWindow, bool embedded, double aspect);
    // Per-config, per-pane layout persistence (QSettings): {floating, x, y,
    // width, height, ...} — whatever map QML hands over comes back verbatim.
    Q_INVOKABLE QVariantMap paneLayout(const QString &paneName) const;
    Q_INVOKABLE void savePaneLayout(const QString &paneName, const QVariantMap &state);
    // Environment lookup for QML (dev hooks like MINISCOPE_PANE_TEST).
    Q_INVOKABLE QString env(const QString &name) const {
        return qEnvironmentVariable(name.toUtf8().constData());
    }

    // Convert a file:// URL from a QML folder/file dialog to a native path, so
    // the path-browse buttons in the config form editor can store a plain path.
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
    // Adds a device to the config. False (and nothing added) when the name is
    // empty or already taken - see deviceNameProblem().
    Q_INVOKABLE bool addDevice(const QString &category, const QString &deviceType, const QString &deviceName, int deviceID);

    // Device-name uniqueness, used by the Add-Device dialog. A name is a folder
    // name in every recording, so uniqueness spans BOTH categories and ignores
    // case and the space->underscore mangling DataSaver applies: two devices
    // whose names differ only that way would write into one folder.
    Q_INVOKABLE QStringList configuredDeviceNames() const;
    // "" when `name` is usable for a new device, otherwise why it isn't.
    Q_INVOKABLE QString deviceNameProblem(const QString &name) const;
    // `base`, or the first "base 2", "base 3"... that isn't taken.
    Q_INVOKABLE QString uniqueDeviceName(const QString &base) const;


    void loadUserConfigFile();
    // Recompute m_hasDevices and emit hasDevicesChanged() if it changed; call after
    // the config is mutated/loaded. Kept out of checkUserConfigForIssues() so that
    // stays purely a validity check.
    void updateHasDevices();
    bool checkUserConfigForIssues();
    void constructUserConfigGUI();
    void parseUserConfig();

    void setupBehaviorTracker();
    // Create + thread the optional commutator worker when the config has an
    // enabled "commutator" block and a Miniscope to drive it. No-op otherwise.
    void setupCommutator();

    bool checkForUniqueDeviceNames();
    bool checkForCompression();


signals:
    void sessionActiveChanged();
    void sessionPanesChanged();
    void sessionControlChanged();
    void recordingChanged();
    void startingChanged();
    void startupStageChanged();
    void recordDirectoryChanged();
    void userConfigFileNameChanged();
    void userConfigDisplayChanged();
    void configCheckNotesChanged();
    void configDirtyChanged();
    void userConfigOKChanged();
    void hasDevicesChanged();
    void availableCodecListChanged();
    void versionNumberChanged();
    void buildInfoChanged();
    void userConfigJsonChanged();

    void closeAll();
    void showErrorMessage();
    void showErrorMessageCompression();
    void sendMessage(QString);

public slots:
    void onRunClicked();
    void onRecordClicked();
    // End the running acquisition session WITHOUT quitting: stop and join all
    // worker threads, destroy the session's windows/objects, and reset state
    // so another config can be loaded and Run in the same process.
    void endSession();
    void exitClicked();
    void handleUserConfigFileNameChanged();

//    Q_INVOKABLE void treeViewclicked();
//    void onStopClicked();

private:
    void connectSnS();
    void setupDataSaver();
    // Stop the current session's capture/saver/commutator/tracker threads and
    // join them, so no thread outlives the objects it uses. Called by
    // endSession(); callers guard with m_sessionActive.
    void stopSessionThreads();
    // The real teardown behind endSession()/exitClicked(). force=true (quit
    // path) proceeds even mid-recording: stopSessionThreads() stops the
    // recording cleanly first, same as the app always did on quit.
    void endSessionImpl(bool force);
    void setRecordingState(bool recording);
    // Build the pane list from the session's live windows / hide the panes'
    // windows and empty the list (notifying QML in both cases).
    void rebuildSessionPanes();
    void clearSessionPanes();
    bool m_sessionActive = false;
    bool m_recording = false;
    // Publish the current startup step AND give the UI a chance to paint it.
    void setStartupStage(const QString &stage);
    bool m_starting = false;
    QString m_startupStage;
    // Pushed from the saver thread on record start (see DataSaver::
    // recordDirectoryReady); kept after the recording stops so the folder is
    // still reachable, and cleared when a new session starts.
    QString m_recordDirectory;
    QVariantList m_sessionPanes;

    void testCodecSupport();

    // User-config generator helpers. defaultFromProps walks a userConfigProps.json
    // node and returns a default value matching its declared types; defaultForType
    // maps a single type string to its zero value; enrichDeviceDefaults fills a
    // freshly-templated device with sensible, catalog-derived starting values.
    QJsonValue defaultFromProps(const QJsonValue &propNode);
    QJsonValue defaultForType(const QString &type);
    void enrichDeviceDefaults(QJsonObject &device, const QString &category, const QString &deviceType);

    // Recursive path write/remove for the form-editor API (QJson types are
    // value types, so nested edits rebuild the chain up to the root).
    static QJsonValue jsonWithValueAtPath(const QJsonValue &node, const QStringList &path, const QJsonValue &value);
    static QJsonValue jsonWithKeyRemoved(const QJsonValue &node, const QStringList &path);
    // Re-run the load-time checks (schema notes, device names, codecs,
    // hasDevices) after any form mutation and notify QML.
    void configEdited();
    // Recompute m_configDirty (m_userConfig vs the m_savedConfig snapshot) and
    // notify QML on change. Call after any config mutation, load, or save.
    void updateDirtyState();

    // Per-OS implementations behind scanVideoDevices(); each is defined only on its
    // platform (calls to the others are #ifdef'd out, so they're never odr-used
    // there). enumerateVideoDevices() lists connected camera names indexed by
    // deviceID (order == the OpenCV backend's index: DirectShow on Windows,
    // AVFoundation on macOS) and is also used by availableDeviceIDs().
    QStringList enumerateVideoDevices();
    // Linux can't use that "index == deviceID" form: there the deviceID is the
    // V4L2 node number itself (/dev/video2 -> deviceID 2), which is sparse once
    // the non-capture nodes are skipped. So the Linux enumerator returns
    // {deviceID -> name} for capture-capable nodes only, and likewise feeds both
    // scanVideoDevicesLinux() and availableDeviceIDs().
    QMap<int, QString> enumerateVideoDevicesLinux();
    QString scanVideoDevicesWindows();
    QString scanVideoDevicesLinux();
    QString scanVideoDevicesMac();

    QString m_versionNumber;
    QString m_buildInfo;
    QString m_userConfigFileName;
    QString m_userConfigDisplay;
    QString m_configCheckNotes;
    bool m_userConfigOK;
    bool m_hasDevices = false;
    QJsonObject m_userConfig;
    // Snapshot of m_userConfig as of the last load/save; the dirty flag is
    // "current config != this". Empty for a brand-new unsaved config.
    QJsonObject m_savedConfig;
    bool m_configDirty = false;
    QJsonObject m_configProps;
    QJsonObject m_deviceCatalog;   // deviceConfigs/videoDevices.json (device types + defaults)

    // Break down of different types in user config file
    // 'uc' stands for userConfig
    QString dataDirectory;

    QJsonObject ucExperiment;
    QJsonObject ucMiniscopes;
    QJsonObject ucBehaviorCams;
    QJsonObject ucBehaviorTracker;
    QJsonObject ucTraceDisplay;
    QJsonObject ucCommutator;

    QVector<Miniscope*> miniscope;
    QVector<BehaviorCam*> behavCam;
    ControlPanel *controlPanel;
    TraceDisplayBackend *traceDisplay;

    DataSaver *dataSaver;
    QThread *dataSaverThread;

    // Optional Open Ephys commutator driver, on its own thread. Null unless the
    // config enables it and a source Miniscope is found.
    Commutator *commutator = nullptr;
    QThread *commutatorThread = nullptr;

    BehaviorTracker *behavTracker;

    QVector<QString> m_availableCodec;
    QString m_availableCodecList;
    QVector<QString> unAvailableCodec;

    qint64 m_softwareStartTime;
};

#endif // BACKEND_H
