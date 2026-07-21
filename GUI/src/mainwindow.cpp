#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "osdetect.h"
#include "platform.h"
#include "previewdialog.h"
#include "uitranslation.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIntValidator>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTextStream>
#include <QThread>
#include <QUrl>
#include <QVariant>
#include <QVersionNumber>

static const char APP_VERSION[] = "2.7.0";
static const char VERSION_URL[] = "https://raw.githubusercontent.com/jlobue10/SteamDeck_rEFInd/main/VERSION";
// The user-visible "empty slot" combo entry. A function, not a file-static
// QString: statics are initialized before main() installs the translator, so
// a static could never be translated. Settings store this by text like every
// other combo entry; a saved value from another UI language simply fails the
// findText lookup and the slot falls back to None/defaults.
static QString noneOption()
{
    return MainWindow::tr("None");
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->TimeOut_lineEdit->setValidator(new QIntValidator(-1, 99, this));

    // OS icon size on the boot screen (rEFInd's big_icon_size). 128 is both
    // rEFInd's default and the shipped PNGs' native size, so it emits no
    // directive; larger sizes upscale the icons. Item texts are (re)applied
    // in applyDynamicTexts() so a runtime language switch refreshes them.
    ui->Icon_Size_comboBox->addItem(QString(), 96);
    ui->Icon_Size_comboBox->addItem(QString(), 128);
    ui->Icon_Size_comboBox->addItem(QString(), 160);
    ui->Icon_Size_comboBox->addItem(QString(), 192);
    ui->Icon_Size_comboBox->addItem(QString(), 256);
    ui->Icon_Size_comboBox->addItem(QString(), 512);
    ui->Icon_Size_comboBox->setCurrentIndex(ui->Icon_Size_comboBox->findData(128));

    homePath = QDir::homePath();
    guiDataDir = Platform::dataDir();
    guiConfigDir = guiDataDir + "/GUI";
    settingsPath = guiConfigDir + "/rEFInd_GUI.ini";
    QDir().mkpath(guiConfigDir);

    // Placeholder hints must reflect the real data dir on each platform
    // (Windows shows %LOCALAPPDATA%, not ~/.local).
    auto pathHint = [this](const QString &name) {
        QString p = guiConfigDir + "/" + name;
        if (p.startsWith(homePath))
            p = "~" + p.mid(homePath.length());
        return QDir::toNativeSeparators(p);
    };
    ui->Background_lineEdit->setPlaceholderText(pathHint(QStringLiteral("background.png")));
    ui->Boot_Option_01_Icon_lineEdit->setPlaceholderText(pathHint(QStringLiteral("os_icon1.png")));
    ui->Boot_Option_02_Icon_lineEdit->setPlaceholderText(pathHint(QStringLiteral("os_icon2.png")));
    ui->Boot_Option_03_Icon_lineEdit->setPlaceholderText(pathHint(QStringLiteral("os_icon3.png")));
    ui->Boot_Option_04_Icon_lineEdit->setPlaceholderText(pathHint(QStringLiteral("os_icon4.png")));

    if (!Platform::firmwareBootnumSupported())
        ui->Firmware_bootnum_CheckBox->setEnabled(false);
    if (!Platform::systemdFeaturesAvailable()) {
        // The bootnext-refind.service toggles are systemd-only.
        ui->Enable_sysd_pushButton->setEnabled(false);
        ui->Disable_sysd_pushButton->setEnabled(false);
    }
    ui->Install_Source_comboBox->clear();
    ui->Install_Source_comboBox->addItems(Platform::installSourceOptions());
    applyDynamicTexts();
    populateLanguageCombo();

    const QList<QComboBox *> combos = bootCombos();
    for (QComboBox *combo : combos) {
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { refreshDefaultBootCombo(); });
    }

    // Detection shells out (lsblk / PowerShell) and can take seconds; run it
    // off the GUI thread so the window appears immediately. Settings load
    // once the combos have real contents to select from.
    startDetection(false);
}

// Texts set from code rather than the .ui, re-applied after a runtime
// language switch (retranslateUi only covers .ui strings).
void MainWindow::applyDynamicTexts()
{
    ui->Icon_Size_comboBox->setItemText(ui->Icon_Size_comboBox->findData(96), tr("Small (96)"));
    ui->Icon_Size_comboBox->setItemText(ui->Icon_Size_comboBox->findData(128), tr("Default (128)"));
    ui->Icon_Size_comboBox->setItemText(ui->Icon_Size_comboBox->findData(160), tr("Medium (160)"));
    ui->Icon_Size_comboBox->setItemText(ui->Icon_Size_comboBox->findData(192), tr("Large (192)"));
    ui->Icon_Size_comboBox->setItemText(ui->Icon_Size_comboBox->findData(256), tr("Extra Large (256)"));
    ui->Icon_Size_comboBox->setItemText(ui->Icon_Size_comboBox->findData(512), tr("XXL (512)"));
    if (!Platform::firmwareBootnumSupported())
        ui->Firmware_bootnum_CheckBox->setToolTip(tr("Requires efibootmgr (Linux only)"));
    if (!Platform::systemdFeaturesAvailable()) {
        ui->Enable_sysd_pushButton->setToolTip(tr("systemd service (Linux only)"));
        ui->Disable_sysd_pushButton->setToolTip(tr("systemd service (Linux only)"));
    }
    if (!Platform::espDeepScanUseful()) {
        // Every ESP is readable already (or this is the elevated Windows
        // build), so a privileged scan would find nothing extra.
        ui->Deep_Scan_pushButton->setToolTip(tr("Not needed: no unreadable EFI System Partition was found"));
    }
}

void MainWindow::populateLanguageCombo()
{
    const bool wasPopulating = populating;
    populating = true;
    ui->Language_comboBox->clear();
    ui->Language_comboBox->addItem(tr("System default"), QString());
    const QStringList codes = UiTranslation::availableLanguages();
    for (const QString &code : codes) {
        QString name = code == QLatin1String("en_US")
                           ? QStringLiteral("English")
                           : QLocale(code).nativeLanguageName();
        if (name.isEmpty())
            name = code; // this Qt predates the code (e.g. Sicilian before 6.7)
        else
            name[0] = name.at(0).toUpper();
        ui->Language_comboBox->addItem(name, code);
    }
    const int idx = ui->Language_comboBox->findData(UiTranslation::saved());
    ui->Language_comboBox->setCurrentIndex(idx >= 0 ? idx : 0);
    populating = wasPopulating;
}

void MainWindow::on_Language_comboBox_currentIndexChanged(int index)
{
    if (populating || index < 0)
        return;
    const QString code = ui->Language_comboBox->itemData(index).toString();
    UiTranslation::save(code);
    // Replacing the translators makes Qt broadcast LanguageChange, which
    // changeEvent() below turns into a full retranslate (and, for RTL
    // languages, a mirrored layout).
    UiTranslation::apply(code);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        applyDynamicTexts();
        populateLanguageCombo();
        // Refresh the translated "None" entries; selections are preserved by
        // key/text where they still match, and fall back to None otherwise.
        populateBootCombos();
    }
    QMainWindow::changeEvent(event);
}

MainWindow::~MainWindow()
{
    // A background detection uses this object's detector member; let it
    // drain before teardown (its queued result callback is then discarded).
    if (scanThread)
        scanThread->wait();
    writeSettings();
    delete ui;
}

void MainWindow::startDetection(bool resetToDefaults)
{
    if (scanThread)
        return; // a scan is already running
    setScanningUi(true);
    QThread *thread = QThread::create([this, resetToDefaults] {
        const QList<BootEntry> result = detector.detect();
        QMetaObject::invokeMethod(this, [this, result, resetToDefaults] {
            detectionFinished(result, resetToDefaults);
        }, Qt::QueuedConnection);
    });
    scanThread = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        if (scanThread == thread)
            scanThread = nullptr;
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::detectionFinished(const QList<BootEntry> &result, bool resetToDefaults)
{
    detected = result;
    setScanningUi(false);
    if (resetToDefaults) {
        // Rescan: discard any manual arrangement and re-apply the packed
        // defaults with the preferred OS leading.
        populateBootCombos();
        applyAutoSelection();
    } else {
        // First run: settings restore the saved arrangement (or defaults).
        readSettings();
    }
    QStringList names;
    for (const BootEntry &e : detected)
        names << e.displayName;
    appendLog(QStringLiteral("detect: %1 entries [%2]")
                  .arg(detected.size()).arg(names.join(QStringLiteral(", "))));
}

void MainWindow::setScanningUi(bool scanning)
{
    const QList<QComboBox *> combos = bootCombos();
    if (scanning) {
        populating = true;
        for (QComboBox *combo : combos) {
            combo->clear();
            combo->addItem(tr("Scanning…"));
        }
        ui->Default_Boot_comboBox->clear();
        populating = false;
    }
    for (QComboBox *combo : combos)
        combo->setEnabled(!scanning);
    ui->Default_Boot_comboBox->setEnabled(!scanning);
    ui->Rescan_pushButton->setEnabled(!scanning);
    ui->Deep_Scan_pushButton->setEnabled(!scanning && Platform::espDeepScanUseful());
    ui->Create_Config->setEnabled(!scanning);
    ui->Preview_pushButton->setEnabled(!scanning);
}

QList<QComboBox *> MainWindow::bootCombos() const
{
    return {ui->Boot_Option_01_comboBox, ui->Boot_Option_02_comboBox,
            ui->Boot_Option_03_comboBox, ui->Boot_Option_04_comboBox};
}

// Detected entries plus static fallbacks for removable media that may not be
// inserted right now.
QList<BootEntry> MainWindow::comboOptions()
{
    QList<BootEntry> options = detected;
    auto addFallback = [&options](const BootEntry &e) {
        for (const BootEntry &existing : options) {
            if (existing.displayName == e.displayName)
                return;
        }
        options.append(e);
    };
    addFallback({QStringLiteral("Windows (SD)"), QStringLiteral("Windows Micro SD"),
                 QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi"),
                 detector.removableEspPartUuid(true), false});
    addFallback({QStringLiteral("Windows (USB)"), QStringLiteral("Windows USB"),
                 QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi"),
                 detector.removableEspPartUuid(false), false});
    addFallback({QStringLiteral("Ventoy"), QStringLiteral("Ventoy"),
                 QStringLiteral("/EFI/BOOT/grubx64_real.efi"), QStringLiteral("VTOYEFI"), false});
    addFallback({QStringLiteral("Batocera (SD)"), QStringLiteral("Batocera"),
                 QStringLiteral("/EFI/BOOT/bootx64.efi"), QStringLiteral("BATOCERA"), false});
    return options;
}

void MainWindow::populateBootCombos()
{
    populating = true;
    const QList<BootEntry> options = comboOptions();
    const QList<QComboBox *> combos = bootCombos();
    for (QComboBox *combo : combos) {
        const QString previous = combo->currentText();
        combo->clear();
        for (const BootEntry &e : options) {
            combo->addItem(e.displayName, QVariant::fromValue(e));
            // Deliberately untranslated: these are refind.conf directive
            // names, shown so multi-ESP setups can tell lookalikes apart.
            QString tip = QStringLiteral("loader %1").arg(e.loaderPath);
            if (!e.volume.isEmpty())
                tip += QStringLiteral("\nvolume %1").arg(e.volume);
            combo->setItemData(combo->count() - 1, tip, Qt::ToolTipRole);
        }
        combo->addItem(noneOption());
        const int idx = combo->findText(previous);
        combo->setCurrentIndex(idx >= 0 ? idx : combo->count() - 1);
    }
    populating = false;
    refreshDefaultBootCombo();
}

void MainWindow::setComboText(QComboBox *combo, const QString &text)
{
    const int idx = combo->findText(text);
    if (idx >= 0)
        combo->setCurrentIndex(idx);
}

// Language-independent identity of a detected entry for settings persistence:
// display text survives neither renames nor (for "None") UI language changes,
// so the key is preferred and text kept as the legacy fallback.
QString MainWindow::entryKey(const BootEntry &entry)
{
    return entry.volume + QLatin1Char('|') + entry.loaderPath;
}

void MainWindow::setComboByKeyOrText(QComboBox *combo, const QString &key, const QString &text)
{
    if (!key.isEmpty()) {
        for (int i = 0; i < combo->count(); ++i) {
            const QVariant data = combo->itemData(i);
            if (data.canConvert<BootEntry>() && entryKey(data.value<BootEntry>()) == key) {
                combo->setCurrentIndex(i);
                return;
            }
        }
    }
    setComboText(combo, text);
}

// First-run defaults: the platform's preferred OS leads (SteamOS on the Deck,
// Windows on the Windows build), then the others. Entries pack into slots
// 1, 2, ... with no gaps; the leading OS is also the default boot.
void MainWindow::applyAutoSelection()
{
    QString windowsPick, linuxPick, steamPick;
    for (const BootEntry &e : detected) {
        if (e.displayName == QLatin1String("Windows")) {
            windowsPick = e.displayName;
            break;
        }
    }
    for (const BootEntry &e : detected) {
        if (!e.supportsFirmwareBootnum && !e.loaderPath.contains(QLatin1String("Microsoft"))) {
            linuxPick = e.displayName;
            break;
        }
    }
    for (const BootEntry &e : detected) {
        if (e.supportsFirmwareBootnum) {
            steamPick = e.displayName;
            break;
        }
    }

    QStringList ordered;
    if (Platform::preferWindowsAsDefault())
        ordered << windowsPick << steamPick << linuxPick;
    else
        ordered << steamPick << windowsPick << linuxPick;

    QStringList picks;
    for (const QString &name : ordered) {
        if (!name.isEmpty())
            picks << name;
    }

    const QList<QComboBox *> combos = bootCombos();
    for (int i = 0; i < combos.size(); ++i)
        setComboText(combos.at(i), i < picks.size() ? picks.at(i) : noneOption());

    if (!picks.isEmpty())
        setComboText(ui->Default_Boot_comboBox, picks.first());
}

// Pull selected OSes toward slot 1 with no gaps, preserving their order, so a
// saved settings file that left slots 1/2 as None with OSes in 3/4 is packed
// back into 1, 2, ... on load.
void MainWindow::compactBootSelections()
{
    const QList<QComboBox *> combos = bootCombos();
    QStringList chosen;
    for (QComboBox *combo : combos) {
        const QString text = combo->currentText();
        if (text != noneOption() && !text.isEmpty())
            chosen << text;
    }
    for (int i = 0; i < combos.size(); ++i)
        setComboText(combos.at(i), i < chosen.size() ? chosen.at(i) : noneOption());
}

void MainWindow::refreshDefaultBootCombo()
{
    if (populating)
        return;
    const QString previous = ui->Default_Boot_comboBox->currentText();
    ui->Default_Boot_comboBox->clear();
    const QList<QComboBox *> combos = bootCombos();
    for (QComboBox *combo : combos) {
        const QString text = combo->currentText();
        if (text != noneOption() && !text.isEmpty()
            && ui->Default_Boot_comboBox->findText(text) < 0)
            ui->Default_Boot_comboBox->addItem(text);
    }
    setComboText(ui->Default_Boot_comboBox, previous);
}

void MainWindow::on_Rescan_pushButton_clicked()
{
    startDetection(true);
}

void MainWindow::on_Deep_Scan_pushButton_clicked()
{
    // Blocks while the script prompts for a password; it shows its own
    // success/error dialogs, so only re-detect here. Detection prefers the
    // cache the script just wrote over the firmware boot entries.
    const int rc = Platform::runEspDeepScan();
    appendLog(QStringLiteral("deep scan: rc %1").arg(rc));
    if (rc != 0)
        return;
    on_Rescan_pushButton_clicked();
}

void MainWindow::browsePng(QLineEdit *edit, const QString &title)
{
    // Reopen the folder the previous browse picked from; fall back to the
    // home directory when there's no history or that folder no longer
    // exists (deleted, or a settings file carried over from another setup).
    QString startDir = lastBrowseDir;
    if (startDir.isEmpty() || !QDir(startDir).exists())
        startDir = homePath;
    const QString fileName = QFileDialog::getOpenFileName(this, title, startDir, tr("Image (*.png)"));
    if (!fileName.isEmpty()) {
        edit->setText(fileName);
        lastBrowseDir = QFileInfo(fileName).absolutePath();
        // Persist immediately rather than waiting for the destructor's
        // writeSettings: on handhelds the app is often force-terminated
        // (Steam/Xbox overlay, task switcher, suspend), which skips destructors.
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("Paths"));
        settings.setValue(QStringLiteral("LastBrowseDir"), lastBrowseDir);
        settings.endGroup();
    }
}

void MainWindow::on_Background_pushButton_clicked()
{
    browsePng(ui->Background_lineEdit, tr("Select Background PNG"));
}

void MainWindow::on_Boot_Option_01_Icon_pushButton_clicked()
{
    browsePng(ui->Boot_Option_01_Icon_lineEdit, tr("Select OS Icon 1 PNG"));
}

void MainWindow::on_Boot_Option_02_Icon_pushButton_clicked()
{
    browsePng(ui->Boot_Option_02_Icon_lineEdit, tr("Select OS Icon 2 PNG"));
}

void MainWindow::on_Boot_Option_03_Icon_pushButton_clicked()
{
    browsePng(ui->Boot_Option_03_Icon_lineEdit, tr("Select OS Icon 3 PNG"));
}

void MainWindow::on_Boot_Option_04_Icon_pushButton_clicked()
{
    browsePng(ui->Boot_Option_04_Icon_lineEdit, tr("Select OS Icon 4 PNG"));
}

void MainWindow::on_Install_rEFInd_clicked()
{
    if (!Platform::runInstallerScript(ui->Install_Source_comboBox->currentText()))
        QMessageBox::warning(this, tr("Install rEFInd"),
                             tr("Failed to launch the installation script."));
}

QString MainWindow::steamFirmwareBootNum()
{
    bool ok = false;
    const QString out = OSDetector::runCommand(QStringLiteral("efibootmgr"), {}, &ok);
    if (!ok)
        return {};
    static const QRegularExpression re(QStringLiteral("^Boot([0-9A-Fa-f]{4})\\*?\\s+.*steam"),
                                       QRegularExpression::CaseInsensitiveOption
                                           | QRegularExpression::MultilineOption);
    const QRegularExpressionMatch match = re.match(out);
    return match.hasMatch() ? match.captured(1) : QString();
}

QString MainWindow::createBootStanza(const BootEntry &entry, int slot)
{
    QString stanza;
    QTextStream out(&stanza);
    out << "\nmenuentry \"" << entry.menuName << "\" {\n";
    out << "\ticon /EFI/refind/os_icon" << slot << ".png\n";
    if (entry.supportsFirmwareBootnum && ui->Firmware_bootnum_CheckBox->isChecked()) {
        const QString bootNum = steamFirmwareBootNum();
        if (!bootNum.isEmpty()) {
            out << "\tfirmware_bootnum " << bootNum << "\n";
            out << "}\n";
            return stanza;
        }
        // Lookup failed: fall through to the regular loader entry.
    }
    if (!entry.volume.isEmpty())
        out << "\tvolume \"" << entry.volume << "\"\n";
    out << "\tloader " << entry.loaderPath << "\n";
    out << "\tgraphics on\n}\n";
    return stanza;
}

// The four slots' current entries, packed with their 1-based slot numbers
// (slot number = icon file number = on-screen order).
QList<MainWindow::Selection> MainWindow::currentSelections()
{
    QList<Selection> selections;
    const QList<QComboBox *> combos = bootCombos();
    for (int i = 0; i < combos.size(); ++i) {
        QComboBox *combo = combos.at(i);
        if (combo->currentText() == noneOption() || combo->currentText().isEmpty())
            continue;
        const QVariant data = combo->currentData();
        if (!data.canConvert<BootEntry>())
            continue;
        selections.append({data.value<BootEntry>(), i + 1});
    }
    return selections;
}

// Renders the full refind.conf as text — shared by Create Config (which
// writes it) and the Preview dialog (which only displays it).
QString MainWindow::generateConfigText(const QList<Selection> &selections)
{
    QString text;
    QString timeout = ui->TimeOut_lineEdit->text();
    if (timeout.isEmpty())
        timeout = QStringLiteral("5");

    QTextStream out(&text);
    out << "# GUI generated refind.conf Config File\n";
    out << "timeout " << timeout << "\n";
    out << "use_nvram false\n";
    out << "hideui singleuser,hints,arrows,label,badges\n";
    out << "banner background.png\n";
    out << "banner_scale fillscreen\n";
    out << "resolution 3\n";
    const int iconSize = ui->Icon_Size_comboBox->currentData().toInt();
    if (iconSize > 0 && iconSize != 128) {
        out << "big_icon_size " << iconSize << "\n";
        // Keep the tools row proportional (rEFInd defaults: big 128, small 48).
        out << "small_icon_size " << iconSize * 48 / 128 << "\n";
    }
    out << "enable_touch\n";
    out << (ui->Enable_Mouse_checkBox->isChecked() ? "" : "#") << "enable_mouse\n";
    out << "log_level 0\n";
    out << "showtools\n";
    out << "scanfor manual\n";

    // default_selection: position of the chosen default among generated
    // stanzas (scanfor is manual-only, so row numbers match stanza order).
    const QString defaultName = ui->Default_Boot_comboBox->currentText();
    QString defaultSelection;
    if (ui->Last_OS_CheckBox->isChecked())
        defaultSelection = QStringLiteral("+");
    for (int i = 0; i < selections.size(); ++i) {
        if (selections.at(i).entry.displayName == defaultName) {
            if (!defaultSelection.isEmpty())
                defaultSelection += QLatin1Char(',');
            defaultSelection += QString::number(i + 1);
            break;
        }
    }
    if (defaultSelection.isEmpty())
        defaultSelection = QStringLiteral("1");
    out << "default_selection \"" << defaultSelection << "\"\n";

    for (const Selection &sel : selections)
        out << createBootStanza(sel.entry, sel.slot);
    return text;
}

void MainWindow::on_Create_Config_clicked()
{
    QDir().mkpath(guiConfigDir);
    QFile conf(guiConfigDir + "/refind.conf");
    if (!conf.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::critical(this, tr("Create Config"),
                              tr("Could not write %1").arg(conf.fileName()));
        return;
    }
    conf.write(generateConfigText(currentSelections()).toUtf8());
    conf.close();

    copyPng(ui->Background_lineEdit, guiConfigDir + "/background.png");
    copyPng(ui->Boot_Option_01_Icon_lineEdit, guiConfigDir + "/os_icon1.png");
    copyPng(ui->Boot_Option_02_Icon_lineEdit, guiConfigDir + "/os_icon2.png");
    copyPng(ui->Boot_Option_03_Icon_lineEdit, guiConfigDir + "/os_icon3.png");
    copyPng(ui->Boot_Option_04_Icon_lineEdit, guiConfigDir + "/os_icon4.png");
}

void MainWindow::on_Preview_pushButton_clicked()
{
    const QList<Selection> selections = currentSelections();
    const QString confText = generateConfigText(selections);

    const QList<QLineEdit *> iconEdits = {ui->Boot_Option_01_Icon_lineEdit,
                                          ui->Boot_Option_02_Icon_lineEdit,
                                          ui->Boot_Option_03_Icon_lineEdit,
                                          ui->Boot_Option_04_Icon_lineEdit};
    QList<PreviewEntry> entries;
    for (const Selection &sel : selections) {
        PreviewEntry e;
        e.name = sel.entry.displayName;
        e.iconPath = iconEdits.at(sel.slot - 1)->text();
        if (e.iconPath.isEmpty()) // fall back to a previously staged copy
            e.iconPath = guiConfigDir + QStringLiteral("/os_icon%1.png").arg(sel.slot);
        entries << e;
    }

    const QString defaultName = ui->Default_Boot_comboBox->currentText();
    int defaultIndex = 0;
    for (int i = 0; i < selections.size(); ++i) {
        if (selections.at(i).entry.displayName == defaultName) {
            defaultIndex = i;
            break;
        }
    }

    QString background = ui->Background_lineEdit->text();
    if (background.isEmpty())
        background = guiConfigDir + QStringLiteral("/background.png");
    int iconSize = ui->Icon_Size_comboBox->currentData().toInt();
    if (iconSize <= 0)
        iconSize = 128;

    PreviewDialog dialog(background, entries, iconSize, defaultIndex, confText, this);
    dialog.exec();
}

// Append one timestamped event (plus optional multi-line detail) to the
// session-persistent log — most bug reports arrive without the dialog text,
// so keep a copy the user can attach. English on purpose: it's diagnostics.
void MainWindow::appendLog(const QString &event, const QString &details)
{
    const QString dir = guiConfigDir + QStringLiteral("/logs");
    QDir().mkpath(dir);
    const QString path =
        dir + QLatin1Char('/') + QCoreApplication::applicationName() + QStringLiteral(".log");
    QFile file(path);
    if (file.size() > 512 * 1024) {
        // Single rotation keeps the pair bounded at ~1 MB.
        QFile::remove(path + QStringLiteral(".old"));
        QFile::rename(path, path + QStringLiteral(".old"));
    }
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&file);
    out << QDateTime::currentDateTime().toString(Qt::ISODate) << ' ' << event << '\n';
    if (!details.isEmpty())
        out << details.trimmed() << '\n';
}

// Dialog-sized excerpt of a script's captured output: PowerShell failures can
// dump long error records, and the useful line is at the end.
static QString outputTail(const QString &output)
{
    const QStringList lines = output.trimmed().split('\n');
    if (lines.size() <= 20)
        return lines.join('\n');
    return QStringLiteral("[...]\n") + QStringList(lines.mid(lines.size() - 20)).join('\n');
}

void MainWindow::on_Install_Config_clicked()
{
    QString badScript;
    if (!Platform::installConfigScriptTrusted(&badScript)) {
        appendLog(QStringLiteral("install config: refused, untrusted script"), badScript);
        QMessageBox::warning(this, tr("Install Config"),
                             tr("The config-install script was NOT run:\n\n%1\n\n"
                                "It does not match the copy shipped with this version of the "
                                "app. Because it runs with root privileges, it is only ever "
                                "run when it is byte-for-byte the shipped version — a mismatch "
                                "means it was modified (possibly tampered with) or belongs to "
                                "a different version.\n\n"
                                "Reinstall the GUI to restore the original script, then try "
                                "again.").arg(badScript));
        return;
    }
    QString output;
    const int rc = Platform::installConfig(&output);
    appendLog(QStringLiteral("install config: rc %1").arg(rc), output);
    if (Platform::installConfigShowsOwnDialogs()) {
        // The Linux script shows its own zenity password + result dialogs, so
        // a nonzero return only means the launch itself failed.
        if (rc != 0)
            QMessageBox::critical(this, tr("Install Config"),
                                  tr("Installing the config failed (code %1).").arg(rc));
        return;
    }
    const QString details = outputTail(output);
    if (rc == 0) {
        QMessageBox::information(this, tr("Install Config"),
                                 details.isEmpty()
                                     ? tr("The config was installed successfully.")
                                     : tr("The config was installed successfully.\n\n%1").arg(details));
    } else {
        QMessageBox::critical(this, tr("Install Config"),
                              details.isEmpty()
                                  ? tr("Installing the config failed (code %1).").arg(rc)
                                  : tr("Installing the config failed (code %1).\n\n%2")
                                        .arg(rc).arg(details));
    }
}

bool MainWindow::copyPng(QLineEdit *edit, const QString &destPath)
{
    checkPNGFile(edit);
    const QString source = edit->text();
    if (source.isEmpty() || source == destPath)
        return true;
    if (QFile::exists(destPath))
        QFile::remove(destPath);
    if (!QFile::copy(source, destPath)) {
        QMessageBox::warning(this, tr("Copy PNG"),
                             tr("Could not copy %1 to %2").arg(source, destPath));
        return false;
    }
    return true;
}

void MainWindow::checkPNGFile(QLineEdit *edit)
{
    const QString path = edit->text();
    if (path.isEmpty())
        return;
    const QFileInfo fileInfo(path);
    bool valid = fileInfo.exists() && fileInfo.isFile()
                 && fileInfo.suffix().toLower() == QLatin1String("png");
    if (valid) {
        // The extension check alone lets a renamed JPEG through; rEFInd would
        // then silently fail to render it, so require the PNG signature too.
        QFile file(path);
        valid = file.open(QIODevice::ReadOnly)
                && file.read(8) == QByteArrayLiteral("\x89PNG\r\n\x1a\n");
    }
    if (!valid) {
        // Clear before the dialog: the modal steals focus, which can re-fire
        // editingFinished, and the empty-path guard above stops the loop.
        edit->clear();
        QMessageBox::warning(this, tr("Invalid PNG"),
                             tr("%1 is not a valid PNG file.\n\nBackgrounds and OS "
                                "icons must be real PNG images (not just files with "
                                "a .png extension).")
                                 .arg(QDir::toNativeSeparators(path)));
    }
}

void MainWindow::on_Background_lineEdit_editingFinished()
{
    checkPNGFile(ui->Background_lineEdit);
}

void MainWindow::on_Boot_Option_01_Icon_lineEdit_editingFinished()
{
    checkPNGFile(ui->Boot_Option_01_Icon_lineEdit);
}

void MainWindow::on_Boot_Option_02_Icon_lineEdit_editingFinished()
{
    checkPNGFile(ui->Boot_Option_02_Icon_lineEdit);
}

void MainWindow::on_Boot_Option_03_Icon_lineEdit_editingFinished()
{
    checkPNGFile(ui->Boot_Option_03_Icon_lineEdit);
}

void MainWindow::on_Boot_Option_04_Icon_lineEdit_editingFinished()
{
    checkPNGFile(ui->Boot_Option_04_Icon_lineEdit);
}

void MainWindow::readSettings()
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("CheckBoxes"));
    ui->Last_OS_CheckBox->setChecked(settings.value(QStringLiteral("LastOSCheckBox")).toBool());
    // SteamOS firmware_bootnum defaults on for the Deck (efibootmgr present);
    // on the Windows build the feature is unsupported and the box is disabled.
    ui->Firmware_bootnum_CheckBox->setChecked(
        settings.value(QStringLiteral("FW_bootNum_CheckBox"), Platform::firmwareBootnumSupported()).toBool());
    ui->Enable_Mouse_checkBox->setChecked(settings.value(QStringLiteral("Enable_Mouse_CheckBox"), true).toBool());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("ComboBoxes"));
    const QString boot1 = settings.value(QStringLiteral("BootOption01Text")).toString();
    const QString boot2 = settings.value(QStringLiteral("BootOption02Text")).toString();
    const QString boot3 = settings.value(QStringLiteral("BootOption03Text")).toString();
    const QString boot4 = settings.value(QStringLiteral("BootOption04Text")).toString();
    // Preferred over the text keys where present (see entryKey); text remains
    // as the fallback for INIs written by older versions.
    const QStringList savedKeys = {
        settings.value(QStringLiteral("BootOption01Key")).toString(),
        settings.value(QStringLiteral("BootOption02Key")).toString(),
        settings.value(QStringLiteral("BootOption03Key")).toString(),
        settings.value(QStringLiteral("BootOption04Key")).toString()};
    const QString defaultBoot = settings.value(QStringLiteral("DefaultBootText")).toString();
    const int installSource = settings.value(QStringLiteral("InstallSourceComboBox")).toInt();
    const int iconSize = settings.value(QStringLiteral("IconSize")).toInt();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Timeout"));
    const QString timeout = settings.value(QStringLiteral("Timeout")).toString();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Paths"));
    lastBrowseDir = settings.value(QStringLiteral("LastBrowseDir")).toString();
    settings.endGroup();

    populateBootCombos();
    if (boot1.isEmpty() && boot2.isEmpty() && boot3.isEmpty() && boot4.isEmpty()) {
        applyAutoSelection();
    } else {
        const QList<QComboBox *> combos = bootCombos();
        const QStringList saved = {boot1, boot2, boot3, boot4};
        for (int i = 0; i < combos.size(); ++i)
            setComboByKeyOrText(combos.at(i), savedKeys.at(i), saved.at(i));
        // Repack any gaps a stale settings file may have left (OSes in 3/4,
        // 1/2 empty) so detected OSes always start at slot 1.
        compactBootSelections();
    }
    setComboText(ui->Default_Boot_comboBox, defaultBoot);
    ui->Install_Source_comboBox->setCurrentIndex(installSource);
    // Stored as the pixel value, not the index/text, so saved settings survive
    // relabeling; an unset key (0) keeps the constructor's 128 default.
    const int iconIdx = ui->Icon_Size_comboBox->findData(iconSize);
    if (iconIdx >= 0)
        ui->Icon_Size_comboBox->setCurrentIndex(iconIdx);
    if (!timeout.isEmpty())
        ui->TimeOut_lineEdit->setText(timeout);
}

void MainWindow::writeSettings()
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("ComboBoxes"));
    settings.setValue(QStringLiteral("BootOption01Text"), ui->Boot_Option_01_comboBox->currentText());
    settings.setValue(QStringLiteral("BootOption02Text"), ui->Boot_Option_02_comboBox->currentText());
    settings.setValue(QStringLiteral("BootOption03Text"), ui->Boot_Option_03_comboBox->currentText());
    settings.setValue(QStringLiteral("BootOption04Text"), ui->Boot_Option_04_comboBox->currentText());
    const auto comboKey = [](QComboBox *combo) {
        const QVariant data = combo->currentData();
        return data.canConvert<BootEntry>() ? entryKey(data.value<BootEntry>()) : QString();
    };
    settings.setValue(QStringLiteral("BootOption01Key"), comboKey(ui->Boot_Option_01_comboBox));
    settings.setValue(QStringLiteral("BootOption02Key"), comboKey(ui->Boot_Option_02_comboBox));
    settings.setValue(QStringLiteral("BootOption03Key"), comboKey(ui->Boot_Option_03_comboBox));
    settings.setValue(QStringLiteral("BootOption04Key"), comboKey(ui->Boot_Option_04_comboBox));
    settings.setValue(QStringLiteral("DefaultBootText"), ui->Default_Boot_comboBox->currentText());
    settings.setValue(QStringLiteral("InstallSourceComboBox"), ui->Install_Source_comboBox->currentIndex());
    settings.setValue(QStringLiteral("IconSize"), ui->Icon_Size_comboBox->currentData().toInt());
    settings.remove(QStringLiteral("LinuxComboBox"));
    settings.endGroup();
    settings.beginGroup(QStringLiteral("CheckBoxes"));
    settings.setValue(QStringLiteral("LastOSCheckBox"), ui->Last_OS_CheckBox->isChecked());
    settings.setValue(QStringLiteral("FW_bootNum_CheckBox"), ui->Firmware_bootnum_CheckBox->isChecked());
    settings.setValue(QStringLiteral("Enable_Mouse_CheckBox"), ui->Enable_Mouse_checkBox->isChecked());
    settings.endGroup();
    settings.beginGroup(QStringLiteral("Timeout"));
    settings.setValue(QStringLiteral("Timeout"), ui->TimeOut_lineEdit->text());
    settings.endGroup();
    settings.beginGroup(QStringLiteral("Paths"));
    settings.setValue(QStringLiteral("LastBrowseDir"), lastBrowseDir);
    settings.endGroup();
}

void MainWindow::on_About_pushButton_clicked()
{
    QMessageBox aboutBox;
    QPushButton *updateButton = new QPushButton(tr("Check For Update"), &aboutBox);
    connect(updateButton, &QPushButton::clicked, this, &MainWindow::on_updateButton_Clicked);
    aboutBox.setTextFormat(Qt::RichText);
    aboutBox.setText(tr("<p align='center'>"
                        "<a href='https://github.com/jlobue10/SteamDeck_rEFInd'>"
                        "rEFInd Customization GUI v%1</a><br><br>"
                        "Original GUI Creator: "
                        "<a href='https://github.com/jlobue10'>jlobue10</a><br><br>"
                        "Special Thanks to Deck Wizard for testing and QA"
                        "<br><br><a href='https://www.youtube.com/watch?v=yBHzVSDVEqw'>"
                        "Deck Wizard Dual Boot Tutorial</a><br></p>")
                         .arg(QLatin1String(APP_VERSION)));
    aboutBox.setStandardButtons(QMessageBox::Ok);
    aboutBox.addButton(updateButton, QMessageBox::ActionRole);
    aboutBox.exec();
}

void MainWindow::on_updateButton_Clicked()
{
    // Asynchronous on purpose: the old synchronous curl call froze the UI for
    // up to its 10-second timeout (and needed curl at runtime at all).
    QPointer<QPushButton> button = qobject_cast<QPushButton *>(sender());
    if (button)
        button->setEnabled(false);
    if (!network)
        network = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(QLatin1String(VERSION_URL))};
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    request.setTransferTimeout(10000);
#endif
    QNetworkReply *reply = network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, button] {
        reply->deleteLater();
        if (button)
            button->setEnabled(true);
        onUpdateReply(reply->error() == QNetworkReply::NoError,
                      QString::fromUtf8(reply->readAll()).trimmed(),
                      reply->errorString());
    });
}

void MainWindow::onUpdateReply(bool ok, const QString &remoteRaw, const QString &errorString)
{
    const QVersionNumber remote = QVersionNumber::fromString(remoteRaw);
    const QVersionNumber local = QVersionNumber::fromString(QLatin1String(APP_VERSION));
    appendLog(QStringLiteral("update check: local %1, remote \"%2\"%3")
                  .arg(QLatin1String(APP_VERSION), remoteRaw,
                       ok ? QString() : QStringLiteral(" (%1)").arg(errorString)));

    QMessageBox updateBox;
    updateBox.setTextFormat(Qt::RichText);
    if (!ok || remote.isNull()) {
        updateBox.setText(tr("<p align='center'>Update check failed. "
                             "Please check your internet connection and try again.<br><br></p>"));
    } else if (remote > local) {
        updateBox.setText(tr("<p align='center'>An update is available "
                             "<a href='https://github.com/jlobue10/SteamDeck_rEFInd/releases'>here</a>"
                             "<br><br></p>"));
    } else {
        updateBox.setText(tr("<p align='center'>No update found. "
                             "You are using the latest version.<br><br></p>"));
    }
    updateBox.setStandardButtons(QMessageBox::Ok);
    updateBox.exec();
}

void MainWindow::on_Enable_sysd_pushButton_clicked()
{
    if (!Platform::setBootnextService(true))
        QMessageBox::warning(this, tr("systemd service"),
                             tr("Failed to launch the service toggle."));
}

void MainWindow::on_Disable_sysd_pushButton_clicked()
{
    if (!Platform::setBootnextService(false))
        QMessageBox::warning(this, tr("systemd service"),
                             tr("Failed to launch the service toggle."));
}

void MainWindow::on_Rand_BG_On_pushButton_clicked()
{
    if (!Platform::setBackgroundRandomizer(true))
        QMessageBox::warning(this, tr("Background Randomizer"),
                             tr("Failed to launch the randomizer setup."));
}

void MainWindow::on_Rand_BG_Off_pushButton_clicked()
{
    if (!Platform::setBackgroundRandomizer(false))
        QMessageBox::warning(this, tr("Background Randomizer"),
                             tr("Failed to launch the randomizer setup."));
}

void MainWindow::on_Open_Folder_pushButton_clicked()
{
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(guiConfigDir)))
        QMessageBox::warning(this, tr("Open Folder"),
                             tr("Failed to open %1 in the file manager.")
                                 .arg(QDir::toNativeSeparators(guiConfigDir)));
}
