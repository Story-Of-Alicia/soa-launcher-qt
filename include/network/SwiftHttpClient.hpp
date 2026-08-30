#pragma once

#include "network/SwiftNetwork.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

namespace core::network
{
    struct HttpResponse
    {
        soa_http_result result {soa_http_result_failed};
        int status {};
        QByteArray data;
        QUrl final_url;
        QString error;
        qulonglong elapsed_ms {};
    };

    struct DnsResponse
    {
        soa_http_result result {soa_http_result_failed};
        QString address;
        QString error;
        qulonglong elapsed_ms {};
    };

    class SwiftHttpClient final : public QObject
    {
    public:
        using HttpCallback = std::function<void(const HttpResponse&)>;
        using DnsCallback = std::function<void(const DnsResponse&)>;

        explicit SwiftHttpClient(QObject* parent = nullptr);
        ~SwiftHttpClient() override;

        qulonglong get(const QUrl& url,
                       quint32 timeout_ms,
                       qulonglong maximum_bytes,
                       const QByteArray& accept,
                       const QByteArray& user_agent,
                       bool allow_insecure_http,
                       HttpCallback callback);
        qulonglong head(const QUrl& url,
                        quint32 timeout_ms,
                        const QByteArray& user_agent,
                        bool allow_insecure_http,
                        HttpCallback callback);
        qulonglong resolve(const QString& hostname,
                           quint32 timeout_ms,
                           DnsCallback callback);
        void cancel(qulonglong request_id);
        void cancel_all();

    private:
        static void http_done_callback(uint64_t request_id,
                                       soa_http_result result,
                                       int http_status,
                                       const uint8_t* data,
                                       uint64_t data_size,
                                       const char* final_url,
                                       const char* error,
                                       uint64_t elapsed_ms,
                                       void* ctx);
        static void dns_done_callback(uint64_t request_id,
                                      soa_http_result result,
                                      const char* address,
                                      const char* error,
                                      uint64_t elapsed_ms,
                                      void* ctx);
        void dispatch_http(qulonglong request_id, HttpResponse response);
        void dispatch_dns(qulonglong request_id, DnsResponse response);

        soa_http_client* client {};
        QHash<qulonglong, HttpCallback> http_callbacks;
        QHash<qulonglong, DnsCallback> dns_callbacks;
    };
}
