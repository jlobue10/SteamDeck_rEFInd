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
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVersionNumber>

static const char APP_VERSION[] = "3.4.1";
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

// Theme combo item *data* keys (the visible texts for None/Random are
// translated; theme names are folder-name identifiers and stay verbatim).
// An empty data string means None; this sentinel means "pick one at Create
// Config time". Settings store the data key, so a saved selection survives
// UI language switches.
static const char kRandomThemeKey[] = "*random*";

static QSize effectivePanelResolution(); // defined above generateConfigText()

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    // The window must track the grid's sizeHint so longer translated strings
    // widen it instead of squeezing; SetFixedSize keeps it non-user-resizable.
    ui->centralwidget->layout()->setSizeConstraint(QLayout::SetFixedSize);
    ui->TimeOut_lineEdit->setValidator(new QIntValidator(-1, 99, this));
    ui->Res_Width_lineEdit->setValidator(new QIntValidator(1, 9999, this));
    ui->Res_Height_lineEdit->setValidator(new QIntValidator(1, 9999, this));
    connect(ui->Res_Override_checkBox, &QCheckBox::toggled,
            ui->Res_Width_lineEdit, &QWidget::setEnabled);
    connect(ui->Res_Override_checkBox, &QCheckBox::toggled,
            ui->Res_Height_lineEdit, &QWidget::setEnabled);
    connect(ui->Showtools_lineEdit, &QLineEdit::textChanged,
            this, [this] { updateShowtoolsValidity(); });

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
    ui->Install_Source_comboBox->clear();
    ui->Install_Source_comboBox->addItems(Platform::installSourceOptions());
    applyDynamicTexts();
    populateThemeCombo();
    populateLanguageCombo();
    equalizeActionButtonWidths();

    const QList<QComboBox *> combos = bootCombos();
    for (QComboBox *combo : combos) {
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { refreshDefaultBootCombo(); });
    }

    // Every control except the boot-option combos (whose contents come from
    // the scan) restores before the window shows and persists as it is
    // changed — see readEarlySettings()/initSettingsPersistence().
    readEarlySettings();
    initSettingsPersistence();

    // Detection shells out (lsblk / PowerShell) and can take seconds; run it
    // off the GUI thread so the window appears immediately. Boot-option
    // selections load once the combos have real contents to select from.
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
    if (!Platform::espDeepScanUseful()) {
        // Every ESP is readable already (or this is the elevated Windows
        // build), so a privileged scan would find nothing extra.
        ui->Deep_Scan_pushButton->setToolTip(tr("Not needed: no unreadable EFI System Partition was found"));
    }
}

// The bottom action buttons form two visual columns across three
// independently laid-out rows (Rescan over Create Config; Deep Scan over
// Install Config over Preview). Each row right-packs its buttons, so giving
// every button in a column the width of its widest member keeps the column
// edges flush in every language.
void MainWindow::equalizeActionButtonWidths()
{
    const QList<QList<QPushButton *>> columns = {
        { ui->Rescan_pushButton, ui->Create_Config },
        { ui->Deep_Scan_pushButton, ui->Install_Config, ui->Preview_pushButton,
          ui->Install_Themes_pushButton },
    };
    for (const auto &column : columns) {
        int width = 140; // the designed shared minimum
        for (QPushButton *button : column) {
            button->setMinimumWidth(0);
            width = qMax(width, button->sizeHint().width());
        }
        for (QPushButton *button : column)
            button->setMinimumWidth(width);
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

// First directory whose themes/<name>/theme.conf entries are usable: the
// per-user data dir (staged by install-GUI.sh / prepareDataDir), then the
// application bundle dir (the shipped defaults on Windows).
QString MainWindow::themesRootDir() const
{
    const QStringList roots = {
        guiDataDir + QStringLiteral("/themes"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/themes"),
    };
    for (const QString &root : roots) {
        const QDir dir(root);
        if (!dir.exists())
            continue;
        const QFileInfoList entries =
            dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries) {
            const QFileInfo conf(entry.filePath() + QStringLiteral("/theme.conf"));
            if (conf.isFile() && conf.size() > 0)
                return root;
        }
    }
    return {};
}

// Valid theme names under themesRootDir(): a theme is a directory holding a
// non-empty theme.conf. Names are folder-name identifiers (they appear in
// the theme.conf's own asset paths), never translated.
QStringList MainWindow::availableThemes() const
{
    QStringList names;
    const QString root = themesRootDir();
    if (root.isEmpty())
        return names;
    const QFileInfoList entries =
        QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QFileInfo conf(entry.filePath() + QStringLiteral("/theme.conf"));
        if (conf.isFile() && conf.size() > 0)
            names << entry.fileName();
    }
    return names;
}

void MainWindow::populateThemeCombo()
{
    const bool wasPopulating = populating;
    populating = true;
    // Selection is keyed by item data, so a rebuild (or a UI language switch,
    // which renames None/Random) preserves the user's choice.
    const QString previous = ui->Theme_comboBox->currentData().toString();
    ui->Theme_comboBox->clear();
    ui->Theme_comboBox->addItem(noneOption(), QString());
    const QStringList names = availableThemes();
    for (const QString &name : names)
        ui->Theme_comboBox->addItem(name, name);
    if (!names.isEmpty())
        ui->Theme_comboBox->addItem(tr("Random"), QLatin1String(kRandomThemeKey));
    const int idx = ui->Theme_comboBox->findData(previous);
    ui->Theme_comboBox->setCurrentIndex(idx >= 0 ? idx : 0);
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
        populateThemeCombo();
        // Refresh the translated "None" entries; selections are preserved by
        // key/text where they still match, and fall back to None otherwise.
        // While a scan is in flight, only re-apply the translated placeholder:
        // populateBootCombos() → comboOptions() reads the detector's partition
        // cache, which the worker thread is writing right then.
        if (scanThread)
            setScanningUi(true);
        else
            populateBootCombos();
        equalizeActionButtonWidths();
        // Re-fit the window to the new strings once the pending layout
        // invalidations from the retranslate have settled.
        QTimer::singleShot(0, this, [this] { adjustSize(); });
    }
    QMainWindow::changeEvent(event);
}

MainWindow::~MainWindow()
{
    // A background detection uses this object's detector member; let it
    // drain before teardown (its queued result callback is then discarded).
    if (scanThread)
        scanThread->wait();
    // Never persist before readSettings() has run (closing during the first
    // scan): the combos still hold the "Scanning…" placeholder and every
    // other control its compile-time default, which would clobber the INI.
    if (settingsLoaded)
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
        // Also resolve the panel resolution here: the quirk probes and the
        // EDID read each shell out on Windows, and readSettings() needs the
        // value on the GUI thread to seed the Res Override boxes.
        const QSize panelRes = effectivePanelResolution();
        QMetaObject::invokeMethod(this, [this, result, panelRes, resetToDefaults] {
            panelPrefill = panelRes;
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
    // Only offered when the medium is actually present: unlike the Ventoy and
    // Batocera fallbacks below, which carry a filesystem label as their volume,
    // these are identified by partition GUID, and that is empty with no SD/USB
    // ESP inserted. A stanza with no volume line makes rEFInd resolve the
    // loader on its own ESP, so picking "Windows (SD)" with no card in would
    // silently boot the *internal* Windows under the SD label.
    const QString sdUuid = detector.removableEspPartUuid(true);
    if (!sdUuid.isEmpty())
        addFallback({QStringLiteral("Windows (SD)"), QStringLiteral("Windows Micro SD"),
                     QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi"), sdUuid, false});
    const QString usbUuid = detector.removableEspPartUuid(false);
    if (!usbUuid.isEmpty())
        addFallback({QStringLiteral("Windows (USB)"), QStringLiteral("Windows USB"),
                     QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi"), usbUuid, false});
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
    const QString out = OSDetector::runCommand(
        QStringLiteral("efibootmgr"), {QStringLiteral("-v")}, &ok);
    if (!ok)
        return {};

    QStringList bootOrder;
    static const QRegularExpression bootOrderRe(
        QStringLiteral("^BootOrder:\\s*([0-9A-Fa-f]{4}(?:,[0-9A-Fa-f]{4})*)\\s*$"),
        QRegularExpression::MultilineOption);
    const QRegularExpressionMatch orderMatch = bootOrderRe.match(out);
    if (orderMatch.hasMatch())
        bootOrder = orderMatch.captured(1).toUpper().split(QLatin1Char(','));

    QStringList loaderMatches;
    QStringList labelMatches;
    static const QRegularExpression entryRe(
        QStringLiteral("^Boot([0-9A-Fa-f]{4})\\*?\\s+([^\\t\\r\\n]*)(?:\\t(.*))?$"));
    static const QRegularExpression steamLoaderRe(
        QStringLiteral("HD\\([^)]*\\)/(?:File\\()?\\\\EFI\\\\steamos\\\\steamcl\\.efi"
                       "(?:\\)|[0-9A-Fa-f]{8}|$)"),
        QRegularExpression::CaseInsensitiveOption);

    const QStringList lines = out.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QRegularExpressionMatch entry = entryRe.match(line);
        if (!entry.hasMatch())
            continue;

        const QString bootNum = entry.captured(1).toUpper();
        const QString label = entry.captured(2).trimmed();
        const QString devicePath = entry.captured(3).trimmed();
        if (steamLoaderRe.match(devicePath).hasMatch())
            loaderMatches.append(bootNum);
        if (label.compare(QStringLiteral("SteamOS"), Qt::CaseInsensitive) == 0)
            labelMatches.append(bootNum);
    }

    auto firstInBootOrder = [&bootOrder](const QStringList &candidates) {
        for (const QString &bootNum : bootOrder) {
            if (candidates.contains(bootNum))
                return bootNum;
        }
        return candidates.value(0);
    };

    // The immutable loader path is authoritative. The exact label is only a
    // compatibility fallback for firmware whose verbose path cannot be parsed.
    const QString loaderMatch = firstInBootOrder(loaderMatches);
    return loaderMatch.isEmpty() ? firstInBootOrder(labelMatches) : loaderMatch;
}

// These values are not user-typed: menuName/loaderPath come from ESP vendor
// directory names and from `title` lines in another ESP's loader/entries/*.conf,
// so a crafted systemd-boot title containing a quote plus braces could inject an
// extra boot stanza into a config that is then installed to the ESP by root.
// Stripping the quote/brace/newline characters closes that off.
static QString confSanitize(const QString &value)
{
    QString clean = value;
    clean.remove(QLatin1Char('"'));
    clean.remove(QLatin1Char('{'));
    clean.remove(QLatin1Char('}'));
    clean.replace(QLatin1Char('\n'), QLatin1Char(' '));
    clean.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return clean;
}

// Only menuentry titles and volume labels may be quoted. rEFInd does not strip
// quotes from icon/loader path tokens, so quoting those lines (v3.1.2) made
// every generated stanza unbootable — path lines get confSanitize() only.
static QString confQuote(const QString &value)
{
    return QLatin1Char('"') + confSanitize(value) + QLatin1Char('"');
}

QString MainWindow::createBootStanza(const BootEntry &entry, const QString &iconPath)
{
    QString stanza;
    QTextStream out(&stanza);
    out << "\nmenuentry " << confQuote(entry.menuName) << " {\n";
    out << "\ticon " << confSanitize(iconPath) << "\n";
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
        out << "\tvolume " << confQuote(entry.volume) << "\n";
    out << "\tloader " << confSanitize(entry.loaderPath) << "\n";
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

// The resolution the built-in panel would report automatically: device
// quirks first (portrait-native Legion Go panels, the Xbox Ally's wrong
// numbered-mode pick), then the panel's native EDID/DRM mode. The generated
// config deliberately does NOT use this — numbered mode 3 is the right mode
// on every Deck — it only seeds the Res Override boxes. Note both Deck
// panels are portrait-native, so the seed reads 800x1280 there; that is the
// honest EDID value, and the override is never armed by default. Each quirk
// probe shells out on Windows, so call from the GUI thread sparingly.
static QSize effectivePanelResolution()
{
    if (OSDetector::isLegionGo2())
        return QSize(1920, 1200);
    if (OSDetector::isLegionGo())
        return QSize(2560, 1600);
    if (OSDetector::isXboxAlly())
        return QSize(1920, 1080);
    return OSDetector::nativePanelResolution();
}

// The user-armed resolution override, or an invalid QSize when the checkbox
// is off or either box doesn't hold a positive number.
QSize MainWindow::resolutionOverride() const
{
    if (!ui->Res_Override_checkBox->isChecked())
        return QSize();
    // Bounded, not just positive: setText() from the INI bypasses the widgets'
    // validators, so a hand-edited or corrupt INI could otherwise emit
    // "resolution 99999 99999" and leave rEFInd with no usable mode.
    const int w = ui->Res_Width_lineEdit->text().toInt();
    const int h = ui->Res_Height_lineEdit->text().toInt();
    if (w < 640 || h < 480 || w > 16384 || h > 16384)
        return QSize();
    return QSize(w, h);
}

// Valid arguments to rEFInd's showtools directive.
static const QStringList &validShowtoolsTokens()
{
    static const QStringList tokens = {
        QStringLiteral("shell"),          QStringLiteral("memtest"),
        QStringLiteral("gdisk"),          QStringLiteral("gptsync"),
        QStringLiteral("install"),        QStringLiteral("bootorder"),
        QStringLiteral("apple_recovery"), QStringLiteral("csr_rotate"),
        QStringLiteral("mok_tool"),       QStringLiteral("fwupdate"),
        QStringLiteral("netboot"),        QStringLiteral("about"),
        QStringLiteral("hidden_tags"),    QStringLiteral("exit"),
        QStringLiteral("shutdown"),       QStringLiteral("reboot"),
        QStringLiteral("firmware")};
    return tokens;
}

// The Showtools box tokenized: split on commas (stray whitespace tolerated),
// lowercased, deduplicated in order. setText() from the INI bypasses any
// widget-level validation, so consumers re-check against the valid set.
QStringList MainWindow::showtoolsTokens() const
{
    static const QRegularExpression separators(QStringLiteral("[,\\s]+"));
    const QStringList raw =
        ui->Showtools_lineEdit->text().split(separators, Qt::SkipEmptyParts);
    QStringList tokens;
    for (const QString &t : raw) {
        const QString token = t.toLower();
        if (!tokens.contains(token))
            tokens << token;
    }
    return tokens;
}

QStringList MainWindow::invalidShowtoolsTokens() const
{
    QStringList bad;
    const QStringList tokens = showtoolsTokens();
    for (const QString &t : tokens)
        if (!validShowtoolsTokens().contains(t))
            bad << t;
    return bad;
}

// Live feedback while typing: unrecognized tools tint the box red. Never
// blocks typing (partial tokens are wrong until finished) — Create Config is
// where an invalid list becomes a hard stop.
void MainWindow::updateShowtoolsValidity()
{
    ui->Showtools_lineEdit->setStyleSheet(
        invalidShowtoolsTokens().isEmpty()
            ? QString()
            : QStringLiteral("QLineEdit { background-color: rgba(200, 60, 60, 0.35); }"));
}

// Renders the full refind.conf as text — shared by Create Config (which
// writes it) and the Preview dialog (which only displays it).
QString MainWindow::generateConfigText(const QList<Selection> &selections)
{
    QString text;
    // Re-validate rather than trusting the widget text: QIntValidator accepts
    // the intermediate string "-" while typing, and setText() from the INI is
    // never validated at all. "timeout -" parses as 0 in rEFInd, which means
    // "wait forever" -- on a handheld with no keyboard attached that is a menu
    // that never boots.
    bool timeoutOk = false;
    int timeoutValue = ui->TimeOut_lineEdit->text().trimmed().toInt(&timeoutOk);
    if (!timeoutOk || timeoutValue < -1 || timeoutValue > 99)
        timeoutValue = 5;
    const QString timeout = QString::number(timeoutValue);

    QTextStream out(&text);
    out << "# GUI generated refind.conf Config File\n";
    out << "timeout " << timeout << "\n";
    out << "use_nvram false\n";
    out << "hideui singleuser,hints,arrows,label,badges\n";
    out << "banner background.png\n";
    out << "banner_scale fillscreen\n";
    // An explicitly armed Res Override wins; numbered mode 3 stays the
    // default — it is the right mode on every Deck. The override is never
    // active unless the user enabled the checkbox.
    const QSize forced = resolutionOverride();
    if (forced.isValid())
        out << "resolution " << forced.width() << " " << forced.height() << "\n";
    else
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
    // The user's tool list for the menu's second row. Unknown tokens are
    // dropped here rather than passed through (Create Config refuses them
    // with an explanation, but Preview and a hand-edited INI still reach
    // this path); blank keeps the bare directive, which shows no tools.
    QStringList tools = showtoolsTokens();
    const QStringList badTools = invalidShowtoolsTokens();
    for (const QString &bad : badTools)
        tools.removeAll(bad);
    out << "showtools";
    if (!tools.isEmpty())
        out << " " << tools.join(QLatin1Char(','));
    out << "\n";
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
        out << createBootStanza(sel.entry,
                                QStringLiteral("/EFI/refind/os_icon%1.png").arg(sel.slot));
    if (ui->Extras_checkBox->isChecked()) {
        // Outlier machines can hold more valid bootloaders than the four
        // slots. Opt-in: append a stanza for every detected entry that is
        // not already slotted, after the slotted ones (so the curated icons
        // stay leftmost), with a stock rEFInd icon since these entries have
        // no user-chosen slot icon.
        for (const BootEntry &extra : extraEntries(selections))
            out << createBootStanza(extra, stockIconFor(extra));
    }
    if (!ui->Theme_comboBox->currentData().toString().isEmpty()) {
        // One stable line, always last: which theme is active is decided
        // solely by the themes/active_theme.conf copy on the ESP (staged by
        // Create Config, swapped by the theme randomizer), so switching or
        // randomizing themes never rewrites refind.conf itself.
        out << "\n# Theme include — theme settings intentionally supersede the settings above\n";
        out << "include themes/active_theme.conf\n";
    }
    return text;
}

// Detected entries not covered by a slot, deduplicated by identity key —
// the sources for the optional extra stanzas. Only genuinely detected
// entries qualify (never the static removable-media fallbacks), which is
// what makes an extra stanza "valid and in use" on this machine.
QList<BootEntry> MainWindow::extraEntries(const QList<Selection> &selections) const
{
    QStringList used;
    for (const Selection &sel : selections)
        used << entryKey(sel.entry);
    QList<BootEntry> extras;
    for (const BootEntry &e : detected) {
        const QString key = entryKey(e);
        if (used.contains(key))
            continue;
        used << key;
        extras << e;
    }
    return extras;
}

// Stock rEFInd icon for stanzas without a user-chosen slot icon. Coarse on
// purpose: Windows vs everything else (the rest of the detected set is
// Linux-family); a missing icon file just makes rEFInd fall back to its
// default, so this can never break a stanza.
QString MainWindow::stockIconFor(const BootEntry &entry)
{
    if (entry.loaderPath.contains(QLatin1String("Microsoft"), Qt::CaseInsensitive))
        return QStringLiteral("/EFI/refind/icons/os_win8.png");
    return QStringLiteral("/EFI/refind/icons/os_linux.png");
}

// Turns the Theme combo's data key into a concrete theme name: Random picks
// one of the currently valid themes now (Create Config time), a named theme
// is re-validated in case it was deleted since the combo was filled. Returns
// an empty string (after its own dialog) when no usable theme exists.
QString MainWindow::resolveThemeChoice(const QString &choice)
{
    const QStringList names = availableThemes();
    if (choice == QLatin1String(kRandomThemeKey)) {
        if (names.isEmpty()) {
            QMessageBox::warning(this, tr("Create Config"),
                                 tr("No themes were found, so a random theme could not be "
                                    "picked. Reinstall the GUI to restore the shipped themes."));
            return {};
        }
        const QString pick = names.at(QRandomGenerator::global()->bounded(names.size()));
        appendLog(QStringLiteral("create config: random theme -> %1").arg(pick));
        return pick;
    }
    if (!names.contains(choice)) {
        QMessageBox::warning(this, tr("Create Config"),
                             tr("The selected theme \"%1\" was not found (or its theme.conf "
                                "is empty). Pick another theme, or reinstall the GUI to "
                                "restore the shipped themes.").arg(choice));
        return {};
    }
    return choice;
}

// Stages GUI/active_theme.conf as a copy of the chosen theme's theme.conf,
// with the same QSaveFile publish-on-commit pattern as the generated config
// (a partial copy must never replace a previously staged good one).
bool MainWindow::stageActiveThemeConf(const QString &themeName)
{
    QFile input(themesRootDir() + QLatin1Char('/') + themeName
                + QStringLiteral("/theme.conf"));
    QSaveFile output(guiConfigDir + QStringLiteral("/active_theme.conf"));
    output.setDirectWriteFallback(false);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
        output.cancelWriting();
        QMessageBox::warning(this, tr("Create Config"),
                             tr("Could not copy %1 to %2").arg(input.fileName(),
                                                               output.fileName()));
        return false;
    }
    const QByteArray payload = input.readAll();
    const qint64 written = output.write(payload);
    if (payload.isEmpty() || written != payload.size() || !output.commit()) {
        output.cancelWriting();
        QMessageBox::warning(this, tr("Create Config"),
                             tr("Could not copy %1 to %2").arg(input.fileName(),
                                                               output.fileName()));
        return false;
    }
    return true;
}

void MainWindow::on_Create_Config_clicked()
{
    // Refuse rather than silently drop: a typo'd tool would otherwise just
    // vanish from the boot menu with no hint why.
    const QStringList badTools = invalidShowtoolsTokens();
    if (!badTools.isEmpty()) {
        QMessageBox::warning(this, tr("Showtools"),
            tr("These showtools entries are not recognized: %1\n\nValid entries: %2")
                .arg(badTools.join(QStringLiteral(", ")),
                     validShowtoolsTokens().join(QStringLiteral(", "))));
        return;
    }

    QDir().mkpath(guiConfigDir);

    // Publish the config last. If any selected image cannot be staged, keep
    // the previous refind.conf rather than pointing it at a partial update.
    if (!copyPng(ui->Background_lineEdit, guiConfigDir + "/background.png")
        || !copyPng(ui->Boot_Option_01_Icon_lineEdit, guiConfigDir + "/os_icon1.png")
        || !copyPng(ui->Boot_Option_02_Icon_lineEdit, guiConfigDir + "/os_icon2.png")
        || !copyPng(ui->Boot_Option_03_Icon_lineEdit, guiConfigDir + "/os_icon3.png")
        || !copyPng(ui->Boot_Option_04_Icon_lineEdit, guiConfigDir + "/os_icon4.png")) {
        return;
    }

    // Theme staging follows the same publish-last rule: active_theme.conf is
    // staged before the config that includes it, and a staging failure keeps
    // the previous refind.conf.
    const QString themeChoice = ui->Theme_comboBox->currentData().toString();
    if (themeChoice.isEmpty()) {
        // Theming off: drop any previously staged copy so Install Config does
        // not publish a stale active_theme.conf alongside a config that no
        // longer includes it.
        QFile::remove(guiConfigDir + QStringLiteral("/active_theme.conf"));
    } else {
        const QString themeName = resolveThemeChoice(themeChoice);
        if (themeName.isEmpty() || !stageActiveThemeConf(themeName))
            return;
    }

    // Deliberately no QIODevice::Text: the same selections must stage the
    // same bytes on Windows and Linux (rEFInd reads either line ending, and
    // the Windows build's CRLF only made byte-comparisons between the two
    // builds' installed configs fail).
    QSaveFile conf(guiConfigDir + "/refind.conf");
    conf.setDirectWriteFallback(false);
    if (!conf.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Create Config"),
                              tr("Could not write %1").arg(conf.fileName()));
        return;
    }
    // A short write must not be reported as success: Install Config would copy
    // a truncated refind.conf to the ESP, and a config cut off mid-stanza is a
    // boot menu missing entries.
    const QByteArray payload = generateConfigText(currentSelections()).toUtf8();
    const qint64 written = conf.write(payload);
    if (written != payload.size() || !conf.commit()) {
        conf.cancelWriting();
        QMessageBox::critical(this, tr("Create Config"),
                              tr("Could not write %1 completely — the disk may be full. "
                                 "The config was not updated.")
                                  .arg(conf.fileName()));
        return;
    }
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
    if (ui->Extras_checkBox->isChecked()) {
        for (const BootEntry &extra : extraEntries(selections)) {
            PreviewEntry e;
            e.name = extra.displayName; // no icon path -> placeholder tile
            entries << e;
        }
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

    // The theme include is the last line of the generated config, so the
    // chosen theme's banner/icon-size/selection supersede the choices above
    // — resolve the combo choice (Random becomes a concrete pick, as at
    // Create Config time) and hand the preview its visual directives.
    PreviewTheme theme;
    bool randomPick = false;
    QString themeChoice = ui->Theme_comboBox->currentData().toString();
    if (themeChoice == QLatin1String(kRandomThemeKey)) {
        const QStringList themes = availableThemes();
        themeChoice = themes.isEmpty()
                          ? QString()
                          : themes.at(QRandomGenerator::global()->bounded(themes.size()));
        randomPick = !themeChoice.isEmpty();
    }
    if (!themeChoice.isEmpty()) {
        const QString themesRoot = themesRootDir();
        theme = PreviewTheme::load(themesRoot + QLatin1Char('/') + themeChoice
                                       + QStringLiteral("/theme.conf"),
                                   themesRoot);
        theme.name = themeChoice;
        theme.randomPick = randomPick;
    }

    PreviewDialog dialog(background, entries, iconSize, defaultIndex, theme, confText, this);
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
        appendLog(QStringLiteral("install config: refused, helper missing or version mismatch"), badScript);
        QMessageBox::warning(this, tr("Install Config"),
                             tr("The privileged install helper was NOT run:\n\n%1\n\n"
                                "It is missing or belongs to a different version of the app, "
                                "so it cannot be run safely.\n\n"
                                "Reinstall the GUI (on SteamOS, re-run install-GUI.sh) to "
                                "restore the matching helper, then try again.").arg(badScript));
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
    const bool hadSelection = !edit->text().isEmpty();
    checkPNGFile(edit);
    const QString source = edit->text();
    if (hadSelection && source.isEmpty())
        return false;
    if (source.isEmpty() || source == destPath)
        return true;

    QFile input(source);
    QSaveFile output(destPath);
    output.setDirectWriteFallback(false);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
        output.cancelWriting();
        QMessageBox::warning(this, tr("Copy PNG"),
                             tr("Could not copy %1 to %2").arg(source, destPath));
        return false;
    }

    char buffer[64 * 1024];
    qint64 bytesRead = 0;
    while ((bytesRead = input.read(buffer, sizeof(buffer))) > 0) {
        if (output.write(buffer, bytesRead) != bytesRead) {
            output.cancelWriting();
            QMessageBox::warning(this, tr("Copy PNG"),
                                 tr("Could not copy %1 to %2").arg(source, destPath));
            return false;
        }
    }
    if (bytesRead < 0 || !output.commit()) {
        output.cancelWriting();
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

// Restores every control whose value doesn't depend on scan results. Runs
// in the constructor, unlike readSettings(): that waits for the first
// detection pass to deliver (many seconds of PowerShell probing on Windows),
// and until then the controls sat on their compile-time defaults as if the
// saved values had been lost — worse, a control changed during that window
// was silently reverted when readSettings() finally applied the INI.
void MainWindow::readEarlySettings()
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("CheckBoxes"));
    ui->Last_OS_CheckBox->setChecked(settings.value(QStringLiteral("LastOSCheckBox")).toBool());
    // SteamOS firmware_bootnum defaults on for the Deck (efibootmgr present);
    // on the Windows build the feature is unsupported and the box is disabled.
    ui->Firmware_bootnum_CheckBox->setChecked(
        settings.value(QStringLiteral("FW_bootNum_CheckBox"), Platform::firmwareBootnumSupported()).toBool());
    ui->Enable_Mouse_checkBox->setChecked(settings.value(QStringLiteral("Enable_Mouse_CheckBox"), true).toBool());
    ui->Extras_checkBox->setChecked(settings.value(QStringLiteral("IncludeExtraEntries")).toBool());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("ComboBoxes"));
    const int installSource = settings.value(QStringLiteral("InstallSourceComboBox")).toInt();
    const int iconSize = settings.value(QStringLiteral("IconSize")).toInt();
    const QString themeKey = settings.value(QStringLiteral("ThemeSelection")).toString();
    settings.endGroup();
    ui->Install_Source_comboBox->setCurrentIndex(installSource);
    // Stored as the item data key (folder name / random sentinel), not the
    // translated text; an unset or no-longer-valid key falls back to None.
    const int themeIdx = ui->Theme_comboBox->findData(themeKey);
    if (themeIdx >= 0)
        ui->Theme_comboBox->setCurrentIndex(themeIdx);
    // Stored as the pixel value, not the index/text, so saved settings survive
    // relabeling; an unset key (0) keeps the constructor's 128 default.
    const int iconIdx = ui->Icon_Size_comboBox->findData(iconSize);
    if (iconIdx >= 0)
        ui->Icon_Size_comboBox->setCurrentIndex(iconIdx);

    settings.beginGroup(QStringLiteral("Timeout"));
    const QString timeout = settings.value(QStringLiteral("Timeout")).toString();
    settings.endGroup();
    if (!timeout.isEmpty())
        ui->TimeOut_lineEdit->setText(timeout);

    settings.beginGroup(QStringLiteral("ShowTools"));
    ui->Showtools_lineEdit->setText(settings.value(QStringLiteral("ShowTools")).toString());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Resolution"));
    ui->Res_Width_lineEdit->setText(settings.value(QStringLiteral("ResOverrideWidth")).toString());
    ui->Res_Height_lineEdit->setText(settings.value(QStringLiteral("ResOverrideHeight")).toString());
    ui->Res_Override_checkBox->setChecked(settings.value(QStringLiteral("ResOverrideEnabled")).toBool());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Paths"));
    lastBrowseDir = settings.value(QStringLiteral("LastBrowseDir")).toString();
    settings.endGroup();
}

void MainWindow::persistSetting(const QString &group, const QString &key, const QVariant &value)
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.beginGroup(group);
    settings.setValue(key, value);
    settings.endGroup();
}

// Writes each setting the moment the user changes its control, like
// browsePng() already did for LastBrowseDir: the destructor's writeSettings()
// never runs when the app is force-terminated (overlay / task switcher,
// suspend on handhelds), which used to drop every change since launch.
void MainWindow::initSettingsPersistence()
{
    connect(ui->Last_OS_CheckBox, &QCheckBox::toggled, this, [this](bool on) {
        persistSetting(QStringLiteral("CheckBoxes"), QStringLiteral("LastOSCheckBox"), on);
    });
    connect(ui->Firmware_bootnum_CheckBox, &QCheckBox::toggled, this, [this](bool on) {
        persistSetting(QStringLiteral("CheckBoxes"), QStringLiteral("FW_bootNum_CheckBox"), on);
    });
    connect(ui->Enable_Mouse_checkBox, &QCheckBox::toggled, this, [this](bool on) {
        persistSetting(QStringLiteral("CheckBoxes"), QStringLiteral("Enable_Mouse_CheckBox"), on);
    });
    connect(ui->Extras_checkBox, &QCheckBox::toggled, this, [this](bool on) {
        persistSetting(QStringLiteral("CheckBoxes"), QStringLiteral("IncludeExtraEntries"), on);
    });
    connect(ui->Install_Source_comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index >= 0)
            persistSetting(QStringLiteral("ComboBoxes"), QStringLiteral("InstallSourceComboBox"), index);
    });
    connect(ui->Icon_Size_comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index >= 0)
            persistSetting(QStringLiteral("ComboBoxes"), QStringLiteral("IconSize"),
                           ui->Icon_Size_comboBox->currentData().toInt());
    });
    connect(ui->Theme_comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        // Gate on populating: populateThemeCombo() rebuilds the items (e.g.
        // on a language switch) and must not clobber the stored key.
        if (index >= 0 && !populating)
            persistSetting(QStringLiteral("ComboBoxes"), QStringLiteral("ThemeSelection"),
                           ui->Theme_comboBox->currentData().toString());
    });
    connect(ui->TimeOut_lineEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        persistSetting(QStringLiteral("Timeout"), QStringLiteral("Timeout"), text);
    });
    connect(ui->Showtools_lineEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        persistSetting(QStringLiteral("ShowTools"), QStringLiteral("ShowTools"), text);
    });
    connect(ui->Res_Override_checkBox, &QCheckBox::toggled, this, [this](bool on) {
        persistSetting(QStringLiteral("Resolution"), QStringLiteral("ResOverrideEnabled"), on);
    });
    connect(ui->Res_Width_lineEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        persistSetting(QStringLiteral("Resolution"), QStringLiteral("ResOverrideWidth"), text);
    });
    connect(ui->Res_Height_lineEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        persistSetting(QStringLiteral("Resolution"), QStringLiteral("ResOverrideHeight"), text);
    });

    // Boot-option selections only restore after the scan, so gate on
    // settingsLoaded (a half-restored set must not overwrite the INI — same
    // reason the destructor is gated) and on populating (combo rebuilds).
    const auto persistSelections = [this] {
        if (settingsLoaded && !populating)
            persistBootSelections();
    };
    for (QComboBox *combo : bootCombos())
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, persistSelections);
    connect(ui->Default_Boot_comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, persistSelections);
}

// Restores the boot-option selections; everything else is handled by
// readEarlySettings() before the window even shows.
void MainWindow::readSettings()
{
    QSettings settings(settingsPath, QSettings::IniFormat);
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
    settings.endGroup();

    // Re-read rather than reusing readEarlySettings()' values: the boxes are
    // live during the scan and edits persist immediately.
    settings.beginGroup(QStringLiteral("Resolution"));
    const QString resW = settings.value(QStringLiteral("ResOverrideWidth")).toString();
    const QString resH = settings.value(QStringLiteral("ResOverrideHeight")).toString();
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
    // Seed the Res Override boxes with the panel's reported resolution
    // (device quirk, else the EDID/DRM native mode) only when nothing was
    // ever stored; saved values — including edits made with the override
    // later disarmed — always win. Deferred here because panelPrefill is
    // computed on the detection worker thread.
    if (resW.isEmpty() && resH.isEmpty() && panelPrefill.isValid() && !panelPrefill.isEmpty()) {
        ui->Res_Width_lineEdit->setText(QString::number(panelPrefill.width()));
        ui->Res_Height_lineEdit->setText(QString::number(panelPrefill.height()));
    }
    settingsLoaded = true;
}

// The nine boot-selection keys, written together (they are only meaningful
// as a set): by the destructor's exit snapshot and, once settingsLoaded, by
// every user change to one of the five combos.
void MainWindow::persistBootSelections()
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
    settings.endGroup();
}

void MainWindow::writeSettings()
{
    persistBootSelections();
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("ComboBoxes"));
    settings.setValue(QStringLiteral("InstallSourceComboBox"), ui->Install_Source_comboBox->currentIndex());
    settings.setValue(QStringLiteral("IconSize"), ui->Icon_Size_comboBox->currentData().toInt());
    settings.setValue(QStringLiteral("ThemeSelection"), ui->Theme_comboBox->currentData().toString());
    settings.remove(QStringLiteral("LinuxComboBox"));
    settings.endGroup();
    settings.beginGroup(QStringLiteral("CheckBoxes"));
    settings.setValue(QStringLiteral("LastOSCheckBox"), ui->Last_OS_CheckBox->isChecked());
    settings.setValue(QStringLiteral("FW_bootNum_CheckBox"), ui->Firmware_bootnum_CheckBox->isChecked());
    settings.setValue(QStringLiteral("Enable_Mouse_CheckBox"), ui->Enable_Mouse_checkBox->isChecked());
    settings.setValue(QStringLiteral("IncludeExtraEntries"), ui->Extras_checkBox->isChecked());
    settings.endGroup();
    settings.beginGroup(QStringLiteral("Timeout"));
    settings.setValue(QStringLiteral("Timeout"), ui->TimeOut_lineEdit->text());
    settings.endGroup();
    settings.beginGroup(QStringLiteral("ShowTools"));
    settings.setValue(QStringLiteral("ShowTools"), ui->Showtools_lineEdit->text());
    settings.endGroup();
    settings.beginGroup(QStringLiteral("Resolution"));
    settings.setValue(QStringLiteral("ResOverrideEnabled"), ui->Res_Override_checkBox->isChecked());
    settings.setValue(QStringLiteral("ResOverrideWidth"), ui->Res_Width_lineEdit->text());
    settings.setValue(QStringLiteral("ResOverrideHeight"), ui->Res_Height_lineEdit->text());
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

void MainWindow::on_Install_Themes_pushButton_clicked()
{
    QString badScript;
    if (!Platform::installThemesScriptTrusted(&badScript)) {
        appendLog(QStringLiteral("install themes: refused, helper missing or version mismatch"),
                  badScript);
        QMessageBox::warning(this, tr("Install Themes"),
                             tr("The privileged install helper was NOT run:\n\n%1\n\n"
                                "It is missing or belongs to a different version of the app, "
                                "so it cannot be run safely.\n\n"
                                "Reinstall the GUI (on SteamOS, re-run install-GUI.sh) to "
                                "restore the matching helper, then try again.").arg(badScript));
        return;
    }
    QString output;
    const int rc = Platform::installThemes(&output);
    appendLog(QStringLiteral("install themes: rc %1").arg(rc), output);
    const QString details = outputTail(output);
    if (rc == 0) {
        QMessageBox::information(this, tr("Install Themes"),
                                 details.isEmpty()
                                     ? tr("The themes were installed successfully.")
                                     : tr("The themes were installed successfully.\n\n%1").arg(details));
    } else {
        QMessageBox::critical(this, tr("Install Themes"),
                              details.isEmpty()
                                  ? tr("Installing the themes failed (code %1).").arg(rc)
                                  : tr("Installing the themes failed (code %1).\n\n%2")
                                        .arg(rc).arg(details));
    }
}

void MainWindow::on_Theme_Rand_On_pushButton_clicked()
{
    if (!Platform::setThemeRandomizer(true))
        QMessageBox::warning(this, tr("Theme Randomizer"),
                             tr("Failed to launch the randomizer setup."));
}

void MainWindow::on_Theme_Rand_Off_pushButton_clicked()
{
    if (!Platform::setThemeRandomizer(false))
        QMessageBox::warning(this, tr("Theme Randomizer"),
                             tr("Failed to launch the randomizer setup."));
}

void MainWindow::on_Open_Folder_pushButton_clicked()
{
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(guiConfigDir)))
        QMessageBox::warning(this, tr("Open Folder"),
                             tr("Failed to open %1 in the file manager.")
                                 .arg(QDir::toNativeSeparators(guiConfigDir)));
}
