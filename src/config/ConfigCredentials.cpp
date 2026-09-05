#include "ConfigPrivate.hpp"

namespace util::config
{
    bool Config::load_env_fallback()
    {
        QFile file(env_path());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;

        const QByteArray contents = file.readAll();
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(contents, &error);
        if (error.error == QJsonParseError::NoError && document.isObject())
        {
            const QJsonObject object = document.object();
            d->username = object.value(QStringLiteral("user")).toString();
            d->token = object.value(QStringLiteral("token")).toString();
            d->display_name = object.value(QStringLiteral("display_name")).toString();
            return has_auth();
        }


        const QString text = QString::fromUtf8(contents);
        for (const QString& rawLine : text.split(QLatin1Char('\n')))
        {
            const QString line = rawLine.trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals <= 0)
                continue;

            const QString key = line.left(equals).trimmed();
            QString value = line.mid(equals + 1).trimmed();
            if (value.size() >= 2 && value.startsWith(QLatin1Char('"'))
                && value.endsWith(QLatin1Char('"')))
            {
                value = value.mid(1, value.size() - 2);
            }

            if (key == QStringLiteral("SOA_USER")) d->username = value;
            else if (key == QStringLiteral("SOA_TOKEN")) d->token = value;
            else if (key == QStringLiteral("SOA_USERNAME")) d->display_name = value;
        }
        return has_auth();
    }

    void Config::load_credentials()
    {
        d->username.clear();
        d->token.clear();
        d->display_name.clear();

        util::credentials::Credentials credentials;
        if (util::credentials::CredentialStore::load(credentials))
        {
            d->username = credentials.user;
            d->token = credentials.token;
            d->display_name = credentials.display_name;
            SPDLOG_DEBUG("config: loaded credentials from platform credential store");
            return;
        }

        if (load_env_fallback())
            SPDLOG_WARN("config: using protected .env credential fallback");
        else
            SPDLOG_DEBUG("config: no saved credentials");
    }

    bool Config::save_env_fallback()
    {
        if (!has_auth())
            return !QFileInfo::exists(env_path()) || QFile::remove(env_path());

        const QJsonObject object {
            {QStringLiteral("user"), d->username},
            {QStringLiteral("token"), d->token},
            {QStringLiteral("display_name"), d->display_name}
        };
        const QByteArray contents = QJsonDocument(object).toJson(QJsonDocument::Compact);

        QSaveFile file(env_path());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        if (file.write(contents) != contents.size() || !file.commit())
            return false;
        if (!QFile::setPermissions(env_path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        {
            SPDLOG_ERROR("config: could not restrict .env permissions");
            QFile::remove(env_path());
            remember_disk_state();
            return false;
        }
        remember_disk_state();
        return true;
    }

    bool Config::clear_saved_credentials()
    {
        const bool store_cleared = !util::credentials::CredentialStore::available()
            || util::credentials::CredentialStore::clear();
        const bool fallback_cleared = !QFileInfo::exists(env_path()) || QFile::remove(env_path());
        watch_files();
        remember_disk_state();
        return store_cleared && fallback_cleared;
    }

    bool Config::save_credentials()
    {
        if (!has_auth())
        {
            return clear_saved_credentials();
        }

        const util::credentials::Credentials credentials {
            d->username, d->token, d->display_name
        };
        if (util::credentials::CredentialStore::save(credentials))
        {
            QFile::remove(env_path());
            watch_files();
            remember_disk_state();
            return true;
        }

        SPDLOG_WARN("config: platform credential store unavailable; using protected .env fallback");
        const bool ok = save_env_fallback();
        watch_files();
        return ok;
    }

    QString Config::username() const { return d->username; }

    QString Config::token() const { return d->token; }

    QString Config::display_name() const { return d->display_name; }

    bool Config::has_auth() const { return !d->username.isEmpty() && !d->token.isEmpty(); }

    bool Config::keep_signed_in() const { return d->values.value(QStringLiteral("keep_signed_in")).toBool(); }

    void Config::set_keep_signed_in(const bool value)
    {
        if (keep_signed_in() == value)
            return;

        d->values[QStringLiteral("keep_signed_in")] = value;
        if (update_depth > 0) update_dirty = true;
        else save();

        if (value)
        {
            if (has_auth() && !save_credentials())
                SPDLOG_ERROR("config: failed to persist credentials after enabling keep-signed-in");
        }
        else if (!clear_saved_credentials())
        {
            SPDLOG_WARN("config: could not fully remove saved credentials after disabling keep-signed-in");
        }

        if (update_depth == 0) emit changed();
    }

    void Config::set_auth(const QString& username, const QString& token,
                          const QString& displayName)
    {
        d->username = username;
        d->token = token;
        d->display_name = displayName;
        if (keep_signed_in())
        {
            if (!save_credentials())
                SPDLOG_ERROR("config: failed to persist credentials securely");
        }
        else if (!clear_saved_credentials())
        {
            SPDLOG_WARN("config: could not clear old saved credentials for this session-only login");
        }
        emit changed();
    }

    void Config::clear_auth()
    {
        d->username.clear();
        d->token.clear();
        d->display_name.clear();
        if (!clear_saved_credentials())
            SPDLOG_WARN("config: could not fully clear the platform credential store");
        emit changed();
    }

}
