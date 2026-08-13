// Regression tests for the Linux ESP resolver internals (parity-locked
// between rEFInd_GUI and SteamDeck_rEFInd like the code under test).
//
// Includes the .cpp to reach anonymous-namespace helpers, so this target
// compiles espresolve_linux.cpp itself instead of linking the espops
// library (which would duplicate its exported symbols).

#include "espops/espresolve_linux.cpp"

#include <QtTest>

class TestEspResolve : public QObject
{
    Q_OBJECT

private slots:
    void mountPointOfFindsDevBackedMount();
};

// mountPointOf() reads /proc/self/mounts, where QFile::atEnd() is true
// before the first read (procfs files report size 0), so an atEnd()-guarded
// loop reads nothing and every lookup silently fails. Pick any /dev/-backed
// mount the kernel reports and require mountPointOf() to find it.
void TestEspResolve::mountPointOfFindsDevBackedMount()
{
    QFile mounts(QStringLiteral("/proc/self/mounts"));
    QVERIFY(mounts.open(QIODevice::ReadOnly));
    QString device;
    const QList<QByteArray> lines = mounts.readAll().split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> cols = line.split(' ');
        if (cols.size() >= 2 && cols.at(0).startsWith("/dev/")) {
            device = QString::fromLocal8Bit(cols.at(0));
            break;
        }
    }
    if (device.isEmpty())
        QSKIP("no /dev/-backed mounts visible in this environment");
    const QString mp = EspOps::mountPointOf(device);
    QVERIFY2(!mp.isEmpty(),
             qPrintable(QStringLiteral("no mountpoint found for ") + device));
    QVERIFY(mp.startsWith(QLatin1Char('/')));
}

QTEST_APPLESS_MAIN(TestEspResolve)
#include "tst_espresolve.moc"
