#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>

// One boot-menu slot as the preview needs it: the icon PNG that will be
// copied to the ESP (may be empty/invalid) and the entry's display name.
struct PreviewEntry {
    QString iconPath;
    QString name;
};

// Approximate mock of the rEFInd boot screen (background, icon row, default
// selection highlight) plus the generated refind.conf text, so the
// create -> install -> reboot loop isn't the first time the user sees the
// result. Purely cosmetic: rEFInd's real rendering also depends on firmware
// mode and theme details.
class PreviewDialog : public QDialog
{
    Q_OBJECT

public:
    PreviewDialog(const QString &backgroundPath, const QList<PreviewEntry> &entries,
                  int iconSize, int defaultIndex, const QString &confText,
                  QWidget *parent = nullptr);
};

#endif // PREVIEWDIALOG_H
