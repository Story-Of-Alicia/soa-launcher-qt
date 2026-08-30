#include "network/SwiftHttpClient.hpp"

#include <QMetaObject>

#include <utility>

namespace core::network
{
    SwiftHttpClient::SwiftHttpClient(QObject* parent)
        : QObject(parent)
    {
        client = soa_http_client_create(
            &SwiftHttpClient::http_done_callback,
            &SwiftHttpClient::dns_done_callback,
            this);
    }

    SwiftHttpClient::~SwiftHttpClient()
    {
        http_callbacks.clear();
        dns_callbacks.clear();
        if (client)
        {
            soa_http_client_shutdown(client);
            soa_http_client_destroy(client);
            client = nullptr;
        }
    }

    qulonglong SwiftHttpClient::get(const QUrl& url,
                                    const quint32 timeout_ms,
                                    const qulonglong maximum_bytes,
                                    const QByteArray& accept,
                                    const QByteArray& user_agent,
                                    const bool allow_insecure_http,
                                    HttpCallback callback)
    {
        if (!client || !callback)
            return 0;
        const QByteArray encoded_url = url.toEncoded();
        const qulonglong request_id = soa_http_client_request(
            client,
            encoded_url.constData(),
            soa_http_method_get,
            timeout_ms,
            maximum_bytes,
            accept.constData(),
            user_agent.constData(),
            allow_insecure_http);
        if (request_id != 0)
            http_callbacks.insert(request_id, std::move(callback));
        return request_id;
    }

    qulonglong SwiftHttpClient::head(const QUrl& url,
                                     const quint32 timeout_ms,
                                     const QByteArray& user_agent,
                                     const bool allow_insecure_http,
                                     HttpCallback callback)
    {
        if (!client || !callback)
            return 0;
        const QByteArray encoded_url = url.toEncoded();
        const qulonglong request_id = soa_http_client_request(
            client,
            encoded_url.constData(),
            soa_http_method_head,
            timeout_ms,
            0,
            "*/*",
            user_agent.constData(),
            allow_insecure_http);
        if (request_id != 0)
            http_callbacks.insert(request_id, std::move(callback));
        return request_id;
    }

    qulonglong SwiftHttpClient::resolve(const QString& hostname,
                                        const quint32 timeout_ms,
                                        DnsCallback callback)
    {
        if (!client || !callback)
            return 0;
        const QByteArray encoded = hostname.toUtf8();
        const qulonglong request_id = soa_http_client_resolve(
            client,
            encoded.constData(),
            timeout_ms);
        if (request_id != 0)
            dns_callbacks.insert(request_id, std::move(callback));
        return request_id;
    }

    void SwiftHttpClient::cancel(const qulonglong request_id)
    {
        http_callbacks.remove(request_id);
        dns_callbacks.remove(request_id);
        if (client)
            soa_http_client_cancel(client, request_id);
    }

    void SwiftHttpClient::cancel_all()
    {
        http_callbacks.clear();
        dns_callbacks.clear();
        if (client)
            soa_http_client_cancel_all(client);
    }

    void SwiftHttpClient::http_done_callback(const uint64_t request_id,
                                             const soa_http_result result,
                                             const int http_status,
                                             const uint8_t* data,
                                             const uint64_t data_size,
                                             const char* final_url,
                                             const char* error,
                                             const uint64_t elapsed_ms,
                                             void* ctx)
    {
        auto* self = static_cast<SwiftHttpClient*>(ctx);
        if (!self)
            return;
        HttpResponse response;
        response.result = result;
        response.status = http_status;
        if (data && data_size > 0)
            response.data = QByteArray(reinterpret_cast<const char*>(data), static_cast<qsizetype>(data_size));
        response.final_url = QUrl(QString::fromUtf8(final_url ? final_url : ""));
        response.error = QString::fromUtf8(error ? error : "");
        response.elapsed_ms = elapsed_ms;
        QMetaObject::invokeMethod(self, [self, request_id, response = std::move(response)]() mutable
        {
            self->dispatch_http(request_id, std::move(response));
        }, Qt::QueuedConnection);
    }

    void SwiftHttpClient::dns_done_callback(const uint64_t request_id,
                                            const soa_http_result result,
                                            const char* address,
                                            const char* error,
                                            const uint64_t elapsed_ms,
                                            void* ctx)
    {
        auto* self = static_cast<SwiftHttpClient*>(ctx);
        if (!self)
            return;
        DnsResponse response;
        response.result = result;
        response.address = QString::fromUtf8(address ? address : "");
        response.error = QString::fromUtf8(error ? error : "");
        response.elapsed_ms = elapsed_ms;
        QMetaObject::invokeMethod(self, [self, request_id, response = std::move(response)]() mutable
        {
            self->dispatch_dns(request_id, std::move(response));
        }, Qt::QueuedConnection);
    }

    void SwiftHttpClient::dispatch_http(const qulonglong request_id, HttpResponse response)
    {
        const auto it = http_callbacks.find(request_id);
        if (it == http_callbacks.end())
            return;
        HttpCallback callback = std::move(it.value());
        http_callbacks.erase(it);
        callback(response);
    }

    void SwiftHttpClient::dispatch_dns(const qulonglong request_id, DnsResponse response)
    {
        const auto it = dns_callbacks.find(request_id);
        if (it == dns_callbacks.end())
            return;
        DnsCallback callback = std::move(it.value());
        dns_callbacks.erase(it);
        callback(response);
    }
}
