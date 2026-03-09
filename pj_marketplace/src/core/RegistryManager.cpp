#include "core/RegistryManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

namespace PJ {

RegistryManager::RegistryManager(QObject* parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

void RegistryManager::fetchRegistry(const QUrl& url) {
  // Cancel any in-flight request before starting a new one.
  if (m_pending_reply && m_pending_reply->isRunning()) {
    m_pending_reply->abort();
  }

  m_extensions.clear();

  emit fetchStarted();

  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

  m_pending_reply = m_network->get(request);

  connect(m_pending_reply, &QNetworkReply::finished, this, [this]() {
    // Guard against a second invocation if the reply is reused.
    auto* reply = m_pending_reply;
    m_pending_reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
      emit fetchError(reply->errorString());
      emit fetchFinished(false);
      reply->deleteLater();
      return;
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    const bool ok = parseJson(data);
    emit fetchFinished(ok);
  });
}

QList<Extension> RegistryManager::extensions() const {
  return m_extensions;
}

Extension RegistryManager::findById(const QString& id) const {
  for (const Extension& ext : m_extensions) {
    if (ext.id == id) {
      return ext;
    }
  }
  return {};  // Default-constructed: id is empty, callers must check is_valid()
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

// Reads a required string field from a JSON object.
// Returns false and emits fetchError() when the field is missing or not a string.
static std::optional<QString> requiredString(const QJsonObject& obj, const QString& key, RegistryManager* self) {
  if (!obj.contains(key) || !obj[key].isString()) {
    emit self->fetchError(QString("Registry parse error: missing required field \"%1\"").arg(key));
    return std::nullopt;
  }
  return obj[key].toString();
}

bool RegistryManager::parseJson(const QByteArray& data) {
  QJsonParseError parse_error;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &parse_error);

  if (doc.isNull()) {
    emit fetchError(QString("JSON parse error: %1").arg(parse_error.errorString()));
    return false;
  }

  if (!doc.isObject()) {
    emit fetchError("Registry JSON root must be an object");
    return false;
  }

  const QJsonObject root = doc.object();

  if (!root.contains("extensions") || !root["extensions"].isArray()) {
    emit fetchError("Registry JSON missing \"extensions\" array");
    return false;
  }

  QList<Extension> parsed;

  for (const QJsonValue& value : root["extensions"].toArray()) {
    if (!value.isObject()) {
      emit fetchError("Each entry in \"extensions\" must be a JSON object");
      return false;
    }

    const QJsonObject obj = value.toObject();
    Extension ext;

    // Required fields — abort the entire fetch if any are missing.
    auto id = requiredString(obj, "id", this);
    auto name = requiredString(obj, "name", this);
    auto version = requiredString(obj, "version", this);

    if (!id || !name || !version) {
      return false;
    }

    ext.id = *id;
    ext.name = *name;
    ext.version = *version;

    // Optional fields — use empty string as sentinel when absent.
    ext.description = obj["description"].toString();
    ext.author = obj["author"].toString();
    ext.publisher = obj["publisher"].toString();
    ext.website = obj["website"].toString();
    ext.repository = obj["repository"].toString();
    ext.license = obj["license"].toString();
    ext.icon_url = obj["icon_url"].toString();
    ext.category = obj["category"].toString();
    ext.min_plotjuggler_version = obj["min_plotjuggler_version"].toString();

    for (const QJsonValue& tag : obj["tags"].toArray()) {
      ext.tags.append(tag.toString());
    }

    // Platforms: { "linux-x86_64": { "url": "...", "checksum": "sha256:..." } }
    const QJsonObject platforms = obj["platforms"].toObject();
    for (auto it = platforms.begin(); it != platforms.end(); ++it) {
      if (!it.value().isObject()) {
        continue;
      }
      const QJsonObject artifact_obj = it.value().toObject();
      Platform artifact;
      artifact.url = artifact_obj["url"].toString();
      artifact.checksum = artifact_obj["checksum"].toString();
      ext.platforms.insert(it.key(), artifact);
    }

    // Changelog: { "1.0.0": "Initial release", "1.1.0": "Bug fixes" }
    const QJsonObject changelog = obj["changelog"].toObject();
    for (auto it = changelog.begin(); it != changelog.end(); ++it) {
      ext.changelog.insert(it.key(), it.value().toString());
    }

    parsed.append(ext);
  }

  m_extensions = std::move(parsed);
  return true;
}

}  // namespace PJ
