#pragma once
#include <QDateTime>
#include <QPoint>
#include <QWidget>

#include <string>

#include "core/Config.h"
#include "game/Game.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

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
    // 恢复上一次保存的配置（config.json.bak），可与当前配置来回切换。
    void restoreLast();
    // 统一落盘：先把现有文件备份为 .bak，再写入新配置并同步时间戳。
    void persist(const Config& config, const QString& message);
    // 把配置文件内容回填到控件，供外部改动后的热重载使用。
    void syncFrom(const Config& config);

    AppState& state_;
    Game& game_;
    std::string path_;

    QCheckBox* espEnabled_{nullptr};
    QCheckBox* espBox_{nullptr};
    QCheckBox* espHealth_{nullptr};
    QCheckBox* espHealthText_{nullptr};
    QCheckBox* espArmor_{nullptr};
    QCheckBox* espDamage_{nullptr};
    QCheckBox* espDistance_{nullptr};
    QCheckBox* espSkeleton_{nullptr};
    QCheckBox* espRadar_{nullptr};
    QSpinBox* espRadarRange_{nullptr};
    QCheckBox* espTrail_{nullptr};
    QSpinBox* espTrailMs_{nullptr};
    QCheckBox* espHighlight_{nullptr};
    QSpinBox* espDamageMs_{nullptr};
    QComboBox* espDamageColor_{nullptr};
    QCheckBox* espTeam_{nullptr};
    QCheckBox* espVisibility_{nullptr};

    QCheckBox* aimEnabled_{nullptr};
    QComboBox* aimStrategy_{nullptr};
    QSpinBox* aimSmooth_{nullptr};
    QSpinBox* aimFov_{nullptr};
    QDoubleSpinBox* aimSensitivity_{nullptr};
    QCheckBox* aimTeam_{nullptr};
    QComboBox* aimScopeKey_{nullptr};
    QComboBox* aimFireKey_{nullptr};

    QCheckBox* recoilEnabled_{nullptr};
    QDoubleSpinBox* recoilStrength_{nullptr};
    QSpinBox* recoilMax_{nullptr};
    QCheckBox* recoilRequireFire_{nullptr};

    QCheckBox* triggerEnabled_{nullptr};
    QSpinBox* triggerDelay_{nullptr};
    QSpinBox* triggerCooldown_{nullptr};
    QComboBox* triggerKey_{nullptr};

    QLabel* status_{nullptr};
    QPushButton* tabs_[4]{nullptr, nullptr, nullptr, nullptr};
    QStackedWidget* pages_{nullptr};
    QPlainTextEdit* logView_{nullptr};

    QPoint dragOffset_;
    QDateTime configStamp_;  // 配置文件最后修改时间，用于热重载比对
    // 保存/恢复提示的驻留截止时间：状态行默认每 400 ms 被连接状态覆盖，
    // 提示信息需要压过这个轮询，否则用户根本看不到「已保存」或偏移缺失警告。
    QDateTime messageUntil_;
    bool placed_{false};
};

}  // namespace wv
