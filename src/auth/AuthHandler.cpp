#include "auth/AuthHandler.hpp"

#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include "common/Log.hpp"
#include "config/Config.hpp"
#include "platform/UrlSchemeHandler.hpp"
#include <spdlog/spdlog.h>

using util::config::Config;

namespace
{
    const char* k_discord_oauth_url =
        "https://discord.com/oauth2/authorize"
        "?client_id=1272602862043795586"
        "&response_type=code"
        "&redirect_uri=https%3A%2F%2Fauthentication.storyofalicia.com%2F"
        "&scope=identify";

    constexpr int k_login_timeout_ms = 10 * 60 * 1000;
    constexpr int k_duplicate_callback_window_ms = 30000;


}

AuthHandler::AuthHandler(QObject* parent)
    : core::status::StatusReporter(QStringLiteral("auth"), parent)
{
    QDesktopServices::setUrlHandler(QStringLiteral("soa"), this, "handle_url");
    timeout_timer = new QTimer(this);
    timeout_timer->setSingleShot(true);
    connect(timeout_timer, &QTimer::timeout, this, [this]()
    {
        if (!pending || completion_scheduled)
            return;
        reset_pending_login();
        fail(QStringLiteral("Discord login timed out. Try again."));
    });

    core::platform::register_launcher_url_scheme();
}

void AuthHandler::open_login()
{
    if (pending || completion_scheduled)
        return;

    const QUrl login_url(QString::fromLatin1(k_discord_oauth_url));

    pending = true;
    pending_since = QDateTime::currentDateTimeUtc();
    last_callback_digest.clear();
    last_callback_seen = {};
    timeout_timer->start(k_login_timeout_ms);
    working(QStringLiteral("discord-login"), -1.0, true);

    const QString encoded_url = login_url.toString(QUrl::FullyEncoded);
    if (QGuiApplication::clipboard())
        QGuiApplication::clipboard()->setText(encoded_url);

    SPDLOG_INFO("opening Discord login in the default browser");
    if (QDesktopServices::openUrl(login_url))
        return;

    if (QGuiApplication::clipboard())
    {
        emit browser_open_failed();
        return;
    }

    reset_pending_login();
    fail(QStringLiteral("The browser could not be opened and the login link could not be copied."));
}

void AuthHandler::cancel_login()
{
    if (!pending && !completion_scheduled
        && status().state != core::status::State::Working)
    {
        return;
    }

    reset_pending_login();
    completion_scheduled = false;
    idle();
    emit login_cancelled();
}

bool AuthHandler::callback_is_expected(const QUrl& url) const
{
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

void AuthHandler::handle_url(const QString& url)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QByteArray digest = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256);
    const bool duplicate = !last_callback_digest.isEmpty()
        && digest == last_callback_digest
        && last_callback_seen.isValid()
        && last_callback_seen.msecsTo(now) <= k_duplicate_callback_window_ms;
    last_callback_digest = digest;
    last_callback_seen = now;

    if (duplicate)
        return;

    SPDLOG_INFO("received soa login callback");
    const QUrl parsed(url, QUrl::StrictMode);

    if (!callback_is_expected(parsed))
    {
        SPDLOG_WARN("ignored unexpected soa callback target");
        return;
    }
    if (!pending || !pending_since.isValid())
    {
        SPDLOG_WARN("ignored unsolicited soa login callback");
        return;
    }
    if (pending_since.msecsTo(now) > k_login_timeout_ms)
    {
        SPDLOG_WARN("ignored expired soa login callback");
        reset_pending_login();
        fail(QStringLiteral("This login response has expired. Start sign-in again."));
        return;
    }

    const QUrlQuery query(parsed);
    const auto query_value = [&query](const QString& key, const bool trim)
    {
        QString value = query.queryItemValue(key, QUrl::FullyDecoded);
        if (trim)
            value = value.trimmed();
        return value;
    };
    const auto first_value = [&query_value](const QStringList& keys, const bool trim = true)
    {
        for (const QString& key : keys)
        {
            const QString value = query_value(key, trim);
            if (!value.isEmpty())
                return value;
        }
        return QString{};
    };

    const QString legacy_user = query_value(QStringLiteral("user"), true);
    QString user = first_value({
        QStringLiteral("ID"),
        QStringLiteral("id"),
        QStringLiteral("discord_id")});
    if (user.isEmpty())
        user = legacy_user;

    QString token = first_value({
        QStringLiteral("OP"),
        QStringLiteral("op")}, false);
    if (token.isEmpty())
        token = query_value(QStringLiteral("token"), false);

    QString username = first_value({
        QStringLiteral("username"),
        QStringLiteral("display_name"),
        QStringLiteral("name")});
    if (username.isEmpty() && !legacy_user.isEmpty() && legacy_user != user)
        username = legacy_user;

    const auto containsUnsafeCredentialCharacter = [](const QString& value)
    {
        for (const QChar character : value)
        {
            if (character.isNull() || character.category() == QChar::Other_Control
                || character == QLatin1Char('\r') || character == QLatin1Char('\n'))
                return true;
        }
        return false;
    };

    if (user.isEmpty() || token.isEmpty() || user.size() > 256 || token.size() > 8192
        || username.size() > 256 || containsUnsafeCredentialCharacter(user)
        || containsUnsafeCredentialCharacter(token) || containsUnsafeCredentialCharacter(username))
    {
        SPDLOG_ERROR("soa login callback contained missing or oversized credentials");
        reset_pending_login();
        fail(QStringLiteral("The login response was malformed."));
        return;
    }

    if (completion_scheduled)
        return;

    schedule_login_completion(user, token, username);
}

void AuthHandler::schedule_login_completion(const QString& user, const QString& token,
                                            const QString& username)
{
    if (!pending || completion_scheduled)
        return;

    completion_scheduled = true;
    pending = false;
    pending_since = {};
    timeout_timer->stop();

    QTimer::singleShot(0, this, [this, user, token, username]()
    {
        if (!completion_scheduled)
            return;

        QPointer<AuthHandler> self(this);
        Config::instance().set_auth(user, token, username);
        if (!self)
            return;

        SPDLOG_INFO("auth ok for Discord user {}", user.toStdString());
        self->completion_scheduled = false;
        self->done(QStringLiteral("Logged in successfully."));
        if (self)
            emit self->authenticated(user, token, username);
    });
}

void AuthHandler::reset_pending_login()
{
    pending = false;
    pending_since = {};
    timeout_timer->stop();
}
