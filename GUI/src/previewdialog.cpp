#include "previewdialog.h"

#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

// Maps a theme.conf asset path onto the local themes directory. The bundled
// themes are normalized to ESP-relative "themes/<name>/..." paths, but the
// marker search also accepts an absolute spelling like
// "/EFI/refind/themes/<name>/..."; anything else is taken relative to the
// theme.conf itself. Only an existing file is returned — a dangling path
// must not silently blank the preview element it feeds.
QString resolveThemeAsset(const QString &value, const QString &themesRoot,
                          const QString &confDir)
{
    if (value.isEmpty())
        return {};
    QString rel = value;
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QString marker = QStringLiteral("themes/");
    const int idx = rel.indexOf(marker);
    QString candidate;
    if (idx >= 0 && !themesRoot.isEmpty())
        candidate = themesRoot + QLatin1Char('/') + rel.mid(idx + marker.size());
    else
        candidate = confDir + QLatin1Char('/') + rel;
    return QFileInfo::exists(candidate) ? candidate : QString();
}

// Renders the mock screen at rEFInd's reference geometry (1280x800, the
// Deck-class panel) and lets the label scale it down; the icon row uses the
// same proportions rEFInd does for big_icon_size at that resolution.
QPixmap renderMockScreen(const QString &backgroundPath, const QList<PreviewEntry> &entries,
                         int iconSize, int defaultIndex, const PreviewTheme &theme)
{
    const QSize canvas(1280, 800);
    QPixmap pix(canvas);
    pix.fill(Qt::black);
    QPainter p(&pix);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing);

    // The theme include is the last line of the config, so its banner and
    // big_icon_size supersede the user's background and Icon Size choice.
    QPixmap bg(theme.bannerPath.isEmpty() ? backgroundPath : theme.bannerPath);
    if (!bg.isNull())
        p.drawPixmap(pix.rect(), bg); // banner_scale fillscreen

    if (entries.isEmpty()) {
        p.setPen(Qt::white);
        p.drawText(pix.rect(), Qt::AlignCenter,
                   PreviewDialog::tr("No boot options selected."));
        return pix;
    }

    int icon = qBound(48, theme.bigIconSize > 0 ? theme.bigIconSize : iconSize, 512);
    // Themes aimed at large screens use icon sizes whose row overflows the
    // 1280-wide mock (rEFInd scrolls in that case); shrink to fit instead so
    // every entry stays visible. Row width is icon*n + spacing*(n-1) with
    // spacing = icon/2, i.e. icon*(3n-1)/2.
    const int maxIcon = 2 * (canvas.width() - 80) / (3 * entries.size() - 1);
    icon = qMax(48, qMin(icon, maxIcon));
    const int spacing = icon / 2;
    const int rowWidth = entries.size() * icon + (entries.size() - 1) * spacing;
    int x = (canvas.width() - rowWidth) / 2;
    const int y = (canvas.height() - icon) / 2;

    QPixmap selection(theme.selectionPath);

    QFont nameFont = p.font();
    nameFont.setPixelSize(qMax(14, icon / 7));
    p.setFont(nameFont);

    for (int i = 0; i < entries.size(); ++i) {
        const QRect iconRect(x, y, icon, icon);
        if (i == defaultIndex) {
            if (!selection.isNull()) {
                // rEFInd scales selection_big to the big icon size and draws
                // the icon on top of it.
                p.drawPixmap(iconRect, selection);
            } else {
                // rEFInd draws the default selection as a light rounded
                // backdrop when the theme brings no selection image.
                QPainterPath path;
                path.addRoundedRect(iconRect.adjusted(-icon / 8, -icon / 8,
                                                      icon / 8, icon / 8),
                                    icon / 8, icon / 8);
                p.fillPath(path, QColor(255, 255, 255, 90));
            }
        }
        QPixmap iconPix(entries.at(i).iconPath);
        if (!iconPix.isNull()) {
            p.drawPixmap(iconRect, iconPix);
        } else {
            // Placeholder when no PNG is chosen yet: rounded tile + initial.
            QPainterPath path;
            path.addRoundedRect(iconRect, icon / 8, icon / 8);
            p.fillPath(path, QColor(70, 70, 70, 220));
            p.setPen(Qt::white);
            QFont bigFont = p.font();
            bigFont.setPixelSize(icon / 2);
            p.setFont(bigFont);
            p.drawText(iconRect, Qt::AlignCenter, entries.at(i).name.left(1));
            p.setFont(nameFont);
        }
        p.setPen(Qt::white);
        const QRect nameRect(x - spacing / 2, iconRect.bottom() + icon / 6,
                             icon + spacing, icon / 3);
        p.drawText(nameRect, Qt::AlignHCenter | Qt::AlignTop, entries.at(i).name);
        x += icon + spacing;
    }
    return pix;
}

} // namespace

PreviewTheme PreviewTheme::load(const QString &themeConfPath, const QString &themesRoot)
{
    PreviewTheme theme;
    QFile file(themeConfPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return theme;
    const QString confDir = QFileInfo(themeConfPath).path();
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        // First whitespace-separated token is the directive; the bundled
        // themes pad some lines with trailing spaces, hence the trims.
        int split = 0;
        while (split < line.size() && !line.at(split).isSpace())
            ++split;
        if (split == line.size())
            continue;
        const QString directive = line.left(split).toLower();
        QString value = line.mid(split).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"'))
            && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);
        if (directive == QLatin1String("banner"))
            theme.bannerPath = resolveThemeAsset(value, themesRoot, confDir);
        else if (directive == QLatin1String("selection_big"))
            theme.selectionPath = resolveThemeAsset(value, themesRoot, confDir);
        else if (directive == QLatin1String("big_icon_size"))
            theme.bigIconSize = value.section(QLatin1Char(' '), 0, 0).toInt();
    }
    return theme;
}

PreviewDialog::PreviewDialog(const QString &backgroundPath, const QList<PreviewEntry> &entries,
                             int iconSize, int defaultIndex, const PreviewTheme &theme,
                             const QString &confText, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preview"));
    resize(760, 560);

    auto *tabs = new QTabWidget(this);

    auto *screenTab = new QWidget(tabs);
    auto *screenLayout = new QVBoxLayout(screenTab);
    auto *screenLabel = new QLabel(screenTab);
    screenLabel->setAlignment(Qt::AlignCenter);
    const QPixmap mock = renderMockScreen(backgroundPath, entries, iconSize,
                                          defaultIndex, theme);
    screenLabel->setPixmap(mock.scaled(720, 450, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
    QString noteText;
    if (theme.name.isEmpty()) {
        noteText = tr("Approximate preview — rEFInd's real rendering also "
                      "depends on the firmware resolution and theme.");
    } else if (theme.randomPick) {
        noteText = tr("Approximate preview showing the randomly picked \"%1\" theme — "
                      "Random picks a theme anew each time the config is created.")
                       .arg(theme.name);
    } else {
        noteText = tr("Approximate preview with the \"%1\" theme applied — rEFInd's "
                      "real rendering also depends on the firmware resolution and "
                      "the theme's other settings.")
                       .arg(theme.name);
    }
    auto *note = new QLabel(noteText, screenTab);
    note->setWordWrap(true);
    screenLayout->addWidget(screenLabel, 1);
    screenLayout->addWidget(note);
    tabs->addTab(screenTab, tr("Boot screen"));

    auto *confView = new QPlainTextEdit(tabs);
    confView->setReadOnly(true);
    confView->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::TypeWriter);
    confView->setFont(mono);
    confView->setPlainText(confText);
    tabs->addTab(confView, QStringLiteral("refind.conf")); // file name, not prose
}
