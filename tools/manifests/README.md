# Tool manifests

Milestone 2.5 loads these UTF-8 JSON files through ToolRegistry. The directory contains 15 real entries. Public metadata only: local executable paths, favorites and recent activity belong in user Config. See docs/manifest.md, docs/catalog.md and tools/manifest.schema.json. Unknown fields are ignored; invalid files are diagnosed and skipped.
