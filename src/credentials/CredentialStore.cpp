#include "credentials/CredentialStore.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#include <cstring>
#include <utility>

#ifdef Q_OS_MACOS
#include <Security/Security.h>
#endif

namespace util::credentials
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

#ifdef Q_OS_LINUX
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
#ifdef Q_OS_MACOS
        return true;
#elif defined(Q_OS_LINUX)
        return !secret_tool().isEmpty();
#else
        return false;
#endif
    }

    bool CredentialStore::load(Credentials& credentials)
    {
#ifdef Q_OS_MACOS
        UInt32 length = 0;
        void* data = nullptr;
        SecKeychainItemRef item = nullptr;
        const OSStatus status = SecKeychainFindGenericPassword(
            nullptr,
            static_cast<UInt32>(std::strlen(k_service)), k_service,
            static_cast<UInt32>(std::strlen(k_account)), k_account,
            &length, &data, &item);
        if (item)
            CFRelease(item);
        if (status != errSecSuccess || !data)
            return false;
        const QByteArray payload(static_cast<const char*>(data), static_cast<qsizetype>(length));
        SecKeychainItemFreeContent(nullptr, data);
        return deserialize(payload, credentials);
#elif defined(Q_OS_LINUX)
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

#ifdef Q_OS_MACOS
        UInt32 old_length = 0;
        void* old_data = nullptr;
        SecKeychainItemRef item = nullptr;
        const OSStatus found = SecKeychainFindGenericPassword(
            nullptr,
            static_cast<UInt32>(std::strlen(k_service)), k_service,
            static_cast<UInt32>(std::strlen(k_account)), k_account,
            &old_length, &old_data, &item);
        if (old_data)
            SecKeychainItemFreeContent(nullptr, old_data);

        OSStatus status;
        if (found == errSecSuccess && item)
        {
            status = SecKeychainItemModifyAttributesAndData(
                item, nullptr, static_cast<UInt32>(payload.size()), payload.constData());
            CFRelease(item);
        }
        else
        {
            status = SecKeychainAddGenericPassword(
                nullptr,
                static_cast<UInt32>(std::strlen(k_service)), k_service,
                static_cast<UInt32>(std::strlen(k_account)), k_account,
                static_cast<UInt32>(payload.size()), payload.constData(), nullptr);
        }
        return status == errSecSuccess;
#elif defined(Q_OS_LINUX)
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
#ifdef Q_OS_MACOS
        UInt32 length = 0;
        void* data = nullptr;
        SecKeychainItemRef item = nullptr;
        const OSStatus found = SecKeychainFindGenericPassword(
            nullptr,
            static_cast<UInt32>(std::strlen(k_service)), k_service,
            static_cast<UInt32>(std::strlen(k_account)), k_account,
            &length, &data, &item);
        if (data)
            SecKeychainItemFreeContent(nullptr, data);
        if (found == errSecItemNotFound)
            return true;
        if (found != errSecSuccess || !item)
            return false;
        const OSStatus status = SecKeychainItemDelete(item);
        CFRelease(item);
        return status == errSecSuccess;
#elif defined(Q_OS_LINUX)
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
