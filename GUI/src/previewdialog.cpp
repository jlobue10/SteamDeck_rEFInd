#include "previewdialog.h"

#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

// Renders the mock screen at rEFInd's reference geometry (1280x800, the
// Deck-class panel) and lets the label scale it down; the icon row uses the
// same proportions rEFInd does for big_icon_size at that resolution.
QPixmap renderMockScreen(const QString &backgroundPath, const QList<PreviewEntry> &entries,
                         int iconSize, int defaultIndex)
{
    const QSize canvas(1280, 800);
    QPixmap pix(canvas);
    pix.fill(Qt::black);
    QPainter p(&pix);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing);

    QPixmap bg(backgroundPath);
    if (!bg.isNull())
        p.drawPixmap(pix.rect(), bg); // banner_scale fillscreen

    if (entries.isEmpty()) {
        p.setPen(Qt::white);
        p.drawText(pix.rect(), Qt::AlignCenter,
                   PreviewDialog::tr("No boot options selected."));
        return pix;
    }

    const int icon = qBound(48, iconSize, 512);
    const int spacing = icon / 2;
    const int rowWidth = entries.size() * icon + (entries.size() - 1) * spacing;
    int x = (canvas.width() - rowWidth) / 2;
    const int y = (canvas.height() - icon) / 2;

    QFont nameFont = p.font();
    nameFont.setPixelSize(qMax(14, icon / 7));
    p.setFont(nameFont);

    for (int i = 0; i < entries.size(); ++i) {
        const QRect iconRect(x, y, icon, icon);
        if (i == defaultIndex) {
            // rEFInd draws the selection as a light rounded backdrop.
            QPainterPath path;
            path.addRoundedRect(iconRect.adjusted(-icon / 8, -icon / 8,
                                                  icon / 8, icon / 8),
                                icon / 8, icon / 8);
            p.fillPath(path, QColor(255, 255, 255, 90));
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

PreviewDialog::PreviewDialog(const QString &backgroundPath, const QList<PreviewEntry> &entries,
                             int iconSize, int defaultIndex, const QString &confText,
                             QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preview"));
    resize(760, 560);

    auto *tabs = new QTabWidget(this);

    auto *screenTab = new QWidget(tabs);
    auto *screenLayout = new QVBoxLayout(screenTab);
    auto *screenLabel = new QLabel(screenTab);
    screenLabel->setAlignment(Qt::AlignCenter);
    const QPixmap mock = renderMockScreen(backgroundPath, entries, iconSize, defaultIndex);
    screenLabel->setPixmap(mock.scaled(720, 450, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
    auto *note = new QLabel(tr("Approximate preview — rEFInd's real rendering also "
                               "depends on the firmware resolution and theme."),
                            screenTab);
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

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
}
