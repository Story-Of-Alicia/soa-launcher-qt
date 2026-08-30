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

class ProcessDetectionTests final : public QObject
{
    Q_OBJECT

private slots:
    void parses_alicia_from_tasklist_csv()
    {
        const QString output = QStringLiteral(
            "\"services.exe\",\"52\",\"Services\",\"0\",\"8,000 K\"\n"
            "\"Alicia.exe\",\"184\",\"Console\",\"1\",\"220,000 K\"\n");
        const auto process = core::wine::find_windows_process(
            output, QStringLiteral("Alicia.exe"));
        QVERIFY(process.has_value());
        QCOMPARE(process->pid, qint64(184));
        QCOMPARE(process->image_name, QStringLiteral("Alicia.exe"));
    }

    void decodes_utf16le_tasklist_output()
    {
        const QString source = QStringLiteral(
            "\"services.exe\",\"52\"\r\n\"Alicia.exe\",\"4242\"\r\n");
        QByteArray utf16;
        utf16.append(char(0xff));
        utf16.append(char(0xfe));
        for (const QChar character : source)
        {
            const ushort unit = character.unicode();
            utf16.append(char(unit & 0xff));
            utf16.append(char((unit >> 8) & 0xff));
        }

        const QString decoded =
            core::wine::decode_windows_process_output(utf16);
        const auto process = core::wine::find_windows_process(
            decoded, QStringLiteral("Alicia.exe"));
        QVERIFY(process.has_value());
        QCOMPARE(process->pid, qint64(4242));
    }

    void decodes_utf16le_tasklist_after_host_warning()
    {
        const QString source = QStringLiteral(
            "\"services.exe\",\"52\"\r\n\"Alicia.exe\",\"4243\"\r\n");
        QByteArray mixed("runtime warning before tasklist\n");
        for (const QChar character : source)
        {
            const ushort unit = character.unicode();
            mixed.append(char(unit & 0xff));
            mixed.append(char((unit >> 8) & 0xff));
        }

        const QString decoded =
            core::wine::decode_windows_process_output(mixed);
        const auto process = core::wine::find_windows_process(
            decoded, QStringLiteral("Alicia.exe"));
        QVERIFY(process.has_value());
        QCOMPARE(process->pid, qint64(4243));
    }

    void parses_actual_host_alicia_process()
    {
        const QString output = QStringLiteral(
            "  912 /opt/wine/bin/wineserver\n"
            " 1001 /opt/wine/bin/wine explorer /desktop=StoryOfAlicia,1280x720 "
            "C:\\Story Of Alicia\\Alicia.exe -GameID 1\n"
            " 1017 /opt/wine/bin/winedbg --auto 1042 Alicia.exe\n"
            " 1042 /opt/wine/bin/wine64-preloader C:\\Story Of Alicia\\Alicia.exe -GameID 1\n");
        const auto process = core::wine::find_host_process(
            output, QStringLiteral("Alicia.exe"));
        QVERIFY(process.has_value());
        QCOMPARE(process->pid, qint64(1042));
        QVERIFY(process->source_line.contains(QStringLiteral("Alicia.exe")));
    }

    void ignores_alicia_name_inside_log_injector_wrapper()
    {
        const QString output = QStringLiteral(
            " 175130 /usr/bin/python3 /usr/bin/umu-run "
            "/prefix/SoaAliciaLogInjector.exe "
            "C:\\users\\test\\game\\Alicia.exe -GameID 4\n"
            " 175228 /prefix/SoaAliciaLogInjector.exe "
            "C:\\users\\test\\game\\Alicia.exe -GameID 4\n");
        const auto process = core::wine::find_host_process(
            output, QStringLiteral("Alicia.exe"));
        QVERIFY(!process.has_value());
    }

};

QTEST_MAIN(ProcessDetectionTests)
#include "process_detection_tests.moc"
