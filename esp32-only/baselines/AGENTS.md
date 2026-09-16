# Frozen runnable baselines

All existing baseline directories below this directory are frozen reference copies.
Do not edit, format, refactor, upgrade, or synchronize their source, tests, dependencies,
configuration, or documentation during ordinary project development.
Develop in esp32-only/firmware, esp32-only/host and the main tests directories instead.
Only modify an existing baseline when the user explicitly requests that baseline be changed.
For a new reference version, create a separately named baseline; preserve old versions.
Run verify_snapshot.ps1 to detect accidental changes. Keep generated logs outside baselines.
Local .pub files and wifi_config.h are deliberately ignored by Git; never publish their contents.
