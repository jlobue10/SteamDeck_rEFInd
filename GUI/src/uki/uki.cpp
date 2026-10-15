#include "uki/uki.h"

#include <QDir>
#include <QFile>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>

namespace Uki {

namespace {

const char kMarkerPrefix[] = "# rEFInd_GUI-uki token=";
const char kMarkerOs[] = " os=";

// .osrel/.uname are a few hundred bytes; anything far larger is not a
// section systemd-stub/ukify wrote.
const quint32 kMaxSectionBytes = 64 * 1024;

quint16 le16(const QByteArray &b, int off)
{
    return quint16(quint8(b.at(off))) | quint16(quint8(b.at(off + 1))) << 8;
}

quint32 le32(const QByteArray &b, int off)
{
    return quint32(quint8(b.at(off))) | quint32(quint8(b.at(off + 1))) << 8
        | quint32(quint8(b.at(off + 2))) << 16 | quint32(quint8(b.at(off + 3))) << 24;
}

QString unquoteOsRelease(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2
        && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

// Characters a loader path may carry unquoted in refind.conf (rEFInd does
// not strip quotes from path tokens) -- anything else is skipped rather than
// written as a stanza that cannot boot.
const QRegularExpression &safeStemRe()
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9._+~-]+$"));
    return re;
}

bool isSafeToken(const QString &token)
{
    return !token.isEmpty() && safeStemRe().match(token).hasMatch();
}

// Brace depth change of one refind.conf line, ignoring quoted text and
// whole-line comments.
int braceDelta(const QString &line)
{
    if (line.trimmed().startsWith(QLatin1Char('#')))
        return 0;
    int delta = 0;
    bool quoted = false;
    for (const QChar c : line) {
        if (c == QLatin1Char('"'))
            quoted = !quoted;
        else if (!quoted && c == QLatin1Char('{'))
            ++delta;
        else if (!quoted && c == QLatin1Char('}'))
            --delta;
    }
    return delta;
}

// First whitespace-separated word of a config line, lowercased.
QString keyword(const QString &line)
{
    const QString t = line.trimmed();
    int end = 0;
    while (end < t.size() && !t.at(end).isSpace() && t.at(end) != QLatin1Char('{'))
        ++end;
    return t.left(end).toLower();
}

QString valueOf(const QString &line)
{
    QString v = line.trimmed();
    int end = 0;
    while (end < v.size() && !v.at(end).isSpace())
        ++end;
    v = v.mid(end).trimmed();
    if (v.size() >= 2 && v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"')))
        v = v.mid(1, v.size() - 2);
    return v;
}

struct Block
{
    int marker = -1;
    int header = -1;
    int close = -1;
    QString token;
    QString osId;
    QString volume;
    int loaderLine = -1;
    QString loader;
    QList<QPair<int, int>> submenus; // inclusive line ranges
};

QList<Block> parseBlocks(const QStringList &lines, QStringList *notes)
{
    QList<Block> blocks;
    const QString prefix = QLatin1String(kMarkerPrefix);
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (!trimmed.startsWith(prefix))
            continue;
        Block b;
        b.marker = i;
        b.token = trimmed.mid(prefix.size()).trimmed();
        const int os = b.token.indexOf(QLatin1String(kMarkerOs));
        if (os >= 0) {
            b.osId = b.token.mid(os + int(qstrlen(kMarkerOs))).trimmed();
            b.token = b.token.left(os).trimmed();
        }
        if (!isSafeToken(b.token) || (!b.osId.isEmpty() && !isSafeToken(b.osId))) {
            if (notes)
                *notes << QStringLiteral("Skipped a UKI marker with an unusable token on line %1.")
                              .arg(i + 1);
            continue;
        }
        int j = i + 1;
        while (j < lines.size() && lines.at(j).trimmed().isEmpty())
            ++j;
        if (j >= lines.size() || keyword(lines.at(j)) != QLatin1String("menuentry")
            || braceDelta(lines.at(j)) != 1) {
            if (notes)
                *notes << QStringLiteral("UKI marker for '%1' on line %2 is not followed by a "
                                         "menuentry; left unchanged.")
                              .arg(b.token)
                              .arg(i + 1);
            continue;
        }
        b.header = j;
        int depth = 1;
        int subStart = -1;
        int k = j + 1;
        for (; k < lines.size(); ++k) {
            const QString &line = lines.at(k);
            const int before = depth;
            depth += braceDelta(line);
            if (before == 1) {
                const QString kw = keyword(line);
                if (kw == QLatin1String("loader") && b.loaderLine < 0) {
                    b.loaderLine = k;
                    b.loader = valueOf(line);
                } else if (kw == QLatin1String("volume") && b.volume.isEmpty()) {
                    b.volume = valueOf(line);
                } else if (kw == QLatin1String("submenuentry")) {
                    subStart = k;
                }
            }
            if (subStart >= 0 && depth == 1) {
                b.submenus.append({subStart, k});
                subStart = -1;
            }
            if (depth <= 0)
                break;
        }
        // Only the shape this module writes is rewritten: a lone closing
        // brace, and no further loader lines hiding at depth 1.
        if (k >= lines.size() || depth != 0 || lines.at(k).trimmed() != QLatin1String("}")
            || subStart >= 0) {
            if (notes)
                *notes << QStringLiteral("The UKI stanza for '%1' (line %2) is not in the "
                                         "expected format; left unchanged.")
                              .arg(b.token)
                              .arg(j + 1);
            continue;
        }
        b.close = k;
        blocks.append(b);
        i = k;
    }
    return blocks;
}

} // namespace

const char kNoToken[] = "_none_";

QString markerLine(const QString &token, const QString &osId)
{
    QString line = QLatin1String(kMarkerPrefix) + token;
    if (isSafeToken(osId))
        line += QLatin1String(kMarkerOs) + osId;
    return line;
}

bool isMachineIdToken(const QString &token)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{32}$"));
    return re.match(token).hasMatch();
}

bool readImageInfo(const QString &path, QString *osName, QString *osId, QString *kernelVersion)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray dos = f.read(64);
    if (dos.size() < 64 || dos.at(0) != 'M' || dos.at(1) != 'Z')
        return false;
    const quint32 peOff = le32(dos, 0x3C);
    if (peOff < 64 || peOff > 16 * 1024 * 1024 || !f.seek(peOff))
        return false;
    const QByteArray coff = f.read(24);
    if (coff.size() < 24 || !coff.startsWith(QByteArray("PE\0\0", 4)))
        return false;
    const quint16 sections = le16(coff, 4 + 2);
    const quint16 optSize = le16(coff, 4 + 16);
    if (sections == 0 || sections > 96 || !f.seek(qint64(peOff) + 24 + optSize))
        return false;
    const QByteArray table = f.read(qint64(sections) * 40);
    if (table.size() < sections * 40)
        return false;

    QByteArray osrel, uname;
    for (int s = 0; s < sections; ++s) {
        const int base = s * 40;
        QByteArray name = table.mid(base, 8);
        const int nul = name.indexOf('\0');
        if (nul >= 0)
            name.truncate(nul);
        if (name != ".osrel" && name != ".uname")
            continue;
        const quint32 virtualSize = le32(table, base + 8);
        const quint32 rawSize = le32(table, base + 16);
        const quint32 rawPtr = le32(table, base + 20);
        quint32 size = virtualSize ? qMin(virtualSize, rawSize) : rawSize;
        size = qMin(size, kMaxSectionBytes);
        if (size == 0 || !f.seek(rawPtr))
            continue;
        QByteArray data = f.read(size);
        const int end = data.indexOf('\0');
        if (end >= 0)
            data.truncate(end);
        (name == ".osrel" ? osrel : uname) = data;
    }

    QString name, pretty, id;
    for (const QByteArray &raw : osrel.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QLatin1Char('#')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq);
        const QString value = unquoteOsRelease(line.mid(eq + 1));
        if (key == QLatin1String("NAME"))
            name = value;
        else if (key == QLatin1String("PRETTY_NAME"))
            pretty = value;
        else if (key == QLatin1String("ID"))
            id = value;
    }
    if (osName)
        *osName = !name.isEmpty() ? name : !pretty.isEmpty() ? pretty : id;
    if (osId)
        *osId = id;
    if (kernelVersion)
        *kernelVersion = QString::fromUtf8(uname).trimmed();
    return true;
}

bool splitFileName(const QString &fileName, const QString &knownVersion, QString *token,
                   QString *version)
{
    if (!fileName.endsWith(QLatin1String(".efi"), Qt::CaseInsensitive))
        return false;
    QString stem = fileName.left(fileName.size() - 4);
    if (!safeStemRe().match(stem).hasMatch())
        return false;
    // kernel-install boot counting: <name>+<left>[-<done>].efi
    static const QRegularExpression bootCount(QStringLiteral("\\+\\d+(-\\d+)?$"));
    stem.remove(bootCount);
    if (stem.isEmpty())
        return false;

    QString t, v;
    static const QRegularExpression machineId(QStringLiteral("^([0-9a-fA-F]{32})-(.+)$"));
    static const QRegularExpression tokenDashDigit(QStringLiteral("^(.+?)-(\\d.*)$"));
    const QString known = knownVersion.trimmed();
    if (!known.isEmpty() && stem.size() > known.size() + 1
        && stem.endsWith(QLatin1Char('-') + known)) {
        t = stem.left(stem.size() - known.size() - 1);
        v = known;
    } else if (const QRegularExpressionMatch m = machineId.match(stem); m.hasMatch()) {
        t = m.captured(1);
        v = m.captured(2);
    } else if (stem.at(0).isDigit()) {
        t = QLatin1String(kNoToken);
        v = !known.isEmpty() ? known : stem;
    } else if (const QRegularExpressionMatch m = tokenDashDigit.match(stem); m.hasMatch()) {
        t = m.captured(1);
        v = m.captured(2);
    } else {
        t = stem;
        v = known;
    }
    if (!isSafeToken(t))
        return false;
    if (token)
        *token = t;
    if (version)
        *version = v;
    return true;
}

int compareVersions(const QString &a, const QString &b)
{
    int i = 0, j = 0;
    while (true) {
        while (i < a.size() && !a.at(i).isLetterOrNumber())
            ++i;
        while (j < b.size() && !b.at(j).isLetterOrNumber())
            ++j;
        if (i >= a.size() || j >= b.size())
            break;
        const bool digitA = a.at(i).isDigit();
        const bool digitB = b.at(j).isDigit();
        if (digitA != digitB)
            return digitA ? 1 : -1;
        int ei = i, ej = j;
        while (ei < a.size() && (digitA ? a.at(ei).isDigit() : a.at(ei).isLetter()))
            ++ei;
        while (ej < b.size() && (digitB ? b.at(ej).isDigit() : b.at(ej).isLetter()))
            ++ej;
        QString sa = a.mid(i, ei - i), sb = b.mid(j, ej - j);
        if (digitA) {
            while (sa.size() > 1 && sa.startsWith(QLatin1Char('0')))
                sa.remove(0, 1);
            while (sb.size() > 1 && sb.startsWith(QLatin1Char('0')))
                sb.remove(0, 1);
            if (sa.size() != sb.size())
                return sa.size() < sb.size() ? -1 : 1;
        }
        const int c = sa.compare(sb);
        if (c != 0)
            return c < 0 ? -1 : 1;
        i = ei;
        j = ej;
    }
    const bool restA = i < a.size();
    const bool restB = j < b.size();
    if (restA == restB)
        return 0;
    return restA ? 1 : -1;
}

bool imageFromLoaderPath(const QString &loaderPath, Image *image)
{
    // /EFI/Linux/<file>.efi exactly -- deeper paths are not kernel-install's.
    const QStringList parts = loaderPath.split(QLatin1Char('/'));
    if (parts.size() != 4 || !parts.at(0).isEmpty()
        || parts.at(1).compare(QLatin1String("EFI"), Qt::CaseInsensitive) != 0
        || parts.at(2).compare(QLatin1String("Linux"), Qt::CaseInsensitive) != 0)
        return false;
    Image img;
    img.loaderPath = loaderPath;
    img.fileName = parts.at(3);
    if (!splitFileName(img.fileName, QString(), &img.token, &img.kernelVersion))
        return false;
    *image = img;
    return true;
}

QList<Family> groupImages(const QList<Image> &images)
{
    QMap<QString, QList<Image>> byToken;
    for (const Image &img : images) {
        if (isSafeToken(img.token) && !img.loaderPath.isEmpty())
            byToken[img.token].append(img);
    }
    // Tokenless images (built with an empty machine-id, like Fedora's UKI
    // cloud image) are the same install as the one tokened family of their
    // OS once kernel-install has added a kernel: fold them in as its older
    // kernels instead of offering a second entry for the same OS.
    const QString none = QLatin1String(kNoToken);
    if (byToken.contains(none)) {
        QString bareOs;
        for (const Image &img : byToken.value(none)) {
            if (!img.osId.isEmpty())
                bareOs = img.osId;
        }
        QStringList sameOs;
        if (!bareOs.isEmpty()) {
            for (auto it = byToken.cbegin(); it != byToken.cend(); ++it) {
                if (it.key() == none)
                    continue;
                for (const Image &img : it.value()) {
                    if (img.osId == bareOs) {
                        sameOs << it.key();
                        break;
                    }
                }
            }
        }
        if (sameOs.size() == 1)
            byToken[sameOs.first()] += byToken.take(none);
    }
    QList<Family> families;
    for (auto it = byToken.begin(); it != byToken.end(); ++it) {
        QList<Image> list = it.value();
        std::sort(list.begin(), list.end(), [](const Image &x, const Image &y) {
            const int c = compareVersions(x.kernelVersion, y.kernelVersion);
            if (c != 0)
                return c > 0;
            return x.fileName > y.fileName;
        });
        Family fam;
        fam.token = it.key();
        for (const Image &img : list) {
            if (fam.osName.isEmpty() && !img.osName.isEmpty())
                fam.osName = img.osName;
            if (fam.osId.isEmpty() && !img.osId.isEmpty())
                fam.osId = img.osId;
            fam.loaders << img.loaderPath;
            fam.versions << img.kernelVersion;
        }
        families.append(fam);
    }
    return families;
}

QList<Family> scanFamilies(const QString &espRoot, bool readSections)
{
    // FAT is case-insensitive but QDir on a case-sensitive view of it (or a
    // test fixture) is not: find the real spelling of EFI/Linux first.
    const QDir root(espRoot);
    QString efiName, linuxName;
    for (const QString &d : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (d.compare(QLatin1String("EFI"), Qt::CaseInsensitive) == 0) {
            efiName = d;
            break;
        }
    }
    if (efiName.isEmpty())
        return {};
    const QDir efi(root.filePath(efiName));
    for (const QString &d : efi.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (d.compare(QLatin1String("Linux"), Qt::CaseInsensitive) == 0) {
            linuxName = d;
            break;
        }
    }
    if (linuxName.isEmpty())
        return {};
    const QDir dir(efi.filePath(linuxName));
    QList<Image> images;
    for (const QString &file : dir.entryList(QDir::Files, QDir::Name)) {
        if (!file.endsWith(QLatin1String(".efi"), Qt::CaseInsensitive))
            continue;
        Image img;
        img.fileName = file;
        img.loaderPath = QLatin1Char('/') + efiName + QLatin1Char('/') + linuxName
            + QLatin1Char('/') + file;
        QString uname;
        if (readSections)
            readImageInfo(dir.filePath(file), &img.osName, &img.osId, &uname);
        if (!splitFileName(file, uname, &img.token, &img.kernelVersion))
            continue;
        images.append(img);
    }
    return groupImages(images);
}

QString renderLoaderLines(const QStringList &loaders, const QStringList &versions)
{
    QString out;
    if (loaders.isEmpty())
        return out;
    out += QLatin1String("\tloader ") + loaders.first() + QLatin1Char('\n');
    for (int i = 1; i < loaders.size(); ++i) {
        const QString version = i < versions.size() ? versions.at(i) : QString();
        const QString title = !version.isEmpty() ? version : loaders.at(i).section(QLatin1Char('/'), -1);
        out += QLatin1String("\tsubmenuentry \"Boot ") + title + QLatin1String("\" {\n");
        out += QLatin1String("\t\tloader ") + loaders.at(i) + QLatin1Char('\n');
        out += QLatin1String("\t}\n");
    }
    return out;
}

QString refreshConf(const QString &conf, const FamilyLookup &lookup, bool *changed,
                    QStringList *notes)
{
    if (changed)
        *changed = false;
    QStringList lines = conf.split(QLatin1Char('\n'));
    const QList<Block> blocks = parseBlocks(lines, notes);

    // Rewrite from the bottom up so earlier line indices stay valid.
    for (int n = blocks.size() - 1; n >= 0; --n) {
        const Block &b = blocks.at(n);
        const QList<Family> onVolume = lookup ? lookup(b.volume) : QList<Family>();
        const Family *match = nullptr;
        for (const Family &f : onVolume) {
            if (f.token == b.token && !f.loaders.isEmpty())
                match = &f;
        }
        if (!match) {
            // The token can change under a family (machine-id reset, or a
            // tokenless image replaced by a kernel-install'ed one): follow
            // the OS id, or the only family there is -- never guess between
            // several.
            QList<const Family *> candidates;
            for (const Family &f : onVolume) {
                if (!f.loaders.isEmpty() && (b.osId.isEmpty() || f.osId == b.osId))
                    candidates << &f;
            }
            if (candidates.size() == 1)
                match = candidates.first();
        }
        if (!match || match->token == QLatin1String(kNoToken)) {
            // A tokenless image (built with an empty machine-id) is followed
            // by kernel-install'ed ones carrying the machine-id: hand the
            // stanza over to the one newer tokened family of the same OS.
            QList<const Family *> successors;
            for (const Family &f : onVolume) {
                if (f.token != QLatin1String(kNoToken) && !f.loaders.isEmpty()
                    && (b.osId.isEmpty() || f.osId == b.osId))
                    successors << &f;
            }
            if (successors.size() == 1
                && (!match
                    || compareVersions(successors.first()->versions.value(0),
                                       match->versions.value(0)) > 0))
                match = successors.first();
        }
        if (!match) {
            if (notes)
                *notes << QStringLiteral("No UKIs for '%1' were found; its stanza was left "
                                         "unchanged.")
                              .arg(b.token);
            continue;
        }
        const Family &fam = *match;
        const QString rendered = renderLoaderLines(fam.loaders, fam.versions);
        const int firstNl = rendered.indexOf(QLatin1Char('\n'));
        const QString loaderLine = rendered.left(firstNl);
        QStringList submenuLines = rendered.mid(firstNl + 1).split(QLatin1Char('\n'));
        if (!submenuLines.isEmpty() && submenuLines.last().isEmpty())
            submenuLines.removeLast();

        QStringList body;
        bool loaderPlaced = false;
        for (int k = b.header + 1; k < b.close; ++k) {
            bool inSubmenu = false;
            for (const auto &range : b.submenus) {
                if (k >= range.first && k <= range.second) {
                    inSubmenu = true;
                    break;
                }
            }
            if (inSubmenu)
                continue;
            if (k == b.loaderLine) {
                body << loaderLine;
                loaderPlaced = true;
                continue;
            }
            body << lines.at(k);
        }
        if (!loaderPlaced)
            body << loaderLine;
        body << submenuLines;

        QStringList replacement;
        replacement << lines.at(b.header) << body << lines.at(b.close);
        const QStringList original = lines.mid(b.header, b.close - b.header + 1);
        if (replacement != original) {
            for (int k = b.close; k >= b.header; --k)
                lines.removeAt(k);
            for (int k = 0; k < replacement.size(); ++k)
                lines.insert(b.header + k, replacement.at(k));
            if (changed)
                *changed = true;
        }
        if (fam.token != b.token || (b.osId.isEmpty() && !fam.osId.isEmpty())) {
            const QString marker = markerLine(fam.token, !fam.osId.isEmpty() ? fam.osId : b.osId);
            const QString old = lines.at(b.marker);
            lines[b.marker] = old.left(old.indexOf(QLatin1Char('#'))) + marker;
            if (notes && fam.token != b.token)
                *notes << QStringLiteral("UKI entry token '%1' is now '%2'.").arg(b.token, fam.token);
            if (changed)
                *changed = true;
        }
    }
    return lines.join(QLatin1Char('\n'));
}

bool kernelInstalled(const QString &version, const QString &modulesRoot)
{
    if (version.isEmpty())
        return true;
    if (version.contains(QLatin1Char('/')) || version.startsWith(QLatin1Char('.')))
        return true; // not a kernel version string; never prune on it
    const QString dir = modulesRoot + QLatin1Char('/') + version;
    return QFile::exists(dir + QLatin1String("/modules.builtin"))
        || QFile::exists(dir + QLatin1String("/modules.order"));
}

bool modulesTreeUsable(const QString &modulesRoot)
{
    const QDir root(modulesRoot);
    for (const QString &d : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (kernelInstalled(d, modulesRoot))
            return true;
    }
    return false;
}

bool isThisMachinesFamily(const QString &token, const QString &osId, const QString &machineId,
                          const QString &runningOsId)
{
    if (!machineId.isEmpty() && token.compare(machineId, Qt::CaseInsensitive) == 0)
        return true;
    return token == QLatin1String(kNoToken) && !osId.isEmpty() && osId == runningOsId;
}

QStringList dropUninstalledKernels(QStringList *loaders, QStringList *versions,
                                   const std::function<bool(const QString &version)> &installed)
{
    QStringList dropped;
    if (!loaders || !versions || !installed)
        return dropped;
    QStringList keptLoaders, keptVersions;
    for (int i = 0; i < loaders->size(); ++i) {
        const QString v = versions->value(i);
        if (installed(v)) {
            keptLoaders << loaders->at(i);
            keptVersions << v;
        } else {
            dropped << loaders->at(i);
        }
    }
    if (keptLoaders.isEmpty())
        return {}; // never empty a family: better a stale entry than none
    *loaders = keptLoaders;
    *versions = keptVersions;
    return dropped;
}

QString ukiBootedBy(const BootVar &var)
{
    QString loader = var.loaderPath;
    loader.replace(QLatin1Char('\\'), QLatin1Char('/'));
    Image img;
    if (imageFromLoaderPath(loader, &img))
        return img.loaderPath;
    const QByteArray &raw = var.optionalData;
    if (raw.size() < 4)
        return {};
    QString arg = QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData()),
                                     raw.size() / 2);
    const int nul = arg.indexOf(QChar(0));
    if (nul >= 0)
        arg.truncate(nul);
    arg = arg.trimmed();
    arg.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (imageFromLoaderPath(arg, &img))
        return img.loaderPath;
    return {};
}

PriorityPlan planRefindPriority(const QList<quint16> &bootOrder, int bootNext,
                                const QList<BootVar> &vars, bool (*isRefindLoader)(const QString &))
{
    PriorityPlan plan;
    const auto find = [&vars](quint16 num) -> const BootVar * {
        for (const BootVar &v : vars) {
            if (v.num == num)
                return &v;
        }
        return nullptr;
    };
    // rEFInd's entry: the first active one in BootOrder.
    int refindPos = -1;
    for (int i = 0; i < bootOrder.size(); ++i) {
        const BootVar *v = find(bootOrder.at(i));
        if (v && v->active && isRefindLoader && isRefindLoader(v->loaderPath)) {
            refindPos = i;
            break;
        }
    }
    if (refindPos < 0)
        return plan; // no rEFInd in BootOrder: the user's choice, not ours
    plan.haveRefind = true;
    plan.refindNum = bootOrder.at(refindPos);

    if (bootNext >= 0 && quint16(bootNext) != plan.refindNum) {
        const BootVar *next = find(quint16(bootNext));
        if (next && !ukiBootedBy(*next).isEmpty())
            plan.clearBootNext = true;
    }
    bool ukiAhead = false;
    for (int i = 0; i < refindPos; ++i) {
        const BootVar *v = find(bootOrder.at(i));
        if (v && !ukiBootedBy(*v).isEmpty())
            ukiAhead = true;
    }
    if (ukiAhead) {
        plan.bootOrder = bootOrder;
        plan.bootOrder.removeAt(refindPos);
        plan.bootOrder.prepend(plan.refindNum);
    }
    return plan;
}

QList<MarkedStanza> markedStanzas(const QString &conf)
{
    QList<MarkedStanza> out;
    const QStringList lines = conf.split(QLatin1Char('\n'));
    for (const Block &b : parseBlocks(lines, nullptr))
        out.append({b.token, b.osId, b.volume, b.loader});
    return out;
}

} // namespace Uki
