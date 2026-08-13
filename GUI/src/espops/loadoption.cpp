// Parity-locked between rEFInd_GUI and SteamDeck_rEFInd — see loadoption.h.

#include "loadoption.h"

#include <QRegularExpression>

namespace EspOps {

namespace {

quint16 readU16(const uchar *p)
{
    return quint16(p[0]) | (quint16(p[1]) << 8);
}

quint32 readU32(const uchar *p)
{
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16)
        | (quint32(p[3]) << 24);
}

// Decode a null-terminated UTF-16LE string starting at `off`, staying inside
// `end`. On success sets `next` to the first byte after the terminator.
// Returns false when no terminator exists inside the window.
bool readUtf16z(const QByteArray &raw, int off, int end, QString *out, int *next)
{
    const uchar *base = reinterpret_cast<const uchar *>(raw.constData());
    QString s;
    int i = off;
    for (; i + 1 < end; i += 2) {
        const quint16 cu = readU16(base + i);
        if (cu == 0) {
            *out = s;
            *next = i + 2;
            return true;
        }
        s.append(QChar(cu));
    }
    return false;
}

// EFI GUIDs store the first three fields little-endian and the last eight
// bytes verbatim (mixed-endian, unlike RFC 4122 text order).
QString formatEfiGuid(const uchar *g)
{
    return QString::asprintf(
        "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        readU32(g), readU16(g + 4), readU16(g + 6),
        g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}

} // namespace

LoadOption parseLoadOption(const QByteArray &raw)
{
    LoadOption opt;
    // UINT32 Attributes + UINT16 FilePathListLength + at least a CHAR16
    // description terminator.
    if (raw.size() < 4 + 2 + 2)
        return opt;

    const uchar *base = reinterpret_cast<const uchar *>(raw.constData());
    opt.attributes = readU32(base);
    const int filePathListLength = readU16(base + 4);

    int pathsBegin = 0;
    if (!readUtf16z(raw, 6, raw.size(), &opt.description, &pathsBegin))
        return opt;

    const int pathsEnd = pathsBegin + filePathListLength;
    if (filePathListLength < 4 || pathsEnd > raw.size())
        return opt; // no room for even an end node: not a load option

    // Walk the device-path nodes. Only the first GPT hard-drive node and the
    // first file-path node matter for ESP resolution; everything else
    // (vendor nodes, ACPI, USB, ...) is skipped by length.
    int i = pathsBegin;
    bool sawEnd = false;
    while (i + 4 <= pathsEnd) {
        const uchar type = base[i];
        const uchar subType = base[i + 1];
        const int len = readU16(base + i + 2);
        if (len < 4 || i + len > pathsEnd)
            return opt; // corrupt node

        if (type == 0x04 && subType == 0x01 && len >= 42) {
            // Media / Hard Drive: PartitionNumber(4) PartitionStart(8)
            // PartitionSize(8) Signature(16) MBRType(1) SignatureType(1).
            const uchar sigType = base[i + 41];
            if (sigType == 0x02 && opt.gptPartitionGuid.isEmpty()) {
                opt.gptPartitionGuid = formatEfiGuid(base + i + 24);
                opt.partitionNumber = readU32(base + i + 4);
            }
        } else if (type == 0x04 && subType == 0x04 && opt.loaderPath.isEmpty()) {
            // Media / File Path: null-terminated CHAR16 path fills the node.
            QString p;
            int after = 0;
            if (readUtf16z(raw, i + 4, i + len, &p, &after))
                opt.loaderPath = p;
        } else if (type == 0x7F && subType == 0xFF) {
            sawEnd = true;
            i += len;
            break;
        }
        // 0x7F/0x01 ends one instance of a multi-instance path; keep walking
        // — the first hard-drive/file-path nodes seen still win.
        i += len;
    }
    if (!sawEnd)
        return opt;

    opt.optionalData = raw.mid(pathsEnd);
    opt.valid = true;
    return opt;
}

QList<quint16> parseBootOrder(const QByteArray &raw)
{
    QList<quint16> order;
    const uchar *base = reinterpret_cast<const uchar *>(raw.constData());
    for (int i = 0; i + 2 <= raw.size(); i += 2)
        order.append(readU16(base + i));
    return order;
}

QString formatBootNum(quint16 num)
{
    return QString::asprintf("Boot%04X", num);
}

QByteArray fromEfivarsPayload(const QByteArray &raw)
{
    if (raw.size() < 4)
        return QByteArray();
    return raw.mid(4);
}

bool loaderLooksLikeRefind(const QString &loaderPath)
{
    QString p = loaderPath;
    p.replace(QLatin1Char('/'), QLatin1Char('\\'));
    static const QRegularExpression re(
        QStringLiteral("\\\\EFI\\\\refind\\\\refind[^\\\\]*\\.efi"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(p).hasMatch();
}

} // namespace EspOps
