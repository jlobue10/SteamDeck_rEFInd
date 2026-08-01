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

// The subset of a theme's theme.conf that changes what the mock screen
// draws. The theme include is the last line of the generated config, so
// these directives supersede the user's background and icon size — a
// preview that ignored them would show a boot screen the theme replaces.
// Default-constructed = no theme (the preview draws the user's choices).
struct PreviewTheme {
    QString name;            // theme directory name; empty = no theme
    bool randomPick = false; // name came from resolving the Random entry
    QString bannerPath;      // resolved banner image; empty = user background
    QString selectionPath;   // resolved selection_big image; empty = builtin
    int bigIconSize = 0;     // theme's big_icon_size; 0 = the GUI's icon size

    // Parses the visual directives out of themeConfPath. Asset paths in
    // theme.conf are ESP-relative ("themes/<name>/..."); themesRoot is the
    // local themes directory they resolve against. An asset that does not
    // resolve to an existing file stays empty, and the preview falls back
    // to its themeless drawing for that element. Never sets name/randomPick.
    static PreviewTheme load(const QString &themeConfPath, const QString &themesRoot);
};

// Approximate mock of the rEFInd boot screen (background, icon row, default
// selection highlight, theme overrides) plus the generated refind.conf text,
// so the create -> install -> reboot loop isn't the first time the user sees
// the result. Purely cosmetic: rEFInd's real rendering also depends on
// firmware mode and theme details the mock doesn't model.
class PreviewDialog : public QDialog
{
    Q_OBJECT

public:
    PreviewDialog(const QString &backgroundPath, const QList<PreviewEntry> &entries,
                  int iconSize, int defaultIndex, const PreviewTheme &theme,
                  const QString &confText, QWidget *parent = nullptr);
};

#endif // PREVIEWDIALOG_H
