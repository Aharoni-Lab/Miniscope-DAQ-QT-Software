#include "configvalidator.h"

#include <QFile>
#include <QJsonDocument>

#include <valijson/adapters/qtjson_adapter.hpp>
#include <valijson/schema.hpp>
#include <valijson/schema_parser.hpp>
#include <valijson/validation_results.hpp>
#include <valijson/validator.hpp>

QStringList migrateUserConfig(QJsonObject &config)
{
    QStringList notes;

    // v1.x shipped the record-length key with a lowercase "in"
    // (recordLengthinSeconds). The canonical key is the corrected spelling;
    // the old one keeps working forever. When both are present the canonical
    // one wins (assume the user added it deliberately).
    const QString oldKey = QStringLiteral("recordLengthinSeconds");
    const QString newKey = QStringLiteral("recordLengthInSeconds");
    if (config.contains(oldKey)) {
        if (!config.contains(newKey)) {
            // Copy the value out BEFORE inserting: config[new] = config[old]
            // would let the insertion reallocate the object while the RHS
            // still references its old storage.
            const QJsonValue oldValue = config.value(oldKey);
            config[newKey] = oldValue;
            notes.append(QStringLiteral("\"%1\" is a deprecated spelling; using it as \"%2\". "
                                        "Consider renaming it in the config file.")
                             .arg(oldKey, newKey));
        } else {
            notes.append(QStringLiteral("Both \"%1\" and \"%2\" are set; using \"%2\".")
                             .arg(oldKey, newKey));
        }
        config.remove(oldKey);
    }

    return notes;
}

QStringList validateUserConfigAgainstSchema(const QJsonObject &config,
                                            const QJsonObject &schema)
{
    QStringList warnings;

    valijson::Schema parsedSchema;
    valijson::SchemaParser parser;
    const QJsonValue schemaValue(schema);
    valijson::adapters::QtJsonAdapter schemaAdapter(schemaValue);
    try {
        parser.populateSchema(schemaAdapter, parsedSchema);
    } catch (const std::exception &e) {
        warnings.append(QStringLiteral("Config schema could not be parsed (%1); "
                                       "skipping config validation.")
                            .arg(QString::fromUtf8(e.what())));
        return warnings;
    }

    valijson::Validator validator;
    valijson::ValidationResults results;
    const QJsonValue configValue(config);
    valijson::adapters::QtJsonAdapter configAdapter(configValue);
    if (validator.validate(parsedSchema, configAdapter, &results)) {
        return warnings;   // conforms
    }

    valijson::ValidationResults::Error error;
    while (results.popError(error)) {
        // Context segments arrive as "<root>", "[devices]", "[cameras]",
        // "[My WebCam]", "[deviceID]"; render as devices.cameras.My WebCam.deviceID.
        QStringList path;
        for (const std::string &segment : error.context) {
            QString s = QString::fromStdString(segment);
            if (s == QStringLiteral("<root>"))
                continue;
            if (s.startsWith(QLatin1Char('[')) && s.endsWith(QLatin1Char(']')))
                s = s.mid(1, s.size() - 2);
            path.append(s);
        }
        const QString where = path.isEmpty() ? QStringLiteral("(config root)")
                                             : path.join(QLatin1Char('.'));
        warnings.append(where + QStringLiteral(": ")
                        + QString::fromStdString(error.description));
    }
    return warnings;
}

QStringList checkUserConfig(QJsonObject &config, const QString &schemaPath)
{
    QStringList messages = migrateUserConfig(config);

    QFile file(schemaPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        messages.append(QStringLiteral("Config schema %1 not found; skipping config validation.")
                            .arg(schemaPath));
        return messages;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        messages.append(QStringLiteral("Config schema %1 is not valid JSON (%2); "
                                       "skipping config validation.")
                            .arg(schemaPath, parseError.errorString()));
        return messages;
    }

    messages.append(validateUserConfigAgainstSchema(config, doc.object()));
    return messages;
}
