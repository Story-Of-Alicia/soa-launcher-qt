#include "common/Log.hpp"
#include "common/QtLogSink.hpp"
#include "common/AppPaths.hpp"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <QStandardPaths>
#include <QSysInfo>
#include <QDir>
#include <QFileInfo>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace core::log
{
    namespace
    {
        constexpr std::size_t k_max_file_size = 5 * 1024 * 1024;
        constexpr std::size_t k_max_files = 3;

        class ResilientRotatingFileSink final : public spdlog::sinks::sink
        {
        public:
            explicit ResilientRotatingFileSink(std::string path)
                : path_(std::move(path))
            {
                reopen_locked();
            }

            void log(const spdlog::details::log_msg& msg) override
            {
                std::lock_guard lock(mutex_);
                ensure_locked();
                sink_->log(msg);
            }

            void flush() override
            {
                std::lock_guard lock(mutex_);
                ensure_locked();
                sink_->flush();
            }

            void set_pattern(const std::string& pattern) override
            {
                std::lock_guard lock(mutex_);
                pattern_ = pattern;
                formatter_.reset();
                sink_->set_pattern(pattern_);
            }

            void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override
            {
                std::lock_guard lock(mutex_);
                formatter_ = std::move(formatter);
                sink_->set_formatter(formatter_->clone());
            }

        private:
            void ensure_locked()
            {
                if (QFileInfo::exists(QString::fromStdString(path_)))
                    return;
                reopen_locked();
            }

            void reopen_locked()
            {
                sink_ = std::make_shared<spdlog::sinks::rotating_file_sink_st>(
                    path_, k_max_file_size, k_max_files);
                if (formatter_)
                    sink_->set_formatter(formatter_->clone());
                else if (!pattern_.empty())
                    sink_->set_pattern(pattern_);
            }

            std::string path_;
            std::string pattern_;
            std::unique_ptr<spdlog::formatter> formatter_;
            std::shared_ptr<spdlog::sinks::rotating_file_sink_st> sink_;
            std::mutex mutex_;
        };
    }

    void init()
    {
#if defined(Q_OS_MACOS)
        const QString dir = core::paths::default_log_root();
#else
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#endif
        if (!QDir().mkpath(dir))
        {
            std::fprintf(stderr, "log: could not create log directory %s\n", dir.toStdString().c_str());
        }

        const std::string log_path = (dir + QStringLiteral("/launcher.log")).toStdString();

        std::vector<spdlog::sink_ptr> sinks;

        const auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console->set_pattern("[%H:%M:%S] [%^%l%$] %v");
        sinks.push_back(console);

        const auto file = std::make_shared<ResilientRotatingFileSink>(log_path);
        file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
        sinks.push_back(file);

        const auto qt = std::make_shared<QtSignalSink_mt>();
        qt->set_pattern("[%H:%M:%S] [%l] %v");
        sinks.push_back(qt);

        const auto logger = std::make_shared<spdlog::logger>("soa", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::warn);
        spdlog::set_default_logger(logger);

        (void) LogBridge::instance();

        SPDLOG_INFO("logging initialised -> {}", log_path);
        SPDLOG_INFO("platform: {}", QSysInfo::prettyProductName().toStdString());
        SPDLOG_INFO("kernel: {} {}", QSysInfo::kernelType().toStdString(), QSysInfo::kernelVersion().toStdString());
        SPDLOG_INFO("arch: {}", QSysInfo::currentCpuArchitecture().toStdString());
        SPDLOG_INFO("qt: {}", qVersion());
    }
}
