#pragma once

#include "network/SwiftNetwork.h"
#include "update/LauncherReleaseCatalogue.hpp"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace core::update
{
    class LauncherUpdateManager final : public QObject
    {
        Q_OBJECT

    public:
        explicit LauncherUpdateManager(QObject* parent = nullptr);
        ~LauncherUpdateManager() override;

        void check_for_updates();
        void manage_versions();
        void download_and_install();
        void cancel_download();
        bool select_version(const QString& version);

        [[nodiscard]] bool update_available() const;
        [[nodiscard]] bool update_required() const;
        [[nodiscard]] QString available_version() const;
        [[nodiscard]] QString release_message() const;
        [[nodiscard]] QString platform_key() const;
        [[nodiscard]] QString downloaded_path() const;
        [[nodiscard]] QStringList available_versions() const;
        [[nodiscard]] static QString current_version();

    signals:
        void check_started();
        void no_update_available();
        void update_found();
        void catalogue_ready();
        void check_failed(const QString& reason);
        void manual_check_failed(const QString& reason);
        void download_started();
        void download_progress(qint64 received, qint64 total);
        void installer_started(const QString& path);
        void update_failed(const QString& reason);

    private:
        enum class CheckPurpose
        {
            None,
            Startup,
            Manual
        };

        static void check_callback(soa_launcher_check_result result,
                                   soa_launcher_error error_code,
                                   int http_status,
                                   const char* error_detail,
                                   const char* version,
                                   const char* minimum_version,
                                   const char* message,
                                   const char* package_kind,
                                   const char* package_file_name,
                                   const char* package_url,
                                   const char* sha256,
                                   uint64_t expected_size,
                                   bool required,
                                   const char* releases_json,
                                   void* ctx);
        static void progress_callback(uint64_t received, uint64_t total, void* ctx);
        static void download_callback(soa_launcher_download_result result,
                                      soa_launcher_error error_code,
                                      int http_status,
                                      const char* error_detail,
                                      const char* final_path,
                                      void* ctx);
        void handle_check(soa_launcher_check_result result,
                          soa_launcher_error error_code,
                          int http_status,
                          QString error_detail,
                          QString version,
                          QString minimum_version,
                          QString release_message,
                          QString package_kind,
                          QString package_file_name,
                          QUrl package_url,
                          QByteArray sha256,
                          qulonglong expected_size,
                          bool required,
                          QString releases_json);
        void handle_download(soa_launcher_download_result result,
                             soa_launcher_error error_code,
                             int http_status,
                             QString error_detail,
                             QString final_path);
        void reset_release();
        void install_downloaded_package();
        void install_linux_appimage();
        void open_macos_installer();
        void verify_macos_installer_signature();
        void open_verified_macos_installer();
        void schedule_health_checkpoint();
        [[nodiscard]] QString error_message(soa_launcher_error error_code,
                                            int http_status,
                                            const QString& detail) const;
        [[nodiscard]] static QString detected_platform_key();
        [[nodiscard]] static QString download_directory();
        static void prune_update_cache(const QString& directory);
        soa_launcher_updater* updater {};
        QString release_version;
        QString minimum_version;
        QString message;
        QString package_kind;
        QString package_file_name;
        QString final_download_path;
        QUrl package_url;
        QByteArray expected_sha256;
        qulonglong expected_size {};
        bool required {};
        bool downloading {};
        CheckPurpose check_purpose {CheckPurpose::None};
        QString configuration_error;
        QList<LauncherRelease> releases;
    };
}
