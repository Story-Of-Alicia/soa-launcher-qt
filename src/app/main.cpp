#include <QApplication>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFileOpenEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QPointer>
#include <QStandardPaths>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWidget>

#include <utility>

#include "app/MainWindow.hpp"
#include "common/Log.hpp"
#include "auth/AuthHandler.hpp"
#include "network/Courier.h"
#include "ui/Assets.hpp"
#include "i18n/LanguageManager.hpp"
#include "ui/LauncherLog.hpp"

#include <spdlog/spdlog.h>

#ifndef SOA_LAUNCHER_VERSION
#define SOA_LAUNCHER_VERSION "0.3.0"
#endif

namespace
{
    constexpr quint32 k_max_ipc_payload = 64 * 1024;

#ifdef Q_OS_LINUX
    bool restore_appimage_host_environment()
    {
        if (qgetenv("SOA_APPIMAGE_ENV_ACTIVE") != QByteArrayLiteral("1"))
            return false;

        const auto restore_variable = [](const char* target, const char* saved, const char* was_set)
        {
            if (qgetenv(was_set) == QByteArrayLiteral("1"))
                qputenv(target, qgetenv(saved));
            else
                qunsetenv(target);

            qunsetenv(saved);
            qunsetenv(was_set);
        };

        restore_variable(
            "LD_LIBRARY_PATH",
            "SOA_HOST_LD_LIBRARY_PATH",
            "SOA_HOST_LD_LIBRARY_PATH_SET");
        restore_variable(
            "QT_PLUGIN_PATH",
            "SOA_HOST_QT_PLUGIN_PATH",
            "SOA_HOST_QT_PLUGIN_PATH_SET");
        restore_variable(
            "QT_QPA_PLATFORM_PLUGIN_PATH",
            "SOA_HOST_QT_QPA_PLATFORM_PLUGIN_PATH",
            "SOA_HOST_QT_QPA_PLATFORM_PLUGIN_PATH_SET");

        qunsetenv("SOA_APPIMAGE_ENV_ACTIVE");
        qunsetenv("SOA_BUNDLED_QT_RUNTIME");
        return true;
    }
#else
    bool restore_appimage_host_environment()
    {
        return false;
    }
#endif

    bool is_auth_url(const QString& value)
    {
        const QUrl url(value, QUrl::StrictMode);
        if (!url.isValid()
            || url.scheme().compare(QStringLiteral("soa"), Qt::CaseInsensitive) != 0)
        {
            return false;
        }

        const QString host = url.host().trimmed().toLower();
        QString path = url.path().trimmed().toLower();
        while (path.startsWith(QLatin1Char('/')))
            path.remove(0, 1);

        if (host == QStringLiteral("launcher")
            || host == QStringLiteral("auth")
            || host == QStringLiteral("login"))
        {
            return path.isEmpty()
                || path == QStringLiteral("callback")
                || path == QStringLiteral("auth")
                || path == QStringLiteral("login");
        }

        return host.isEmpty()
            && (path == QStringLiteral("launcher")
                || path == QStringLiteral("callback")
                || path == QStringLiteral("auth")
                || path == QStringLiteral("login"));
    }

    QString soa_url_from_args(const QStringList& args)
    {
        for (const QString& arg : args)
        {
            if (is_auth_url(arg))
                return arg;
        }
        return {};
    }

    QString instance_suffix()
    {
        const QByteArray identity = (QDir::homePath() + QLatin1Char('|')
            + QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).toUtf8();
        return QString::fromLatin1(
            QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(16));
    }

    QString instance_key()
    {
        return QStringLiteral("soa_launcher_") + instance_suffix();
    }

    QString lock_path()
    {
        QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
        if (directory.isEmpty())
            directory = QDir::tempPath();
        directory = QDir(directory).filePath(QStringLiteral("story-of-alicia-launcher"));
        QDir().mkpath(directory);
        return QDir(directory).filePath(QStringLiteral("instance-") + instance_suffix() + QStringLiteral(".lock"));
    }

    QByteArray make_frame(const QString& url)
    {
        QJsonObject command {
            {QStringLiteral("version"), 1},
            {QStringLiteral("command"), QStringLiteral("activate")},
            {QStringLiteral("url"), is_auth_url(url) ? url : QString()}
        };
        const QByteArray payload = QJsonDocument(command).toJson(QJsonDocument::Compact);
        QByteArray frame;
        QDataStream stream(&frame, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::BigEndian);
        stream << static_cast<quint32>(payload.size());
        frame.append(payload);
        return frame;
    }

    bool forward_to_existing(const QString& key, const QString& url, const int timeout_ms)
    {
        QLocalSocket socket;
        socket.connectToServer(key, QIODevice::WriteOnly);
        if (!socket.waitForConnected(timeout_ms))
            return false;

        const QByteArray frame = make_frame(url);
        if (socket.write(frame) != frame.size())
            return false;
        socket.flush();
        const bool written = socket.waitForBytesWritten(timeout_ms);
        socket.disconnectFromServer();
        return written;
    }

    class LauncherApplication final : public QApplication
    {
        Q_OBJECT

    public:
        LauncherApplication(int& argc, char** argv) : QApplication(argc, argv) {}

        QString take_pending_url()
        {
            return std::exchange(pending_url, {});
        }

    signals:
        void soa_url_opened(const QString& url);

    protected:
        bool notify(QObject* receiver, QEvent* event) override
        {
            if (event && (event->type() == QEvent::Polish || event->type() == QEvent::Show))
            {
                if (auto* widget = qobject_cast<QWidget*>(receiver))
                {
#if defined(Q_OS_MACOS)
                    widget->setAttribute(Qt::WA_MacShowFocusRect, false);
#endif
                    const Qt::FocusPolicy policy = widget->focusPolicy();
                    if (policy == Qt::TabFocus || policy == Qt::StrongFocus
                        || policy == Qt::WheelFocus)
                    {
                        widget->setFocusPolicy(Qt::ClickFocus);
                    }
                }
            }
            return QApplication::notify(receiver, event);
        }

        bool event(QEvent* event) override
        {
            if (event->type() != QEvent::FileOpen)
                return QApplication::event(event);

            const auto* open_event = static_cast<QFileOpenEvent*>(event);
            const QString url = open_event->url().toString();
            if (!is_auth_url(url))
                return QApplication::event(event);

            pending_url = url;
            emit soa_url_opened(url);
            return true;
        }

    private:
        QString pending_url;
    };
}

int main(int argc, char* argv[])
{
    LauncherApplication app(argc, argv);
    const bool restored_appimage_environment = restore_appimage_host_environment();

    app.setApplicationName(QStringLiteral("Story Of Alicia Launcher"));
    app.setApplicationVersion(QString::fromLatin1(SOA_LAUNCHER_VERSION));
    app.setOrganizationName(QStringLiteral("Story Of Alicia"));
    app.setOrganizationDomain(QStringLiteral("storyofalicia.com"));
    app.setStyleSheet(QStringLiteral(
        "QWidget { outline: none; }"));

    QString url = soa_url_from_args(app.arguments());
    if (url.isEmpty())
        url = app.take_pending_url();

    const QString server_key = instance_key();
    if (forward_to_existing(server_key, url, 350))
        return 0;

    QLockFile instance_lock(lock_path());
    instance_lock.setStaleLockTime(0);

    if (!instance_lock.tryLock(0))
    {
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            QThread::msleep(100);
            if (forward_to_existing(server_key, url, 250))
                return 0;
        }

        qint64 pid = -1;
        QString hostname;
        QString application;

        if (instance_lock.getLockInfo(&pid, &hostname, &application))
        {
            SPDLOG_ERROR(
                "single-instance lock is held but IPC failed: pid={} application=\"{}\" host=\"{}\" lock=\"{}\"",
                pid,
                application.toStdString(),
                hostname.toStdString(),
                instance_lock.fileName().toStdString());
        }
        else
        {
            SPDLOG_ERROR(
                "single-instance lock failed and owner information is unavailable: error={} lock=\"{}\"",
                static_cast<int>(instance_lock.error()),
                instance_lock.fileName().toStdString());
        }

        return 1;
    }

    core::log::init();
    LauncherLog* launcher_log = LauncherLog::instance();
    if (restored_appimage_environment)
        SPDLOG_DEBUG("restored host environment after AppImage Qt bootstrap");
    SPDLOG_INFO("Running Story Of Alicia for Linux and macOS");
    SPDLOG_INFO("Version: {}", SOA_LAUNCHER_VERSION);
    util::assets::load_all();
    SPDLOG_DEBUG("loaded all assets successfully");

    QLocalServer::removeServer(server_key);
    QLocalServer server;
    if (!server.listen(server_key))
    {
        SPDLOG_ERROR("single-instance server failed to listen: {}", server.errorString().toStdString());
        return 1;
    }

    auto& language_manager = util::i18n::LanguageManager::instance();
    QObject::connect(&language_manager, &util::i18n::LanguageManager::language_changed,
                     &app, [](const QString& code)
    {
        util::assets::set_translated_button_assets(code != QStringLiteral("en"));
    });

    MainWindow* window = new MainWindow;
    app.setQuitOnLastWindowClosed(true);
    language_manager.register_tree(window);
    language_manager.register_tree(launcher_log);
    language_manager.apply_configured_language();
    window->show();

    const auto handle_command = [&window](const QJsonObject& command)
    {
        if (!window
            || command.value(QStringLiteral("version")).toInt() != 1
            || command.value(QStringLiteral("command")).toString() != QStringLiteral("activate"))
        {
            return;
        }

        const QString incoming = command.value(QStringLiteral("url")).toString();
        if (is_auth_url(incoming))
        {
            QPointer<AuthHandler> handler(window->auth_handler());
            QTimer::singleShot(0, handler, [handler, incoming]()
            {
                if (handler)
                    handler->handle_url(incoming);
            });
        }

        window->setWindowState((window->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        window->show();
        window->raise();
        window->activateWindow();
    };

    QObject::connect(&app, &LauncherApplication::soa_url_opened, &app,
                     [&handle_command](const QString& incoming)
    {
        handle_command(QJsonObject{
            {QStringLiteral("version"), 1},
            {QStringLiteral("command"), QStringLiteral("activate")},
            {QStringLiteral("url"), incoming}
        });
    });

    QObject::connect(&server, &QLocalServer::newConnection, &app,
                     [&server, &handle_command]()
    {
        while (QLocalSocket* client = server.nextPendingConnection())
        {
            client->setProperty("soa_buffer", QByteArray{});
            const auto consume = [client, &handle_command]()
            {
                QByteArray buffer = client->property("soa_buffer").toByteArray();
                buffer.append(client->readAll());

                while (buffer.size() >= static_cast<int>(sizeof(quint32)))
                {
                    QByteArray header = buffer.left(static_cast<int>(sizeof(quint32)));
                    QDataStream stream(&header, QIODevice::ReadOnly);
                    stream.setByteOrder(QDataStream::BigEndian);
                    quint32 length = 0;
                    stream >> length;
                    if (length == 0 || length > k_max_ipc_payload)
                    {
                        client->disconnectFromServer();
                        buffer.clear();
                        break;
                    }
                    const int frame_size = static_cast<int>(sizeof(quint32) + length);
                    if (buffer.size() < frame_size)
                        break;

                    const QByteArray payload = buffer.mid(static_cast<int>(sizeof(quint32)),
                                                          static_cast<int>(length));
                    buffer.remove(0, frame_size);
                    QJsonParseError error;
                    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
                    if (error.error == QJsonParseError::NoError && document.isObject())
                        handle_command(document.object());
                }
                client->setProperty("soa_buffer", buffer);
            };

            QObject::connect(client, &QLocalSocket::readyRead, client, consume);
            QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            QTimer::singleShot(5000, client, [client]()
            {
                if (client->state() != QLocalSocket::UnconnectedState)
                    client->disconnectFromServer();
            });
            if (client->bytesAvailable() > 0)
                consume();
        }
    });

    if (!url.isEmpty())
    {
        QTimer::singleShot(0, &app, [&handle_command, url]()
        {
            handle_command(QJsonObject{
                {QStringLiteral("version"), 1},
                {QStringLiteral("command"), QStringLiteral("activate")},
                {QStringLiteral("url"), url}
            });
        });
    }

    return QApplication::exec();
}

#include "main.moc"
