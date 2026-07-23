# valijson (vendored)

Header-only JSON Schema validator, used to validate user config files against
`deviceConfigs/userConfigSchema.json` at load time.

- Upstream: https://github.com/tristanpenman/valijson
- Version: v1.0.6
- License: BSD 2-Clause (see LICENSE)

Trimmed to what this project uses: the core validator plus the Qt
(QJsonDocument) adapter. Adapters/utils for other JSON libraries (nlohmann,
rapidjson, boost, poco, ...) were deleted. To upgrade, replace `include/`
with the new release's `include/`, re-apply the same trim, and update the
version line above.
