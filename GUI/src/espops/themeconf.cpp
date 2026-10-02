// Parity-locked between rEFInd_GUI and SteamDeck_rEFInd — see loadoption.h.

#include "themeconf.h"

namespace EspOps {

namespace {

// The directives whose value is a path relative to rEFInd's directory.
// "include" is deliberately absent: rEFInd honors it only in the main
// config file, never inside an included theme.conf.
const char *const kPathDirectives[] = {
    "banner", "icons_dir", "selection_big", "selection_small", "font", "icon",
};

// Where a line's asset path names its theme directory.
struct DirRef
{
    int tokenStart = 0; // the path token, without any surrounding quotes
    int tokenEnd = 0;   // one past the token's last byte
    int dirStart = 0;   // the component following "themes/"
    int dirLength = 0;
    bool quoted = false;
};

bool isBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool isSeparator(char c)
{
    return c == '/' || c == '\\';
}

// Finds the theme-directory component in one config line: the path
// component following the first "themes" component of a path directive's
// value ("themes/<dir>/..." as well as "/EFI/refind/themes/<dir>/...").
bool findDirRef(const QByteArray &line, DirRef *ref)
{
    const int n = int(line.size());
    int i = 0;
    while (i < n && isBlank(line.at(i)))
        ++i;
    if (i >= n || line.at(i) == '#')
        return false;
    const int directiveStart = i;
    while (i < n && !isBlank(line.at(i)))
        ++i;
    const QByteArray directive =
        line.mid(directiveStart, i - directiveStart).toLower();
    bool isPathDirective = false;
    for (const char *d : kPathDirectives) {
        if (directive == d) {
            isPathDirective = true;
            break;
        }
    }
    if (!isPathDirective)
        return false;

    while (i < n && isBlank(line.at(i)))
        ++i;
    if (i >= n)
        return false;
    if (line.at(i) == '"') {
        ref->quoted = true;
        ref->tokenStart = i + 1;
        ref->tokenEnd = int(line.indexOf('"', ref->tokenStart));
        if (ref->tokenEnd < 0)
            return false;
    } else {
        ref->quoted = false;
        ref->tokenStart = i;
        ref->tokenEnd = i;
        while (ref->tokenEnd < n && !isBlank(line.at(ref->tokenEnd)))
            ++ref->tokenEnd;
    }

    bool afterThemes = false;
    int componentStart = ref->tokenStart;
    for (int p = ref->tokenStart; p <= ref->tokenEnd; ++p) {
        if (p != ref->tokenEnd && !isSeparator(line.at(p)))
            continue;
        const int length = p - componentStart;
        if (afterThemes && length > 0) {
            // The component is a directory only when more path follows it;
            // "banner themes/bg.png" names a file kept directly in themes/.
            // icons_dir is the one directive whose whole value is a
            // directory.
            if (p == ref->tokenEnd && directive != "icons_dir")
                return false;
            ref->dirStart = componentStart;
            ref->dirLength = length;
            return true;
        }
        if (!afterThemes && length == 6
            && qstrnicmp(line.constData() + componentStart, "themes", 6) == 0)
            afterThemes = true;
        componentStart = p + 1;
    }
    return false;
}

bool isUtf16(const QByteArray &conf)
{
    return conf.startsWith("\xff\xfe") || conf.startsWith("\xfe\xff");
}

// Calls visit(line, ref-or-null) for every line of conf, newline included.
template <typename Visitor>
void forEachLine(const QByteArray &conf, Visitor visit)
{
    int pos = 0;
    while (pos < conf.size()) {
        const int newline = int(conf.indexOf('\n', pos));
        const int end = newline < 0 ? int(conf.size()) : newline + 1;
        const QByteArray line = conf.mid(pos, end - pos);
        pos = end;
        DirRef ref;
        visit(line, findDirRef(line, &ref) ? &ref : nullptr);
    }
}

} // namespace

QByteArray retargetThemeConf(const QByteArray &conf, const QString &themeDir,
                             const QStringList &siblingDirs, int *rewritten)
{
    if (rewritten)
        *rewritten = 0;
    const QByteArray own = themeDir.toUtf8();
    if (own.isEmpty() || isUtf16(conf))
        return conf;
    bool needsQuotes = false;
    for (const char c : own) {
        // Not expressible inside a (quoted) config token.
        if (c == '"' || c == '/' || c == '\\' || c == '\r' || c == '\n')
            return conf;
        if (c == ' ' || c == '\t')
            needsQuotes = true;
    }

    int count = 0;
    QByteArray out;
    out.reserve(conf.size() + 64);
    forEachLine(conf, [&](const QByteArray &line, const DirRef *ref) {
        if (!ref) {
            out += line;
            return;
        }
        const QString named =
            QString::fromUtf8(line.mid(ref->dirStart, ref->dirLength));
        if (named.compare(themeDir, Qt::CaseInsensitive) == 0
            || siblingDirs.contains(named, Qt::CaseInsensitive)) {
            out += line;
            return;
        }
        const int dirEnd = ref->dirStart + ref->dirLength;
        if (needsQuotes && !ref->quoted) {
            // A directory name with a space only parses as one token when
            // the whole path is quoted.
            out += line.left(ref->tokenStart);
            out += '"';
            out += line.mid(ref->tokenStart, ref->dirStart - ref->tokenStart);
            out += own;
            out += line.mid(dirEnd, ref->tokenEnd - dirEnd);
            out += '"';
            out += line.mid(ref->tokenEnd);
        } else {
            out += line.left(ref->dirStart);
            out += own;
            out += line.mid(dirEnd);
        }
        ++count;
    });
    if (rewritten)
        *rewritten = count;
    return count > 0 ? out : conf;
}

QStringList themeDirsReferenced(const QByteArray &conf)
{
    QStringList dirs;
    if (isUtf16(conf))
        return dirs;
    forEachLine(conf, [&](const QByteArray &line, const DirRef *ref) {
        if (!ref)
            return;
        const QString named =
            QString::fromUtf8(line.mid(ref->dirStart, ref->dirLength));
        if (!dirs.contains(named, Qt::CaseInsensitive))
            dirs << named;
    });
    return dirs;
}

} // namespace EspOps
