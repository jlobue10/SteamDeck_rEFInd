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
    void on_updateButton_Clicked();
    void on_Enable_sysd_pushButton_clicked();
    void on_Disable_sysd_pushButton_clicked();
    void on_Rand_BG_On_pushButton_clicked();
    void on_Rand_BG_Off_pushButton_clicked();

private:
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

    Ui::MainWindow *ui;
    QString homePath;
    QString guiDataDir;   // Platform::dataDir()
    QString guiConfigDir; // <dataDir>/GUI
    QString settingsPath;
    OSDetector detector;
    QList<BootEntry> detected;
    bool populating = false;
};
#endif // MAINWINDOW_H
