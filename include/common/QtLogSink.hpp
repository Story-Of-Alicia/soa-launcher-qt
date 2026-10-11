#pragma once
#include <spdlog/sinks/base_sink.h>
#include <spdlog/fmt/fmt.h>
#include <QObject>
#include <QString>
#include <mutex>

class LogBridge : public QObject
{
    Q_OBJECT
    public:
        static LogBridge& instance()
        {
            static LogBridge b;
            return b;
        }

        void post(int level, const QString& line) { emit message(level, line); }

        signals:
            void message(int level, const QString& line);

    private:
        LogBridge() = default;
};

template<typename Mutex>
class QtSignalSink : public spdlog::sinks::base_sink<Mutex>
{
    protected:
        void sink_it_(const spdlog::details::log_msg& msg) override
        {
            spdlog::memory_buf_t buf;
            this->formatter_->format(msg, buf);
            const QString line  = QString::fromStdString(fmt::to_string(buf)).trimmed();
            const int     level = static_cast<int>(msg.level);
            QMetaObject::invokeMethod(&LogBridge::instance(),
                [level, line] { LogBridge::instance().post(level, line); },
                Qt::QueuedConnection);
        }
        void flush_() override {}
};

using QtSignalSink_mt = QtSignalSink<std::mutex>;
