// Xlib 头会定义 Status / Bool / CursorShape 等宏，与 Qt6 头冲突
// （Qt 明确要求自己的头先包含）。这里只声明需要的符号，不引入那套宏。
extern "C" int XInitThreads(void);

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QString>

#include <cstdio>
#include <memory>
#include <string>

#include "core/Config.h"
#include "core/LogPath.h"
#include "features/Features.h"
#include "features/MemoryEngine.h"
#include "game/Game.h"
#include "render/Overlay.h"
#include "ui/Menu.h"

namespace {

void appendLog(const QString& line) {
    if (FILE* file = std::fopen(wv::logPath().c_str(), "a")) {
        std::fprintf(file, "[offsets] %s\n", line.toUtf8().constData());
        std::fclose(file);
    }
}

// 启动时重跑一次偏移生成器：游戏更新后 dw* 与字段偏移都会漂移，
// 每次启动自动刷新，失败则安静沿用现有 config/offsets.json。
// WV_SKIP_OFFSETS=1 可跳过（生成器异常或不想等待时使用）。
void refreshOffsets() {
    if (qEnvironmentVariableIsSet("WV_SKIP_OFFSETS")) {
        appendLog("跳过自动生成（WV_SKIP_OFFSETS 已设置）");
        return;
    }

    // 从可执行文件目录向上找仓库根：从仓库根或 build/ 启动都能命中。
    QDir dir(QCoreApplication::applicationDirPath());
    QString root;
    for (int depth = 0; depth < 5; ++depth) {
        if (QFile::exists(dir.absoluteFilePath("tools/cs2_linux_offsets.py"))) {
            root = dir.absolutePath();
            break;
        }
        if (!dir.cdUp()) break;
    }
    if (root.isEmpty()) {
        appendLog("未找到 tools/cs2_linux_offsets.py，沿用现有偏移");
        return;
    }

    QProcess process;
    process.setProgram("python3");
    process.setArguments({root + "/tools/cs2_linux_offsets.py"});
    process.setWorkingDirectory(root);
    process.start();
    if (!process.waitForStarted(3000)) {
        appendLog("python3 启动失败，沿用现有偏移");
        return;
    }
    // 生成器要读 /proc/<pid>/mem 并扫特征码，正常 2~5 s；超过 15 s 视为卡死。
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(1000);
        appendLog("生成超时，沿用现有偏移");
        return;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        appendLog("生成失败（cs2 未运行？），沿用现有偏移");
        return;
    }

    // 生成器写的是仓库根的 config/offsets.json；运行时读取的是可执行文件旁那份。
    const QString source = root + "/config/offsets.json";
    const QString targetDir = QCoreApplication::applicationDirPath() + "/config";
    const QString target = targetDir + "/offsets.json";
    QDir().mkpath(targetDir);
    QFile::remove(target);
    if (QFile::copy(source, target)) {
        appendLog("已刷新偏移: " + target);
    } else {
        appendLog("偏移已生成，但复制到可执行文件目录失败");
    }
}

}  // namespace

int main(int argc, char** argv) {
    // Features 与 MemoryEngine 各持一条 Xlib 连接。Xlib 默认非线程安全，
    // 第二条 XOpenDisplay 会踩内部锁导致引擎线程看似启动、实则永不进循环
    // （表现为所有内存功能静默失效）。必须在任何 Xlib 调用前切成线程安全模式。
    XInitThreads();
    QApplication app(argc, argv);
    QApplication::setApplicationName("cs2_lite");

    const QString configPath = QCoreApplication::applicationDirPath() + "/config.json";
    const std::string configPathUtf8 = configPath.toStdString();

    if (!QFile::exists(configPath)) {
        wv::Config defaults;
        defaults.save(configPathUtf8);
    }

    // 必须在 Config::load 之前：load 会立刻读取 offsetsFile，晚一步就只能吃到旧值。
    refreshOffsets();

    auto config = std::make_shared<wv::Config>(wv::Config::load(configPathUtf8));

    wv::AppState state;
    state.config.store(config);

    wv::Game game(config->offsets, config->process, config->module);
    wv::Menu menu(state, game, configPathUtf8);
    wv::Overlay overlay(game, state);
    wv::Features features(game, state);
    wv::MemoryEngine engine(game, state);

    menu.show();
    features.start();
    engine.start();

    const int code = QApplication::exec();
    engine.stop();
    features.stop();
    return code;
}
