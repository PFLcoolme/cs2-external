#include "Menu.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace wv {
namespace {

// 面板视觉：无边框半透明玻璃底板（paintEvent 绘制）+ 极简控件。
// 黑白双色：勾选态与主按钮用白，其余全部走中性灰阶，不使用任何彩色强调。
const char* kStyle = R"QSS(
QWidget {
    background: transparent;
    color: #E9EAEC;
    font-family: "Noto Sans CJK SC", "Source Han Sans SC", "DejaVu Sans", sans-serif;
    font-size: 12px;
}
QLabel#title {
    color: #F4F5F7;
    font-family: "DejaVu Sans Mono", monospace;
    font-size: 12px;
    font-weight: 700;
    letter-spacing: 3px;
}
QLabel#field { color: #C2C7CC; font-size: 11px; }
QLabel#status {
    color: #B0B6BC;
    font-family: "DejaVu Sans Mono", monospace;
    font-size: 10px;
}
QLabel#hint { color: #9AA0A6; font-size: 10px; }
QFrame#rule { background: rgba(255, 255, 255, 70); border: none; min-height: 1px; max-height: 1px; }
QFrame#vrule { background: rgba(255, 255, 255, 52); border: none; min-width: 1px; max-width: 1px; }
QListWidget#nav {
    background: transparent;
    border: none;
    outline: none;
}
QListWidget#nav::item {
    background: transparent;
    border: none;
    border-radius: 7px;
    padding: 7px 6px;
    margin-bottom: 4px;
    color: #B4B9BF;
    letter-spacing: 2px;
}
QListWidget#nav::item:hover { background: rgba(255, 255, 255, 18); color: #F2F3F5; }
QListWidget#nav::item:selected { background: rgba(255, 255, 255, 235); color: #0F1113; }
QCheckBox { spacing: 7px; color: #E9EBEE; }
QCheckBox:hover { color: #FFFFFF; }
QCheckBox::indicator {
    width: 13px;
    height: 13px;
    border: 1px solid rgba(255, 255, 255, 110);
    border-radius: 4px;
    background: transparent;
}
QCheckBox::indicator:hover { border-color: rgba(255, 255, 255, 170); }
QCheckBox::indicator:checked { background: #F2F3F5; border-color: #F2F3F5; }
QSpinBox, QDoubleSpinBox, QComboBox {
    background: transparent;
    border: 1px solid rgba(255, 255, 255, 70);
    border-radius: 6px;
    padding: 3px 7px;
    min-height: 16px;
    color: #FFFFFF;
    font-family: "DejaVu Sans Mono", monospace;
    font-size: 11px;
    selection-background-color: #EDEFF2;
    selection-color: #0F1113;
}
QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover {
    background: rgba(255, 255, 255, 26);
    border-color: rgba(255, 255, 255, 120);
}
QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border-color: rgba(255, 255, 255, 200);
    background: rgba(255, 255, 255, 26);
}
QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    width: 0;
    height: 0;
    border: none;
}
QComboBox::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView {
    background: #14171C;
    border: 1px solid rgba(255, 255, 255, 30);
    border-radius: 6px;
    padding: 4px;
    outline: none;
    selection-background-color: rgba(255, 255, 255, 235);
    selection-color: #0F1113;
}
QPushButton#apply {
    background: transparent;
    color: #F2F3F5;
    border: 1px solid rgba(255, 255, 255, 90);
    border-radius: 8px;
    padding: 8px 12px;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 3px;
}
QPushButton#apply:hover { background: rgba(255, 255, 255, 235); color: #0F1113; border-color: transparent; }
QPushButton#apply:pressed { background: rgba(255, 255, 255, 180); color: #0F1113; }
QPushButton#close {
    background: transparent;
    border: 0;
    border-radius: 9px;
    color: #6E747B;
    font-size: 12px;
    padding: 0;
}
QPushButton#close:hover { background: rgba(255, 255, 255, 20); color: #FFFFFF; }
)QSS";

QLabel* fieldLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("field");
    return label;
}

QFrame* rule(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setObjectName("rule");
    line->setFrameShape(QFrame::NoFrame);
    return line;
}

}  // namespace

Menu::Menu(AppState& state, Game& game, std::string configPath)
    : QWidget(nullptr), state_(state), game_(game), path_(std::move(configPath)) {
    setWindowTitle("CS2 辅助 · 控制面板");
    // 无边框 + 透明底：整块面板由 paintEvent 画成圆角玻璃，WM 不再画标题栏。
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedWidth(440);
    setStyleSheet(kStyle);
    buildUi();

    auto timer = new QTimer(this);
    timer->setInterval(400);
    connect(timer, &QTimer::timeout, this, [this] {
        if (status_) status_->setText(QString::fromStdString(game_.status()));
    });
    timer->start();
}

void Menu::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    // 背景完全透明：不填充、不做高光，只留一圈 1px 圆角线框界定可拖动区域。
    painter.setPen(QPen(QColor(255, 255, 255, 56), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(panel, 14.0, 14.0);
}

void Menu::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (placed_) return;
    placed_ = true;
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        move(area.center() - QPoint(width() / 2, height() / 2));
    }
}

void Menu::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragOffset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
    QWidget::mousePressEvent(event);
}

void Menu::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - dragOffset_);
    }
    QWidget::mouseMoveEvent(event);
}

void Menu::buildUi() {
    const std::shared_ptr<Config> config = state_.config.load();
    const Config initial = config ? *config : Config{};

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(18, 15, 18, 15);

    // ---- 标题行 ----
    auto* header = new QHBoxLayout();
    header->setSpacing(6);
    auto* title = new QLabel("CS2 LITE", this);
    title->setObjectName("title");
    auto* closeButton = new QPushButton(QString::fromUtf8("✕"), this);
    closeButton->setObjectName("close");
    closeButton->setFixedSize(18, 18);
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QPushButton::clicked, this, [this] { close(); });
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(closeButton);
    root->addLayout(header);

    status_ = new QLabel(QString::fromStdString(game_.status()), this);
    status_->setObjectName("status");
    status_->setWordWrap(true);
    root->addWidget(status_);
    root->addWidget(rule(this));

    // ---- 主体：左侧类别导航 + 右侧内容页 ----
    auto* body = new QHBoxLayout();
    body->setSpacing(14);

    // 类别导航：QListWidget 的原生选中项即「当前分类」高亮，切换只换右侧页。
    auto* nav = new QListWidget(this);
    nav->setObjectName("nav");
    nav->setFixedWidth(88);
    nav->setFocusPolicy(Qt::NoFocus);
    nav->setFrameShape(QFrame::NoFrame);
    nav->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav->addItem("透 视");
    nav->addItem("自 瞄");
    nav->addItem("扳 机");

    auto* pages = new QStackedWidget(this);
    pages->setObjectName("pages");

    // ===== 页一：透视 =====
    auto* espPage = new QWidget(this);
    auto* espPageLayout = new QVBoxLayout(espPage);
    espPageLayout->setContentsMargins(0, 0, 0, 0);
    espPageLayout->setSpacing(8);

    auto* espGrid = new QGridLayout();
    espGrid->setHorizontalSpacing(10);
    espGrid->setVerticalSpacing(6);

    espEnabled_ = new QCheckBox("启用透视", this);
    espEnabled_->setChecked(initial.esp.enabled);
    espBox_ = new QCheckBox("方框", this);
    espBox_->setChecked(initial.esp.box);
    espHealth_ = new QCheckBox("血量条", this);
    espHealth_->setChecked(initial.esp.health);
    espHealthText_ = new QCheckBox("血量数值", this);
    espHealthText_->setChecked(initial.esp.healthText);
    espDistance_ = new QCheckBox("距离", this);
    espDistance_->setChecked(initial.esp.distance);
    espSkeleton_ = new QCheckBox("骨骼连线", this);
    espSkeleton_->setChecked(initial.esp.skeleton);
    espTeam_ = new QCheckBox("过滤队友", this);
    espTeam_->setChecked(initial.esp.teamCheck);
    espVisibility_ = new QCheckBox("遮挡虚线", this);
    espVisibility_->setChecked(initial.esp.visibilityCheck);

    espGrid->addWidget(espEnabled_, 0, 0);
    espGrid->addWidget(espBox_, 0, 1);
    espGrid->addWidget(espHealth_, 1, 0);
    espGrid->addWidget(espHealthText_, 1, 1);
    espGrid->addWidget(espDistance_, 2, 0);
    espGrid->addWidget(espSkeleton_, 2, 1);
    espGrid->addWidget(espTeam_, 3, 0);
    espGrid->addWidget(espVisibility_, 3, 1);
    espPageLayout->addLayout(espGrid);

    auto* radarRow = new QHBoxLayout();
    radarRow->setSpacing(8);
    espRadar_ = new QCheckBox("2D 雷达", this);
    espRadar_->setChecked(initial.esp.radar);
    espRadarRange_ = new QSpinBox(this);
    espRadarRange_->setRange(5, 300);
    espRadarRange_->setSuffix(" m");
    espRadarRange_->setValue(static_cast<int>(initial.esp.radarRange));
    espRadarRange_->setFixedWidth(74);
    radarRow->addWidget(espRadar_);
    radarRow->addStretch(1);
    radarRow->addWidget(fieldLabel("半径", this));
    radarRow->addWidget(espRadarRange_);
    espPageLayout->addLayout(radarRow);
    espPageLayout->addStretch(1);
    pages->addWidget(espPage);

    // ===== 页二：自瞄 =====
    auto* aimPage = new QWidget(this);
    auto* aimPageLayout = new QVBoxLayout(aimPage);
    aimPageLayout->setContentsMargins(0, 0, 0, 0);
    aimPageLayout->setSpacing(8);

    aimEnabled_ = new QCheckBox("启用自瞄", this);
    aimEnabled_->setChecked(initial.aim.enabled);
    aimTeam_ = new QCheckBox("过滤队友", this);
    aimTeam_->setChecked(initial.aim.teamCheck);
    aimStrategy_ = new QComboBox(this);
    aimStrategy_->addItem("屏幕最近");
    aimStrategy_->addItem("血量最低");
    aimStrategy_->addItem("世界最近");
    aimStrategy_->setCurrentIndex(std::clamp(initial.aim.strategy, 0, 2));
    aimSmooth_ = new QSpinBox(this);
    aimSmooth_->setRange(1, 40);
    aimSmooth_->setValue(initial.aim.smooth);
    aimFov_ = new QSpinBox(this);
    aimFov_->setRange(20, 1200);
    aimFov_->setSuffix(" px");
    aimFov_->setValue(static_cast<int>(initial.aim.fovPixels));
    aimSensitivity_ = new QDoubleSpinBox(this);
    aimSensitivity_->setRange(0.05, 3.0);
    aimSensitivity_->setSingleStep(0.05);
    aimSensitivity_->setValue(initial.aim.sensitivityFactor);

    // 数字框固定宽度、不参与拉伸：整列对齐靠左，避免宽输入框把标签挤到右侧。
    aimSmooth_->setFixedWidth(60);
    aimFov_->setFixedWidth(80);
    aimSensitivity_->setFixedWidth(80);

    auto* aimToggles = new QHBoxLayout();
    aimToggles->setSpacing(10);
    aimToggles->addWidget(aimEnabled_);
    aimToggles->addWidget(aimTeam_);
    aimToggles->addStretch(1);
    aimPageLayout->addLayout(aimToggles);

    auto* aimGrid = new QGridLayout();
    aimGrid->setHorizontalSpacing(8);
    aimGrid->setVerticalSpacing(6);
    aimGrid->addWidget(fieldLabel("策略", this), 0, 0);
    aimGrid->addWidget(aimStrategy_, 0, 1, 1, 4);
    aimGrid->addWidget(fieldLabel("平滑", this), 1, 0);
    aimGrid->addWidget(aimSmooth_, 1, 1);
    aimGrid->addWidget(fieldLabel("范围", this), 1, 2);
    aimGrid->addWidget(aimFov_, 1, 3);
    aimGrid->addWidget(fieldLabel("灵敏度", this), 2, 0);
    aimGrid->addWidget(aimSensitivity_, 2, 1);
    aimGrid->setColumnStretch(4, 1);
    aimPageLayout->addLayout(aimGrid);
    aimPageLayout->addStretch(1);
    pages->addWidget(aimPage);

    // ===== 页三：扳机 =====
    auto* triggerPage = new QWidget(this);
    auto* triggerPageLayout = new QVBoxLayout(triggerPage);
    triggerPageLayout->setContentsMargins(0, 0, 0, 0);
    triggerPageLayout->setSpacing(8);

    triggerEnabled_ = new QCheckBox("启用扳机", this);
    triggerEnabled_->setChecked(initial.trigger.enabled);
    triggerDelay_ = new QSpinBox(this);
    triggerDelay_->setRange(1, 500);
    triggerDelay_->setSuffix(" ms");
    triggerDelay_->setValue(initial.trigger.delayMs);
    triggerCooldown_ = new QSpinBox(this);
    triggerCooldown_->setRange(10, 2000);
    triggerCooldown_->setSuffix(" ms");
    triggerCooldown_->setValue(initial.trigger.cooldownMs);

    triggerDelay_->setFixedWidth(78);
    triggerCooldown_->setFixedWidth(78);

    triggerPageLayout->addWidget(triggerEnabled_);

    auto* triggerGrid = new QGridLayout();
    triggerGrid->setHorizontalSpacing(8);
    triggerGrid->setVerticalSpacing(6);
    triggerGrid->addWidget(fieldLabel("按住", this), 0, 0);
    triggerGrid->addWidget(triggerDelay_, 0, 1);
    triggerGrid->addWidget(fieldLabel("冷却", this), 0, 2);
    triggerGrid->addWidget(triggerCooldown_, 0, 3);
    triggerGrid->setColumnStretch(4, 1);
    triggerPageLayout->addLayout(triggerGrid);
    triggerPageLayout->addStretch(1);
    pages->addWidget(triggerPage);

    auto* divider = new QFrame(this);
    divider->setObjectName("vrule");
    divider->setFrameShape(QFrame::NoFrame);

    body->addWidget(nav);
    body->addWidget(divider);
    body->addWidget(pages, 1);
    root->addLayout(body, 1);

    connect(nav, &QListWidget::currentRowChanged, pages,
            &QStackedWidget::setCurrentIndex);
    nav->setCurrentRow(0);

    root->addWidget(rule(this));

    auto* hint = new QLabel(
        QString::fromStdString("偏移来源 " + initial.offsetsSource + " · 改动需重启"),
        this);
    hint->setObjectName("hint");
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto* applyButton = new QPushButton("应 用 并 保 存", this);
    applyButton->setObjectName("apply");
    applyButton->setCursor(Qt::PointingHandCursor);
    connect(applyButton, &QPushButton::clicked, this, &Menu::apply);
    root->addWidget(applyButton);
}

void Menu::apply() {
    const std::shared_ptr<Config> current = state_.config.load();
    Config next = current ? *current : Config{};

    next.esp.enabled = espEnabled_->isChecked();
    next.esp.box = espBox_->isChecked();
    next.esp.health = espHealth_->isChecked();
    next.esp.healthText = espHealthText_->isChecked();
    next.esp.distance = espDistance_->isChecked();
    next.esp.skeleton = espSkeleton_->isChecked();
    next.esp.teamCheck = espTeam_->isChecked();
    next.esp.visibilityCheck = espVisibility_->isChecked();
    next.esp.radar = espRadar_->isChecked();
    next.esp.radarRange = static_cast<float>(espRadarRange_->value());

    next.aim.enabled = aimEnabled_->isChecked();
    next.aim.strategy = aimStrategy_->currentIndex();
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
