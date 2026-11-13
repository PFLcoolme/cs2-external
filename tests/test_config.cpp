#include "core/Config.h"
#include "test_util.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

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
    EXPECT(config.resetStatsKey == 0xFFC7);
    EXPECT(!config.esp.statsHud);
    EXPECT(config.esp.statsHudPos == 1);
    EXPECT(!config.memory.speed);
    EXPECT(!config.memory.bulletTrack);
    EXPECT(!config.memory.fly);
    EXPECT(!config.memory.spin);
    EXPECT(!config.memory.noRecoil);
    EXPECT(!config.memory.accuracy);
    EXPECT(config.memory.flyUpKey == 0x20);
    EXPECT(config.offsets.m_angEyeAngles == 0);
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
    config.esp.statsHud = true;
    config.esp.statsHudPos = 4;
    config.resetStatsKey = 0xFFC8;  // F11
    config.memory.speed = true;
    config.memory.speedFactor = 2.25f;
    config.memory.bulletTrack = true;
    config.memory.bulletTrackKey = 1;
    config.memory.fly = true;
    config.memory.flySpeed = 420.f;
    config.memory.spin = true;
    config.memory.spinKeepView = false;
    config.memory.bulletLockProtectView = false;
    config.memory.bulletLockSyncView = true;
    config.memory.noRecoil = true;
    config.memory.accuracy = true;
    config.offsets.m_angEyeAngles = 0x14A0;
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
    EXPECT(loaded.esp.statsHud);
    EXPECT(loaded.esp.statsHudPos == 4);
    EXPECT(loaded.resetStatsKey == 0xFFC8);
    EXPECT(loaded.memory.speed);
    EXPECT_NEAR(loaded.memory.speedFactor, 2.25f, 0.01f);
    EXPECT(loaded.memory.bulletTrack);
    EXPECT(loaded.memory.bulletTrackKey == 1);
    EXPECT(loaded.memory.fly);
    EXPECT_NEAR(loaded.memory.flySpeed, 420.f, 0.01f);
    EXPECT(loaded.memory.spin);
    EXPECT(!loaded.memory.spinKeepView);
    EXPECT(!loaded.memory.bulletLockProtectView);
    EXPECT(loaded.memory.bulletLockSyncView);
    EXPECT(loaded.memory.accuracySafeMode);
    EXPECT(loaded.memory.noRecoil);
    EXPECT(loaded.memory.accuracy);
    EXPECT(loaded.offsets.m_angEyeAngles == 0x14A0);

    std::remove(kTempPath.c_str());
}

void testMissingFileFallsBackToDefaults() {
    const wv::Config loaded = wv::Config::load("/tmp/cs2_no_such_config.json");
    EXPECT(loaded.esp.enabled);
    EXPECT(loaded.aim.scopeKey == 3);
    EXPECT(loaded.esp.enemyListMax == 5);
}

void testUnknownOffsetsPreserved() {
    wv::Config config;
    config.save(kTempPath);

    // 模拟生成器写入一个本二进制不认识的字段：面板改开关再次存盘后必须保留。
    QFile file(QString::fromStdString(kTempPath));
    EXPECT(file.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    QJsonObject offsets = root.value("offsets").toObject();
    offsets.insert("m_futureField", 4242);
    root.insert("offsets", offsets);
    EXPECT(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();

    config.esp.box = false;
    config.save(kTempPath);

    const wv::Config loaded = wv::Config::load(kTempPath);
    EXPECT(!loaded.esp.box);
    EXPECT(file.open(QIODevice::ReadOnly));
    const QJsonObject saved = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    EXPECT(saved.value("offsets").toObject().value("m_futureField").toInt() == 4242);
    std::remove(kTempPath.c_str());
}

void testBulletLockDefaults() {
    // 内存锁必须默认常驻：不看按键、不看准星，不退化成 XTest 贴头。
    const wv::MemoryConfig memory;
    EXPECT(!memory.bulletTrack);
    EXPECT(memory.bulletTrackKey == 0);
    EXPECT(memory.bulletLockHoldMs > 0);
    EXPECT(memory.bulletTrackTeamCheck);
    // 视角不动是两个保护开关的默认契约，不能被后续改动悄悄翻掉。
    EXPECT(memory.bulletLockProtectView);
    EXPECT(memory.spinKeepView);
    // 散布安全模式默认开：不清参与弹道预测的字段，避免「打中却没伤害」。
    EXPECT(memory.accuracySafeMode);
    // 命中优先默认关：默认保持视角不动，需要命中才由用户显式打开。
    EXPECT(!memory.bulletLockSyncView);
}

}  // namespace

void runConfigTests() {
    testDefaults();
    testRoundTrip();
    testMissingFileFallsBackToDefaults();
    testUnknownOffsetsPreserved();
    testBulletLockDefaults();
}
