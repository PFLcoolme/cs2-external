#include "core/Config.h"
#include "test_util.h"

#include <cstdio>
#include <string>

namespace {

const std::string kTempPath = "/tmp/cs2_test_config.json";

void testDefaults() {
    const wv::Config config;
    EXPECT(config.process == "cs2");
    EXPECT(config.esp.enabled);
    EXPECT(config.aim.scopeKey == 3);
    EXPECT(config.esp.radarRotate);
    EXPECT(!config.esp.enemyList);
    EXPECT(config.esp.enemyListMax == 5);
    EXPECT(!config.trigger.enabled);
}

void testRoundTrip() {
    wv::Config config;
    config.esp.radar = true;
    config.esp.radarRotate = false;
    config.esp.offscreen = true;
    config.esp.enemyList = true;
    config.esp.enemyListMax = 7;
    config.aim.scopeKey = 0x45;  // E
    config.aim.fovPixels = 180.f;
    config.trigger.key = 0x46;  // F
    config.save(kTempPath);

    const wv::Config loaded = wv::Config::load(kTempPath);
    EXPECT(loaded.esp.radar);
    EXPECT(!loaded.esp.radarRotate);
    EXPECT(loaded.esp.offscreen);
    EXPECT(loaded.esp.enemyList);
    EXPECT(loaded.esp.enemyListMax == 7);
    EXPECT(loaded.aim.scopeKey == 0x45);
    EXPECT_NEAR(loaded.aim.fovPixels, 180.f, 0.01f);
    EXPECT(loaded.trigger.key == 0x46);

    std::remove(kTempPath.c_str());
}

void testMissingFileFallsBackToDefaults() {
    const wv::Config loaded = wv::Config::load("/tmp/cs2_no_such_config.json");
    EXPECT(loaded.esp.enabled);
    EXPECT(loaded.aim.scopeKey == 3);
    EXPECT(loaded.esp.enemyListMax == 5);
}

}  // namespace

void runConfigTests() {
    testDefaults();
    testRoundTrip();
    testMissingFileFallsBackToDefaults();
}
