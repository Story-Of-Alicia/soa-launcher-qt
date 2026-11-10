// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#include "credentials/CredentialStore.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#include <cstring>
#include <utility>

namespace soa::credentials
{
    namespace
    {
        constexpr const char* k_service = "com.storyofalicia.launcher";
        constexpr const char* k_account = "default";

        QByteArray serialize(const Credentials& credentials)
        {
            return QJsonDocument(QJsonObject{
                {QStringLiteral("user"), credentials.user},
                {QStringLiteral("token"), credentials.token},
                {QStringLiteral("display_name"), credentials.display_name}
            }).toJson(QJsonDocument::Compact);
        }

        bool deserialize(const QByteArray& data, Credentials& credentials)
        {
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(data, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject())
                return false;

            const QJsonObject object = document.object();
            Credentials parsed;
            parsed.user = object.value(QStringLiteral("user")).toString();
            parsed.token = object.value(QStringLiteral("token")).toString();
            parsed.display_name = object.value(QStringLiteral("display_name")).toString();
            if (!parsed.valid())
                return false;
            credentials = std::move(parsed);
            return true;
        }

#if defined(Q_OS_LINUX)
        QString secret_tool()
        {
            if (qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS"))
                return {};
            return QStandardPaths::findExecutable(QStringLiteral("secret-tool"));
        }

        bool run_secret_tool(const QStringList& arguments, const QByteArray& input, QByteArray* output)
        {
            const QString executable = secret_tool();
            if (executable.isEmpty())
                return false;

            QProcess process;
            process.setProcessChannelMode(QProcess::SeparateChannels);
            process.start(executable, arguments);
            if (!process.waitForStarted(1000))
                return false;
            if (!input.isEmpty())
                process.write(input);
            process.closeWriteChannel();
            if (!process.waitForFinished(2500))
            {
                process.kill();
                process.waitForFinished(1000);
                return false;
            }
            if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
                return false;
            if (output)
                *output = process.readAllStandardOutput().trimmed();
            return true;
        }
#endif
    }

    bool CredentialStore::available()
    {
#if defined(Q_OS_LINUX)
        return !secret_tool().isEmpty();
#else
        return false;
#endif
    }

    bool CredentialStore::load(Credentials& credentials)
    {
#if defined(Q_OS_LINUX)
        QByteArray output;
        if (!run_secret_tool(
                {QStringLiteral("lookup"), QStringLiteral("service"), QString::fromLatin1(k_service),
                 QStringLiteral("account"), QString::fromLatin1(k_account)}, {}, &output))
            return false;
        return deserialize(output, credentials);
#else
        Q_UNUSED(credentials);
        return false;
#endif
    }

    bool CredentialStore::save(const Credentials& credentials)
    {
        if (!credentials.valid())
            return clear();
        const QByteArray payload = serialize(credentials);

#if defined(Q_OS_LINUX)
        QByteArray input = payload;
        input.append('\n');
        return run_secret_tool(
            {QStringLiteral("store"), QStringLiteral("--label=Story of Alicia Launcher"),
             QStringLiteral("service"), QString::fromLatin1(k_service),
             QStringLiteral("account"), QString::fromLatin1(k_account)}, input, nullptr);
#else
        return false;
#endif
    }

    bool CredentialStore::clear()
    {
#if defined(Q_OS_LINUX)
        if (secret_tool().isEmpty())
            return false;


        return run_secret_tool(
            {QStringLiteral("clear"), QStringLiteral("service"), QString::fromLatin1(k_service),
             QStringLiteral("account"), QString::fromLatin1(k_account)}, {}, nullptr);
#else
        return false;
#endif
    }
}
