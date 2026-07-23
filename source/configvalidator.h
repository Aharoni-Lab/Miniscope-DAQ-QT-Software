#ifndef CONFIGVALIDATOR_H
#define CONFIGVALIDATOR_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

// User-config schema validation and in-memory migration.
//
// Policy (per lab decision, July 2026): configs are validated against
// deviceConfigs/userConfigSchema.json but validation NEVER blocks running -
// extra keys are explicitly allowed (labs keep notes and retired settings in
// their configs), and violations surface as warnings so a typo'd key or a
// wrong type is no longer silently replaced by a default. The only blocking
// checks remain the ones that make a config unusable (no devices, duplicate
// device names, unsupported codec), enforced in backEnd as before.

// Rewrites deprecated spellings in-place so the rest of the code base only
// ever sees the canonical keys. Returns one human-readable note per applied
// migration (empty when the config was already canonical). Old spellings
// remain accepted forever; files on disk are never rewritten behind the
// user's back (an explicit save from the editor writes canonical keys).
QStringList migrateUserConfig(QJsonObject &config);

// Validates config against a JSON Schema (the parsed contents of
// userConfigSchema.json). Returns one warning line per violation, formatted
// as "<json path>: <problem>"; empty when the config conforms. Never throws.
QStringList validateUserConfigAgainstSchema(const QJsonObject &config,
                                            const QJsonObject &schema);

// Convenience used by backEnd at load time: migrate, then validate against
// the schema file at schemaPath (relative to the working directory, like all
// deviceConfigs reads). A missing/unparseable schema file yields a single
// warning rather than blocking - the app must still run from a source tree
// or a damaged install.
QStringList checkUserConfig(QJsonObject &config,
                            const QString &schemaPath = QStringLiteral("deviceConfigs/userConfigSchema.json"));

#endif // CONFIGVALIDATOR_H
