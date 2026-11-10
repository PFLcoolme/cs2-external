#include "Menu.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
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
QFrame#rule { background: rgba(255, 255, 255, 70); border: none; min-height: 1px; max-height: 1px; }
QFrame#vrule { background: rgba(255, 255, 255, 52); border: none; min-width: 1px; max-width: 1px; }
QPushButton#tab {
    background: transparent;
    border: none;
    border-radius: 7px;
    color: #B4B9BF;
    font-size: 12px;
    letter-spacing: 2px;
    text-align: left;
    padding: 6px 8px;
}
QPushButton#tab:hover { background: rgba(255, 255, 255, 18); color: #F2F3F5; }
QPushButton#tab:checked { background: rgba(255, 255, 255, 235); color: #0F1113; }
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
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedWidth(440);
    setStyleSheet(kStyle);
    buildUi();

    configStamp_ = QFileInfo(QString::fromStdString(path_)).lastModified();

    auto timer = new QTimer(this);
    timer->setInterval(400);
    connect(timer, &QTimer::timeout, this, [this] {
        if (status_) status_->setText(QString::fromStdString(game_.status()));

        // 日志页可见时才读文件，其他页面零开销。
        if (pages_ && logView_ && pages_->currentIndex() == 3) {
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

    // 类别导航：四个互斥的可选中按钮，点击只切右侧分页。
    // 不用 QListWidget：列表项会被主体区高度裁掉，按钮列不会。
    auto* navCol = new QVBoxLayout();
    navCol->setSpacing(4);
    navCol->setContentsMargins(0, 0, 0, 0);
    const QStringList tabNames{"透 视", "自 瞄", "扳 机", "日 志"};
    for (int i = 0; i < tabNames.size(); ++i) {
        auto* tab = new QPushButton(tabNames.at(i), this);
        tab->setObjectName("tab");
        tab->setCheckable(true);
        tab->setAutoExclusive(true);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setFocusPolicy(Qt::NoFocus);
        tab->setFixedSize(88, 30);
        navCol->addWidget(tab);
        tabs_[i] = tab;
        connect(tab, &QPushButton::clicked, this, [this, i] { pages_->setCurrentIndex(i); });
    }
    navCol->addStretch(1);

    pages_ = new QStackedWidget(this);
    pages_->setObjectName("pages");

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
    espArmor_ = new QCheckBox("护甲数值", this);
    espArmor_->setChecked(initial.esp.armor);
    espDamage_ = new QCheckBox("伤害数值", this);
    espDamage_->setChecked(initial.esp.damageNumbers);
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
    espGrid->addWidget(espArmor_, 4, 0);
    espGrid->addWidget(espDamage_, 4, 1);
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
    pages_->addWidget(espPage);

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
    pages_->addWidget(aimPage);

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
    pages_->addWidget(triggerPage);

    // ===== 页四：日志 =====
    auto* logPage = new QWidget(this);
    auto* logLayout = new QVBoxLayout(logPage);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(6);
    logView_ = new QPlainTextEdit(logPage);
    logView_->setObjectName("log");
    logView_->setReadOnly(true);
    logView_->setFrameShape(QFrame::NoFrame);
    logView_->setPlainText(tailOfLog("/tmp/wv_lite.log"));
    logLayout->addWidget(logView_, 1);
    pages_->addWidget(logPage);

    auto* divider = new QFrame(this);
    divider->setObjectName("vrule");
    divider->setFrameShape(QFrame::NoFrame);

    body->addLayout(navCol);
    body->addWidget(divider);
    body->addWidget(pages_, 1);
    root->addLayout(body, 1);

    tabs_[0]->setChecked(true);
    pages_->setCurrentIndex(0);

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

    next.aim.enabled = aimEnabled_->isChecked();
    next.aim.strategy = aimStrategy_->currentIndex();
    next.aim.smooth = aimSmooth_->value();
    next.aim.fovPixels = static_cast<float>(aimFov_->value());
    next.aim.sensitivityFactor = static_cast<float>(aimSensitivity_->value());
    next.aim.teamCheck = aimTeam_->isChecked();

    next.trigger.enabled = triggerEnabled_->isChecked();
    next.trigger.delayMs = triggerDelay_->value();
    next.trigger.cooldownMs = triggerCooldown_->value();

    persist(next, "已保存并生效");
}

void Menu::persist(const Config& config, const QString& message) {
    const QString target = QString::fromStdString(path_);
    const QString backup = backupPath(path_);

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

    aimEnabled_->setChecked(config.aim.enabled);
    aimStrategy_->setCurrentIndex(std::clamp(config.aim.strategy, 0, 2));
    aimSmooth_->setValue(config.aim.smooth);
    aimFov_->setValue(static_cast<int>(config.aim.fovPixels));
    aimSensitivity_->setValue(config.aim.sensitivityFactor);
    aimTeam_->setChecked(config.aim.teamCheck);

    triggerEnabled_->setChecked(config.trigger.enabled);
    triggerDelay_->setValue(config.trigger.delayMs);
    triggerCooldown_->setValue(config.trigger.cooldownMs);
}

}  // namespace wv
