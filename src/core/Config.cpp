#include "Config.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace wv {
namespace {

std::uintptr_t asUInt(const QJsonObject& obj, const char* key, std::uintptr_t fallback) {
    return static_cast<std::uintptr_t>(obj.value(key).toDouble(static_cast<double>(fallback)));
}

int asInt(const QJsonObject& obj, const char* key, int fallback) {
    return obj.value(key).toInt(fallback);
}

std::string asString(const QJsonObject& obj, const char* key) {
    return obj.value(key).toString().toStdString();
}

Offsets offsetsFromJson(const QJsonObject& obj) {
    Offsets o;
    o.sigEntityList = asString(obj, "sigEntityList");
    o.sigLocalPlayerPawn = asString(obj, "sigLocalPlayerPawn");
    o.sigViewMatrix = asString(obj, "sigViewMatrix");

    o.dwEntityList = asUInt(obj, "dwEntityList", o.dwEntityList);
    o.dwLocalPlayerPawn = asUInt(obj, "dwLocalPlayerPawn", o.dwLocalPlayerPawn);
    o.dwViewMatrix = asUInt(obj, "dwViewMatrix", o.dwViewMatrix);

    o.entityListChunkBase = asUInt(obj, "entityListChunkBase", o.entityListChunkBase);
    o.entityChunkPtrStride = asUInt(obj, "entityChunkPtrStride", o.entityChunkPtrStride);
    o.entityChunkShift = asInt(obj, "entityChunkShift", o.entityChunkShift);
    o.entityChunkMask = asInt(obj, "entityChunkMask", o.entityChunkMask);
    o.entityChunkStride = asUInt(obj, "entityChunkStride", o.entityChunkStride);

    o.m_iHealth = asInt(obj, "m_iHealth", o.m_iHealth);
    o.m_ArmorValue = asInt(obj, "m_ArmorValue", o.m_ArmorValue);
    o.m_iTeamNum = asInt(obj, "m_iTeamNum", o.m_iTeamNum);
    o.m_lifeState = asInt(obj, "m_lifeState", o.m_lifeState);
    o.m_pGameSceneNode = asInt(obj, "m_pGameSceneNode", o.m_pGameSceneNode);
    o.m_vecAbsOrigin = asInt(obj, "m_vecAbsOrigin", o.m_vecAbsOrigin);
    o.m_modelState = asInt(obj, "m_modelState", o.m_modelState);
    o.m_boneArray = asInt(obj, "m_boneArray", o.m_boneArray);
    o.m_iIDEntIndex = asInt(obj, "m_iIDEntIndex", o.m_iIDEntIndex);
    o.m_bSpotted = asInt(obj, "m_bSpotted", o.m_bSpotted);
    o.m_pAimPunchServices = asInt(obj, "m_pAimPunchServices", o.m_pAimPunchServices);
    o.m_aimPunchAngle = asInt(obj, "m_aimPunchAngle", o.m_aimPunchAngle);
    o.entityHandleMask = asInt(obj, "entityHandleMask", o.entityHandleMask);

    o.boneStride = asInt(obj, "boneStride", o.boneStride);
    o.bonePosX = asInt(obj, "bonePosX", o.bonePosX);
    o.bonePosY = asInt(obj, "bonePosY", o.bonePosY);
    o.bonePosZ = asInt(obj, "bonePosZ", o.bonePosZ);
    o.boneHead = asInt(obj, "boneHead", o.boneHead);
    o.boneNeck = asInt(obj, "boneNeck", o.boneNeck);
    o.boneChest = asInt(obj, "boneChest", o.boneChest);
    o.bonePelvis = asInt(obj, "bonePelvis", o.bonePelvis);

    o.m_angEyeAngles = asInt(obj, "m_angEyeAngles", o.m_angEyeAngles);
    o.m_angStashedShootAngles =
        asInt(obj, "m_angStashedShootAngles", o.m_angStashedShootAngles);
    o.m_angEyeAnglesVelocity =
        asInt(obj, "m_angEyeAnglesVelocity", o.m_angEyeAnglesVelocity);
    o.m_arrOldEyeAngles = asInt(obj, "m_arrOldEyeAngles", o.m_arrOldEyeAngles);
    o.m_angViewAngles = asInt(obj, "m_angViewAngles", o.m_angViewAngles);
    o.m_angRotation = asInt(obj, "m_angRotation", o.m_angRotation);
    o.m_vecVelocity = asInt(obj, "m_vecVelocity", o.m_vecVelocity);
    o.m_flVelocityModifier = asInt(obj, "m_flVelocityModifier", o.m_flVelocityModifier);
    o.m_MoveType = asInt(obj, "m_MoveType", o.m_MoveType);
    o.m_aimPunchCache = asInt(obj, "m_aimPunchCache", o.m_aimPunchCache);
    o.aimPunchCacheBytes = asInt(obj, "aimPunchCacheBytes", o.aimPunchCacheBytes);
    o.eyeHeight = static_cast<float>(obj.value("eyeHeight").toDouble(o.eyeHeight));
    o.m_angAbsRotation = asInt(obj, "m_angAbsRotation", o.m_angAbsRotation);
    o.m_pCameraServices = asInt(obj, "m_pCameraServices", o.m_pCameraServices);
    o.m_pMovementServices = asInt(obj, "m_pMovementServices", o.m_pMovementServices);
    o.m_flMaxspeed = asInt(obj, "m_flMaxspeed", o.m_flMaxspeed);
    o.m_vecCsViewPunchAngle =
        asInt(obj, "m_vecCsViewPunchAngle", o.m_vecCsViewPunchAngle);
    o.m_aimPunchVel = asInt(obj, "m_aimPunchVel", o.m_aimPunchVel);
    o.m_unpredictableBaseAngle =
        asInt(obj, "m_unpredictableBaseAngle", o.m_unpredictableBaseAngle);
    o.m_vecAbsVelocity = asInt(obj, "m_vecAbsVelocity", o.m_vecAbsVelocity);
    o.m_pWeaponServices = asInt(obj, "m_pWeaponServices", o.m_pWeaponServices);
    o.m_hActiveWeapon = asInt(obj, "m_hActiveWeapon", o.m_hActiveWeapon);
    o.m_iShotsFired = asInt(obj, "m_iShotsFired", o.m_iShotsFired);
    o.m_flFlinchStack = asInt(obj, "m_flFlinchStack", o.m_flFlinchStack);
    o.m_flInterpolatedInaccuracy =
        asInt(obj, "m_flInterpolatedInaccuracy", o.m_flInterpolatedInaccuracy);
    o.m_vClientScopeInaccuracy =
        asInt(obj, "m_vClientScopeInaccuracy", o.m_vClientScopeInaccuracy);
    o.m_fAccuracyPenalty = asInt(obj, "m_fAccuracyPenalty", o.m_fAccuracyPenalty);
    o.m_fAccuracySmoothedForZoom =
        asInt(obj, "m_fAccuracySmoothedForZoom", o.m_fAccuracySmoothedForZoom);
    o.m_flTurningInaccuracy =
        asInt(obj, "m_flTurningInaccuracy", o.m_flTurningInaccuracy);
    o.m_flTurningInaccuracyDelta =
        asInt(obj, "m_flTurningInaccuracyDelta", o.m_flTurningInaccuracyDelta);
    o.m_iRecoilIndex = asInt(obj, "m_iRecoilIndex", o.m_iRecoilIndex);
    o.m_flRecoilIndex = asInt(obj, "m_flRecoilIndex", o.m_flRecoilIndex);
    o.m_flLastShakeTime = asInt(obj, "m_flLastShakeTime", o.m_flLastShakeTime);
    o.m_flSpreadVData = asInt(obj, "m_flSpreadVData", o.m_flSpreadVData);
    return o;
}

QJsonObject offsetsToJson(const Offsets& o) {
    QJsonObject obj;
    obj["sigEntityList"] = QString::fromStdString(o.sigEntityList);
    obj["sigLocalPlayerPawn"] = QString::fromStdString(o.sigLocalPlayerPawn);
    obj["sigViewMatrix"] = QString::fromStdString(o.sigViewMatrix);
    obj["dwEntityList"] = static_cast<double>(o.dwEntityList);
    obj["dwLocalPlayerPawn"] = static_cast<double>(o.dwLocalPlayerPawn);
    obj["dwViewMatrix"] = static_cast<double>(o.dwViewMatrix);
    obj["entityListChunkBase"] = static_cast<double>(o.entityListChunkBase);
    obj["entityChunkPtrStride"] = static_cast<double>(o.entityChunkPtrStride);
    obj["entityChunkShift"] = o.entityChunkShift;
    obj["entityChunkMask"] = o.entityChunkMask;
    obj["entityChunkStride"] = static_cast<double>(o.entityChunkStride);
    obj["m_iHealth"] = o.m_iHealth;
    obj["m_ArmorValue"] = o.m_ArmorValue;
    obj["m_iTeamNum"] = o.m_iTeamNum;
    obj["m_lifeState"] = o.m_lifeState;
    obj["m_pGameSceneNode"] = o.m_pGameSceneNode;
    obj["m_vecAbsOrigin"] = o.m_vecAbsOrigin;
    obj["m_modelState"] = o.m_modelState;
    obj["m_boneArray"] = o.m_boneArray;
    obj["m_iIDEntIndex"] = o.m_iIDEntIndex;
    obj["m_bSpotted"] = o.m_bSpotted;
    obj["m_pAimPunchServices"] = o.m_pAimPunchServices;
    obj["m_aimPunchAngle"] = o.m_aimPunchAngle;
    obj["entityHandleMask"] = o.entityHandleMask;
    obj["boneStride"] = o.boneStride;
    obj["bonePosX"] = o.bonePosX;
    obj["bonePosY"] = o.bonePosY;
    obj["bonePosZ"] = o.bonePosZ;
    obj["boneHead"] = o.boneHead;
    obj["boneNeck"] = o.boneNeck;
    obj["boneChest"] = o.boneChest;
    obj["bonePelvis"] = o.bonePelvis;
    obj["m_angEyeAngles"] = o.m_angEyeAngles;
    obj["m_angStashedShootAngles"] = o.m_angStashedShootAngles;
    obj["m_angEyeAnglesVelocity"] = o.m_angEyeAnglesVelocity;
    obj["m_arrOldEyeAngles"] = o.m_arrOldEyeAngles;
    obj["m_angViewAngles"] = o.m_angViewAngles;
    obj["m_angRotation"] = o.m_angRotation;
    obj["m_vecVelocity"] = o.m_vecVelocity;
    obj["m_flVelocityModifier"] = o.m_flVelocityModifier;
    obj["m_MoveType"] = o.m_MoveType;
    obj["m_aimPunchCache"] = o.m_aimPunchCache;
    obj["aimPunchCacheBytes"] = o.aimPunchCacheBytes;
    obj["eyeHeight"] = static_cast<double>(o.eyeHeight);
    obj["m_angAbsRotation"] = o.m_angAbsRotation;
    obj["m_pCameraServices"] = o.m_pCameraServices;
    obj["m_pMovementServices"] = o.m_pMovementServices;
    obj["m_flMaxspeed"] = o.m_flMaxspeed;
    obj["m_vecCsViewPunchAngle"] = o.m_vecCsViewPunchAngle;
    obj["m_aimPunchVel"] = o.m_aimPunchVel;
    obj["m_unpredictableBaseAngle"] = o.m_unpredictableBaseAngle;
    obj["m_vecAbsVelocity"] = o.m_vecAbsVelocity;
    obj["m_pWeaponServices"] = o.m_pWeaponServices;
    obj["m_hActiveWeapon"] = o.m_hActiveWeapon;
    obj["m_iShotsFired"] = o.m_iShotsFired;
    obj["m_flFlinchStack"] = o.m_flFlinchStack;
    obj["m_flInterpolatedInaccuracy"] = o.m_flInterpolatedInaccuracy;
    obj["m_vClientScopeInaccuracy"] = o.m_vClientScopeInaccuracy;
    obj["m_fAccuracyPenalty"] = o.m_fAccuracyPenalty;
    obj["m_fAccuracySmoothedForZoom"] = o.m_fAccuracySmoothedForZoom;
    obj["m_flTurningInaccuracy"] = o.m_flTurningInaccuracy;
    obj["m_flTurningInaccuracyDelta"] = o.m_flTurningInaccuracyDelta;
    obj["m_iRecoilIndex"] = o.m_iRecoilIndex;
    obj["m_flRecoilIndex"] = o.m_flRecoilIndex;
    obj["m_flLastShakeTime"] = o.m_flLastShakeTime;
    obj["m_flSpreadVData"] = o.m_flSpreadVData;
    return obj;
}

}  // namespace

Config Config::load(const std::string& path) {
    Config cfg;

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) return cfg;

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return cfg;

    const QJsonObject root = doc.object();
    if (root.contains("process")) cfg.process = asString(root, "process");
    if (root.contains("module")) cfg.module = asString(root, "module");

    if (root.value("offsets").isObject()) {
        cfg.offsets = offsetsFromJson(root.value("offsets").toObject());
    }

    cfg.offsetsFile = asString(root, "offsetsFile");
    if (!cfg.offsetsFile.empty()) {
        const ImportResult imported = importOffsetsFile(cfg.offsetsFile, cfg.offsets);
        cfg.offsetsSource = imported.message;
    }

    cfg.togglePanelKey = asInt(root, "togglePanelKey", cfg.togglePanelKey);
    cfg.panicKey = asInt(root, "panicKey", cfg.panicKey);
    cfg.resetStatsKey = asInt(root, "resetStatsKey", cfg.resetStatsKey);

    if (root.value("aim").isObject()) {
        const QJsonObject o = root.value("aim").toObject();
        cfg.aim.enabled = o.value("enabled").toBool(cfg.aim.enabled);
        cfg.aim.scopeKey = asInt(o, "scopeKey", cfg.aim.scopeKey);
        cfg.aim.fireKey = asInt(o, "fireKey", cfg.aim.fireKey);
        cfg.aim.fovPixels = static_cast<float>(o.value("fovPixels").toDouble(cfg.aim.fovPixels));
        cfg.aim.smooth = asInt(o, "smooth", cfg.aim.smooth);
        cfg.aim.sensitivityFactor =
            static_cast<float>(o.value("sensitivityFactor").toDouble(cfg.aim.sensitivityFactor));
        cfg.aim.teamCheck = o.value("teamCheck").toBool(cfg.aim.teamCheck);
        cfg.aim.strategy = asInt(o, "strategy", cfg.aim.strategy);
        cfg.aim.lockTarget = o.value("lockTarget").toBool(cfg.aim.lockTarget);
        cfg.aim.maxDistance =
            static_cast<float>(o.value("maxDistance").toDouble(cfg.aim.maxDistance));
    }

    if (root.value("recoil").isObject()) {
        const QJsonObject o = root.value("recoil").toObject();
        cfg.recoil.enabled = o.value("enabled").toBool(cfg.recoil.enabled);
        cfg.recoil.strength =
            static_cast<float>(o.value("strength").toDouble(cfg.recoil.strength));
        cfg.recoil.maxDegrees =
            static_cast<float>(o.value("maxDegrees").toDouble(cfg.recoil.maxDegrees));
        cfg.recoil.requireFire = o.value("requireFire").toBool(cfg.recoil.requireFire);
    }

    if (root.value("track").isObject()) {
        const QJsonObject o = root.value("track").toObject();
        cfg.track.enabled = o.value("enabled").toBool(cfg.track.enabled);
        cfg.track.key = asInt(o, "key", cfg.track.key);
        cfg.track.fovPixels = static_cast<float>(o.value("fovPixels").toDouble(cfg.track.fovPixels));
        cfg.track.smooth = asInt(o, "smooth", cfg.track.smooth);
        cfg.track.teamCheck = o.value("teamCheck").toBool(cfg.track.teamCheck);
        cfg.track.maxDistance =
            static_cast<float>(o.value("maxDistance").toDouble(cfg.track.maxDistance));
    }

    if (root.value("silent").isObject()) {
        const QJsonObject o = root.value("silent").toObject();
        cfg.silent.enabled = o.value("enabled").toBool(cfg.silent.enabled);
        cfg.silent.key = asInt(o, "key", cfg.silent.key);
        cfg.silent.holdMs = asInt(o, "holdMs", cfg.silent.holdMs);
        cfg.silent.cooldownMs = asInt(o, "cooldownMs", cfg.silent.cooldownMs);
        cfg.silent.fovPixels =
            static_cast<float>(o.value("fovPixels").toDouble(cfg.silent.fovPixels));
        cfg.silent.teamCheck = o.value("teamCheck").toBool(cfg.silent.teamCheck);
        cfg.silent.maxDistance =
            static_cast<float>(o.value("maxDistance").toDouble(cfg.silent.maxDistance));
    }

    if (root.value("trigger").isObject()) {
        const QJsonObject o = root.value("trigger").toObject();
        cfg.trigger.enabled = o.value("enabled").toBool(cfg.trigger.enabled);
        cfg.trigger.key = asInt(o, "key", cfg.trigger.key);
        cfg.trigger.delayMs = asInt(o, "delayMs", cfg.trigger.delayMs);
        cfg.trigger.cooldownMs = asInt(o, "cooldownMs", cfg.trigger.cooldownMs);
        cfg.trigger.teamCheck = o.value("teamCheck").toBool(cfg.trigger.teamCheck);
    }

    if (root.value("memory").isObject()) {
        const QJsonObject o = root.value("memory").toObject();
        cfg.memory.speed = o.value("speed").toBool(cfg.memory.speed);
        cfg.memory.speedFactor =
            static_cast<float>(o.value("speedFactor").toDouble(cfg.memory.speedFactor));
        cfg.memory.bulletTrack = o.value("bulletTrack").toBool(cfg.memory.bulletTrack);
        cfg.memory.bulletTrackKey = asInt(o, "bulletTrackKey", cfg.memory.bulletTrackKey);
        cfg.memory.bulletTrackMaxDistance = static_cast<float>(
            o.value("bulletTrackMaxDistance").toDouble(cfg.memory.bulletTrackMaxDistance));
        cfg.memory.bulletTrackTeamCheck =
            o.value("bulletTrackTeamCheck").toBool(cfg.memory.bulletTrackTeamCheck);
        cfg.memory.bulletLockHoldMs =
            asInt(o, "bulletLockHoldMs", cfg.memory.bulletLockHoldMs);
        cfg.memory.bulletLockProtectView =
            o.value("bulletLockProtectView").toBool(cfg.memory.bulletLockProtectView);
        cfg.memory.bulletLockSyncView =
            o.value("bulletLockSyncView").toBool(cfg.memory.bulletLockSyncView);
        cfg.memory.fly = o.value("fly").toBool(cfg.memory.fly);
        cfg.memory.flyUpKey = asInt(o, "flyUpKey", cfg.memory.flyUpKey);
        cfg.memory.flyDownKey = asInt(o, "flyDownKey", cfg.memory.flyDownKey);
        cfg.memory.flySpeed = static_cast<float>(o.value("flySpeed").toDouble(cfg.memory.flySpeed));
        cfg.memory.flyMoveType = asInt(o, "flyMoveType", cfg.memory.flyMoveType);
        cfg.memory.spin = o.value("spin").toBool(cfg.memory.spin);
        cfg.memory.spinDegreesPerSec = static_cast<float>(
            o.value("spinDegreesPerSec").toDouble(cfg.memory.spinDegreesPerSec));
        cfg.memory.spinKeepView = o.value("spinKeepView").toBool(cfg.memory.spinKeepView);
        cfg.memory.noRecoil = o.value("noRecoil").toBool(cfg.memory.noRecoil);
        cfg.memory.accuracy = o.value("accuracy").toBool(cfg.memory.accuracy);
        cfg.memory.accuracySafeMode =
            o.value("accuracySafeMode").toBool(cfg.memory.accuracySafeMode);
    }

    if (root.value("esp").isObject()) {
        const QJsonObject o = root.value("esp").toObject();
        cfg.esp.enabled = o.value("enabled").toBool(cfg.esp.enabled);
        cfg.esp.box = o.value("box").toBool(cfg.esp.box);
        cfg.esp.health = o.value("health").toBool(cfg.esp.health);
        cfg.esp.healthText = o.value("healthText").toBool(cfg.esp.healthText);
        cfg.esp.armor = o.value("armor").toBool(cfg.esp.armor);
        cfg.esp.damageNumbers = o.value("damageNumbers").toBool(cfg.esp.damageNumbers);
        cfg.esp.distance = o.value("distance").toBool(cfg.esp.distance);
        cfg.esp.skeleton = o.value("skeleton").toBool(cfg.esp.skeleton);
        cfg.esp.radar = o.value("radar").toBool(cfg.esp.radar);
        cfg.esp.radarRotate = o.value("radarRotate").toBool(cfg.esp.radarRotate);
        cfg.esp.radarRange =
            static_cast<float>(o.value("radarRange").toDouble(cfg.esp.radarRange));
        cfg.esp.offscreen = o.value("offscreen").toBool(cfg.esp.offscreen);
        cfg.esp.enemyList = o.value("enemyList").toBool(cfg.esp.enemyList);
        cfg.esp.enemyListMax = asInt(o, "enemyListMax", cfg.esp.enemyListMax);
        cfg.esp.heightIndicator =
            o.value("heightIndicator").toBool(cfg.esp.heightIndicator);
        cfg.esp.crosshairInfo = o.value("crosshairInfo").toBool(cfg.esp.crosshairInfo);
        cfg.esp.statsHud = o.value("statsHud").toBool(cfg.esp.statsHud);
        cfg.esp.statsHudPos = asInt(o, "statsHudPos", cfg.esp.statsHudPos);
        cfg.esp.radarPos = asInt(o, "radarPos", cfg.esp.radarPos);
        cfg.esp.listPos = asInt(o, "listPos", cfg.esp.listPos);
        cfg.esp.trail = o.value("trail").toBool(cfg.esp.trail);
        cfg.esp.trailMs = asInt(o, "trailMs", cfg.esp.trailMs);
        cfg.esp.highlight = o.value("highlight").toBool(cfg.esp.highlight);
        cfg.esp.damageMs = asInt(o, "damageMs", cfg.esp.damageMs);
        cfg.esp.damageColor = asInt(o, "damageColor", cfg.esp.damageColor);
        cfg.esp.teamCheck = o.value("teamCheck").toBool(cfg.esp.teamCheck);
        cfg.esp.visibilityCheck = o.value("visibilityCheck").toBool(cfg.esp.visibilityCheck);
        cfg.esp.maxDistance =
            static_cast<float>(o.value("maxDistance").toDouble(cfg.esp.maxDistance));
    }

    return cfg;
}

void Config::save(const std::string& path) const {
    // 面板改一个开关就会用内存中的 Config 覆盖整份文件。若磁盘上有本进程
    // 不认识的字段（旧二进制 + 新生成器输出），直接写会把它们抹掉，
    // 下一轮又变成「偏移全 0、功能没反应」。先读回并保留未知键。
    QJsonObject root;
    QFile existing(QString::fromStdString(path));
    if (existing.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(existing.readAll());
        existing.close();
        if (doc.isObject()) root = doc.object();
    }
    root["process"] = QString::fromStdString(process);
    root["module"] = QString::fromStdString(module);
    root["offsetsFile"] = QString::fromStdString(offsetsFile);
    QJsonObject offsetsObj = offsetsToJson(offsets);
    if (root.value("offsets").isObject()) {
        const QJsonObject disk = root.value("offsets").toObject();
        for (auto it = disk.constBegin(); it != disk.constEnd(); ++it) {
            if (!offsetsObj.contains(it.key())) offsetsObj.insert(it.key(), it.value());
        }
    }
    root["offsets"] = offsetsObj;
    root["togglePanelKey"] = togglePanelKey;
    root["panicKey"] = panicKey;
    root["resetStatsKey"] = resetStatsKey;

    QJsonObject aimObj;
    aimObj["enabled"] = aim.enabled;
    aimObj["scopeKey"] = aim.scopeKey;
    aimObj["fireKey"] = aim.fireKey;
    aimObj["fovPixels"] = static_cast<double>(aim.fovPixels);
    aimObj["smooth"] = aim.smooth;
    aimObj["sensitivityFactor"] = static_cast<double>(aim.sensitivityFactor);
    aimObj["teamCheck"] = aim.teamCheck;
    aimObj["strategy"] = aim.strategy;
    aimObj["lockTarget"] = aim.lockTarget;
    aimObj["maxDistance"] = static_cast<double>(aim.maxDistance);
    root["aim"] = aimObj;

    QJsonObject recoilObj;
    recoilObj["enabled"] = recoil.enabled;
    recoilObj["strength"] = static_cast<double>(recoil.strength);
    recoilObj["maxDegrees"] = static_cast<double>(recoil.maxDegrees);
    recoilObj["requireFire"] = recoil.requireFire;
    root["recoil"] = recoilObj;

    QJsonObject trackObj;
    trackObj["enabled"] = track.enabled;
    trackObj["key"] = track.key;
    trackObj["fovPixels"] = static_cast<double>(track.fovPixels);
    trackObj["smooth"] = track.smooth;
    trackObj["teamCheck"] = track.teamCheck;
    trackObj["maxDistance"] = static_cast<double>(track.maxDistance);
    root["track"] = trackObj;

    QJsonObject silentObj;
    silentObj["enabled"] = silent.enabled;
    silentObj["key"] = silent.key;
    silentObj["holdMs"] = silent.holdMs;
    silentObj["cooldownMs"] = silent.cooldownMs;
    silentObj["fovPixels"] = static_cast<double>(silent.fovPixels);
    silentObj["teamCheck"] = silent.teamCheck;
    silentObj["maxDistance"] = static_cast<double>(silent.maxDistance);
    root["silent"] = silentObj;

    QJsonObject triggerObj;
    triggerObj["enabled"] = trigger.enabled;
    triggerObj["key"] = trigger.key;
    triggerObj["delayMs"] = trigger.delayMs;
    triggerObj["cooldownMs"] = trigger.cooldownMs;
    triggerObj["teamCheck"] = trigger.teamCheck;
    root["trigger"] = triggerObj;

    QJsonObject memoryObj;
    memoryObj["speed"] = memory.speed;
    memoryObj["speedFactor"] = static_cast<double>(memory.speedFactor);
    memoryObj["bulletTrack"] = memory.bulletTrack;
    memoryObj["bulletTrackKey"] = memory.bulletTrackKey;
    memoryObj["bulletTrackMaxDistance"] = static_cast<double>(memory.bulletTrackMaxDistance);
    memoryObj["bulletTrackTeamCheck"] = memory.bulletTrackTeamCheck;
    memoryObj["bulletLockHoldMs"] = memory.bulletLockHoldMs;
    memoryObj["bulletLockProtectView"] = memory.bulletLockProtectView;
    memoryObj["bulletLockSyncView"] = memory.bulletLockSyncView;
    memoryObj["fly"] = memory.fly;
    memoryObj["flyUpKey"] = memory.flyUpKey;
    memoryObj["flyDownKey"] = memory.flyDownKey;
    memoryObj["flySpeed"] = static_cast<double>(memory.flySpeed);
    memoryObj["flyMoveType"] = memory.flyMoveType;
    memoryObj["spin"] = memory.spin;
    memoryObj["spinDegreesPerSec"] = static_cast<double>(memory.spinDegreesPerSec);
    memoryObj["spinKeepView"] = memory.spinKeepView;
    memoryObj["noRecoil"] = memory.noRecoil;
    memoryObj["accuracy"] = memory.accuracy;
    memoryObj["accuracySafeMode"] = memory.accuracySafeMode;
    root["memory"] = memoryObj;

    QJsonObject espObj;
    espObj["enabled"] = esp.enabled;
    espObj["box"] = esp.box;
    espObj["health"] = esp.health;
    espObj["healthText"] = esp.healthText;
    espObj["armor"] = esp.armor;
    espObj["damageNumbers"] = esp.damageNumbers;
    espObj["distance"] = esp.distance;
    espObj["skeleton"] = esp.skeleton;
    espObj["radar"] = esp.radar;
    espObj["radarRotate"] = esp.radarRotate;
    espObj["radarRange"] = static_cast<double>(esp.radarRange);
    espObj["offscreen"] = esp.offscreen;
    espObj["enemyList"] = esp.enemyList;
    espObj["enemyListMax"] = esp.enemyListMax;
    espObj["heightIndicator"] = esp.heightIndicator;
    espObj["crosshairInfo"] = esp.crosshairInfo;
    espObj["statsHud"] = esp.statsHud;
    espObj["statsHudPos"] = esp.statsHudPos;
    espObj["radarPos"] = esp.radarPos;
    espObj["listPos"] = esp.listPos;
    espObj["trail"] = esp.trail;
    espObj["trailMs"] = esp.trailMs;
    espObj["highlight"] = esp.highlight;
    espObj["damageMs"] = esp.damageMs;
    espObj["damageColor"] = esp.damageColor;
    espObj["teamCheck"] = esp.teamCheck;
    espObj["visibilityCheck"] = esp.visibilityCheck;
    espObj["maxDistance"] = static_cast<double>(esp.maxDistance);
    root["esp"] = espObj;

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

}  // namespace wv
