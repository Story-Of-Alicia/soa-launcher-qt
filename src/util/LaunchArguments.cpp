#include "util/LaunchArguments.hpp"

#include <QProcess>
#include <QRegularExpression>
#include <QtGlobal>

#include <utility>

namespace util::launch_arguments
{
    namespace
    {
        enum class ReservedArgument
        {
            None,
            Operation,
            Id,
            GameId
        };

        ReservedArgument reserved_argument(const QString& argument)
        {
            const QString normalized = argument.trimmed();
            for (const auto& [name, kind] : {
                     std::pair {QStringLiteral("-OP"), ReservedArgument::Operation},
                     std::pair {QStringLiteral("-ID"), ReservedArgument::Id},
                     std::pair {QStringLiteral("-GameID"), ReservedArgument::GameId}})
            {
                if (normalized.compare(name, Qt::CaseInsensitive) == 0
                    || normalized.startsWith(name + QLatin1Char('='), Qt::CaseInsensitive))
                {
                    return kind;
                }
            }
            return ReservedArgument::None;
        }

        bool is_environment_assignment(const QString& argument)
        {
            static const QRegularExpression key_pattern(
                QStringLiteral(R"(^[A-Za-z_][A-Za-z0-9_]*$)"));
            const int equals = argument.indexOf(QLatin1Char('='));
            return equals > 0 && key_pattern.match(argument.left(equals)).hasMatch();
        }

        bool invalid_argument(const QString& argument)
        {
            static const QRegularExpression control_characters(
                QStringLiteral("[\\x00-\\x1F\\x7F]"));
            return argument.size() > 1024 || control_characters.match(argument).hasMatch();
        }

        QString inline_value(const QString& argument)
        {
            const int equals = argument.indexOf(QLatin1Char('='));
            return equals >= 0 ? argument.mid(equals + 1) : QString {};
        }
    }

    bool developer_mode_enabled()
    {
        const QString value = qEnvironmentVariable("DEVELOPER_MODE").trimmed().toLower();
        return value == QStringLiteral("on") || value == QStringLiteral("true");
    }

    ValidationResult validate(const QString& raw)
    {
        ValidationResult result;
        if (raw.size() > 4096)
        {
            result.error = QStringLiteral("Game launch arguments are too long.");
            return result;
        }

        const QStringList tokens = QProcess::splitCommand(raw);
        if (tokens.size() > 64)
        {
            result.error = QStringLiteral("Too many game launch arguments were provided.");
            return result;
        }

        const bool developer_mode = developer_mode_enabled();
        for (qsizetype index = 0; index < tokens.size(); ++index)
        {
            const QString& argument = tokens[index];
            if (invalid_argument(argument))
            {
                result.error = QStringLiteral("A game launch argument is invalid or too long.");
                return result;
            }

            const ReservedArgument reserved = reserved_argument(argument);
            if (reserved != ReservedArgument::None)
            {
                if (!developer_mode || reserved == ReservedArgument::GameId)
                {
                    result.error = developer_mode
                        ? QStringLiteral("-GameID is managed by the launcher and cannot be overridden.")
                        : QStringLiteral(
                              "-OP, -ID, and -GameID are managed by the launcher and cannot be overridden.");
                    return result;
                }

                const QString normalized = argument.trimmed();
                QString value = inline_value(normalized);
                if (!normalized.contains(QLatin1Char('=')))
                {
                    if (++index >= tokens.size() || invalid_argument(tokens[index])
                        || reserved_argument(tokens[index]) != ReservedArgument::None)
                    {
                        result.error = QStringLiteral(
                            "Developer mode requires a value after -ID and -OP.");
                        return result;
                    }
                    value = tokens[index];
                }
                if (value.isEmpty())
                {
                    result.error = QStringLiteral(
                        "Developer mode requires a value after -ID and -OP.");
                    return result;
                }

                QString* destination = reserved == ReservedArgument::Id
                    ? &result.developer_id
                    : &result.developer_op;
                if (!destination->isEmpty())
                {
                    result.error = QStringLiteral(
                        "Developer mode accepts only one -ID and one -OP argument.");
                    return result;
                }
                *destination = value;
                continue;
            }

            if (is_environment_assignment(argument))
            {
                result.environment_entries.append(argument);
            }
            else
            {
                result.arguments.append(argument);
            }
        }

        result.valid = true;
        return result;
    }
}
