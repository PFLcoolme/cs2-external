#include "Menu.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
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
QLabel#warn { color: #F2F3F5; font-size: 10px; font-weight: 700; }
QWidget#row { border-radius: 6px; }
QWidget#row:hover { background: rgba(255, 255, 255, 16); }
QWidget#popup {
    background: #16191E;
    border: 1px solid rgba(255, 255, 255, 40);
}
QFrame#rule { background: rgba(255, 255, 255, 70); border: none; min-height: 1px; max-height: 1px; }
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
QPushButton#restore {
    background: transparent;
    color: #A8AEB5;
    border: 1px solid rgba(255, 255, 255, 50);
    border-radius: 8px;
    padding: 8px 10px;
    font-size: 11px;
    letter-spacing: 2px;
}
QPushButton#restore:hover { background: rgba(255, 255, 255, 30); color: #F2F3F5; border-color: rgba(255, 255, 255, 110); }
QPushButton#restore:pressed { background: rgba(255, 255, 255, 70); color: #0F1113; }
QPushButton#apply:pressed { background: rgba(255, 255, 255, 180); color: #0F1113; }
QPushButton#tab {
    background: rgba(255, 255, 255, 12);
    border: 1px solid rgba(255, 255, 255, 26);
    border-radius: 7px;
    color: #B4B9BF;
    font-size: 12px;
    letter-spacing: 2px;
    padding: 0 6px;
}
QPushButton#tab:hover { background: rgba(255, 255, 255, 28); color: #F2F3F5; }
QPushButton#tab:checked { background: rgba(255, 255, 255, 235); color: #0F1113; border-color: transparent; }
QPushButton#close {
    background: transparent;
    border: 0;
    border-radius: 9px;
    color: #6E747B;
    font-size: 12px;
    padding: 0;
}
QPushButton#close:hover { background: rgba(255, 255, 255, 20); color: #FFFFFF; }
QPlainTextEdit#log {
    background: transparent;
    border: none;
    color: #A9AFB6;
    font-family: "DejaVu Sans Mono", monospace;
    font-size: 9px;
    selection-background-color: rgba(255, 255, 255, 235);
    selection-color: #0F1113;
}
QScrollBar:vertical {
    background: transparent;
    width: 4px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: rgba(255, 255, 255, 60);
    border-radius: 2px;
    min-height: 16px;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)QSS";

// 鼠标键下拉：X11 核心协议只暴露 Button1~Button5，侧键不在掩码内，
// 因此只提供 不限 / 左键 / 中键 / 右键，避免给出按键位却读不到状态。
QComboBox* mouseKeyCombo(QWidget* parent, int value) {
    auto* combo = new QComboBox(parent);
    combo->addItem("不限", 0);
    combo->addItem("左键", 1);
    combo->addItem("中键", 2);
    combo->addItem("右键", 3);
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : 0);
    combo->setFixedWidth(78);
    return combo;
}

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

// 参数弹层里的一行：左标签、右控件。
QWidget* paramRow(const QString& text, QWidget* control, QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(fieldLabel(text, row));
    layout->addStretch(1);
    layout->addWidget(control);
    return row;
}

// 上一次配置的备份路径：与主配置同目录，每次保存前主配置会被复制到这里。
QString backupPath(const std::string& path) {
    return QString::fromStdString(path) + ".bak";
}

// 读取诊断日志尾部：只 seek 到末尾 8 KB，日志变长后也不拖慢 UI 线程。
QString tailOfLog(const QString& path, int maxBytes = 8192, int maxLines = 16) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return "（暂无日志）";
    const qint64 size = file.size();
    if (size <= 0) return "（暂无日志）";
    if (size > maxBytes) file.seek(size - maxBytes);
    QString text = QString::fromUtf8(file.readAll());
    if (size > maxBytes) {
        const int cut = text.indexOf('\n');
        if (cut >= 0) text = text.mid(cut + 1);
    }
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    return lines.mid(std::max(0, static_cast<int>(lines.size()) - maxLines)).join('\n');
}

}  // namespace

Menu::Menu(AppState& state, Game& game, std::string configPath)
    : QWidget(nullptr), state_(state), game_(game), path_(std::move(configPath)) {
    setWindowTitle("CS2 辅助 · 控制面板");
    // 无边框 + 透明底：整块面板由 paintEvent 画成圆角玻璃，WM 不再画标题栏。
    // 无边框后没有标题栏可点回前台，必须自己置顶，否则被 IDE / 浏览器整块盖住。
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedWidth(440);
    setStyleSheet(kStyle);
    buildUi();

    configStamp_ = QFileInfo(QString::fromStdString(path_)).lastModified();

    auto timer = new QTimer(this);
    timer->setInterval(400);
    connect(timer, &QTimer::timeout, this, [this] {
        if (status_ && QDateTime::currentDateTime() >= messageUntil_) {
            status_->setText(QString::fromStdString(game_.status()));
        }

        // 日志区可见时才读文件，滚出视野后零开销。
        if (logView_ && logView_->isVisible()) {
            logView_->setPlainText(tailOfLog("/tmp/wv_lite.log"));
            logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
        }

        // 配置文件热重载：外部编辑（或换版本后重新生成）只要 mtime 变了就重新读取，
        // 无需重启进程。写入方是本面板时 apply() 会同步更新 configStamp_。
        const QDateTime stamp = QFileInfo(QString::fromStdString(path_)).lastModified();
        if (!stamp.isValid() || stamp == configStamp_) return;
        configStamp_ = stamp;
        const Config reloaded = Config::load(path_);
        state_.config.store(std::make_shared<Config>(reloaded));
        syncFrom(reloaded);
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

    // 延后一轮事件循环再居中：showEvent 里布局还没完成，此时 height() 不是最终值。
    QTimer::singleShot(0, this, [this] {
        if (const QScreen* screen = QGuiApplication::primaryScreen()) {
            const QRect area = screen->availableGeometry();
            move(area.center() - QPoint(width() / 2, height() / 2));
        }
    });
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

bool Menu::eventFilter(QObject* watched, QEvent* event) {
    // 参数弹层一关就落盘：右键调完即生效，不必再回到底部点保存。
    if (event->type() == QEvent::Hide &&
        popups_.values().contains(static_cast<QWidget*>(watched))) {
        apply();
    }
    return QWidget::eventFilter(watched, event);
}

QWidget* Menu::makeRow(QCheckBox* toggle, const QString& name, const QString& hint) {
    auto* row = new QWidget(this);
    row->setObjectName("row");
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 3, 8, 3);
    layout->setSpacing(8);

    toggle->setText(name);
    toggle->setCursor(Qt::PointingHandCursor);
    layout->addWidget(toggle);
    layout->addStretch(1);

    auto* hintLabel = new QLabel(hint, row);
    hintLabel->setObjectName("hint");
    layout->addWidget(hintLabel);
    return row;
}

void Menu::attachParams(QWidget* row, QCheckBox* toggle, const QString& title,
                        const std::function<void(QVBoxLayout*)>& build) {
    popupBuilders_.insert(title, build);
    // 右键在行内任意位置都能出弹层：勾选框本身也要单独接一次，
    // 否则点在文字上时事件被 QCheckBox 吃掉，父行收不到右键。
    for (QWidget* target : {row, static_cast<QWidget*>(toggle)}) {
        target->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(target, &QWidget::customContextMenuRequested, this,
                [this, row, title](const QPoint& pos) {
                    QWidget* popup = ensurePopup(title);
                    popup->move(row->mapToGlobal(pos));
                    popup->show();
                    popup->raise();
                });
    }
}

QWidget* Menu::ensurePopup(const QString& title) {
    if (QWidget* existing = popups_.value(title, nullptr)) return existing;

    auto* popup = new QWidget(this, Qt::Popup);
    popup->setObjectName("popup");
    popup->setAttribute(Qt::WA_StyledBackground, true);
    popup->setStyleSheet(kStyle);
    popup->setMinimumWidth(258);

    auto* layout = new QVBoxLayout(popup);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    auto* heading = new QLabel(title, popup);
    heading->setObjectName("field");
    layout->addWidget(heading);

    if (const auto build = popupBuilders_.value(title); build) build(layout);

    auto* foot = new QLabel("松开即生效", popup);
    foot->setObjectName("hint");
    layout->addWidget(foot);

    popup->installEventFilter(this);
    popups_.insert(title, popup);
    return popup;
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

    // ---- 主体：顶部横向类别栏 + 单行功能表 ----
    // 类别按钮横排铺满一行，避免左侧竖直堆放时挤成一团。
    auto* navBar = new QHBoxLayout();
    navBar->setSpacing(6);
    navBar->setContentsMargins(0, 0, 0, 0);
    const QStringList tabNames{"透 视", "自 瞄", "扳 机", "日 志"};
    for (int i = 0; i < tabNames.size(); ++i) {
        auto* tab = new QPushButton(tabNames.at(i), this);
        tab->setObjectName("tab");
        tab->setCheckable(true);
        tab->setAutoExclusive(true);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setFocusPolicy(Qt::NoFocus);
        tab->setFixedHeight(30);
        tab->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        navBar->addWidget(tab, 1);
        tabs_[i] = tab;
        connect(tab, &QPushButton::clicked, this, [this, i] { pages_->setCurrentIndex(i); });
    }
    if (tabs_[0]) tabs_[0]->setChecked(true);

    pages_ = new QStackedWidget(this);
    pages_->setObjectName("pages");

    root->addLayout(navBar);
    root->addSpacing(8);
    root->addWidget(pages_, 1);

    // 每页一个竖排行表：行高固定，内容超出时页内滚动。
    auto addPage = [this]() -> QVBoxLayout* {
        auto* page = new QWidget(this);
        auto* outer = new QVBoxLayout(page);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        auto* scroll = new QScrollArea(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // 行表是本面板的主体：给一个下限高度，避免 QStackedWidget 按空页压缩整窗。
        scroll->setMinimumHeight(330);
        auto* list = new QWidget(scroll);
        auto* listLayout = new QVBoxLayout(list);
        listLayout->setContentsMargins(0, 0, 6, 0);
        listLayout->setSpacing(2);
        scroll->setWidget(list);

        outer->addWidget(scroll);
        pages_->addWidget(page);
        return listLayout;
    };

    // ---- 控件构造（参数控件不在页面里，只在右键弹层里出现一次）----
    espEnabled_ = new QCheckBox(this);
    espEnabled_->setChecked(initial.esp.enabled);
    espBox_ = new QCheckBox(this);
    espBox_->setChecked(initial.esp.box);
    espHealth_ = new QCheckBox(this);
    espHealth_->setChecked(initial.esp.health);
    espHealthText_ = new QCheckBox(this);
    espHealthText_->setChecked(initial.esp.healthText);
    espArmor_ = new QCheckBox(this);
    espArmor_->setChecked(initial.esp.armor);
    espDamage_ = new QCheckBox(this);
    espDamage_->setChecked(initial.esp.damageNumbers);
    espDistance_ = new QCheckBox(this);
    espDistance_->setChecked(initial.esp.distance);
    espSkeleton_ = new QCheckBox(this);
    espSkeleton_->setChecked(initial.esp.skeleton);
    espTeam_ = new QCheckBox(this);
    espTeam_->setChecked(initial.esp.teamCheck);
    espVisibility_ = new QCheckBox(this);
    espVisibility_->setChecked(initial.esp.visibilityCheck);

    espRadar_ = new QCheckBox(this);
    espRadar_->setChecked(initial.esp.radar);
    espRadarRange_ = new QSpinBox(this);
    espRadarRange_->setRange(5, 300);
    espRadarRange_->setSuffix(" m");
    espRadarRange_->setValue(static_cast<int>(initial.esp.radarRange));
    espRadarRange_->setFixedWidth(78);

    espTrail_ = new QCheckBox(this);
    espTrail_->setChecked(initial.esp.trail);
    espTrailMs_ = new QSpinBox(this);
    espTrailMs_->setRange(50, 1000);
    espTrailMs_->setSuffix(" ms");
    espTrailMs_->setValue(initial.esp.trailMs);
    espTrailMs_->setFixedWidth(84);

    espDamageMs_ = new QSpinBox(this);
    espDamageMs_->setRange(100, 3000);
    espDamageMs_->setSuffix(" ms");
    espDamageMs_->setValue(initial.esp.damageMs);
    espDamageMs_->setFixedWidth(88);
    espDamageColor_ = new QComboBox(this);
    espDamageColor_->addItem("橙红", 0xFF8460);
    espDamageColor_->addItem("纯白", 0xFFFFFF);
    espDamageColor_->addItem("亮黄", 0xFFD24A);
    espDamageColor_->addItem("青蓝", 0x6CC8FF);
    const int colorIndex = espDamageColor_->findData(initial.esp.damageColor);
    espDamageColor_->setCurrentIndex(colorIndex >= 0 ? colorIndex : 0);
    espDamageColor_->setFixedWidth(84);

    espHighlight_ = new QCheckBox(this);
    espHighlight_->setChecked(initial.esp.highlight);

    aimEnabled_ = new QCheckBox(this);
    aimEnabled_->setChecked(initial.aim.enabled);
    aimTeam_ = new QCheckBox(this);
    aimTeam_->setChecked(initial.aim.teamCheck);
    aimStrategy_ = new QComboBox(this);
    aimStrategy_->addItem("屏幕最近");
    aimStrategy_->addItem("血量最低");
    aimStrategy_->addItem("世界最近");
    aimStrategy_->setCurrentIndex(std::clamp(initial.aim.strategy, 0, 2));
    aimStrategy_->setFixedWidth(120);
    aimSmooth_ = new QSpinBox(this);
    aimSmooth_->setRange(1, 40);
    aimSmooth_->setValue(initial.aim.smooth);
    aimSmooth_->setFixedWidth(66);
    aimFov_ = new QSpinBox(this);
    aimFov_->setRange(20, 1200);
    aimFov_->setSuffix(" px");
    aimFov_->setValue(static_cast<int>(initial.aim.fovPixels));
    aimFov_->setFixedWidth(84);
    aimSensitivity_ = new QDoubleSpinBox(this);
    aimSensitivity_->setRange(0.05, 3.0);
    aimSensitivity_->setSingleStep(0.05);
    aimSensitivity_->setValue(initial.aim.sensitivityFactor);
    aimSensitivity_->setFixedWidth(84);
    aimScopeKey_ = mouseKeyCombo(this, initial.aim.scopeKey);
    aimFireKey_ = mouseKeyCombo(this, initial.aim.fireKey);

    recoilEnabled_ = new QCheckBox(this);
    recoilEnabled_->setChecked(initial.recoil.enabled);
    recoilRequireFire_ = new QCheckBox(this);
    recoilRequireFire_->setChecked(initial.recoil.requireFire);
    recoilStrength_ = new QDoubleSpinBox(this);
    recoilStrength_->setRange(-3.0, 3.0);
    recoilStrength_->setSingleStep(0.1);
    recoilStrength_->setDecimals(2);
    recoilStrength_->setValue(initial.recoil.strength);
    recoilStrength_->setFixedWidth(74);
    recoilMax_ = new QSpinBox(this);
    recoilMax_->setRange(1, 60);
    recoilMax_->setSuffix(" °");
    recoilMax_->setValue(static_cast<int>(initial.recoil.maxDegrees));
    recoilMax_->setFixedWidth(74);

    trackEnabled_ = new QCheckBox(this);
    trackEnabled_->setChecked(initial.track.enabled);
    trackTeam_ = new QCheckBox(this);
    trackTeam_->setChecked(initial.track.teamCheck);
    trackFov_ = new QSpinBox(this);
    trackFov_->setRange(0, 2000);
    trackFov_->setSpecialValueText("全屏");
    trackFov_->setSuffix(" px");
    trackFov_->setValue(static_cast<int>(initial.track.fovPixels));
    trackFov_->setFixedWidth(92);
    trackSmooth_ = new QSpinBox(this);
    trackSmooth_->setRange(1, 20);
    trackSmooth_->setValue(initial.track.smooth);
    trackSmooth_->setFixedWidth(66);
    trackKey_ = mouseKeyCombo(this, initial.track.key);

    triggerEnabled_ = new QCheckBox(this);
    triggerEnabled_->setChecked(initial.trigger.enabled);
    triggerDelay_ = new QSpinBox(this);
    triggerDelay_->setRange(1, 500);
    triggerDelay_->setSuffix(" ms");
    triggerDelay_->setValue(initial.trigger.delayMs);
    triggerDelay_->setFixedWidth(82);
    triggerCooldown_ = new QSpinBox(this);
    triggerCooldown_->setRange(10, 2000);
    triggerCooldown_->setSuffix(" ms");
    triggerCooldown_->setValue(initial.trigger.cooldownMs);
    triggerCooldown_->setFixedWidth(82);
    triggerKey_ = mouseKeyCombo(this, initial.trigger.key);

    silentEnabled_ = new QCheckBox(this);
    silentEnabled_->setChecked(initial.silent.enabled);
    silentTeam_ = new QCheckBox(this);
    silentTeam_->setChecked(initial.silent.teamCheck);
    silentFov_ = new QSpinBox(this);
    silentFov_->setRange(0, 2000);
    silentFov_->setSpecialValueText("全屏");
    silentFov_->setSuffix(" px");
    silentFov_->setValue(static_cast<int>(initial.silent.fovPixels));
    silentFov_->setFixedWidth(92);
    silentHold_ = new QSpinBox(this);
    silentHold_->setRange(5, 200);
    silentHold_->setSuffix(" ms");
    silentHold_->setValue(initial.silent.holdMs);
    silentHold_->setFixedWidth(82);
    silentKey_ = mouseKeyCombo(this, initial.silent.key);

    // ---- 页一：透视 ----
    QVBoxLayout* espLayout = addPage();
    espLayout->addWidget(makeRow(espEnabled_, "玩家透视", ""));
    espLayout->addWidget(makeRow(espBox_, "方框", ""));
    espLayout->addWidget(makeRow(espHealth_, "血量条", ""));
    espLayout->addWidget(makeRow(espHealthText_, "血量数值", ""));
    espLayout->addWidget(makeRow(espArmor_, "护甲数值", ""));
    espLayout->addWidget(makeRow(espDistance_, "距离", ""));
    espLayout->addWidget(makeRow(espSkeleton_, "骨骼连线", ""));
    espLayout->addWidget(makeRow(espVisibility_, "遮挡虚线", ""));
    espLayout->addWidget(makeRow(espTeam_, "过滤队友", ""));
    {
        QWidget* row = makeRow(espDamage_, "伤害浮字", "右键参数");
        attachParams(row, espDamage_, "伤害浮字", [this, row](QVBoxLayout* layout) {
            layout->addWidget(paramRow("显示时长", espDamageMs_, row));
            layout->addWidget(paramRow("颜色", espDamageColor_, row));
        });
        espLayout->addWidget(row);
    }
    {
        QWidget* row = makeRow(espTrail_, "残影轨迹", "右键参数");
        attachParams(row, espTrail_, "残影轨迹", [this, row](QVBoxLayout* layout) {
            layout->addWidget(paramRow("拖尾时长", espTrailMs_, row));
        });
        espLayout->addWidget(row);
    }
    espLayout->addWidget(makeRow(espHighlight_, "残血描边", ""));
    {
        QWidget* row = makeRow(espRadar_, "2D 雷达", "右键参数");
        attachParams(row, espRadar_, "2D 雷达", [this, row](QVBoxLayout* layout) {
            layout->addWidget(paramRow("半径", espRadarRange_, row));
        });
        espLayout->addWidget(row);
    }
    espLayout->addStretch(1);

    // ---- 页二：自瞄 ----
    QVBoxLayout* aimLayout = addPage();
    {
        QWidget* row = makeRow(aimEnabled_, "自瞄", "右键参数");
        attachParams(row, aimEnabled_, "自瞄参数", [this, row](QVBoxLayout* layout) {
            layout->addWidget(paramRow("策略", aimStrategy_, row));
            layout->addWidget(paramRow("平滑", aimSmooth_, row));
            layout->addWidget(paramRow("范围", aimFov_, row));
            layout->addWidget(paramRow("灵敏度", aimSensitivity_, row));
            layout->addWidget(paramRow("开镜键", aimScopeKey_, row));
            layout->addWidget(paramRow("开火键", aimFireKey_, row));
            layout->addWidget(paramRow("过滤队友", aimTeam_, row));
        });
        aimLayout->addWidget(row);
    }
    {
        QWidget* row = makeRow(recoilEnabled_, "后坐力抵消", "右键参数");
        attachParams(row, recoilEnabled_, "后坐力抵消", [this, row](QVBoxLayout* layout) {
            layout->addWidget(paramRow("强度", recoilStrength_, row));
            layout->addWidget(paramRow("单帧上限", recoilMax_, row));
            layout->addWidget(paramRow("仅开火时", recoilRequireFire_, row));
        });
        aimLayout->addWidget(row);
    }
    {
        QWidget* row = makeRow(trackEnabled_, "子弹追踪", "右键参数");
        attachParams(row, trackEnabled_, "子弹追踪", [this, row](QVBoxLayout* layout) {
            layout->addWidget(paramRow("半径", trackFov_, row));
            layout->addWidget(paramRow("平滑", trackSmooth_, row));
            layout->addWidget(paramRow("触发键", trackKey_, row));
            layout->addWidget(paramRow("过滤队友", trackTeam_, row));
        });
        aimLayout->addWidget(row);
    }
    aimLayout->addStretch(1);

    // ---- 页三：扳机 ----
    QVBoxLayout* triggerLayout = addPage();
    {
        QWidget* row = makeRow(triggerEnabled_, "自动扳机", "右键参数");
        attachParams(row, triggerEnabled_, "自动扳机", [this, row](QVBoxLayout* layout) {
            layout->addWidget(paramRow("按住", triggerDelay_, row));
            layout->addWidget(paramRow("冷却", triggerCooldown_, row));
            layout->addWidget(paramRow("触发键", triggerKey_, row));
        });
        triggerLayout->addWidget(row);
    }
    {
        QWidget* row = makeRow(silentEnabled_, "静默锁头", "右键参数");
        attachParams(row, silentEnabled_, "静默锁头", [this, row](QVBoxLayout* layout) {
            layout->addWidget(paramRow("半径", silentFov_, row));
            layout->addWidget(paramRow("停留", silentHold_, row));
            layout->addWidget(paramRow("触发键", silentKey_, row));
            layout->addWidget(paramRow("过滤队友", silentTeam_, row));
        });
        triggerLayout->addWidget(row);
    }
    triggerLayout->addStretch(1);

    // ---- 页四：日志 ----
    QVBoxLayout* logLayout = addPage();
    logView_ = new QPlainTextEdit(this);
    logView_->setObjectName("log");
    logView_->setReadOnly(true);
    logView_->setFrameShape(QFrame::NoFrame);
    logView_->setMinimumHeight(200);
    logView_->setPlainText(tailOfLog("/tmp/wv_lite.log"));
    logLayout->addWidget(logView_);
    logLayout->addStretch(1);

    root->addWidget(rule(this));

    auto* hint = new QLabel(this);
    hint->setWordWrap(true);
    // 偏移守卫：关键偏移为 0 / 特征码缺失时明示，避免「面板能开但什么都不显示」。
    QStringList missing;
    const Offsets& offsets = initial.offsets;
    if (!offsets.entityListReady()) missing << "实体列表";
    if (!offsets.localPawnReady()) missing << "本地玩家";
    if (!offsets.viewMatrixReady()) missing << "视图矩阵";
    if (offsets.m_iHealth == 0) missing << "血量";
    if (offsets.m_iTeamNum == 0) missing << "阵营";
    if (offsets.m_pGameSceneNode == 0 || offsets.m_vecAbsOrigin == 0) missing << "坐标";
    if (missing.isEmpty()) {
        hint->setObjectName("hint");
        hint->setText(QString::fromStdString("偏移来源 " + initial.offsetsSource + " · 偏移改动需重启"));
    } else {
        hint->setObjectName("warn");
        hint->setText("偏移缺失：" + missing.join(" / ") +
                      " · 透视与自瞄不会生效，先运行 tools/cs2_linux_offsets.py");
    }
    root->addWidget(hint);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(10);

    auto* restoreButton = new QPushButton("恢 复 上 一 次", this);
    restoreButton->setObjectName("restore");
    restoreButton->setCursor(Qt::PointingHandCursor);
    connect(restoreButton, &QPushButton::clicked, this, &Menu::restoreLast);

    auto* applyButton = new QPushButton("应 用 并 保 存", this);
    applyButton->setObjectName("apply");
    applyButton->setCursor(Qt::PointingHandCursor);
    connect(applyButton, &QPushButton::clicked, this, &Menu::apply);

    actions->addWidget(restoreButton, 1);
    actions->addWidget(applyButton, 2);
    root->addLayout(actions);
}

void Menu::apply() {
    const std::shared_ptr<Config> current = state_.config.load();
    Config next = current ? *current : Config{};

    next.esp.enabled = espEnabled_->isChecked();
    next.esp.box = espBox_->isChecked();
    next.esp.health = espHealth_->isChecked();
    next.esp.healthText = espHealthText_->isChecked();
    next.esp.armor = espArmor_->isChecked();
    next.esp.damageNumbers = espDamage_->isChecked();
    next.esp.distance = espDistance_->isChecked();
    next.esp.skeleton = espSkeleton_->isChecked();
    next.esp.teamCheck = espTeam_->isChecked();
    next.esp.visibilityCheck = espVisibility_->isChecked();
    next.esp.radar = espRadar_->isChecked();
    next.esp.radarRange = static_cast<float>(espRadarRange_->value());
    next.esp.trail = espTrail_->isChecked();
    next.esp.trailMs = espTrailMs_->value();
    next.esp.highlight = espHighlight_->isChecked();
    next.esp.damageMs = espDamageMs_->value();
    next.esp.damageColor = espDamageColor_->currentData().toInt();

    next.aim.enabled = aimEnabled_->isChecked();
    next.aim.strategy = aimStrategy_->currentIndex();
    next.aim.smooth = aimSmooth_->value();
    next.aim.fovPixels = static_cast<float>(aimFov_->value());
    next.aim.sensitivityFactor = static_cast<float>(aimSensitivity_->value());
    next.aim.teamCheck = aimTeam_->isChecked();
    next.aim.scopeKey = aimScopeKey_->currentData().toInt();
    next.aim.fireKey = aimFireKey_->currentData().toInt();

    next.recoil.enabled = recoilEnabled_->isChecked();
    next.recoil.requireFire = recoilRequireFire_->isChecked();
    next.recoil.strength = static_cast<float>(recoilStrength_->value());
    next.recoil.maxDegrees = static_cast<float>(recoilMax_->value());

    next.track.enabled = trackEnabled_->isChecked();
    next.track.teamCheck = trackTeam_->isChecked();
    next.track.fovPixels = static_cast<float>(trackFov_->value());
    next.track.smooth = trackSmooth_->value();
    next.track.key = trackKey_->currentData().toInt();

    next.silent.enabled = silentEnabled_->isChecked();
    next.silent.teamCheck = silentTeam_->isChecked();
    next.silent.fovPixels = static_cast<float>(silentFov_->value());
    next.silent.holdMs = silentHold_->value();
    next.silent.key = silentKey_->currentData().toInt();

    next.trigger.enabled = triggerEnabled_->isChecked();
    next.trigger.delayMs = triggerDelay_->value();
    next.trigger.cooldownMs = triggerCooldown_->value();
    next.trigger.key = triggerKey_->currentData().toInt();

    // 后坐力依赖 punch 角度：两个偏移都缺失时明确告知，不然用户只会看到「没反应」。
    const bool punchReady =
        next.offsets.m_pAimPunchServices != 0 && next.offsets.m_aimPunchAngle != 0;
    if (next.recoil.enabled && !punchReady) {
        persist(next, "已保存 · 后坐力偏移缺失，先跑 tools/cs2_linux_offsets.py");
        return;
    }
    persist(next, "已保存并生效");
}

void Menu::persist(const Config& config, const QString& message) {
    const QString target = QString::fromStdString(path_);
    const QString backup = backupPath(path_);

    // 三个全局偏移是全功能的根：任一为空说明这份配置读不到偏移，
    // 落盘会把整份 config.json 的 offsets 归零，下次启动直接全失效。
    if (config.offsets.dwEntityList == 0 || config.offsets.dwLocalPlayerPawn == 0 ||
        config.offsets.dwViewMatrix == 0) {
        if (status_) status_->setText("偏移缺失，未保存 · 先跑 tools/cs2_linux_offsets.py");
        return;
    }

    // 覆盖前把现有配置留作「上一次」：QFile::copy 不覆盖已有文件，先删旧备份。
    if (QFile::exists(target)) {
        QFile::remove(backup);
        QFile::copy(target, backup);
    }

    config.save(path_);
    state_.config.store(std::make_shared<Config>(config));
    // 自己写盘后同步时间戳，避免热重载把刚刚保存的配置再读一遍。
    configStamp_ = QFileInfo(target).lastModified();

    if (status_) status_->setText(message);
    messageUntil_ = QDateTime::currentDateTime().addSecs(4);
}

void Menu::restoreLast() {
    const QString backup = backupPath(path_);

    QFile file(backup);
    if (!file.open(QIODevice::ReadOnly)) {
        if (status_) status_->setText("没有上一次保存的配置");
        return;
    }
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        if (status_) status_->setText("上一次保存的配置已损坏");
        return;
    }

    // persist 会先把当前配置备份为新的 .bak，因此再点一次即可切回。
    const Config restored = Config::load(backup.toStdString());
    syncFrom(restored);
    persist(restored, "已恢复上一次保存");
}

void Menu::syncFrom(const Config& config) {
    espEnabled_->setChecked(config.esp.enabled);
    espBox_->setChecked(config.esp.box);
    espHealth_->setChecked(config.esp.health);
    espHealthText_->setChecked(config.esp.healthText);
    espArmor_->setChecked(config.esp.armor);
    espDamage_->setChecked(config.esp.damageNumbers);
    espDistance_->setChecked(config.esp.distance);
    espSkeleton_->setChecked(config.esp.skeleton);
    espTeam_->setChecked(config.esp.teamCheck);
    espVisibility_->setChecked(config.esp.visibilityCheck);
    espRadar_->setChecked(config.esp.radar);
    espRadarRange_->setValue(static_cast<int>(config.esp.radarRange));
    espTrail_->setChecked(config.esp.trail);
    espTrailMs_->setValue(config.esp.trailMs);
    espHighlight_->setChecked(config.esp.highlight);
    espDamageMs_->setValue(config.esp.damageMs);
    const int colorIndex = espDamageColor_->findData(config.esp.damageColor);
    espDamageColor_->setCurrentIndex(colorIndex >= 0 ? colorIndex : 0);

    aimEnabled_->setChecked(config.aim.enabled);
    aimStrategy_->setCurrentIndex(std::clamp(config.aim.strategy, 0, 2));
    aimSmooth_->setValue(config.aim.smooth);
    aimFov_->setValue(static_cast<int>(config.aim.fovPixels));
    aimSensitivity_->setValue(config.aim.sensitivityFactor);
    aimTeam_->setChecked(config.aim.teamCheck);
    const int scopeIndex = aimScopeKey_->findData(config.aim.scopeKey);
    aimScopeKey_->setCurrentIndex(scopeIndex >= 0 ? scopeIndex : 0);
    const int fireIndex = aimFireKey_->findData(config.aim.fireKey);
    aimFireKey_->setCurrentIndex(fireIndex >= 0 ? fireIndex : 0);

    recoilEnabled_->setChecked(config.recoil.enabled);
    recoilRequireFire_->setChecked(config.recoil.requireFire);
    recoilStrength_->setValue(config.recoil.strength);
    recoilMax_->setValue(static_cast<int>(config.recoil.maxDegrees));

    trackEnabled_->setChecked(config.track.enabled);
    trackTeam_->setChecked(config.track.teamCheck);
    trackFov_->setValue(static_cast<int>(config.track.fovPixels));
    trackSmooth_->setValue(config.track.smooth);
    const int trackKeyIndex = trackKey_->findData(config.track.key);
    trackKey_->setCurrentIndex(trackKeyIndex >= 0 ? trackKeyIndex : 0);

    silentEnabled_->setChecked(config.silent.enabled);
    silentTeam_->setChecked(config.silent.teamCheck);
    silentFov_->setValue(static_cast<int>(config.silent.fovPixels));
    silentHold_->setValue(config.silent.holdMs);
    const int silentKeyIndex = silentKey_->findData(config.silent.key);
    silentKey_->setCurrentIndex(silentKeyIndex >= 0 ? silentKeyIndex : 0);

    triggerEnabled_->setChecked(config.trigger.enabled);
    triggerDelay_->setValue(config.trigger.delayMs);
    triggerCooldown_->setValue(config.trigger.cooldownMs);
    const int keyIndex = triggerKey_->findData(config.trigger.key);
    triggerKey_->setCurrentIndex(keyIndex >= 0 ? keyIndex : 0);
}

}  // namespace wv
