#include <QtTest>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <utility>

#include "common/GameVersion.hpp"
#include "runtime/GameSession.hpp"
#include "runtime/MacWineRuntime.hpp"
#include "runtime/PrefixInspector.hpp"
#include "runtime/ProcessRunner.hpp"
#include "runtime/RuntimeLocator.hpp"
#include "runtime/WineProcess.hpp"
#include "common/DesktopEntry.hpp"
#include "common/LaunchArguments.hpp"

class PrefixPathTests final : public QObject
{
    Q_OBJECT

private slots:
    void maps_prefix_file_to_windows_c_path()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("prefix"));
        const QString gameDirectory = QDir(prefix).filePath(
            QStringLiteral("drive_c/users/test/Story Of Alicia"));
        QVERIFY(QDir().mkpath(gameDirectory));

        const QString executable = QDir(gameDirectory).filePath(QStringLiteral("Alicia.exe"));
        QFile file(executable);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("MZ") > 0);
        file.close();

        QString error;
        QCOMPARE(core::wine::windows_path_for_prefix_file(prefix, executable, &error),
                 QStringLiteral("C:\\users\\test\\Story Of Alicia\\Alicia.exe"));
        QVERIFY(error.isEmpty());
    }

    void rejects_executable_outside_wine_c_drive()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("prefix"));
        QVERIFY(QDir().mkpath(QDir(prefix).filePath(QStringLiteral("drive_c"))));

        const QString executable = directory.filePath(QStringLiteral("Alicia.exe"));
        QFile file(executable);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("MZ") > 0);
        file.close();

        QString error;
        QVERIFY(core::wine::windows_path_for_prefix_file(prefix, executable, &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("outside"), Qt::CaseInsensitive));
    }

    void rejects_symlink_escape_from_wine_c_drive()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("prefix"));
        const QString gameDirectory = QDir(prefix).filePath(QStringLiteral("drive_c/game"));
        QVERIFY(QDir().mkpath(gameDirectory));

        const QString outside = directory.filePath(QStringLiteral("outside-Alicia.exe"));
        QFile file(outside);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("MZ") > 0);
        file.close();

        const QString linked = QDir(gameDirectory).filePath(QStringLiteral("Alicia.exe"));
        QVERIFY(QFile::link(outside, linked));

        QString error;
        QVERIFY(core::wine::windows_path_for_prefix_file(prefix, linked, &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("outside"), Qt::CaseInsensitive));
    }

    void allows_user_alias_that_resolves_inside_prefix()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("prefix"));
        const QString users = QDir(prefix).filePath(QStringLiteral("drive_c/users"));
        const QString steamUser = QDir(users).filePath(QStringLiteral("steamuser"));
        const QString roaming = QDir(steamUser).filePath(QStringLiteral("AppData/Roaming"));
        QVERIFY(QDir().mkpath(roaming));

        const QString alias = QDir(users).filePath(QStringLiteral("fennavb"));
        QVERIFY(QFile::link(steamUser, alias));
        const QString gameDirectory = QDir(alias).filePath(
            QStringLiteral("AppData/Roaming/Story Of Alicia/game"));

        QVERIFY(core::wine::host_path_is_inside_prefix(prefix, gameDirectory));
    }

    void rejects_directory_alias_that_resolves_outside_prefix()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString prefix = directory.filePath(QStringLiteral("prefix"));
        const QString users = QDir(prefix).filePath(QStringLiteral("drive_c/users"));
        QVERIFY(QDir().mkpath(users));

        const QString outside = directory.filePath(QStringLiteral("outside"));
        QVERIFY(QDir().mkpath(outside));
        const QString alias = QDir(users).filePath(QStringLiteral("escape"));
        QVERIFY(QFile::link(outside, alias));

        QVERIFY(!core::wine::host_path_is_inside_prefix(
            prefix, QDir(alias).filePath(QStringLiteral("game"))));
    }

};

QTEST_MAIN(PrefixPathTests)
#include "prefix_path_tests.moc"
