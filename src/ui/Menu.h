#pragma once
#include <QDateTime>
#include <QHash>
#include <QPoint>
#include <QWidget>

#include <functional>
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
class QVBoxLayout;

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
    // 参数弹层关闭时落盘：右键调完参数松手即生效，不需要再点保存。
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    // 单行 = 一个功能：开关 + 名称 + 右侧「右键」提示。
    QWidget* makeRow(QCheckBox* toggle, const QString& name, const QString& hint);
    // 给某一行挂上参数弹层；build 只在第一次右键时调用。
    void attachParams(QWidget* row, QCheckBox* toggle, const QString& title,
                      const std::function<void(QVBoxLayout*)>& build);
    QWidget* ensurePopup(const QString& title);
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

    QCheckBox* trackEnabled_{nullptr};
    QCheckBox* trackTeam_{nullptr};
    QSpinBox* trackFov_{nullptr};
    QSpinBox* trackSmooth_{nullptr};
    QComboBox* trackKey_{nullptr};

    QCheckBox* silentEnabled_{nullptr};
    QCheckBox* silentTeam_{nullptr};
    QSpinBox* silentFov_{nullptr};
    QSpinBox* silentHold_{nullptr};
    QComboBox* silentKey_{nullptr};

    QCheckBox* triggerEnabled_{nullptr};
    QSpinBox* triggerDelay_{nullptr};
    QSpinBox* triggerCooldown_{nullptr};
    QComboBox* triggerKey_{nullptr};

    QLabel* status_{nullptr};
    QPlainTextEdit* logView_{nullptr};

    // 左侧类别导航 + 右侧分页：类别仍然是四页，页内一个功能一行。
    QPushButton* tabs_[4]{};
    QStackedWidget* pages_{nullptr};

    // 参数弹层：按标题缓存已建好的弹层与该弹层的构造器。
    QHash<QString, QWidget*> popups_;
    QHash<QString, std::function<void(QVBoxLayout*)>> popupBuilders_;

    QPoint dragOffset_;
    QDateTime configStamp_;  // 配置文件最后修改时间，用于热重载比对
    // 保存/恢复提示的驻留截止时间：状态行默认每 400 ms 被连接状态覆盖，
    // 提示信息需要压过这个轮询，否则用户根本看不到「已保存」或偏移缺失警告。
    QDateTime messageUntil_;
    bool placed_{false};
};

}  // namespace wv
