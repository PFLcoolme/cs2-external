#include "Menu.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace wv {

Menu::Menu(AppState& state, Game& game, std::string configPath)
    : QWidget(nullptr), state_(state), game_(game), path_(std::move(configPath)) {
    setWindowTitle("CS2 辅助 · 控制面板");
    setFixedWidth(320);
    buildUi();

    auto timer = new QTimer(this);
    timer->setInterval(400);
    connect(timer, &QTimer::timeout, this, [this] {
        if (status_) status_->setText(QString::fromStdString(game_.status()));
    });
    timer->start();
}

void Menu::buildUi() {
    const std::shared_ptr<Config> config = state_.config.load();
    const Config initial = config ? *config : Config{};

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);

    // ---- 透视 ----
    auto* espGroup = new QGroupBox("透视", this);
    auto* espLayout = new QVBoxLayout(espGroup);
    espEnabled_ = new QCheckBox("启用透视", espGroup);
    espEnabled_->setChecked(initial.esp.enabled);
    espBox_ = new QCheckBox("方框", espGroup);
    espBox_->setChecked(initial.esp.box);
    espHealth_ = new QCheckBox("血量条", espGroup);
    espHealth_->setChecked(initial.esp.health);
    espDistance_ = new QCheckBox("距离", espGroup);
    espDistance_->setChecked(initial.esp.distance);
    espSkeleton_ = new QCheckBox("骨骼连线", espGroup);
    espSkeleton_->setChecked(initial.esp.skeleton);
    espTeam_ = new QCheckBox("过滤队友", espGroup);
    espTeam_->setChecked(initial.esp.teamCheck);
    espVisibility_ = new QCheckBox("可见性双色", espGroup);
    espVisibility_->setChecked(initial.esp.visibilityCheck);
    espLayout->addWidget(espEnabled_);
    espLayout->addWidget(espBox_);
    espLayout->addWidget(espHealth_);
    espLayout->addWidget(espDistance_);
    espLayout->addWidget(espSkeleton_);
    espLayout->addWidget(espTeam_);
    espLayout->addWidget(espVisibility_);
    root->addWidget(espGroup);

    // ---- 自瞄 ----
    auto* aimGroup = new QGroupBox("自瞄（无条件）", this);
    auto* aimLayout = new QFormLayout(aimGroup);
    aimEnabled_ = new QCheckBox("启用自瞄", aimGroup);
    aimEnabled_->setChecked(initial.aim.enabled);
    aimSmooth_ = new QSpinBox(aimGroup);
    aimSmooth_->setRange(1, 40);
    aimSmooth_->setValue(initial.aim.smooth);
    aimFov_ = new QSpinBox(aimGroup);
    aimFov_->setRange(20, 1200);
    aimFov_->setValue(static_cast<int>(initial.aim.fovPixels));
    aimSensitivity_ = new QDoubleSpinBox(aimGroup);
    aimSensitivity_->setRange(0.05, 3.0);
    aimSensitivity_->setSingleStep(0.05);
    aimSensitivity_->setValue(initial.aim.sensitivityFactor);
    aimTeam_ = new QCheckBox("过滤队友", aimGroup);
    aimTeam_->setChecked(initial.aim.teamCheck);
    aimLayout->addRow(aimEnabled_);
    aimLayout->addRow("平滑度", aimSmooth_);
    aimLayout->addRow("范围(像素)", aimFov_);
    aimLayout->addRow("灵敏度系数", aimSensitivity_);
    aimLayout->addRow(aimTeam_);
    root->addWidget(aimGroup);

    // ---- 扳机 ----
    auto* triggerGroup = new QGroupBox("扳机", this);
    auto* triggerLayout = new QFormLayout(triggerGroup);
    triggerEnabled_ = new QCheckBox("启用扳机", triggerGroup);
    triggerEnabled_->setChecked(initial.trigger.enabled);
    triggerDelay_ = new QSpinBox(triggerGroup);
    triggerDelay_->setRange(1, 500);
    triggerDelay_->setValue(initial.trigger.delayMs);
    triggerCooldown_ = new QSpinBox(triggerGroup);
    triggerCooldown_->setRange(10, 2000);
    triggerCooldown_->setValue(initial.trigger.cooldownMs);
    triggerLayout->addRow(triggerEnabled_);
    triggerLayout->addRow("按住时长(ms)", triggerDelay_);
    triggerLayout->addRow("冷却(ms)", triggerCooldown_);
    root->addWidget(triggerGroup);

    // ---- 状态 / 保存 ----
    status_ = new QLabel(QString::fromStdString(game_.status()), this);
    status_->setWordWrap(true);
    root->addWidget(status_);

    auto* hint = new QLabel(
        QString::fromStdString("偏移来源: " + initial.offsetsSource +
                               "\n偏移改动需重启生效；偏移缺失时不会绘制。"),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray;");
    root->addWidget(hint);

    auto* applyButton = new QPushButton("应用并保存", this);
    connect(applyButton, &QPushButton::clicked, this, &Menu::apply);
    root->addWidget(applyButton);
    root->addStretch();
}

void Menu::apply() {
    const std::shared_ptr<Config> current = state_.config.load();
    Config next = current ? *current : Config{};

    next.esp.enabled = espEnabled_->isChecked();
    next.esp.box = espBox_->isChecked();
    next.esp.health = espHealth_->isChecked();
    next.esp.distance = espDistance_->isChecked();
    next.esp.skeleton = espSkeleton_->isChecked();
    next.esp.teamCheck = espTeam_->isChecked();
    next.esp.visibilityCheck = espVisibility_->isChecked();

    next.aim.enabled = aimEnabled_->isChecked();
    next.aim.smooth = aimSmooth_->value();
    next.aim.fovPixels = static_cast<float>(aimFov_->value());
    next.aim.sensitivityFactor = static_cast<float>(aimSensitivity_->value());
    next.aim.teamCheck = aimTeam_->isChecked();

    next.trigger.enabled = triggerEnabled_->isChecked();
    next.trigger.delayMs = triggerDelay_->value();
    next.trigger.cooldownMs = triggerCooldown_->value();

    next.save(path_);
    state_.config.store(std::make_shared<Config>(next));

    if (status_) status_->setText("已保存并生效");
}

}  // namespace wv
