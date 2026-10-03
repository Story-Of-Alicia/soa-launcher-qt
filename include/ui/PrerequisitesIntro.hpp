#pragma once

#include <QFutureWatcher>
#include <QVector>

#include "runtime/SystemProfile.hpp"
#include "runtime/WineRegistry.hpp"
#include "ui/ModalOverlay.hpp"

class QLabel;
class QPushButton;
class QShowEvent;

class PrerequisitesIntro : public soa::ui::ModalOverlay
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
            soa::runtime::SystemProfile profile;
            QVector<soa::runtime::WineInstall> runtimes;
            bool winetricks_ready {};
            bool umu_ready {};
        };

        void setup_controls();
        void start_detection();
        void finish_detection();
        void update_recommendation();
        void apply_recommendation();

        [[nodiscard]] soa::runtime::RuntimeType recommended_runtime() const;
        [[nodiscard]] const soa::runtime::WineInstall* best_runtime(soa::runtime::RuntimeType type) const;
        [[nodiscard]] QStringList missing_requirements(soa::runtime::RuntimeType type) const;
        [[nodiscard]] bool host_winetricks_required(soa::runtime::RuntimeType type) const;
        [[nodiscard]] bool profile_ready(QString* blocker = nullptr) const;

        QFutureWatcher<DetectionResult>* detector {};
        soa::runtime::SystemProfile system_profile;
        QVector<soa::runtime::WineInstall> runtimes;

        QLabel* recommendation_title {};
        QLabel* recommendation_body {};

        QPushButton* continue_button {};
        QPushButton* choose_own_button {};

        bool detection_complete {};
        bool winetricks_ready {};
        bool umu_ready {};
};
