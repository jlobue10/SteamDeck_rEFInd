#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <string>
#include <QMainWindow>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

using std::string;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    string Get_FW_BootNum();
    string CreateBootStanza(QString &BootOption, const char *BootNum, bool FW_BootNum);
    string getDefaultBoot(QString &DefaultBootOption, bool Last_OS, bool FW_BootNum_boot_bool, QString &Boot_Opt1, QString &Boot_Opt2, QString &Boot_Opt3, QString &Boot_Opt4);
    string getPartitionGUIDLabel(string &GUID_Source);

private slots:
    void on_Background_pushButton_clicked();

    void on_Boot_Option_01_Icon_pushButton_clicked();

    void on_Boot_Option_02_Icon_pushButton_clicked();

    void on_Boot_Option_03_Icon_pushButton_clicked();

    void on_Boot_Option_04_Icon_pushButton_clicked();

    void on_Install_rEFInd_clicked();

    void on_Create_Config_clicked();

    void on_Install_Config_clicked();

    void readSettings();

    void writeSettings();

    void on_About_pushButton_clicked();

private:
    Ui::MainWindow *ui;
};
#endif // MAINWINDOW_H
