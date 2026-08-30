#pragma once

#include <QFutureWatcher>
#include <QVector>

#include "runtime/SystemProfile.hpp"
#include "runtime/WineRegistry.hpp"
#include "ui/ModalOverlay.hpp"

class QLabel;
class QPushButton;
class QShowEvent;

class PrerequisitesIntro : public util::modal_overlay::ModalOverlay
{
    Q_OBJECT

    public:
        explicit PrerequisitesIntro(QWidget* parent = nullptr);

        signals:
            void accepted();
        void choose_own_requested();

    protected:
        void paint_content(QPainter& painter) override;
        void showEvent(QShowEvent* event) override;

    private:
        struct DetectionResult
        {
            core::system::SystemProfile profile;
            QVector<core::wine::WineInstall> runtimes;
            bool winetricks_ready {};
            bool umu_ready {};
        };

        void setup_controls();
        void start_detection();
        void finish_detection();
        void update_recommendation();
        void apply_recommendation();

        [[nodiscard]] core::wine::RuntimeType recommended_runtime() const;
        [[nodiscard]] const core::wine::WineInstall* best_runtime(core::wine::RuntimeType type) const;
        [[nodiscard]] QStringList missing_requirements(core::wine::RuntimeType type) const;
        [[nodiscard]] bool profile_ready(QString* blocker = nullptr) const;

        QFutureWatcher<DetectionResult>* detector {};
        core::system::SystemProfile system_profile;
        QVector<core::wine::WineInstall> runtimes;

        QLabel* recommendation_title {};
        QLabel* recommendation_body {};

        QPushButton* continue_button {};
        QPushButton* choose_own_button {};

        bool detection_complete {};
        bool winetricks_ready {};
        bool umu_ready {};
};
