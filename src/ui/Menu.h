#pragma once
#include <QPoint>
#include <QWidget>

#include <string>

#include "core/Config.h"
#include "game/Game.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace wv {

class Menu : public QWidget {
    Q_OBJECT

public:
    Menu(AppState& state, Game& game, std::string configPath);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void buildUi();
    void apply();

    AppState& state_;
    Game& game_;
    std::string path_;

    QCheckBox* espEnabled_{nullptr};
    QCheckBox* espBox_{nullptr};
    QCheckBox* espHealth_{nullptr};
    QCheckBox* espHealthText_{nullptr};
    QCheckBox* espDistance_{nullptr};
    QCheckBox* espSkeleton_{nullptr};
    QCheckBox* espRadar_{nullptr};
    QSpinBox* espRadarRange_{nullptr};
    QCheckBox* espTeam_{nullptr};
    QCheckBox* espVisibility_{nullptr};

    QCheckBox* aimEnabled_{nullptr};
    QComboBox* aimStrategy_{nullptr};
    QSpinBox* aimSmooth_{nullptr};
    QSpinBox* aimFov_{nullptr};
    QDoubleSpinBox* aimSensitivity_{nullptr};
    QCheckBox* aimTeam_{nullptr};

    QCheckBox* triggerEnabled_{nullptr};
    QSpinBox* triggerDelay_{nullptr};
    QSpinBox* triggerCooldown_{nullptr};

    QLabel* status_{nullptr};

    QPoint dragOffset_;
    bool placed_{false};
};

}  // namespace wv
