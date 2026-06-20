#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
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
    QStandardItem *handleJsonObject(QStandardItem* parent, QJsonObject obj, QJsonObject objProps);
    QStandardItem *handleJsonArray(QStandardItem* parent, QJsonArray arry, QString type);
    void generateUserConfigFromModel();
    QJsonObject getObjectFromModel(QModelIndex index);
    QJsonArray getArrayFromModel(QModelIndex index);
    Q_INVOKABLE void saveConfigObject();
    // Save the (edited) user config to a user-chosen path from the Save-As dialog.
    Q_INVOKABLE void saveConfigObjectAs(const QString &filePath);


    void loadUserConfigFile();
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

    QString m_versionNumber;
    QString m_buildInfo;
    QString m_userConfigFileName;
    QString m_userConfigDisplay;
    bool m_userConfigOK;
    QJsonObject m_userConfig;
    QJsonObject m_configProps;

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
