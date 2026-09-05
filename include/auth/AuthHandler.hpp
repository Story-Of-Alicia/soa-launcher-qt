#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include "common/StatusReporter.hpp"

class QTimer;
class QUrl;

class AuthHandler : public core::status::StatusReporter
{
    Q_OBJECT

    public:
        explicit AuthHandler(QObject* parent = nullptr);

        void open_login();
        void cancel_login();

    public slots:
        void handle_url(const QString& url);

    signals:
        void authenticated(const QString& user, const QString& token, const QString& username);
        void login_cancelled();
        void browser_open_failed();

    private:
        bool callback_is_expected(const QUrl& url) const;
        void schedule_login_completion(const QString& user, const QString& token,
                                       const QString& username);
        void reset_pending_login();

        QTimer* timeout_timer {};
        bool pending {};
        bool completion_scheduled {};
        QDateTime pending_since;
        QByteArray last_callback_digest;
        QDateTime last_callback_seen;
};
