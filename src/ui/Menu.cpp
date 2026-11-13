#include "Menu.h"

#include "core/LogPath.h"

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
#include <QLineEdit>
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
// 深色底板需要更高的不透明度：浅色文字压在半透明黑上对比度天然低，
// 235 让背后画面只剩一层很淡的流动感，文字保持清晰。
constexpr int kGlassAlpha = 235;

// 面板视觉：无边框半透明玻璃底板（paintEvent 绘制）+ 极简控件。
// 浅色玻璃：底板用近白，文字与线条走深灰；只有描边和阴影压一点灰度，不用彩色强调。
// 深色双栏：侧栏承载品牌 / 搜索 / 分类，右栏是页头 + 卡片行表。
// 全部走半透明描边与低对比填充，避免纯黑大色块在游戏画面上显得突兀。
const char* kStyle = R"QSS(
QWidget {
    background: transparent;
    color: #E7E9EC;
    font-family: "Noto Sans CJK SC", "Source Han Sans SC", "DejaVu Sans", sans-serif;
    font-size: 12px;
}
QLabel#brand {
    color: #FFFFFF;
    font-family: "DejaVu Sans Mono", monospace;
    font-size: 15px;
    font-weight: 700;
    letter-spacing: 3px;
}
QLabel#brandSub { color: #7C828B; font-size: 10px; }
QLabel#pageTitle { color: #FFFFFF; font-size: 19px; font-weight: 700; }
QLabel#pageSub { color: #7C828B; font-size: 10px; }
QLabel#rowTitle { color: #EFF1F4; font-size: 13px; font-weight: 600; }
QLabel#rowSub { color: #757B84; font-size: 9.5px; }
QLabel#field { color: #AEB4BC; font-size: 11px; }
QLabel#status {
    color: #6E747D;
    font-family: "DejaVu Sans Mono", monospace;
    font-size: 10px;
}
QLabel#stats {
    color: #6E747D;
    font-family: "DejaVu Sans Mono", monospace;
    font-size: 10px;
}
QLabel#diag {
    color: #8C939C;
    font-family: "DejaVu Sans Mono", monospace;
    font-size: 9.5px;
}
QLabel#hint { color: #6E747D; font-size: 10px; }
QLabel#warn { color: #F0A6A6; font-size: 10px; font-weight: 700; }
QWidget#card {
    background: rgba(255, 255, 255, 12);
    border: 1px solid rgba(255, 255, 255, 10);
    border-radius: 10px;
}
QWidget#card:hover { background: rgba(255, 255, 255, 20); }
QWidget#navColumn {
    background: rgba(255, 255, 255, 6);
    border: 1px solid rgba(255, 255, 255, 10);
    border-radius: 12px;
}
QFrame#rule { background: rgba(255, 255, 255, 22); border: none; min-height: 1px; max-height: 1px; }
QLineEdit#search {
    background: rgba(255, 255, 255, 10);
    border: 1px solid rgba(255, 255, 255, 12);
    border-radius: 8px;
    padding: 6px 9px;
    color: #E7E9EC;
    font-size: 11px;
}
QLineEdit#search:focus { border-color: rgba(255, 255, 255, 40); }
/* 开关的轨道与滑块由 ToggleSwitch 自绘（iOS 形制），这里只保留文字色基线。 */
QCheckBox { color: #E7E9EC; }
QSpinBox, QDoubleSpinBox, QComboBox {
    background: rgba(255, 255, 255, 10);
    border: 1px solid rgba(255, 255, 255, 14);
    border-radius: 7px;
    padding: 3px 7px;
    min-height: 16px;
    color: #E7E9EC;
    font-family: "DejaVu Sans Mono", monospace;
    font-size: 11px;
    selection-background-color: #E7E9EC;
    selection-color: #14171A;
}
QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover {
    background: rgba(255, 255, 255, 18);
    border-color: rgba(255, 255, 255, 30);
}
QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border-color: rgba(255, 255, 255, 55);
    background: rgba(255, 255, 255, 16);
}
QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    width: 0;
    height: 0;
    border: none;
}
QComboBox::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView {
    background: #1B1E22;
    border: 1px solid rgba(255, 255, 255, 26);
    border-radius: 7px;
    padding: 4px;
    outline: none;
    color: #E7E9EC;
    selection-background-color: rgba(255, 255, 255, 210);
    selection-color: #14171A;
}
QPushButton#apply {
    background: rgba(255, 255, 255, 16);
    color: #FFFFFF;
    border: 1px solid rgba(255, 255, 255, 26);
    border-radius: 8px;
    padding: 6px 12px;
    font-size: 11px;
}
QPushButton#apply:hover { background: rgba(255, 255, 255, 235); color: #14171A; border-color: transparent; }
QPushButton#apply:pressed { background: rgba(255, 255, 255, 160); color: #14171A; }
QPushButton#restore {
    background: transparent;
    color: #9AA0A8;
    border: 1px solid rgba(255, 255, 255, 16);
    border-radius: 8px;
    padding: 8px 10px;
    font-size: 11px;
}
QPushButton#restore:hover { background: rgba(255, 255, 255, 14); color: #FFFFFF; border-color: rgba(255, 255, 255, 40); }
QPushButton#restore:pressed { background: rgba(255, 255, 255, 40); color: #FFFFFF; }
QPushButton#close {
    background: transparent;
    border: 1px solid rgba(255, 255, 255, 16);
    border-radius: 8px;
    color: #9AA0A8;
    font-size: 11px;
    padding: 8px 10px;
}
QPushButton#close:hover { background: rgba(255, 255, 255, 14); color: #FFFFFF; }
QPlainTextEdit#log {
    background: rgba(0, 0, 0, 60);
    border: 1px solid rgba(255, 255, 255, 10);
    border-radius: 8px;
    color: #9AA1AA;
    font-family: "DejaVu Sans Mono", monospace;
    font-size: 9px;
    selection-background-color: rgba(255, 255, 255, 200);
    selection-color: #14171A;
}
QScrollBar:vertical {
    background: transparent;
    width: 5px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: rgba(255, 255, 255, 34);
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
    border-radius: 8px;
    color: #9AA0A8;
    font-size: 12px;
    padding: 0 10px;
    text-align: left;
}
QPushButton:hover { background: rgba(255, 255, 255, 14); color: #FFFFFF; }
QPushButton:checked {
    background: rgba(255, 255, 255, 222);
    color: #14171A;
    font-weight: 700;
    border-color: transparent;
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
                // 深色底板：颗粒必须提亮才看得见，alpha 同样压到极低。
                const bool light = rng.bounded(2) == 0;
                line[x] = light ? qRgba(255, 255, 255, 3) : qRgba(255, 255, 255, 0);
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
        // 深色底板：关闭是低对比灰轨道，开启是近白轨道，靠明度差表达状态。
        painter.setBrush(blend(QColor(255, 255, 255, 36), QColor(240, 242, 245, 245), knob_));
        painter.drawRoundedRect(track, radius, radius);

        // 滑块随状态反转：关闭时白点压在暗轨道上，开启时暗点压在近白轨道上，
        // 两种状态都保持对比，不会出现「白点贴白轨」看不清。
        const qreal knobSize = kTrackHeight - 4.0;
        const qreal knobX = track.left() + 2.0 + knob_ * (kTrackWidth - 4.0 - knobSize);
        painter.setBrush(blend(QColor(245, 247, 250), QColor(24, 27, 31), knob_));
        painter.drawEllipse(QRectF(knobX, track.top() + 2.0, knobSize, knobSize));

        const QFontMetrics metrics(font());
        const int textLeft = kTrackWidth + kGap;
        const QRect textRect(textLeft, 0, std::max(0, width() - textLeft), height());
        painter.setPen(isEnabled() ? QColor(231, 233, 236) : QColor(120, 126, 134));
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

// 当前页的行副标题默认值：参照参考图，每行名称下方挂一行模块名。
// UI 单线程构建，用函数内静态量传递，避免再给 makeRow 加参数。
QString& rowSubFallback() {
    static QString value = "模块设置";
    return value;
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
            "QPushButton{background:rgba(255,255,255,10);border:1px solid rgba(255,255,255,14);"
            "border-radius:7px;color:#E7E9EC;font-size:11px;padding:3px 6px;}"
            "QPushButton:hover{background:rgba(255,255,255,20);border-color:rgba(255,255,255,34);}"
            "QPushButton:focus{border-color:rgba(255,255,255,70);}");
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
    // 侧栏 196px + 右侧内容区：与参考图的双栏比例接近，卡片行有足够宽度。
    setFixedWidth(720);
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

        // F10：清零本局战绩。实际写在 UI 线程做，避免与掉血检测的 fetch_add 交叉。
        if (state_.statsReset.exchange(0) != 0) resetStats();

        // 直接内存页：把引擎的可写状态与缺失偏移摊在面板上，避免「开了没反应」。
        if (memStatus_) {
            if (state_.memoryWritable.load() == 0) {
                memStatus_->setText("内存只读：/proc/<pid>/mem 需 O_RDWR（root 或 ptrace_scope=0）");
            } else if (const int missing = state_.memoryMissing.load(); missing > 0) {
                memStatus_->setText(
                    QString("缺 %1 项偏移：重跑生成器或导入 dumper 输出").arg(missing));
            } else {
                memStatus_->setText(
                    QString("可写 · 累计写入 %1 次").arg(state_.memoryWrites.load()));
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
                stats_->setText(QString("采集 %1 · 方框 %2 · 队友 %3 · 距离 %4 · 命中 %5 · 伤害 %6")
                                    .arg(seen)
                                    .arg(state_.espDrawn.load())
                                    .arg(state_.espTeamFiltered.load())
                                    .arg(state_.espDistanceFiltered.load())
                                    .arg(state_.espHits.load())
                                    .arg(state_.espDamage.load()));
            }
        }

        // 自检每 2 秒刷新一次：重建 HTML 有开销，不必跟着 400 ms 轮询跑。
        if (diag_ && ++diagTick_ % 5 == 0) {
            QString html;
            for (const DiagItem& item : game_.diagnose()) {
                // 一次性四参数替换，避免链式 arg 在富文本里错位；
                // 用 OK / -- 而非勾叉，规避等宽字体缺符号时的回退。
                html += QStringLiteral("<span style='color:%1'>%2</span> %3  "
                                       "<span style='color:#6B7280'>%4</span><br>")
                            .arg(item.ok ? QStringLiteral("#2E7D32") : QStringLiteral("#B91C1C"),
                                 item.ok ? QStringLiteral("OK") : QStringLiteral("--"),
                                 QString::fromStdString(item.name),
                                 QString::fromStdString(item.detail));
            }
            diag_->setText(html.isEmpty() ? QStringLiteral("（无数据）") : html);
        }

        // 日志区可见时才读文件，滚出视野后零开销。
        if (logView_ && logView_->isVisible()) {
            logView_->setPlainText(tailOfLog(wv::logPath().c_str()));
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
        painter.fillRect(rect(), QColor(16, 18, 21, kGlassAlpha));
    } else {
        if (backdropDirty_) rebuildBackdrop();
        if (!backdropBlurred_.isNull()) {
            painter.drawPixmap(rect(), backdropBlurred_);
            // 压一层深色玻璃：保证浅色文字可读，同时让背后画面只留一点变化感。
            painter.fillRect(rect(), QColor(16, 18, 21, kGlassAlpha));
        } else {
            painter.fillRect(rect(), QColor(16, 18, 21, 255));
        }
    }

    // 磨砂质感：浅色噪声颗粒（深底上要提亮才看得见）+ 顶部一道极淡的提亮。
    painter.drawTiledPixmap(rect(), grainTexture());
    QLinearGradient sheen(0.0, 0.0, 0.0, static_cast<qreal>(height()));
    sheen.setColorAt(0.0, QColor(255, 255, 255, 7));
    sheen.setColorAt(0.4, QColor(255, 255, 255, 0));
    painter.fillRect(rect(), sheen);
    painter.setClipping(false);

    // 外描边压一层很淡的浅灰，只勾出圆角轮廓，不留下明显边框。
    painter.setPen(QPen(QColor(255, 255, 255, 26), 1.0));
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

    // 卡片行：左侧两行文字（名称 + 副标题），右侧参数入口与开关。
    // 与参考图一致，开关是行的主角，名称不再挂在开关自己的文字上。
    auto* row = new QWidget(unit);
    row->setObjectName("card");
    row->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(14, 10, 12, 10);
    layout->setSpacing(10);

    auto* textColumn = new QVBoxLayout();
    textColumn->setContentsMargins(0, 0, 0, 0);
    textColumn->setSpacing(2);
    auto* nameLabel = new QLabel(name, row);
    nameLabel->setObjectName("rowTitle");
    auto* subLabel = new QLabel(hint.isEmpty() ? rowSubFallback() : hint, row);
    subLabel->setObjectName("rowSub");
    textColumn->addWidget(nameLabel);
    textColumn->addWidget(subLabel);
    layout->addLayout(textColumn, 1);

    // 记录行名供搜索过滤：只匹配名称，不做子串以外的模糊。
    row->setProperty("rowName", name);

    // 开关只画轨道，不吃文字；命中范围由 ToggleSwitch::hitButton 覆盖整块。
    toggle->setText(QString());
    toggle->setCursor(Qt::PointingHandCursor);
    toggle->setFixedWidth(34);
    unitLayout->addWidget(row);

    // 参数入口放在开关左侧：先声明、后入列，顺序即「参数 → 开关」。
    QPushButton* entry = nullptr;
    if (build) {
        entry = new QPushButton(QString::fromUtf8("参数"), row);
        entry->setCursor(Qt::PointingHandCursor);
        entry->setFocusPolicy(Qt::NoFocus);
        entry->setStyleSheet(
            "QPushButton{background:rgba(255,255,255,10);border:1px solid rgba(255,255,255,12);"
            "border-radius:6px;color:#AEB4BC;font-size:10px;padding:3px 8px;}"
            "QPushButton:hover{background:rgba(255,255,255,22);color:#FFFFFF;}");
        layout->addWidget(entry, 0, Qt::AlignVCenter);
    }
    layout->addWidget(toggle, 0, Qt::AlignVCenter);

    if (!build) {
        return unit;
    }

    auto* panel = new QWidget(unit);
    panel->setObjectName("paramPanel");
    panel->setAttribute(Qt::WA_StyledBackground, true);
    panel->setStyleSheet(
        "QWidget#paramPanel{background:rgba(0,0,0,70);border:1px solid rgba(255,255,255,8);"
        "border-radius:9px;}");
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
    root->setSpacing(0);
    root->setContentsMargins(0, 0, 0, 0);

    // ---- 主体：左品牌 / 搜索 / 分类 侧栏 + 右页头 / 卡片行表 ----
    auto* body = new QHBoxLayout();
    body->setSpacing(0);
    body->setContentsMargins(0, 0, 0, 0);

    auto* navColumn = new QWidget(this);
    navColumn->setObjectName("navColumn");
    navColumn->setFixedWidth(196);
    auto* navLayout = new QVBoxLayout(navColumn);
    navLayout->setContentsMargins(16, 18, 14, 16);
    navLayout->setSpacing(6);

    // 品牌区：大写字标 + 一行状态副标题（沿用原来的连接状态文本）。
    auto* brand = new QLabel("CS2 LITE", navColumn);
    brand->setObjectName("brand");
    navLayout->addWidget(brand);
    status_ = new QLabel(QString::fromStdString(game_.status()), navColumn);
    status_->setObjectName("brandSub");
    status_->setWordWrap(true);
    navLayout->addWidget(status_);
    navLayout->addSpacing(10);

    // 搜索：只过滤当前页的卡片行，空串恢复全部。
    search_ = new QLineEdit(navColumn);
    search_->setObjectName("search");
    search_->setPlaceholderText("搜索功能…");
    search_->setClearButtonEnabled(true);
    navLayout->addWidget(search_);
    navLayout->addSpacing(8);

    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(18, 18, 18, 16);
    contentLayout->setSpacing(10);

    // 侧栏图标走几何符号，避免依赖 emoji 字体（叠加层与面板都只用等宽/无衬线）。
    const QStringList tabNames{"◈  玩家透视", "◎  瞄准", "⌖  扳机", "▤  内存", "≡  日志"};
    for (int i = 0; i < tabNames.size(); ++i) {
        auto* tab = new QPushButton(tabNames.at(i), navColumn);
        tab->setObjectName("nav");
        tab->setStyleSheet(kNavStyle);
        tab->setCheckable(true);
        tab->setAutoExclusive(true);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setFocusPolicy(Qt::NoFocus);
        tab->setFixedHeight(36);
        tab->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        navLayout->addWidget(tab);
        tabs_[i] = tab;
        connect(tab, &QPushButton::clicked, this, [this, i] { setPage(i); });
    }
    if (tabs_[0]) tabs_[0]->setChecked(true);
    navLayout->addStretch(1);

    // 侧栏底部：恢复与关闭，与参考图的「重置当前页面 / 关闭」同位。
    auto* restoreButton = new QPushButton("恢 复 上 一 次", navColumn);
    restoreButton->setObjectName("restore");
    restoreButton->setCursor(Qt::PointingHandCursor);
    connect(restoreButton, &QPushButton::clicked, this, &Menu::restoreLast);
    navLayout->addWidget(restoreButton);

    auto* closeButton = new QPushButton("关 闭", navColumn);
    closeButton->setObjectName("close");
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QPushButton::clicked, this, [this] { close(); });
    navLayout->addWidget(closeButton);

    // ---- 页头：标题 + 副标题，右侧落盘按钮 ----
    auto* pageHeader = new QHBoxLayout();
    pageHeader->setSpacing(10);
    auto* titleColumn = new QVBoxLayout();
    titleColumn->setContentsMargins(0, 0, 0, 0);
    titleColumn->setSpacing(3);
    pageTitle_ = new QLabel("玩家透视", content);
    pageTitle_->setObjectName("pageTitle");
    pageSub_ = new QLabel("ESP 模块与绘制", content);
    pageSub_->setObjectName("pageSub");
    titleColumn->addWidget(pageTitle_);
    titleColumn->addWidget(pageSub_);
    pageHeader->addLayout(titleColumn, 1);

    auto* applyButton = new QPushButton("保存", content);
    applyButton->setObjectName("apply");
    applyButton->setCursor(Qt::PointingHandCursor);
    connect(applyButton, &QPushButton::clicked, this, &Menu::apply);
    pageHeader->addWidget(applyButton, 0, Qt::AlignTop);
    contentLayout->addLayout(pageHeader);

    stats_ = new QLabel(content);
    stats_->setObjectName("stats");
    contentLayout->addWidget(stats_);

    pages_ = new QStackedWidget(content);
    pages_->setObjectName("pages");
    contentLayout->addWidget(pages_, 1);

    body->addWidget(navColumn);
    body->addWidget(content, 1);
    root->addLayout(body, 1);

    // 每页一个竖排行表：行高固定，内容超出时页内滚动。
    auto addPage = [this](const QString& sub) -> QVBoxLayout* {
        // 该页所有未显式给 hint 的行，副标题统一用模块名。
        rowSubFallback() = sub;
        auto* page = new QWidget(this);
        auto* outer = new QVBoxLayout(page);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        auto* scroll = new QScrollArea(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // 行表是本面板的主体：给一个下限高度，避免 QStackedWidget 按空页压缩整窗。
        // 卡片行比旧版高（两行文字 + 10px 内边距），下限相应抬到 520。
        scroll->setMinimumHeight(520);
        auto* list = new QWidget(scroll);
        auto* listLayout = new QVBoxLayout(list);
        listLayout->setContentsMargins(0, 0, 8, 0);
        listLayout->setSpacing(8);
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
    espCrosshair_ = new ToggleSwitch(this);
    espCrosshair_->setChecked(initial.esp.crosshairInfo);

    espRadarPos_ = posCombo(this, initial.esp.radarPos);
    espListPos_ = posCombo(this, initial.esp.listPos);
    espStatsHud_ = new ToggleSwitch(this);
    espStatsHud_->setChecked(initial.esp.statsHud);
    espStatsHudPos_ = posCombo(this, initial.esp.statsHudPos);

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
    // trigger.teamCheck 一直存在于配置里，但面板从来没给过入口 —— 补上。
    triggerTeam_ = new ToggleSwitch(this);
    triggerTeam_->setChecked(initial.trigger.teamCheck);

    togglePanelKey_ = makeKeyBind(this, initial.togglePanelKey);
    togglePanelKey_->onChanged = [this] { armPersist(); };
    panicKey_ = makeKeyBind(this, initial.panicKey);
    panicKey_->onChanged = [this] { armPersist(); };
    resetStatsKey_ = makeKeyBind(this, initial.resetStatsKey);
    resetStatsKey_->onChanged = [this] { armPersist(); };
    resetStatsButton_ = new QPushButton("重置", this);
    resetStatsButton_->setCursor(Qt::PointingHandCursor);
    resetStatsButton_->setFixedWidth(96);
    connect(resetStatsButton_, &QPushButton::clicked, this, [this] { resetStats(); });

    // ---- 直接内存页控件（开关默认全关，偏移全 0 时功能静默跳过）----
    memSpeed_ = new ToggleSwitch(this);
    memSpeed_->setChecked(initial.memory.speed);
    memSpeedFactor_ = new QDoubleSpinBox(this);
    memSpeedFactor_->setRange(1.0, 5.0);
    memSpeedFactor_->setSingleStep(0.1);
    memSpeedFactor_->setValue(initial.memory.speedFactor);
    memSpeedFactor_->setFixedWidth(84);

    memBulletTrack_ = new ToggleSwitch(this);
    memBulletTrack_->setChecked(initial.memory.bulletTrack);
    memBulletTrackKey_ = makeKeyBind(this, initial.memory.bulletTrackKey);
    memBulletTrackKey_->onChanged = [this] { armPersist(); };
    memBulletTrackDistance_ = new QSpinBox(this);
    memBulletTrackDistance_->setRange(0, 500);
    memBulletTrackDistance_->setSuffix(" m");
    memBulletTrackDistance_->setValue(
        static_cast<int>(initial.memory.bulletTrackMaxDistance));
    memBulletTrackDistance_->setFixedWidth(84);
    memBulletTeam_ = new ToggleSwitch(this);
    memBulletTeam_->setChecked(initial.memory.bulletTrackTeamCheck);
    memBulletProtectView_ = new ToggleSwitch(this);
    memBulletProtectView_->setChecked(initial.memory.bulletLockProtectView);
    memBulletSyncView_ = new ToggleSwitch(this);
    memBulletSyncView_->setChecked(initial.memory.bulletLockSyncView);

    memFly_ = new ToggleSwitch(this);
    memFly_->setChecked(initial.memory.fly);
    memFlyUpKey_ = makeKeyBind(this, initial.memory.flyUpKey);
    memFlyUpKey_->onChanged = [this] { armPersist(); };
    memFlyDownKey_ = makeKeyBind(this, initial.memory.flyDownKey);
    memFlyDownKey_->onChanged = [this] { armPersist(); };
    memFlySpeed_ = new QSpinBox(this);
    memFlySpeed_->setRange(50, 2000);
    memFlySpeed_->setSingleStep(50);
    memFlySpeed_->setValue(static_cast<int>(initial.memory.flySpeed));
    memFlySpeed_->setFixedWidth(84);
    memFlyMoveType_ = new QSpinBox(this);
    memFlyMoveType_->setRange(0, 15);
    memFlyMoveType_->setValue(initial.memory.flyMoveType);
    memFlyMoveType_->setFixedWidth(84);

    memSpin_ = new ToggleSwitch(this);
    memSpin_->setChecked(initial.memory.spin);
    memSpinRate_ = new QSpinBox(this);
    memSpinRate_->setRange(-720, 720);
    memSpinRate_->setSuffix(" °/s");
    memSpinRate_->setValue(static_cast<int>(initial.memory.spinDegreesPerSec));
    memSpinRate_->setFixedWidth(84);
    memSpinKeepView_ = new ToggleSwitch(this);
    memSpinKeepView_->setChecked(initial.memory.spinKeepView);

    memNoRecoil_ = new ToggleSwitch(this);
    memNoRecoil_->setChecked(initial.memory.noRecoil);
    memAccuracy_ = new ToggleSwitch(this);
    memAccuracy_->setChecked(initial.memory.accuracy);
    memAccuracySafe_ = new ToggleSwitch(this);
    memAccuracySafe_->setChecked(initial.memory.accuracySafeMode);
    memStatus_ = new QLabel(this);
    memStatus_->setObjectName("field");
    memStatus_->setWordWrap(true);

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
    // silent.cooldownMs 同样一直没入口：贴头开火的节奏全由它决定。
    silentCooldown_ = new QSpinBox(this);
    silentCooldown_->setRange(10, 2000);
    silentCooldown_->setSuffix(" ms");
    silentCooldown_->setValue(initial.silent.cooldownMs);
    silentCooldown_->setFixedWidth(82);
    silentKey_ = makeKeyBind(this, initial.silent.key);
    silentKey_->onChanged = [this] { armPersist(); };

    // 参数控件直接挂在各行下方的下拉区里，不再需要临时隐藏容器。

    // ---- 页一：透视 ----
    QVBoxLayout* espLayout = addPage(QStringLiteral("ESP"));
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
    espLayout->addWidget(makeRow(espCrosshair_, "准星信息", ""));
    espLayout->addWidget(makeRow(espStatsHud_, "战绩 HUD", "", [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("位置", espStatsHudPos_, parent));
    }));
    espLayout->addStretch(1);

    // ---- 页二：自瞄 ----
    QVBoxLayout* aimLayout = addPage(QStringLiteral("Combat"));
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
    // 后坐力抵消与子弹追踪已归到「扳机」页：它们都属于开火链路，
    // 放在瞄准页会让两个页面都显得空。
    aimLayout->addStretch(1);

    // ---- 页三：扳机 ----
    QVBoxLayout* triggerLayout = addPage(QStringLiteral("Trigger"));
    triggerLayout->addWidget(makeRow(triggerEnabled_, "自动扳机", "准星进人即开火",
                                     [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("按住", triggerDelay_, parent));
        layout->addWidget(paramRow("冷却", triggerCooldown_, parent));
        layout->addWidget(paramRow("触发键", triggerKey_, parent));
        layout->addWidget(paramRow("过滤队友", triggerTeam_, parent));
    }));
    triggerLayout->addWidget(makeRow(silentEnabled_, "静默锁头", "贴头开火后归位",
                                     [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("半径", silentFov_, parent));
        layout->addWidget(paramRow("停留", silentHold_, parent));
        layout->addWidget(paramRow("冷却", silentCooldown_, parent));
        layout->addWidget(paramRow("触发键", silentKey_, parent));
        layout->addWidget(paramRow("过滤队友", silentTeam_, parent));
    }));
    // 子弹追踪（XTest 贴头）本质是开火行为，放在扳机页比放在瞄准页更顺手。
    triggerLayout->addWidget(makeRow(trackEnabled_, "子弹追踪", "贴头跟随准星",
                                     [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("半径", trackFov_, parent));
        layout->addWidget(paramRow("平滑", trackSmooth_, parent));
        layout->addWidget(paramRow("触发键", trackKey_, parent));
        layout->addWidget(paramRow("过滤队友", trackTeam_, parent));
    }));
    // 后坐力抵消按按下沿反向补偿，同属开火链路。
    triggerLayout->addWidget(makeRow(recoilEnabled_, "后坐力抵消", "反向补偿准星",
                                     [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("强度", recoilStrength_, parent));
        layout->addWidget(paramRow("单帧上限", recoilMax_, parent));
        layout->addWidget(paramRow("仅开火时", recoilRequireFire_, parent));
    }));
    triggerLayout->addStretch(1);

    // ---- 页四：直接内存 ----
    QVBoxLayout* memoryLayout = addPage(QStringLiteral("Memory"));
    memoryLayout->addWidget(makeRow(memSpeed_, "加速", "", [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("倍率", memSpeedFactor_, parent));
    }));
    memoryLayout->addWidget(makeRow(memBulletTrack_, "子弹追踪内存锁", "锁射击角·视角不动",
                                    [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("触发键 0=常驻", memBulletTrackKey_, parent));
        layout->addWidget(paramRow("最远距离", memBulletTrackDistance_, parent));
        layout->addWidget(paramRow("过滤队友", memBulletTeam_, parent));
        layout->addWidget(paramRow("视角不动(写回v_angle)", memBulletProtectView_, parent));
        layout->addWidget(paramRow("命中优先(视角跟随)", memBulletSyncView_, parent));
    }));
    memoryLayout->addWidget(makeRow(memFly_, "飞天", "", [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("上升键", memFlyUpKey_, parent));
        layout->addWidget(paramRow("下降键", memFlyDownKey_, parent));
        layout->addWidget(paramRow("速度", memFlySpeed_, parent));
        layout->addWidget(paramRow("移动类型", memFlyMoveType_, parent));
    }));
    memoryLayout->addWidget(makeRow(memSpin_, "身体旋转", "", [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("角速度", memSpinRate_, parent));
        layout->addWidget(paramRow("视角不动(不写absRot)", memSpinKeepView_, parent));
    }));
    memoryLayout->addWidget(makeRow(memNoRecoil_, "真无后坐力", ""));
    memoryLayout->addWidget(makeRow(memAccuracy_, "弹着点收成一点", "清散布/抖动",
                                    [this](QVBoxLayout* layout) {
        QWidget* parent = layout->parentWidget();
        layout->addWidget(paramRow("安全模式(不清预测字段)", memAccuracySafe_, parent));
    }));
    memStatus_->setText("偏移未配置：重跑 tools/cs2_linux_offsets.py 或导入 dumper 输出");
    memoryLayout->addWidget(memStatus_);
    memoryLayout->addStretch(1);

    // ---- 页五：日志 ----
    QVBoxLayout* logLayout = addPage(QStringLiteral("Diagnostics"));

    // 全局快捷键放在日志页顶部：这页空间充裕，且不属于任何单个功能。
    logLayout->addWidget(paramRow("面板显隐键", togglePanelKey_, this));
    logLayout->addWidget(paramRow("一键静默键", panicKey_, this));
    logLayout->addWidget(paramRow("清零战绩键", resetStatsKey_, this));
    logLayout->addWidget(paramRow("本局战绩", resetStatsButton_, this));
    logLayout->addSpacing(6);

    // 自检区：偏移与运行时指针逐项列出，缺哪个一眼可见。
    diag_ = new QLabel(this);
    diag_->setObjectName("diag");
    diag_->setTextFormat(Qt::RichText);
    diag_->setText("（等待连接…）");
    logLayout->addWidget(diag_);
    logLayout->addSpacing(8);

    logView_ = new QPlainTextEdit(this);
    logView_->setObjectName("log");
    logView_->setReadOnly(true);
    logView_->setFrameShape(QFrame::NoFrame);
    logView_->setMinimumHeight(200);
    logView_->setPlainText(tailOfLog(wv::logPath().c_str()));
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

    // 搜索过滤：只作用当前页，按行卡片上记录的 rowName 匹配。
    connect(search_, &QLineEdit::textChanged, this,
            [this](const QString& text) { filterRows(text); });

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

void Menu::setPage(int index) {
    if (!pages_) return;
    pages_->setCurrentIndex(index);
    // 页头与侧栏选中态一起走，避免两处状态分叉。
    if (index >= 0 && index < 5 && tabs_[index]) {
        tabs_[index]->setChecked(true);
    }
    static const char* kTitles[] = {"玩家透视", "瞄准", "扳机", "内存", "日志"};
    static const char* kSubs[] = {"ESP 模块与绘制", "自瞄与追踪", "扳机与静默",
                                  "直接内存写入", "诊断与快捷键"};
    if (pageTitle_ && index >= 0 && index < 5) {
        pageTitle_->setText(QString::fromUtf8(kTitles[index]));
        pageSub_->setText(QString::fromUtf8(kSubs[index]));
    }
    // 切页后搜索词沿用：用户常常在多个页里找同一类功能。
    if (search_ && !search_->text().isEmpty()) filterRows(search_->text());
}

void Menu::filterRows(const QString& keyword) {
    if (!pages_) return;
    QWidget* page = pages_->currentWidget();
    if (!page) return;
    const QString needle = keyword.trimmed();
    for (QWidget* card : page->findChildren<QWidget*>()) {
        const QVariant name = card->property("rowName");
        if (!name.isValid()) continue;
        const bool match = needle.isEmpty() ||
                           name.toString().contains(needle, Qt::CaseInsensitive);
        // 卡片所在 unit 可能是「行 + 参数面板」组合，隐藏 unit 才能连参数一起收起。
        QWidget* target = card->parentWidget() ? card->parentWidget() : card;
        target->setVisible(match);
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
    next.esp.crosshairInfo = espCrosshair_->isChecked();
    next.esp.statsHud = espStatsHud_->isChecked();
    next.esp.statsHudPos = espStatsHudPos_->currentData().toInt();
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
    next.silent.cooldownMs = silentCooldown_->value();
    next.silent.key = silentKey_->code();

    next.trigger.enabled = triggerEnabled_->isChecked();
    next.trigger.delayMs = triggerDelay_->value();
    next.trigger.cooldownMs = triggerCooldown_->value();
    next.trigger.key = triggerKey_->code();
    next.trigger.teamCheck = triggerTeam_->isChecked();

    next.togglePanelKey = togglePanelKey_->code();
    next.panicKey = panicKey_->code();
    next.resetStatsKey = resetStatsKey_->code();

    next.memory.speed = memSpeed_->isChecked();
    next.memory.speedFactor = static_cast<float>(memSpeedFactor_->value());
    next.memory.bulletTrack = memBulletTrack_->isChecked();
    next.memory.bulletTrackKey = memBulletTrackKey_->code();
    next.memory.bulletTrackMaxDistance =
        static_cast<float>(memBulletTrackDistance_->value());
    next.memory.bulletTrackTeamCheck = memBulletTeam_->isChecked();
    next.memory.bulletLockProtectView = memBulletProtectView_->isChecked();
    next.memory.bulletLockSyncView = memBulletSyncView_->isChecked();
    next.memory.fly = memFly_->isChecked();
    next.memory.flyUpKey = memFlyUpKey_->code();
    next.memory.flyDownKey = memFlyDownKey_->code();
    next.memory.flySpeed = static_cast<float>(memFlySpeed_->value());
    next.memory.flyMoveType = memFlyMoveType_->value();
    next.memory.spin = memSpin_->isChecked();
    next.memory.spinDegreesPerSec = static_cast<float>(memSpinRate_->value());
    next.memory.spinKeepView = memSpinKeepView_->isChecked();
    next.memory.noRecoil = memNoRecoil_->isChecked();
    next.memory.accuracy = memAccuracy_->isChecked();
    next.memory.accuracySafeMode = memAccuracySafe_->isChecked();

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

void Menu::resetStats() {
    state_.espHits.store(0);
    state_.espDamage.store(0);
    state_.espKills.store(0);
    if (status_) {
        status_->setText("本局战绩已清零");
        // 状态行每 400 ms 会被连接状态覆盖，提示要压过这轮轮询。
        messageUntil_ = QDateTime::currentDateTime().addSecs(2);
    }
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
    espCrosshair_->setChecked(config.esp.crosshairInfo);
    espStatsHud_->setChecked(config.esp.statsHud);
    espStatsHudPos_->setCurrentIndex(
        std::clamp(espStatsHudPos_->findData(config.esp.statsHudPos), 0, 4));
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
    silentCooldown_->setValue(config.silent.cooldownMs);
    silentKey_->setCode(config.silent.key);

    triggerEnabled_->setChecked(config.trigger.enabled);
    triggerDelay_->setValue(config.trigger.delayMs);
    triggerCooldown_->setValue(config.trigger.cooldownMs);
    triggerKey_->setCode(config.trigger.key);
    triggerTeam_->setChecked(config.trigger.teamCheck);
    togglePanelKey_->setCode(config.togglePanelKey);
    panicKey_->setCode(config.panicKey);
    resetStatsKey_->setCode(config.resetStatsKey);

    memSpeed_->setChecked(config.memory.speed);
    memSpeedFactor_->setValue(config.memory.speedFactor);
    memBulletTrack_->setChecked(config.memory.bulletTrack);
    memBulletTrackKey_->setCode(config.memory.bulletTrackKey);
    memBulletTrackDistance_->setValue(
        static_cast<int>(config.memory.bulletTrackMaxDistance));
    memBulletTeam_->setChecked(config.memory.bulletTrackTeamCheck);
    memBulletProtectView_->setChecked(config.memory.bulletLockProtectView);
    memBulletSyncView_->setChecked(config.memory.bulletLockSyncView);
    memFly_->setChecked(config.memory.fly);
    memFlyUpKey_->setCode(config.memory.flyUpKey);
    memFlyDownKey_->setCode(config.memory.flyDownKey);
    memFlySpeed_->setValue(static_cast<int>(config.memory.flySpeed));
    memFlyMoveType_->setValue(config.memory.flyMoveType);
    memSpin_->setChecked(config.memory.spin);
    memSpinRate_->setValue(static_cast<int>(config.memory.spinDegreesPerSec));
    memSpinKeepView_->setChecked(config.memory.spinKeepView);
    memNoRecoil_->setChecked(config.memory.noRecoil);
    memAccuracy_->setChecked(config.memory.accuracy);
    memAccuracySafe_->setChecked(config.memory.accuracySafeMode);
    syncing_ = false;
}

}  // namespace wv
