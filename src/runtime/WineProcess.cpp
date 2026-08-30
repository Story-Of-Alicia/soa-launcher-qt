#include "runtime/WineProcess.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtGlobal>

namespace core::wine
{
    namespace
    {
        QString canonical_existing_path(const QString& path)
        {
            const QFileInfo info(path);
            return info.exists() ? info.canonicalFilePath() : QString();
        }

        QString absolute_clean_path(const QString& path)
        {
            return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        }

        bool path_has_prefix(const QString& candidate, const QString& root)
        {
            if (candidate == root)
                return true;
            const QString with_separator = root.endsWith(QLatin1Char('/'))
                ? root : root + QLatin1Char('/');
            return candidate.startsWith(with_separator);
        }

        std::optional<WindowsProcessInfo> match_csv_line(const QString& line,
                                                         const QString& wanted)
        {
            static const QRegularExpression csv(
                QStringLiteral(R"rx(^\s*"([^"]+)"\s*,\s*"?(\d+)"?(?:\s*,|\s*$))rx"));
            const auto match = csv.match(line);
            if (!match.hasMatch()
                || match.captured(1).compare(wanted, Qt::CaseInsensitive) != 0)
            {
                return std::nullopt;
            }

            bool ok = false;
            const qint64 pid = match.captured(2).toLongLong(&ok);
            return WindowsProcessInfo {match.captured(1), ok ? pid : -1, line.trimmed()};
        }

        std::optional<WindowsProcessInfo> match_table_line(const QString& line,
                                                           const QString& wanted)
        {
            static const QRegularExpression table(
                QStringLiteral(R"(^\s*(\S+)\s+(\d+)(?:\s+|$))"));
            const auto match = table.match(line);
            if (!match.hasMatch()
                || match.captured(1).compare(wanted, Qt::CaseInsensitive) != 0)
            {
                return std::nullopt;
            }

            bool ok = false;
            const qint64 pid = match.captured(2).toLongLong(&ok);
            return WindowsProcessInfo {match.captured(1), ok ? pid : -1, line.trimmed()};
        }
    }

    QString decode_windows_process_output(const QByteArray& output)
    {
        if (output.isEmpty())
            return {};

        const QByteArray littleEndianMarker("\xff\xfe", 2);
        const QByteArray bigEndianMarker("\xfe\xff", 2);
        const qsizetype littleEndianBom = output.indexOf(littleEndianMarker);
        const qsizetype bigEndianBom = output.indexOf(bigEndianMarker);




        const auto alignedRun = [&output](const bool bigEndian) -> qsizetype
        {
            constexpr int requiredPairs = 6;
            const qsizetype limit = qMin<qsizetype>(
                output.size() - requiredPairs * 2, 4096);
            for (qsizetype start = 0; start <= limit; ++start)
            {
                bool matches = true;
                for (int pair = 0; pair < requiredPairs; ++pair)
                {
                    const auto first = static_cast<unsigned char>(
                        output[start + pair * 2]);
                    const auto second = static_cast<unsigned char>(
                        output[start + pair * 2 + 1]);
                    const auto character = bigEndian ? second : first;
                    const auto zeroByte = bigEndian ? first : second;
                    if (zeroByte != 0
                        || (character < 0x20 && character != '\r'
                            && character != '\n' && character != '\t'))
                    {
                        matches = false;
                        break;
                    }
                }
                if (matches)
                    return start;
            }
            return -1;
        };

        int oddNuls = 0;
        int sampledPairs = 0;
        for (qsizetype index = 0;
             index + 1 < output.size() && sampledPairs < 256;
             index += 2, ++sampledPairs)
        {
            if (output[index + 1] == '\0')
                ++oddNuls;
        }
        const bool hasBom = littleEndianBom >= 0 || bigEndianBom >= 0;
        const qsizetype detectedLittleEndian = hasBom
            ? -1 : alignedRun(false);
        const qsizetype detectedBigEndian = hasBom
            ? -1 : alignedRun(true);
        const bool looksUtf16Le = !hasBom
            && (detectedLittleEndian >= 0
                || (sampledPairs >= 4 && oddNuls * 2 >= sampledPairs));
        const bool looksUtf16Be = !hasBom && detectedBigEndian >= 0;
        if (hasBom || looksUtf16Le || looksUtf16Be)
        {
            const bool bigEndian = hasBom
                ? (bigEndianBom >= 0
                    && (littleEndianBom < 0 || bigEndianBom < littleEndianBom))
                : (!looksUtf16Le && looksUtf16Be);
            const qsizetype start = hasBom
                ? (bigEndian ? bigEndianBom + 2 : littleEndianBom + 2)
                : (bigEndian ? detectedBigEndian
                             : (detectedLittleEndian >= 0
                                 ? detectedLittleEndian : 0));
            QString decoded;
            decoded.reserve((output.size() - start) / 2);
            for (qsizetype index = start; index + 1 < output.size(); index += 2)
            {
                const auto first = static_cast<unsigned char>(output[index]);
                const auto second = static_cast<unsigned char>(output[index + 1]);
                const ushort codeUnit = bigEndian
                    ? ushort((first << 8) | second)
                    : ushort(first | (second << 8));
                decoded.append(QChar(codeUnit));
            }
            return decoded;
        }

        QString decoded = QString::fromUtf8(output);
        if (decoded.contains(QChar::ReplacementCharacter))
            decoded = QString::fromLocal8Bit(output);
        return decoded;
    }

    QString windows_path_for_prefix_file(const QString& prefix,
                                         const QString& host_path,
                                         QString* error)
    {
        const QString drive_c = canonical_existing_path(
            QDir(prefix).filePath(QStringLiteral("drive_c")));
        if (drive_c.isEmpty())
        {
            if (error)
                *error = QStringLiteral(
                    "The compatibility prefix drive_c directory does not exist.");
            return {};
        }

        const QString canonical_file = canonical_existing_path(host_path);
        if (canonical_file.isEmpty() || !QFileInfo(canonical_file).isFile())
        {
            if (error)
                *error = QStringLiteral("The game executable does not exist or cannot be resolved.");
            return {};
        }

        if (!path_has_prefix(canonical_file, drive_c))
        {
            if (error)
                *error = QStringLiteral(
                    "The game executable resolves outside the compatibility C: drive.");
            return {};
        }

        QString relative = QDir(drive_c).relativeFilePath(canonical_file);
        if (relative == QStringLiteral(".") || relative.startsWith(QStringLiteral("../")))
        {
            if (error)
                *error = QStringLiteral("The game executable could not be mapped to a safe C: path.");
            return {};
        }

        relative = QDir::cleanPath(relative);
        relative.replace(QLatin1Char('/'), QLatin1Char('\\'));
        if (error)
            error->clear();
        return QStringLiteral("C:\\") + relative;
    }

    bool host_path_is_inside_prefix(const QString& prefix, const QString& host_path)
    {
        const QString root_absolute = absolute_clean_path(prefix);
        const QString candidate_absolute = absolute_clean_path(host_path);
        if (!path_has_prefix(candidate_absolute, root_absolute))
            return false;

        const QString root_canonical = canonical_existing_path(root_absolute);
        if (root_canonical.isEmpty())
            return false;

        QString ancestor = candidate_absolute;
        while (true)
        {
            const QFileInfo info(ancestor);
            if (info.isSymLink() && !info.exists())
                return false;
            if (info.exists())
            {
                const QString ancestor_canonical = info.canonicalFilePath();
                return !ancestor_canonical.isEmpty()
                    && path_has_prefix(QDir::cleanPath(ancestor_canonical),
                                       QDir::cleanPath(root_canonical));
            }

            const QString parent = info.dir().absolutePath();
            if (parent == ancestor)
                return false;
            ancestor = parent;
        }
    }

    std::optional<WindowsProcessInfo> find_windows_process(const QString& tasklist_output,
                                                           const QString& image_name)
    {
        const QString wanted = QFileInfo(image_name).fileName();
        for (const QString& raw_line : tasklist_output.split(QLatin1Char('\n')))
        {
            const QString line = raw_line.trimmed();
            if (line.isEmpty())
                continue;
            if (const auto csv = match_csv_line(line, wanted))
                return csv;
            if (const auto table = match_table_line(line, wanted))
                return table;
        }
        return std::nullopt;
    }
    std::optional<WindowsProcessInfo> find_host_process(const QString& process_list_output,
                                                        const QString& image_name)
    {
        const QString wanted = QFileInfo(image_name).fileName();
        static const QRegularExpression process_line(
            QStringLiteral(R"(^\s*(\d+)\s+(.+)$)"));
        std::optional<WindowsProcessInfo> best;
        int best_score = -1;

        for (const QString& raw_line : process_list_output.split(QLatin1Char('\n')))
        {
            const auto match = process_line.match(raw_line);
            if (!match.hasMatch())
                continue;
            const QString command = match.captured(2);
            if (!command.contains(wanted, Qt::CaseInsensitive))
                continue;

            if ((command.contains(QStringLiteral("/desktop="), Qt::CaseInsensitive)
                 && command.contains(QStringLiteral("explorer"), Qt::CaseInsensitive))
                || command.contains(QStringLiteral("tasklist.exe"), Qt::CaseInsensitive)
                || command.contains(QStringLiteral("winedbg"), Qt::CaseInsensitive)
                || command.contains(QStringLiteral("wineconsole"), Qt::CaseInsensitive)
                || command.contains(QStringLiteral("SoaAliciaLogInjector.exe"),
                                    Qt::CaseInsensitive)
                || command.contains(QStringLiteral("umu-run"), Qt::CaseInsensitive))
            {
                continue;
            }

            bool ok = false;
            const qint64 pid = match.captured(1).toLongLong(&ok);
            if (!ok || pid <= 0)
                continue;

            int score = 10;
            if (command.contains(QStringLiteral("wine64-preloader"), Qt::CaseInsensitive)
                || command.contains(QStringLiteral("wine-preloader"), Qt::CaseInsensitive))
            {
                score += 100;
            }
            else if (command.startsWith(wanted, Qt::CaseInsensitive))
            {
                score += 80;
            }
            else if (command.contains(QStringLiteral("/wine64 "), Qt::CaseInsensitive)
                     || command.contains(QStringLiteral("/wine "), Qt::CaseInsensitive))
            {
                score += 40;
            }

            if (score > best_score)
            {
                best_score = score;
                best = WindowsProcessInfo {wanted, pid, raw_line.trimmed()};
            }
        }
        return best;
    }

}
