#pragma once
#include <QWidget>

#include <string>

#include "core/Config.h"
#include "game/Game.h"

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace wv {

class Menu : public QWidget {
    Q_OBJECT

public:
    Menu(AppState& state, Game& game, std::string configPath);

private:
    void buildUi();
    void apply();

    AppState& state_;
    Game& game_;
    std::string path_;

    QCheckBox* espEnabled_{nullptr};
    QCheckBox* espBox_{nullptr};
    QCheckBox* espHealth_{nullptr};
    QCheckBox* espDistance_{nullptr};
    QCheckBox* espSkeleton_{nullptr};
    QCheckBox* espTeam_{nullptr};
    QCheckBox* espVisibility_{nullptr};

    QCheckBox* aimEnabled_{nullptr};
    QSpinBox* aimSmooth_{nullptr};
    QSpinBox* aimFov_{nullptr};
    QDoubleSpinBox* aimSensitivity_{nullptr};
    QCheckBox* aimTeam_{nullptr};

    QCheckBox* triggerEnabled_{nullptr};
    QSpinBox* triggerDelay_{nullptr};
    QSpinBox* triggerCooldown_{nullptr};

    QLabel* status_{nullptr};
};

}  // namespace wv
