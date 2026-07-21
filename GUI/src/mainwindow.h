#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "osdetect.h"

#include <QComboBox>
#include <QLineEdit>
#include <QList>
#include <QMainWindow>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class QNetworkAccessManager;
class QThread;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_Background_pushButton_clicked();
    void on_Boot_Option_01_Icon_pushButton_clicked();
    void on_Boot_Option_02_Icon_pushButton_clicked();
    void on_Boot_Option_03_Icon_pushButton_clicked();
    void on_Boot_Option_04_Icon_pushButton_clicked();
    void on_Install_rEFInd_clicked();
    void on_Create_Config_clicked();
    void on_Install_Config_clicked();
    void on_About_pushButton_clicked();
    void on_Background_lineEdit_editingFinished();
    void on_Boot_Option_01_Icon_lineEdit_editingFinished();
    void on_Boot_Option_02_Icon_lineEdit_editingFinished();
    void on_Boot_Option_03_Icon_lineEdit_editingFinished();
    void on_Boot_Option_04_Icon_lineEdit_editingFinished();
    void on_Rescan_pushButton_clicked();
    void on_Deep_Scan_pushButton_clicked();
    void on_updateButton_Clicked();
    void on_Enable_sysd_pushButton_clicked();
    void on_Disable_sysd_pushButton_clicked();
    void on_Rand_BG_On_pushButton_clicked();
    void on_Rand_BG_Off_pushButton_clicked();
    void on_Open_Folder_pushButton_clicked();
    void on_Preview_pushButton_clicked();
    void on_Language_comboBox_currentIndexChanged(int index);

protected:
    void changeEvent(QEvent *event) override;

private:
    // One generated stanza: the entry plus its 1-based icon slot.
    struct Selection {
        BootEntry entry;
        int slot;
    };

    void readSettings();
    void writeSettings();
    QList<QComboBox *> bootCombos() const;
    QList<BootEntry> comboOptions();
    void populateBootCombos();
    void applyAutoSelection();
    void compactBootSelections();
    void refreshDefaultBootCombo();
    static void setComboText(QComboBox *combo, const QString &text);
    void browsePng(QLineEdit *edit, const QString &title);
    void checkPNGFile(QLineEdit *edit);
    bool copyPng(QLineEdit *edit, const QString &destPath);
    QString createBootStanza(const BootEntry &entry, int slot);
    QString steamFirmwareBootNum();
    QList<Selection> currentSelections();
    QString generateConfigText(const QList<Selection> &selections);
    void onUpdateReply(bool ok, const QString &remoteRaw, const QString &errorString);
    void startDetection(bool resetToDefaults);
    void detectionFinished(const QList<BootEntry> &result, bool resetToDefaults);
    void setScanningUi(bool scanning);
    void applyDynamicTexts();
    void populateLanguageCombo();
    void appendLog(const QString &event, const QString &details = QString());
    static QString entryKey(const BootEntry &entry);
    void setComboByKeyOrText(QComboBox *combo, const QString &key, const QString &text);

    Ui::MainWindow *ui;
    QString homePath;
    QString lastBrowseDir; // last folder a browse dialog picked from
    QString guiDataDir;   // Platform::dataDir()
    QString guiConfigDir; // <dataDir>/GUI
    QString settingsPath;
    OSDetector detector;
    QList<BootEntry> detected;
    bool populating = false;
    QThread *scanThread = nullptr;          // active background detection, if any
    QNetworkAccessManager *network = nullptr; // lazy, for the update check
};
#endif // MAINWINDOW_H
