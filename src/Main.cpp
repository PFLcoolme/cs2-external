#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QString>

#include <memory>
#include <string>

#include "core/Config.h"
#include "features/Features.h"
#include "game/Game.h"
#include "render/Overlay.h"
#include "ui/Menu.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("cs2_lite");

    const QString configPath = QCoreApplication::applicationDirPath() + "/config.json";
    const std::string configPathUtf8 = configPath.toStdString();

    if (!QFile::exists(configPath)) {
        wv::Config defaults;
        defaults.save(configPathUtf8);
    }

    auto config = std::make_shared<wv::Config>(wv::Config::load(configPathUtf8));

    wv::AppState state;
    state.config.store(config);

    wv::Game game(config->offsets, config->process, config->module);
    wv::Menu menu(state, game, configPathUtf8);
    wv::Overlay overlay(game, state);
    wv::Features features(game, state);

    menu.show();
    features.start();

    const int code = QApplication::exec();
    features.stop();
    return code;
}
