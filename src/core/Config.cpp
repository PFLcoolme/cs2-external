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
    o.m_aimPunchAngle = asInt(obj, "m_aimPunchAngle", o.m_aimPunchAngle);
    o.m_aimPunchCache = asInt(obj, "m_aimPunchCache", o.m_aimPunchCache);
    o.entityHandleMask = asInt(obj, "entityHandleMask", o.entityHandleMask);

    o.boneStride = asInt(obj, "boneStride", o.boneStride);
    o.bonePosX = asInt(obj, "bonePosX", o.bonePosX);
    o.bonePosY = asInt(obj, "bonePosY", o.bonePosY);
    o.bonePosZ = asInt(obj, "bonePosZ", o.bonePosZ);
    o.boneHead = asInt(obj, "boneHead", o.boneHead);
    o.boneNeck = asInt(obj, "boneNeck", o.boneNeck);
    o.boneChest = asInt(obj, "boneChest", o.boneChest);
    o.bonePelvis = asInt(obj, "bonePelvis", o.bonePelvis);
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
    obj["m_aimPunchAngle"] = o.m_aimPunchAngle;
    obj["m_aimPunchCache"] = o.m_aimPunchCache;
    obj["entityHandleMask"] = o.entityHandleMask;
    obj["boneStride"] = o.boneStride;
    obj["bonePosX"] = o.bonePosX;
    obj["bonePosY"] = o.bonePosY;
    obj["bonePosZ"] = o.bonePosZ;
    obj["boneHead"] = o.boneHead;
    obj["boneNeck"] = o.boneNeck;
    obj["boneChest"] = o.boneChest;
    obj["bonePelvis"] = o.bonePelvis;
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

    if (root.value("trigger").isObject()) {
        const QJsonObject o = root.value("trigger").toObject();
        cfg.trigger.enabled = o.value("enabled").toBool(cfg.trigger.enabled);
        cfg.trigger.key = asInt(o, "key", cfg.trigger.key);
        cfg.trigger.delayMs = asInt(o, "delayMs", cfg.trigger.delayMs);
        cfg.trigger.cooldownMs = asInt(o, "cooldownMs", cfg.trigger.cooldownMs);
        cfg.trigger.teamCheck = o.value("teamCheck").toBool(cfg.trigger.teamCheck);
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
        cfg.esp.radarRange =
            static_cast<float>(o.value("radarRange").toDouble(cfg.esp.radarRange));
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
    QJsonObject root;
    root["process"] = QString::fromStdString(process);
    root["module"] = QString::fromStdString(module);
    root["offsetsFile"] = QString::fromStdString(offsetsFile);
    root["offsets"] = offsetsToJson(offsets);

    QJsonObject aimObj;
    aimObj["enabled"] = aim.enabled;
    aimObj["scopeKey"] = aim.scopeKey;
    aimObj["fireKey"] = aim.fireKey;
    aimObj["fovPixels"] = static_cast<double>(aim.fovPixels);
    aimObj["smooth"] = aim.smooth;
    aimObj["sensitivityFactor"] = static_cast<double>(aim.sensitivityFactor);
    aimObj["teamCheck"] = aim.teamCheck;
    aimObj["strategy"] = aim.strategy;
    aimObj["maxDistance"] = static_cast<double>(aim.maxDistance);
    root["aim"] = aimObj;

    QJsonObject recoilObj;
    recoilObj["enabled"] = recoil.enabled;
    recoilObj["strength"] = static_cast<double>(recoil.strength);
    recoilObj["maxDegrees"] = static_cast<double>(recoil.maxDegrees);
    recoilObj["requireFire"] = recoil.requireFire;
    root["recoil"] = recoilObj;

    QJsonObject triggerObj;
    triggerObj["enabled"] = trigger.enabled;
    triggerObj["key"] = trigger.key;
    triggerObj["delayMs"] = trigger.delayMs;
    triggerObj["cooldownMs"] = trigger.cooldownMs;
    triggerObj["teamCheck"] = trigger.teamCheck;
    root["trigger"] = triggerObj;

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
    espObj["radarRange"] = static_cast<double>(esp.radarRange);
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
