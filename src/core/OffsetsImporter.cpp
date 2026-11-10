#include "OffsetsImporter.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <functional>
#include <map>
#include <vector>

namespace wv {
namespace {

struct FieldBinding {
    const char* primary;              // 主字段名
    const char* alias;                // 别名（可为 nullptr）
    std::function<void(Offsets&, std::uint64_t)> apply;
};

const std::vector<FieldBinding>& bindings() {
    static const std::vector<FieldBinding> table = {
        {"dwEntityList", nullptr,
         [](Offsets& o, std::uint64_t v) { o.dwEntityList = static_cast<std::uintptr_t>(v); }},
        {"dwLocalPlayerPawn", nullptr,
         [](Offsets& o, std::uint64_t v) {
             o.dwLocalPlayerPawn = static_cast<std::uintptr_t>(v);
         }},
        {"dwViewMatrix", nullptr,
         [](Offsets& o, std::uint64_t v) { o.dwViewMatrix = static_cast<std::uintptr_t>(v); }},

        {"m_iHealth", nullptr,
         [](Offsets& o, std::uint64_t v) { o.m_iHealth = static_cast<int>(v); }},
        {"m_ArmorValue", nullptr,
         [](Offsets& o, std::uint64_t v) { o.m_ArmorValue = static_cast<int>(v); }},
        {"m_iTeamNum", nullptr,
         [](Offsets& o, std::uint64_t v) { o.m_iTeamNum = static_cast<int>(v); }},
        {"m_lifeState", nullptr,
         [](Offsets& o, std::uint64_t v) { o.m_lifeState = static_cast<int>(v); }},
        {"m_pGameSceneNode", nullptr,
         [](Offsets& o, std::uint64_t v) { o.m_pGameSceneNode = static_cast<int>(v); }},
        {"m_vecAbsOrigin", nullptr,
         [](Offsets& o, std::uint64_t v) { o.m_vecAbsOrigin = static_cast<int>(v); }},
        {"m_modelState", nullptr,
         [](Offsets& o, std::uint64_t v) { o.m_modelState = static_cast<int>(v); }},
        {"m_boneArray", "m_pBones",
         [](Offsets& o, std::uint64_t v) { o.m_boneArray = static_cast<int>(v); }},
        {"m_iIDEntIndex", nullptr,
         [](Offsets& o, std::uint64_t v) { o.m_iIDEntIndex = static_cast<int>(v); }},
        {"m_bSpotted", nullptr,
         [](Offsets& o, std::uint64_t v) { o.m_bSpotted = static_cast<int>(v); }},
    };
    return table;
}

bool toNumber(const QJsonValue& value, std::uint64_t& out) {
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (number < 0) return false;
        out = static_cast<std::uint64_t>(number);
        return true;
    }
    if (value.isString()) {
        bool ok = false;
        const quint64 parsed = value.toString().toULongLong(&ok, 0);  // 支持 0x 前缀
        if (!ok) return false;
        out = static_cast<std::uint64_t>(parsed);
        return true;
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.contains("value")) return toNumber(object.value("value"), out);
    }
    return false;
}

bool looksLikeClass(const QString& key) {
    if (key.size() < 2 || key.contains('.') || key.contains('/')) return false;
    if (key.at(0) != QLatin1Char('C')) return false;
    return key.at(1) == QLatin1Char('_') || key.at(1).isUpper();
}

int classPriority(const QString& className) {
    if (className == QStringLiteral("C_CSPlayerPawn") ||
        className == QStringLiteral("CCSPlayerPawn")) {
        return 3;
    }
    if (className == QStringLiteral("C_BaseEntity") ||
        className == QStringLiteral("CGameSceneNode") ||
        className == QStringLiteral("CSkeletonInstance")) {
        return 2;
    }
    if (className == QStringLiteral("C_BaseModelEntity") ||
        className == QStringLiteral("CModelState")) {
        return 1;
    }
    return 0;
}

struct Found {
    QString className;
    std::uint64_t value{0};
    int priority{0};
};

void collect(const QJsonValue& node, const QString& className, std::map<QString, Found>& out) {
    if (node.isObject()) {
        const QJsonObject object = node.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString key = it.key();
            const QJsonValue child = it.value();

            if (child.isObject() || child.isArray()) {
                collect(child, looksLikeClass(key) ? key : className, out);
            }

            std::uint64_t value = 0;
            if (!toNumber(child, value)) continue;

            const int priority = classPriority(className);
            auto existing = out.find(key);
            if (existing == out.end() || priority >= existing->second.priority) {
                out[key] = Found{className, value, priority};
            }
        }
    } else if (node.isArray()) {
        const QJsonArray array = node.toArray();
        for (const QJsonValue& child : array) collect(child, className, out);
    }
}

}  // namespace

ImportResult importOffsetsFile(const std::string& path, Offsets& offsets) {
    ImportResult result;

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        result.message = "无法打开偏移文件: " + path;
        return result;
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        result.message = "偏移文件解析失败: " + error.errorString().toStdString();
        return result;
    }

    std::map<QString, Found> found;
    collect(QJsonValue(document.object()), QString(), found);
    if (found.empty()) {
        result.message = "偏移文件内未发现可识别字段";
        return result;
    }

    for (const FieldBinding& binding : bindings()) {
        auto hit = found.find(QString::fromLatin1(binding.primary));
        if (hit == found.end() && binding.alias) {
            hit = found.find(QString::fromLatin1(binding.alias));
        }
        if (hit == found.end()) continue;

        binding.apply(offsets, hit->second.value);
        ++result.applied;
    }

    result.loaded = result.applied > 0;
    result.message = result.loaded
                         ? "已从 " + path + " 导入 " + std::to_string(result.applied) + " 项偏移"
                         : "偏移文件内没有匹配的字段";
    return result;
}

}  // namespace wv
