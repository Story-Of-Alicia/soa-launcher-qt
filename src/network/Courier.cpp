#include "network/Courier.h"

#include <spdlog/spdlog.h>

#include "network/CourierBridge.hpp"
#include "network/DownloadStatus.hpp"

#include <QMetaObject>
#include <QString>

using core::status::State;
using core::network::DownloadStatus;

namespace
{
    void on_log_cb(const int level, const char* message, void*)
    {
        switch (level)
        {
            case 0:  SPDLOG_TRACE("[courier] {}", message ? message : ""); break;
            case 1:  SPDLOG_DEBUG("[courier] {}", message ? message : ""); break;
            case 2:  SPDLOG_INFO ("[courier] {}", message ? message : ""); break;
            case 3:  SPDLOG_WARN ("[courier] {}", message ? message : ""); break;
            case 4:  SPDLOG_ERROR("[courier] {}", message ? message : ""); break;
            default: SPDLOG_INFO ("[courier] {}", message ? message : ""); break;
        }
    }

    const char* phase_name(const courier_phase phase)
    {
        switch (phase)
        {
            case courier_phase_preparing:   return "preparing";
            case courier_phase_checking:    return "checking";
            case courier_phase_downloading: return "downloading";
            case courier_phase_verifying:   return "verifying";
        }
        return "preparing";
    }

    void on_progress_cb(const uint64_t operation_id,
                        const courier_phase phase,
                        const char* message,
                        const int percent,
                        const uint64_t received,
                        const uint64_t total,
                        const uint64_t throughput,
                        const int file_index,
                        const int file_count,
                        void*)
    {
        const QString msg = QString::fromUtf8(message ? message : "");
        QMetaObject::invokeMethod(
            &core::network::CourierBridge::instance(),
            [operation_id, phase, msg, percent, received, total, throughput, file_index, file_count]()
            {
                DownloadStatus ds;
                ds.operation_id = static_cast<qulonglong>(operation_id);
                ds.base.state    = State::Working;
                ds.base.phase    = phase_name(phase);
                ds.base.message  = msg;
                ds.base.progress = qBound(0, percent, 100) / 100.0;
                ds.phase      = phase;
                ds.received   = static_cast<qulonglong>(received);
                ds.total      = static_cast<qulonglong>(total);
                ds.speed      = static_cast<qulonglong>(throughput);
                ds.file_index = file_index;
                ds.file_count = file_count;
                core::network::CourierBridge::instance().report(ds);
            },
            Qt::QueuedConnection);
    }

    void on_done_cb(const uint64_t operation_id, const courier_result result, const char* message, void*)
    {
        const QString msg = QString::fromUtf8(message ? message : "");
        QMetaObject::invokeMethod(
            &core::network::CourierBridge::instance(),
            [operation_id, result, msg]()
            {
                DownloadStatus ds;
                ds.operation_id = static_cast<qulonglong>(operation_id);
                ds.result = result;
                ds.base.state = result == courier_result_failed || result == courier_result_cancelled
                    ? State::Failed : State::Done;
                ds.base.message  = msg;
                ds.base.progress = ds.base.state == State::Done ? 1.0 : -1.0;
                core::network::CourierBridge::instance().report(ds);
            },
            Qt::QueuedConnection);
    }
}

namespace core::network
{
    CourierBridge::CourierBridge() : StatusReporter("courier")
    {
        courier_set_log_callback(&on_log_cb, nullptr);
    }

    CourierBridge& CourierBridge::instance()
    {
        static CourierBridge bridge;
        return bridge;
    }

    void CourierBridge::begin_operation(const qulonglong operation_id)
    {
        if (operation_id != 0) active_operations.insert(operation_id);
    }

    void CourierBridge::clear_operation(const qulonglong operation_id)
    {
        if (operation_id == 0) active_operations.clear();
        else active_operations.remove(operation_id);
    }

    void CourierBridge::report(const DownloadStatus& ds)
    {
        if (ds.operation_id == 0 || !active_operations.contains(ds.operation_id))
        {
            SPDLOG_DEBUG("courier: ignoring stale callback for operation {}", ds.operation_id);
            return;
        }

        set_status(ds.base);
        emit download_status(ds);
        if (ds.base.state == State::Done || ds.base.state == State::Failed)
            active_operations.remove(ds.operation_id);
    }

    courier_progress_cb CourierBridge::progress_callback()
    {
        return &on_progress_cb;
    }

    courier_done_cb CourierBridge::done_callback()
    {
        return &on_done_cb;
    }
}
