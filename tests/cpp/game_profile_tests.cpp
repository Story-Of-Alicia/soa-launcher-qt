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

class GameProfileTests final : public QObject
{
    Q_OBJECT

private slots:
    void game_profiles_remain_distinct()
    {
        const auto& first = core::game::profile(core::game::GameVersion::Playtest);
        const auto& second = core::game::profile(core::game::GameVersion::Alicia2);
        QVERIFY(QString::fromLatin1(first.default_install_directory)
                != QString::fromLatin1(second.default_install_directory));
        QVERIFY(QString::fromLatin1(first.video_settings_registry_key)
                != QString::fromLatin1(second.video_settings_registry_key));
        QCOMPARE(core::game::to_string(core::game::GameVersion::Playtest), QStringLiteral("1.0"));
        QCOMPARE(core::game::to_string(core::game::GameVersion::Alicia2), QStringLiteral("2.0"));
    }

};

QTEST_MAIN(GameProfileTests)
#include "game_profile_tests.moc"
