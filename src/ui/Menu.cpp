#include "Menu.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QEasingCurve>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QRegion>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <QMoveEvent>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

// X11 与 Qt 的符号冲突：Qt 头文件已在上方解析完毕，这里解除宏污染。
#undef None
#undef Bool
#undef Status
#undef CursorShape
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#undef FontChange
#undef Expose

#include <algorithm>
#include <cstdio>

namespace wv {
namespace {

// 玻璃底板的不透明度（0-255）：越低背后画面透出越明显，深色文字的对比度也越低。
// 188 让面板接近雾面白玻璃：背景仍有变化感，但穿透的细节已经淡到不干扰阅读。
constexpr int kGlassAlpha = 188;

// 面板视觉：无边框半透明玻璃底板（paintEvent 绘制）+ 极简控件。
// 浅色玻璃：底板用近白，文字与线条走深灰；只有描边和阴影压一点灰度，不用彩色强调。
const char* kStyle = R"QSS(
QWidget {
    background: transparent;
    color: #1F2328;
    font-family: "Noto Sans CJK SC", "Source Han Sans SC", "DejaVu Sans", sans-serif;
    font-size: 12px;
}
QLabel#title {
    color: #0E1114;
    font-family: "DejaVu Sans Mono", monospace;
    font-size: 12px;
    font-weight: 700;
    letter-spacing: 3px;
}
QLabel#field { color: #4A5158; font-size: 11px; }
QLabel#status {
    color: #5C636B;
    font-family: "DejaVu Sans Mono", monospace;
    font-size: 10px;
}
QLabel#stats {
    color: #8A9199;
    font-family: "DejaVu Sans Mono", monospace;
    font-size: 10px;
}
QLabel#hint { color: #71787F; font-size: 10px; }
QLabel#warn { color: #101417; font-size: 10px; font-weight: 700; }
QWidget#row { border-radius: 6px; }
QWidget#row:hover { background: rgba(0, 0, 0, 10); }
QFrame#rule { background: rgba(0, 0, 0, 45); border: none; min-height: 1px; max-height: 1px; }
/* 开关的轨道与滑块由 ToggleSwitch 自绘（iOS 形制），这里只保留文字色基线。 */
QCheckBox { color: #1F2328; }
QSpinBox, QDoubleSpinBox, QComboBox {
    background: transparent;
    border: 1px solid rgba(0, 0, 0, 55);
    border-radius: 6px;
    padding: 3px 7px;
    min-height: 16px;
    color: #14181C;
    font-family: "DejaVu Sans Mono", monospace;
    font-size: 11px;
    selection-background-color: #1F2328;
    selection-color: #FFFFFF;
}
QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover {
    background: rgba(0, 0, 0, 10);
    border-color: rgba(0, 0, 0, 110);
}
QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border-color: rgba(0, 0, 0, 160);
    background: rgba(0, 0, 0, 10);
}
QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    width: 0;
    height: 0;
    border: none;
}
QComboBox::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView {
    background: #FFFFFF;
    border: 1px solid rgba(0, 0, 0, 30);
    border-radius: 6px;
    padding: 4px;
    outline: none;
    selection-background-color: rgba(0, 0, 0, 220);
    selection-color: #FFFFFF;
}
QPushButton#apply {
    background: transparent;
    color: #14181C;
    border: 1px solid rgba(0, 0, 0, 80);
    border-radius: 8px;
    padding: 8px 12px;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 3px;
}
QPushButton#apply:hover { background: #14181C; color: #FFFFFF; border-color: transparent; }
QPushButton#restore {
    background: transparent;
    color: #5A6169;
    border: 1px solid rgba(0, 0, 0, 45);
    border-radius: 8px;
    padding: 8px 10px;
    font-size: 11px;
    letter-spacing: 2px;
}
QPushButton#restore:hover { background: rgba(0, 0, 0, 12); color: #14181C; border-color: rgba(0, 0, 0, 90); }
QPushButton#restore:pressed { background: rgba(0, 0, 0, 55); color: #FFFFFF; }
QPushButton#apply:pressed { background: rgba(0, 0, 0, 150); color: #FFFFFF; }
QPushButton#close {
    background: transparent;
    border: 0;
    border-radius: 9px;
    color: #70777E;
    font-size: 12px;
    padding: 0;
}
QPushButton#close:hover { background: rgba(0, 0, 0, 12); color: #14181C; }
QPlainTextEdit#log {
    background: transparent;
    border: none;
    color: #4E555C;
    font-family: "DejaVu Sans Mono", monospace;
    font-size: 9px;
    selection-background-color: rgba(0, 0, 0, 220);
    selection-color: #FFFFFF;
}
QScrollBar:vertical {
    background: transparent;
    width: 4px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: rgba(0, 0, 0, 45);
    border-radius: 2px;
    min-height: 16px;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)QSS";

// 侧栏按钮样式不走全局样式表：ID 选择器配 :checked 时 Qt 会回退到原生绘制，
// 选中态直接变成系统高亮的蓝色。改成挂在按钮自身的样式表，优先级最高。
const char* kNavStyle = R"QSS(
QPushButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 7px;
    color: #5C636B;
    font-size: 12px;
    padding: 0 10px;
    text-align: left;
}
QPushButton:hover { background: rgba(0, 0, 0, 10); color: #14181C; }
QPushButton:checked {
    background: rgba(0, 0, 0, 16);
    color: #101417;
    font-weight: 700;
}
)QSS";

// 合成器探测：_NET_WM_CM_S0 有 owner 才说明有合成器在做窗口合成。
// 有它才能安全地开 ARGB 透明窗口（透明区由合成器补上后景）；
// 没有的话透明区会被直接贴屏成未初始化像素。
bool compositorRunning() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) return false;
    const Atom selection = XInternAtom(display, "_NET_WM_CM_S0", False);
    // None 宏已被 #undef，XID 0 即 None。
    const bool running = selection != 0 && XGetSelectionOwner(display, selection) != 0;
    XCloseDisplay(display);
    return running;
}

// 磨砂颗粒：固定种子的 128×128 二值噪声，平铺后给玻璃面一点散射质感。
// 白底上必须用深色颗粒才看得见；alpha 压得很低（5/255），只留极细一层砂感。
// 静态缓存，避免每次重绘都重新生成。
QPixmap grainTexture() {
    static const QPixmap tile = [] {
        constexpr int kSize = 128;
        QImage image(kSize, kSize, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QRandomGenerator rng(0x9E3779B9u);
        for (int y = 0; y < kSize; ++y) {
            auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < kSize; ++x) {
                const bool dark = rng.bounded(2) == 0;
                line[x] = dark ? qRgba(0, 0, 0, 2) : qRgba(0, 0, 0, 0);
            }
        }
        return QPixmap::fromImage(image);
    }();
    return tile;
}

// iOS 风格开关：继承 QCheckBox 以保留 isChecked/setChecked 的全部既有调用点，
// 只把绘制换成「圆角轨道 + 圆形滑块」，切换时 140 ms 缓动。
// 面板是黑白双色规范，因此开启态轨道用白、滑块用深色，不引入绿色强调。
class ToggleSwitch : public QCheckBox {
public:
    explicit ToggleSwitch(QWidget* parent = nullptr) : QCheckBox(parent) {
        setCursor(Qt::PointingHandCursor);
        knob_ = isChecked() ? 1.0 : 0.0;

        anim_ = new QVariantAnimation(this);
        anim_->setDuration(140);
        anim_->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            knob_ = value.toReal();
            update();
        });
        connect(this, &QCheckBox::toggled, this, [this](bool checked) {
            anim_->stop();
            anim_->setStartValue(knob_);
            anim_->setEndValue(checked ? 1.0 : 0.0);
            anim_->start();
        });
    }

    QSize sizeHint() const override {
        const QFontMetrics metrics(font());
        return QSize(kTrackWidth + kGap + metrics.horizontalAdvance(text()),
                     std::max(kTrackHeight, metrics.height()));
    }

    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    // 自绘控件：整块区域都应响应点击。默认实现按 style 里 13px 的 indicator
    // 加文字计算命中范围，自绘成 34px 轨道后就点不到了。
    bool hitButton(const QPoint& pos) const override { return rect().contains(pos); }

    void paintEvent(QPaintEvent* /*event*/) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const qreal radius = kTrackHeight / 2.0;
        const QRectF track(0.0, (height() - kTrackHeight) / 2.0, kTrackWidth, kTrackHeight);
        painter.setPen(Qt::NoPen);
        // 浅色底板：关闭是淡灰轨道，开启是近黑轨道，靠明度差表达状态。
        painter.setBrush(blend(QColor(0, 0, 0, 38), QColor(31, 35, 40), knob_));
        painter.drawRoundedRect(track, radius, radius);

        // 滑块恒为白色：在浅色面板上由「白点压在深轨道里」形成对比。
        const qreal knobSize = kTrackHeight - 4.0;
        const qreal knobX = track.left() + 2.0 + knob_ * (kTrackWidth - 4.0 - knobSize);
        painter.setBrush(QColor(255, 255, 255));
        painter.drawEllipse(QRectF(knobX, track.top() + 2.0, knobSize, knobSize));

        const QFontMetrics metrics(font());
        const int textLeft = kTrackWidth + kGap;
        const QRect textRect(textLeft, 0, std::max(0, width() - textLeft), height());
        painter.setPen(isEnabled() ? QColor(31, 35, 40) : QColor(154, 160, 166));
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                         metrics.elidedText(text(), Qt::ElideRight, textRect.width()));
    }

private:
    static QColor blend(const QColor& from, const QColor& to, qreal t) {
        return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                                from.greenF() + (to.greenF() - from.greenF()) * t,
                                from.blueF() + (to.blueF() - from.blueF()) * t,
                                from.alphaF() + (to.alphaF() - from.alphaF()) * t);
    }

    static constexpr int kTrackWidth = 34;
    static constexpr int kTrackHeight = 18;
    static constexpr int kGap = 9;

    QVariantAnimation* anim_{nullptr};
    qreal knob_{0.0};
};

// 按键编码转可读名称。编码与 Features::keyDown 一致：
// 0 = 不限，1..5 = 鼠标 Button1..5，>5 = X11 keysym。
// 复用同一个 Display，避免每次刷新都重新连服务器。
QString describeKey(int code) {
    if (code <= 0) return "不限";
    switch (code) {
        case 1: return "左键";
        case 2: return "中键";
        case 3: return "右键";
        case 4: return "侧键1";
        case 5: return "侧键2";
        default: break;
    }
    static Display* display = XOpenDisplay(nullptr);
    const char* name = display ? XKeysymToString(static_cast<KeySym>(code)) : nullptr;
    if (!name) return QString("键 0x%1").arg(code, 0, 16);
    // X11 名称用下划线（Shift_L / Caps_Lock），换成空格更好读。
    return QString::fromLatin1(name).replace('_', ' ');
}

// HUD 位置下拉：0 底部居中 / 1 左上 / 2 右上 / 3 左下 / 4 右下。
QComboBox* posCombo(QWidget* parent, int value) {
    auto* combo = new QComboBox(parent);
    combo->addItem("底部居中", 0);
    combo->addItem("左上", 1);
    combo->addItem("右上", 2);
    combo->addItem("左下", 3);
    combo->addItem("右下", 4);
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : 0);
    combo->setFixedWidth(96);
    return combo;
}

// 只比较核心偏移字段：面板改样式参数也会走 persist，不能每次都触发特征码重扫。
bool sameOffsets(const Offsets& a, const Offsets& b) {
    return a.dwEntityList == b.dwEntityList && a.dwLocalPlayerPawn == b.dwLocalPlayerPawn &&
           a.dwViewMatrix == b.dwViewMatrix && a.m_iHealth == b.m_iHealth &&
           a.m_pGameSceneNode == b.m_pGameSceneNode && a.m_vecAbsOrigin == b.m_vecAbsOrigin &&
           a.m_iTeamNum == b.m_iTeamNum && a.entityChunkStride == b.entityChunkStride &&
           a.m_modelState == b.m_modelState;
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

// 按键绑定控件：点一下进入录制，随后按任意键（含鼠标中/右键）即绑定。
// 放在匿名 namespace 之外，Menu.h 才能用前向声明持有它。
class KeyBindButton : public QPushButton {
public:
    explicit KeyBindButton(QWidget* parent = nullptr) : QPushButton(parent) {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setFixedWidth(96);
        setStyleSheet(
            "QPushButton{background:transparent;border:1px solid rgba(0,0,0,55);border-radius:6px;"
            "color:#14181C;font-size:11px;padding:3px 6px;}"
            "QPushButton:hover{background:rgba(0,0,0,10);border-color:rgba(0,0,0,110);}"
            "QPushButton:focus{border-color:rgba(0,0,0,170);}");
        setText(describeKey(code_));
        connect(this, &QPushButton::clicked, this, [this] {
            // 延后一点再进入录制：本次点击的左键释放还在路上，
            // 立刻进入会把「左键」当成绑定值写进去。
            QTimer::singleShot(180, this, [this] {
                if (!isVisible()) return;
                capturing_ = true;
                setText("按按键…");
            });
        });
    }

    int code() const { return code_; }

    // 绑定值变化时回调：让面板立刻落盘，而不是等收起参数区。
    // 键位是「按一下就想用」的东西，多一步保存很容易漏。
    std::function<void()> onChanged;

    void setCode(int code) {
        const bool changed = code != code_;
        code_ = code;
        capturing_ = false;
        setText(describeKey(code_));
        if (changed && onChanged) onChanged();
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (!capturing_) {
            QPushButton::keyPressEvent(event);
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            setCode(0);  // Esc 取消绑定
            return;
        }
        const int native = event->nativeVirtualKey();  // xcb 平台即 X11 keysym
        if (native > 5) setCode(native);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (capturing_) {
            switch (event->button()) {
                case Qt::LeftButton: setCode(1); return;
                case Qt::MiddleButton: setCode(2); return;
                case Qt::RightButton: setCode(3); return;
                default: break;
            }
        }
        QPushButton::mousePressEvent(event);
    }

    void focusOutEvent(QFocusEvent* event) override {
        if (capturing_) {
            capturing_ = false;
            setText(describeKey(code_));
        }
        QPushButton::focusOutEvent(event);
    }

private:
    int code_{0};
    bool capturing_{false};
};

KeyBindButton* makeKeyBind(QWidget* parent, int code) {
    auto* button = new KeyBindButton(parent);
    button->setCode(code);
    return button;
}

Menu::Menu(AppState& state, Game& game, std::string configPath)
    : QWidget(nullptr), state_(state), game_(game), path_(std::move(configPath)) {
    setWindowTitle("CS2 辅助 · 控制面板");
    // 无边框 + 透明底：整块面板由 paintEvent 画成圆角玻璃，WM 不再画标题栏。
    // 无边框后没有标题栏可点回前台，必须自己置顶，否则被 IDE / 浏览器整块盖住。
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window | Qt::WindowStaysOnTopHint);
    // 毛玻璃两条路径：
    // - 有合成器：ARGB 半透明窗口，背景由 picom / KWin 实时合成，能跟随画面变化；
    // - 无合成器：透明区会被贴成未初始化像素，必须退回不透明 + 快照自模糊。
    translucent_ = compositorRunning();
    if (translucent_) setAttribute(Qt::WA_TranslucentBackground, true);
    // 左侧分类列表固定 92px + 间距 12px，右侧内容区保持原来的 ~400px。
    setFixedWidth(540);
    setStyleSheet(kStyle);

    // 参数防抖：改动后静默 400 ms 才落盘，拖动 spinbox 时不会疯狂写文件。
    persistTimer_ = new QTimer(this);
    persistTimer_->setSingleShot(true);
    persistTimer_->setInterval(400);
    connect(persistTimer_, &QTimer::timeout, this, &Menu::apply);

    buildUi();

    // 快照只在没有合成器时才会用到；此时窗口还没显示，抓屏拍不到面板自身。
    if (!translucent_) captureBackdrop();

    configStamp_ = QFileInfo(QString::fromStdString(path_)).lastModified();
    offsetsStamp_ = QFileInfo(offsetsPath()).lastModified();

    auto timer = new QTimer(this);
    timer->setInterval(400);
    connect(timer, &QTimer::timeout, this, [this] {
        // F8：显隐面板。Features 线程检测到按下沿会置位，这里消费并清零。
        if (state_.panelToggle.exchange(0) != 0) {
            if (isVisible()) {
                hide();
            } else {
                show();
            }
        }

        if (status_ && QDateTime::currentDateTime() >= messageUntil_) {
            status_->setText(QString::fromStdString(game_.status()));
        }

        // 实时统计：把「采集了但画不出来」的原因摊开，省得靠日志猜。
        if (stats_) {
            const std::shared_ptr<Config> current = state_.config.load();
            const int seen = state_.espSeen.load();
            if (!game_.attached() || !current || !current->esp.enabled) {
                stats_->clear();
            } else if (seen == 0) {
                stats_->setText("采集 0 · 当前视野内没有实体");
            } else {
                stats_->setText(QString("采集 %1 · 方框 %2 · 队友 %3 · 距离 %4")
                                    .arg(seen)
                                    .arg(state_.espDrawn.load())
                                    .arg(state_.espTeamFiltered.load())
                                    .arg(state_.espDistanceFiltered.load()));
            }
        }

        // 日志区可见时才读文件，滚出视野后零开销。
        if (logView_ && logView_->isVisible()) {
            logView_->setPlainText(tailOfLog("/tmp/wv_lite.log"));
            logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
        }

        // 配置文件热重载：外部编辑（或换版本后重新生成）只要 mtime 变了就重新读取，
        // 无需重启进程。写入方是本面板时 apply() 会同步更新 configStamp_。
        const QDateTime stamp = QFileInfo(QString::fromStdString(path_)).lastModified();
        if (stamp.isValid() && stamp != configStamp_) {
            configStamp_ = stamp;
            const Config reloaded = Config::load(path_);
            const std::shared_ptr<Config> previous = state_.config.load();
            state_.config.store(std::make_shared<Config>(reloaded));
            // 偏移变了才重解析：特征码扫描很贵，不能每次改开关都跑一遍。
            if (!previous || !sameOffsets(previous->offsets, reloaded.offsets)) {
                game_.reloadOffsets(reloaded.offsets);
            }
            syncFrom(reloaded);
        }

        // 生成器重跑会改写 config/offsets.json：重新导入并替换，免重启进程。
        const QDateTime offsetsFileStamp = QFileInfo(offsetsPath()).lastModified();
        if (offsetsFileStamp.isValid() && offsetsFileStamp != offsetsStamp_) {
            offsetsStamp_ = offsetsFileStamp;
            const Config refreshed = Config::load(path_);
            state_.config.store(std::make_shared<Config>(refreshed));
            game_.reloadOffsets(refreshed.offsets);
            syncFrom(refreshed);
            if (status_) {
                status_->setText("偏移已重载，无需重启");
                messageUntil_ = QDateTime::currentDateTime().addSecs(3);
            }
        }
    });
    timer->start();
}

void Menu::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath shape;
    shape.addRoundedRect(panel, 14.0, 14.0);
    painter.setClipPath(shape);

    // 玻璃底层：有合成器时只铺一层半透明白，背后画面由合成器实时合成后透出；
    // 无合成器时才退回静态快照。
    if (translucent_) {
        painter.fillRect(rect(), QColor(252, 253, 254, kGlassAlpha));
    } else {
        if (backdropDirty_) rebuildBackdrop();
        if (!backdropBlurred_.isNull()) {
            painter.drawPixmap(rect(), backdropBlurred_);
            // 压一层白色玻璃：既保证深色文字可读，也不让模糊背景抢主体。
            painter.fillRect(rect(), QColor(252, 253, 254, kGlassAlpha));
        } else {
            painter.fillRect(rect(), QColor(252, 253, 254, 255));
        }
    }

    // 磨砂质感：深色噪声颗粒 + 顶部一道极淡的压暗，模拟玻璃的散射与厚度。
    painter.drawTiledPixmap(rect(), grainTexture());
    QLinearGradient sheen(0.0, 0.0, 0.0, static_cast<qreal>(height()));
    sheen.setColorAt(0.0, QColor(0, 0, 0, 5));
    sheen.setColorAt(0.4, QColor(0, 0, 0, 0));
    painter.fillRect(rect(), sheen);
    painter.setClipping(false);

    // 外描边压一层很淡的深灰，只勾出圆角轮廓，不留下明显边框。
    painter.setPen(QPen(QColor(0, 0, 0, 30), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(panel, 14.0, 14.0);
}

void Menu::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // 半透明路径下圆角由 ARGB 像素决定，mask 会把边缘切硬，必须跳过。
    if (translucent_) {
        clearMask();
        backdropDirty_ = true;
        return;
    }
    // 圆角靠窗口遮罩实现（XShape），不依赖合成器；四角直接被裁掉而不是画成透明。
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()), 14.0, 14.0);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
    // 尺寸变了，背景裁剪范围也要跟着重算。
    backdropDirty_ = true;
}

void Menu::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    // 拖动后窗口落到屏幕的新位置，需要从快照里重新裁一块。
    backdropDirty_ = true;
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
        // 居中后位置才最终确定，这时再裁背景。
        backdropDirty_ = true;
        update();
    });
}

void Menu::captureBackdrop() {
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    // 抓整个屏幕（WId 0 即 root 窗口）。构造阶段窗口还没 show，
    // 所以这张快照里不会有面板自身的内容。
    backdrop_ = screen->grabWindow(0).toImage();
    backdropDirty_ = true;
}

void Menu::rebuildBackdrop() {
    backdropDirty_ = false;
    backdropBlurred_ = QPixmap();
    if (backdrop_.isNull() || width() <= 0 || height() <= 0) return;

    // 从快照里裁出窗口当前覆盖的区域；越界部分由 copy 自动裁掉。
    QImage piece = backdrop_.copy(QRect(geometry().topLeft(), size()));
    if (piece.width() < 2 || piece.height() < 2) return;

    // 降采样到 1/8 再平滑放大：等价于一次大半径高斯，开销远低于逐像素卷积。
    const QSize tiny(std::max(1, width() / 8), std::max(1, height() / 8));
    piece = piece.scaled(tiny, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    piece = piece.scaled(size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    backdropBlurred_ = QPixmap::fromImage(piece);
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

QWidget* Menu::makeRow(QCheckBox* toggle, const QString& name, const QString& hint,
                       const std::function<void(QVBoxLayout*)>& build) {
    // 每行是一个单元：上面是功能行，下面是可展开的参数区（默认收起）。
    auto* unit = new QWidget(this);
    auto* unitLayout = new QVBoxLayout(unit);
    unitLayout->setContentsMargins(0, 0, 0, 0);
    unitLayout->setSpacing(0);

    auto* row = new QWidget(unit);
    row->setObjectName("row");
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 3, 8, 3);
    layout->setSpacing(8);

    toggle->setText(name);
    toggle->setCursor(Qt::PointingHandCursor);
    layout->addWidget(toggle);
    layout->addStretch(1);
    unitLayout->addWidget(row);

    if (!build) {
        auto* hintLabel = new QLabel(hint, row);
        hintLabel->setObjectName("hint");
        layout->addWidget(hintLabel);
        return unit;
    }

    // 参数入口：点一下就地展开/收起，不再另开浮窗。
    auto* entry = new QPushButton(QString::fromUtf8("参数 ▸"), row);
    entry->setCursor(Qt::PointingHandCursor);
    entry->setFocusPolicy(Qt::NoFocus);
    entry->setStyleSheet(
        "QPushButton{background:transparent;border:none;color:#71787F;font-size:10px;padding:1px 2px;}"
        "QPushButton:hover{color:#14171A;}");
    layout->addWidget(entry);

    auto* panel = new QWidget(unit);
    panel->setObjectName("paramPanel");
    panel->setAttribute(Qt::WA_StyledBackground, true);
    panel->setStyleSheet("QWidget#paramPanel{background:rgba(0,0,0,10);border-radius:8px;}");
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(14, 8, 10, 8);
    panelLayout->setSpacing(7);
    build(panelLayout);
    panel->hide();
    unitLayout->addWidget(panel);

    auto togglePanel = [this, panel, entry] {
        const bool shown = !panel->isVisible();
        panel->setVisible(shown);
        entry->setText(shown ? QString::fromUtf8("参数 ▾") : QString::fromUtf8("参数 ▸"));
        // 收起即落盘：保持「调完就生效」的旧手感，不用再回底部点保存。
        if (!shown) apply();
    };
    connect(entry, &QPushButton::clicked, this, [togglePanel] { togglePanel(); });
    // 行内任意位置右键也能开合，保留原来的手感；参数改动由底部「应用并保存」落盘。
    for (QWidget* target : {row, static_cast<QWidget*>(toggle)}) {
        target->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(target, &QWidget::customContextMenuRequested, this,
                [togglePanel](const QPoint&) { togglePanel(); });
    }
    return unit;
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

    stats_ = new QLabel(this);
    stats_->setObjectName("stats");
    root->addWidget(stats_);
    root->addWidget(rule(this));

    // ---- 主体：左侧分类列表 + 右侧内容页 ----
    // 左列固定宽度竖排导航，右列 QStackedWidget 只显示当前类别的功能行。
    auto* body = new QHBoxLayout();
    body->setSpacing(12);
    body->setContentsMargins(0, 0, 0, 0);

    auto* navColumn = new QWidget(this);
    navColumn->setObjectName("navColumn");
    navColumn->setFixedWidth(92);
    auto* navLayout = new QVBoxLayout(navColumn);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(4);

    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(10);

    const QStringList tabNames{"透视", "自瞄", "扳机", "日志"};
    for (int i = 0; i < tabNames.size(); ++i) {
        auto* tab = new QPushButton(tabNames.at(i), navColumn);
        tab->setObjectName("nav");
        tab->setStyleSheet(kNavStyle);
        tab->setCheckable(true);
        tab->setAutoExclusive(true);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setFocusPolicy(Qt::NoFocus);
        tab->setFixedHeight(34);
        tab->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        navLayout->addWidget(tab);
        tabs_[i] = tab;
        connect(tab, &QPushButton::clicked, this, [this, i] { pages_->setCurrentIndex(i); });
    }
    if (tabs_[0]) tabs_[0]->setChecked(true);
    navLayout->addStretch(1);

    pages_ = new QStackedWidget(content);
    pages_->setObjectName("pages");
    contentLayout->addWidget(pages_, 1);

    body->addWidget(navColumn);
    body->addWidget(content, 1);
    root->addLayout(body, 1);

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
        // 透视页已有 16 行，330 会把最后三项挤进滚动区，抬到 400 后基本一屏可见。
        scroll->setMinimumHeight(400);
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
    espEnabled_ = new ToggleSwitch(this);
    espEnabled_->setChecked(initial.esp.enabled);
    espBox_ = new ToggleSwitch(this);
    espBox_->setChecked(initial.esp.box);
    espHealth_ = new ToggleSwitch(this);
    espHealth_->setChecked(initial.esp.health);
    espHealthText_ = new ToggleSwitch(this);
    espHealthText_->setChecked(initial.esp.healthText);
    espArmor_ = new ToggleSwitch(this);
    espArmor_->setChecked(initial.esp.armor);
    espDamage_ = new ToggleSwitch(this);
    espDamage_->setChecked(initial.esp.damageNumbers);
    espDistance_ = new ToggleSwitch(this);
    espDistance_->setChecked(initial.esp.distance);
    espSkeleton_ = new ToggleSwitch(this);
    espSkeleton_->setChecked(initial.esp.skeleton);
    espTeam_ = new ToggleSwitch(this);
    espTeam_->setChecked(initial.esp.teamCheck);
    espVisibility_ = new ToggleSwitch(this);
    espVisibility_->setChecked(initial.esp.visibilityCheck);

    espRadar_ = new ToggleSwitch(this);
    espRadar_->setChecked(initial.esp.radar);
    espRadarRotate_ = new ToggleSwitch(this);
    espRadarRotate_->setChecked(initial.esp.radarRotate);
    espRadarRange_ = new QSpinBox(this);
    espRadarRange_->setRange(5, 300);
    espRadarRange_->setSuffix(" m");
    espRadarRange_->setValue(static_cast<int>(initial.esp.radarRange));
    espRadarRange_->setFixedWidth(78);

    espOffscreen_ = new ToggleSwitch(this);
    espOffscreen_->setChecked(initial.esp.offscreen);

    espEnemyList_ = new ToggleSwitch(this);
    espEnemyList_->setChecked(initial.esp.enemyList);
    espEnemyListMax_ = new QSpinBox(this);
    espEnemyListMax_->setRange(1, 10);
    espEnemyListMax_->setSuffix(" 个");
    espEnemyListMax_->setValue(initial.esp.enemyListMax);
    espEnemyListMax_->setFixedWidth(70);

    espHeight_ = new ToggleSwitch(this);
    espHeight_->setChecked(initial.esp.heightIndicator);

    espRadarPos_ = posCombo(this, initial.esp.radarPos);
    espListPos_ = posCombo(this, initial.esp.listPos);

    espTrail_ = new ToggleSwitch(this);
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

    espHighlight_ = new ToggleSwitch(this);
    espHighlight_->setChecked(initial.esp.highlight);

    aimEnabled_ = new ToggleSwitch(this);
    aimEnabled_->setChecked(initial.aim.enabled);
    aimTeam_ = new ToggleSwitch(this);
    aimTeam_->setChecked(initial.aim.teamCheck);
    aimStrategy_ = new QComboBox(this);
    aimStrategy_->addItem("屏幕最近");
    aimStrategy_->addItem("血量最低");
    aimStrategy_->addItem("世界最近");
    aimStrategy_->setCurrentIndex(std::clamp(initial.aim.strategy, 0, 2));
    aimStrategy_->setFixedWidth(120);
    aimLock_ = new ToggleSwitch(this);
    aimLock_->setChecked(initial.aim.lockTarget);
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
    aimScopeKey_ = makeKeyBind(this, initial.aim.scopeKey);
    aimScopeKey_->onChanged = [this] { armPersist(); };
    aimFireKey_ = makeKeyBind(this, initial.aim.fireKey);
    aimFireKey_->onChanged = [this] { armPersist(); };

    recoilEnabled_ = new ToggleSwitch(this);
    recoilEnabled_->setChecked(initial.recoil.enabled);
    recoilRequireFire_ = new ToggleSwitch(this);
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

    trackEnabled_ = new ToggleSwitch(this);
    trackEnabled_->setChecked(initial.track.enabled);
    trackTeam_ = new ToggleSwitch(this);
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
    trackKey_ = makeKeyBind(this, initial.track.key);
    trackKey_->onChanged = [this] { armPersist(); };

    triggerEnabled_ = new ToggleSwitch(this);
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
    triggerKey_ = makeKeyBind(this, initial.trigger.key);
    triggerKey_->onChanged = [this] { armPersist(); };

    silentEnabled_ = new ToggleSwitch(this);
    silentEnabled_->setChecked(initial.silent.enabled);
    silentTeam_ = new ToggleSwitch(this);
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
    silentKey_ = makeKeyBind(this, initial.silent.key);
    silentKey_->onChanged = [this] { armPersist(); };

    // 参数控件直接挂在各行下方的下拉区里，不再需要临时隐藏容器。

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
    espLayout->addWidget(makeRow(espDamage_, "伤害浮字", "", [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("显示时长", espDamageMs_, parent));
        layout->addWidget(paramRow("颜色", espDamageColor_, parent));
    }));
    espLayout->addWidget(makeRow(espTrail_, "残影轨迹", "", [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("拖尾时长", espTrailMs_, parent));
    }));
    espLayout->addWidget(makeRow(espHighlight_, "残血描边", ""));
    espLayout->addWidget(makeRow(espRadar_, "2D 雷达", "", [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("半径", espRadarRange_, parent));
        layout->addWidget(paramRow("位置", espRadarPos_, parent));
        layout->addWidget(paramRow("随视角旋转", espRadarRotate_, parent));
    }));
    espLayout->addWidget(makeRow(espOffscreen_, "屏外指示", ""));
    espLayout->addWidget(makeRow(espEnemyList_, "敌人列表", "", [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("最多显示", espEnemyListMax_, parent));
        layout->addWidget(paramRow("位置", espListPos_, parent));
    }));
    espLayout->addWidget(makeRow(espHeight_, "高度差", ""));
    espLayout->addStretch(1);

    // ---- 页二：自瞄 ----
    QVBoxLayout* aimLayout = addPage();
    aimLayout->addWidget(makeRow(aimEnabled_, "自瞄", "", [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("策略", aimStrategy_, parent));
        layout->addWidget(paramRow("锁定目标", aimLock_, parent));
        layout->addWidget(paramRow("平滑", aimSmooth_, parent));
        layout->addWidget(paramRow("范围", aimFov_, parent));
        layout->addWidget(paramRow("灵敏度", aimSensitivity_, parent));
        layout->addWidget(paramRow("开镜键", aimScopeKey_, parent));
        layout->addWidget(paramRow("开火键", aimFireKey_, parent));
        layout->addWidget(paramRow("过滤队友", aimTeam_, parent));
    }));
    aimLayout->addWidget(makeRow(recoilEnabled_, "后坐力抵消", "", [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("强度", recoilStrength_, parent));
        layout->addWidget(paramRow("单帧上限", recoilMax_, parent));
        layout->addWidget(paramRow("仅开火时", recoilRequireFire_, parent));
    }));
    aimLayout->addWidget(makeRow(trackEnabled_, "子弹追踪", "", [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("半径", trackFov_, parent));
        layout->addWidget(paramRow("平滑", trackSmooth_, parent));
        layout->addWidget(paramRow("触发键", trackKey_, parent));
        layout->addWidget(paramRow("过滤队友", trackTeam_, parent));
    }));
    aimLayout->addStretch(1);

    // ---- 页三：扳机 ----
    QVBoxLayout* triggerLayout = addPage();
    triggerLayout->addWidget(makeRow(triggerEnabled_, "自动扳机", "", [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("按住", triggerDelay_, parent));
        layout->addWidget(paramRow("冷却", triggerCooldown_, parent));
        layout->addWidget(paramRow("触发键", triggerKey_, parent));
    }));
    triggerLayout->addWidget(makeRow(silentEnabled_, "静默锁头", "", [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("半径", silentFov_, parent));
        layout->addWidget(paramRow("停留", silentHold_, parent));
        layout->addWidget(paramRow("触发键", silentKey_, parent));
        layout->addWidget(paramRow("过滤队友", silentTeam_, parent));
    }));
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

    contentLayout->addWidget(rule(this));

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
    contentLayout->addWidget(hint);

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
    contentLayout->addLayout(actions);

    // 所有参数控件改完 400 ms 自动落盘：勾选框 / 数值框 / 下拉框统一挂防抖，
    // 避免再出现「改完没生效」。键位按钮走自己的 onChanged。
    for (QCheckBox* box : findChildren<QCheckBox*>()) {
        connect(box, &QCheckBox::toggled, this, &Menu::armPersist);
    }
    for (QSpinBox* spin : findChildren<QSpinBox*>()) {
        connect(spin, &QSpinBox::valueChanged, this, &Menu::armPersist);
    }
    for (QDoubleSpinBox* spin : findChildren<QDoubleSpinBox*>()) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, &Menu::armPersist);
    }
    for (QComboBox* combo : findChildren<QComboBox*>()) {
        connect(combo, &QComboBox::currentIndexChanged, this, &Menu::armPersist);
    }
}

QString Menu::offsetsPath() const {
    // path_ 形如 <dir>/config.json，配置里的 offsetsFile 是相对路径 config/offsets.json。
    const QFileInfo configFile(QString::fromStdString(path_));
    return configFile.absolutePath() + "/config/offsets.json";
}

void Menu::armPersist() {
    // 回填控件期间不落盘，否则热重载会把自己刚读到的值再写回去。
    if (syncing_) return;
    if (persistTimer_) persistTimer_->start();
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
    next.esp.radarRotate = espRadarRotate_->isChecked();
    next.esp.radarRange = static_cast<float>(espRadarRange_->value());
    next.esp.offscreen = espOffscreen_->isChecked();
    next.esp.enemyList = espEnemyList_->isChecked();
    next.esp.enemyListMax = espEnemyListMax_->value();
    next.esp.heightIndicator = espHeight_->isChecked();
    next.esp.radarPos = espRadarPos_->currentData().toInt();
    next.esp.listPos = espListPos_->currentData().toInt();
    next.esp.trail = espTrail_->isChecked();
    next.esp.trailMs = espTrailMs_->value();
    next.esp.highlight = espHighlight_->isChecked();
    next.esp.damageMs = espDamageMs_->value();
    next.esp.damageColor = espDamageColor_->currentData().toInt();

    next.aim.enabled = aimEnabled_->isChecked();
    next.aim.strategy = aimStrategy_->currentIndex();
    next.aim.lockTarget = aimLock_->isChecked();
    next.aim.smooth = aimSmooth_->value();
    next.aim.fovPixels = static_cast<float>(aimFov_->value());
    next.aim.sensitivityFactor = static_cast<float>(aimSensitivity_->value());
    next.aim.teamCheck = aimTeam_->isChecked();
    next.aim.scopeKey = aimScopeKey_->code();
    next.aim.fireKey = aimFireKey_->code();

    next.recoil.enabled = recoilEnabled_->isChecked();
    next.recoil.requireFire = recoilRequireFire_->isChecked();
    next.recoil.strength = static_cast<float>(recoilStrength_->value());
    next.recoil.maxDegrees = static_cast<float>(recoilMax_->value());

    next.track.enabled = trackEnabled_->isChecked();
    next.track.teamCheck = trackTeam_->isChecked();
    next.track.fovPixels = static_cast<float>(trackFov_->value());
    next.track.smooth = trackSmooth_->value();
    next.track.key = trackKey_->code();

    next.silent.enabled = silentEnabled_->isChecked();
    next.silent.teamCheck = silentTeam_->isChecked();
    next.silent.fovPixels = static_cast<float>(silentFov_->value());
    next.silent.holdMs = silentHold_->value();
    next.silent.key = silentKey_->code();

    next.trigger.enabled = triggerEnabled_->isChecked();
    next.trigger.delayMs = triggerDelay_->value();
    next.trigger.cooldownMs = triggerCooldown_->value();
    next.trigger.key = triggerKey_->code();

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

    const std::shared_ptr<Config> previous = state_.config.load();
    const bool offsetsChanged = !previous || !sameOffsets(previous->offsets, config.offsets);

    config.save(path_);
    state_.config.store(std::make_shared<Config>(config));
    // 自己写盘后同步时间戳，避免热重载把刚刚保存的配置再读一遍。
    configStamp_ = QFileInfo(target).lastModified();
    // 偏移有变化才重解析：特征码扫描很贵，改开关不该触发。
    if (offsetsChanged) game_.reloadOffsets(config.offsets);

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
    // 回填期间屏蔽防抖，避免热重载把刚读到的值再写回去形成循环。
    syncing_ = true;
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
    espRadarRotate_->setChecked(config.esp.radarRotate);
    espRadarRange_->setValue(static_cast<int>(config.esp.radarRange));
    espOffscreen_->setChecked(config.esp.offscreen);
    espEnemyList_->setChecked(config.esp.enemyList);
    espEnemyListMax_->setValue(config.esp.enemyListMax);
    espHeight_->setChecked(config.esp.heightIndicator);
    espRadarPos_->setCurrentIndex(
        std::clamp(espRadarPos_->findData(config.esp.radarPos), 0, 4));
    espListPos_->setCurrentIndex(std::clamp(espListPos_->findData(config.esp.listPos), 0, 4));
    espTrail_->setChecked(config.esp.trail);
    espTrailMs_->setValue(config.esp.trailMs);
    espHighlight_->setChecked(config.esp.highlight);
    espDamageMs_->setValue(config.esp.damageMs);
    const int colorIndex = espDamageColor_->findData(config.esp.damageColor);
    espDamageColor_->setCurrentIndex(colorIndex >= 0 ? colorIndex : 0);

    aimEnabled_->setChecked(config.aim.enabled);
    aimStrategy_->setCurrentIndex(std::clamp(config.aim.strategy, 0, 2));
    aimLock_->setChecked(config.aim.lockTarget);
    aimSmooth_->setValue(config.aim.smooth);
    aimFov_->setValue(static_cast<int>(config.aim.fovPixels));
    aimSensitivity_->setValue(config.aim.sensitivityFactor);
    aimTeam_->setChecked(config.aim.teamCheck);
    aimScopeKey_->setCode(config.aim.scopeKey);
    aimFireKey_->setCode(config.aim.fireKey);

    recoilEnabled_->setChecked(config.recoil.enabled);
    recoilRequireFire_->setChecked(config.recoil.requireFire);
    recoilStrength_->setValue(config.recoil.strength);
    recoilMax_->setValue(static_cast<int>(config.recoil.maxDegrees));

    trackEnabled_->setChecked(config.track.enabled);
    trackTeam_->setChecked(config.track.teamCheck);
    trackFov_->setValue(static_cast<int>(config.track.fovPixels));
    trackSmooth_->setValue(config.track.smooth);
    trackKey_->setCode(config.track.key);

    silentEnabled_->setChecked(config.silent.enabled);
    silentTeam_->setChecked(config.silent.teamCheck);
    silentFov_->setValue(static_cast<int>(config.silent.fovPixels));
    silentHold_->setValue(config.silent.holdMs);
    silentKey_->setCode(config.silent.key);

    triggerEnabled_->setChecked(config.trigger.enabled);
    triggerDelay_->setValue(config.trigger.delayMs);
    triggerCooldown_->setValue(config.trigger.cooldownMs);
    triggerKey_->setCode(config.trigger.key);
    syncing_ = false;
}

}  // namespace wv
