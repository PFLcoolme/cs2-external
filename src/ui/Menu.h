#pragma once
#include <QDateTime>
#include <QImage>
#include <QPixmap>
#include <QPoint>
#include <QWidget>

#include <functional>
#include <string>

#include "core/Config.h"
#include "game/Game.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QMoveEvent;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

namespace wv {

// 定义在 Menu.cpp，可录制的按键绑定按钮。
class KeyBindButton;

class Menu : public QWidget {
    Q_OBJECT

public:
    Menu(AppState& state, Game& game, std::string configPath);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    // 拖动会改变窗口在屏幕上的位置，毛玻璃背景要跟着重新裁剪。
    void moveEvent(QMoveEvent* event) override;

private:
    void buildUi();
    // 单行 = 一个功能：开关 + 名称 + 右侧「参数」入口。
    // build 非空时，参数控件就地展开在该行下方（下拉式），不再另开浮窗。
    QWidget* makeRow(QCheckBox* toggle, const QString& name, const QString& hint,
                     const std::function<void(QVBoxLayout*)>& build = {});
    // 参数改动后启动 400 ms 防抖定时器；回填控件期间不触发。
    void armPersist();
    void apply();
    // 恢复上一次保存的配置（config.json.bak），可与当前配置来回切换。
    void restoreLast();
    // 统一落盘：先把现有文件备份为 .bak，再写入新配置并同步时间戳。
    void persist(const Config& config, const QString& message);
    // 把配置文件内容回填到控件，供外部改动后的热重载使用。
    void syncFrom(const Config& config);
    // 清零本局命中 / 伤害 / 击杀：热键与日志页按钮共用同一入口。
    void resetStats();

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
    QCheckBox* espRadarRotate_{nullptr};
    QSpinBox* espRadarRange_{nullptr};
    QCheckBox* espOffscreen_{nullptr};
    QCheckBox* espEnemyList_{nullptr};
    QSpinBox* espEnemyListMax_{nullptr};
    QCheckBox* espHeight_{nullptr};
    QCheckBox* espCrosshair_{nullptr};
    QComboBox* espRadarPos_{nullptr};
    QComboBox* espListPos_{nullptr};
    QCheckBox* espTrail_{nullptr};
    QSpinBox* espTrailMs_{nullptr};
    QCheckBox* espHighlight_{nullptr};
    QSpinBox* espDamageMs_{nullptr};
    QComboBox* espDamageColor_{nullptr};
    QCheckBox* espTeam_{nullptr};
    QCheckBox* espVisibility_{nullptr};
    QCheckBox* espStatsHud_{nullptr};
    QComboBox* espStatsHudPos_{nullptr};

    QCheckBox* aimEnabled_{nullptr};
    QComboBox* aimStrategy_{nullptr};
    QCheckBox* aimLock_{nullptr};
    QSpinBox* aimSmooth_{nullptr};
    QSpinBox* aimFov_{nullptr};
    QDoubleSpinBox* aimSensitivity_{nullptr};
    QCheckBox* aimTeam_{nullptr};
    KeyBindButton* aimScopeKey_{nullptr};
    KeyBindButton* aimFireKey_{nullptr};

    QCheckBox* recoilEnabled_{nullptr};
    QDoubleSpinBox* recoilStrength_{nullptr};
    QSpinBox* recoilMax_{nullptr};
    QCheckBox* recoilRequireFire_{nullptr};

    QCheckBox* trackEnabled_{nullptr};
    QCheckBox* trackTeam_{nullptr};
    QSpinBox* trackFov_{nullptr};
    QSpinBox* trackSmooth_{nullptr};
    KeyBindButton* trackKey_{nullptr};

    QCheckBox* silentEnabled_{nullptr};
    QCheckBox* silentTeam_{nullptr};
    QSpinBox* silentFov_{nullptr};
    QSpinBox* silentHold_{nullptr};
    QSpinBox* silentCooldown_{nullptr};
    KeyBindButton* silentKey_{nullptr};

    QCheckBox* triggerEnabled_{nullptr};
    QSpinBox* triggerDelay_{nullptr};
    QSpinBox* triggerCooldown_{nullptr};
    KeyBindButton* triggerKey_{nullptr};
    QCheckBox* triggerTeam_{nullptr};

    // 全局快捷键：显隐面板 / 一键静默 / 清零战绩，可录制改键。
    KeyBindButton* togglePanelKey_{nullptr};
    KeyBindButton* panicKey_{nullptr};
    KeyBindButton* resetStatsKey_{nullptr};
    QPushButton* resetStatsButton_{nullptr};

    // 页四：直接内存功能（五组开关与参数）。
    QCheckBox* memSpeed_{nullptr};
    QDoubleSpinBox* memSpeedFactor_{nullptr};
    QCheckBox* memBulletTrack_{nullptr};
    KeyBindButton* memBulletTrackKey_{nullptr};
    QSpinBox* memBulletTrackDistance_{nullptr};
    QCheckBox* memBulletTeam_{nullptr};
    QCheckBox* memBulletProtectView_{nullptr};
    QCheckBox* memBulletSyncView_{nullptr};
    QCheckBox* memFly_{nullptr};
    KeyBindButton* memFlyUpKey_{nullptr};
    KeyBindButton* memFlyDownKey_{nullptr};
    QSpinBox* memFlySpeed_{nullptr};
    QSpinBox* memFlyMoveType_{nullptr};
    QCheckBox* memSpin_{nullptr};
    QSpinBox* memSpinRate_{nullptr};
    QCheckBox* memSpinKeepView_{nullptr};
    QCheckBox* memNoRecoil_{nullptr};
    QCheckBox* memAccuracy_{nullptr};
    QCheckBox* memAccuracySafe_{nullptr};
    QLabel* memStatus_{nullptr};

    QLabel* status_{nullptr};
    // 叠加层每帧的采集/绘制/过滤计数：区分「没敌人」和「被过滤」。
    QLabel* stats_{nullptr};
    // 自检结果：逐项列出偏移与运行时指针是否可用。
    QLabel* diag_{nullptr};
    int diagTick_{0};

    // 参数改动后 400 ms 自动落盘，避免「改完忘了保存」。
    QTimer* persistTimer_{nullptr};
    // 热重载回填控件时置位，防止回填动作本身触发写盘。
    bool syncing_{false};
    QPlainTextEdit* logView_{nullptr};

    // 左侧类别导航 + 右侧分页：透视 / 自瞄 / 扳机 / 内存 / 日志。
    QPushButton* tabs_[5]{};
    QStackedWidget* pages_{nullptr};
    // 页头：标题 + 副标题随分类切换，右侧是落盘按钮。
    QLabel* pageTitle_{nullptr};
    QLabel* pageSub_{nullptr};
    QLineEdit* search_{nullptr};
    // 按名称过滤当前页的卡片行，空串恢复全部。
    void filterRows(const QString& keyword);
    void setPage(int index);

    // 有合成器时走 ARGB 半透明窗口，背景由合成器实时模糊，能跟随画面变化；
    // 没有合成器才退回「全屏快照 + 自模糊」，那套是静态的，只为兜底。
    bool translucent_{false};

    QImage backdrop_;
    QPixmap backdropBlurred_;
    bool backdropDirty_{true};
    void captureBackdrop();
    void rebuildBackdrop();

    QPoint dragOffset_;
    QDateTime configStamp_;  // 配置文件最后修改时间，用于热重载比对
    // config/offsets.json 的修改时间：生成器重跑后自动重载偏移，不必重启进程。
    QDateTime offsetsStamp_;
    QString offsetsPath() const;
    // 保存/恢复提示的驻留截止时间：状态行默认每 400 ms 被连接状态覆盖，
    // 提示信息需要压过这个轮询，否则用户根本看不到「已保存」或偏移缺失警告。
    QDateTime messageUntil_;
    bool placed_{false};
};

}  // namespace wv
