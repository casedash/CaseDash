# Config Descriptor Codegen

This document describes the planned config metadata codegen rework. The goal is to remove the current config macro and template-reflection path from normal compilation while keeping one maintained source of truth for the config language, layout-edit field metadata, parser and writer field tables, and config section binding graph.

The source review did not leave a blocking design question. The plan below records the design decisions that close the gaps in the old sketch and should be followed before implementation starts.

## Source Review

The current implementation has these relevant pieces:

- [src/config/config.h](../src/config/config.h) owns the runtime config structs and uses `CONFIG_VALUE`, `CONFIG_EDITABLE_VALUE`, `CONFIG_SECTION`, `CONFIG_DYNAMIC_SECTION`, and binding macros to describe fields, section names, dynamic section prefixes, and root paths.
- [src/config/config_schema.h](../src/config/config_schema.h) owns the macro and `consteval` reflection machinery. It creates field descriptors, section descriptors, structured binding descriptors, root-field lenses, edit traits, and policy clamps.
- [src/config/config_runtime_fields.cpp](../src/config/config_runtime_fields.cpp) turns reflected section fields into compact offset descriptors. Parser, writer, color resolver, and layout-edit code already consume this smaller runtime shape.
- [src/config/config_parser.cpp](../src/config/config_parser.cpp) and [src/config/config_writer.cpp](../src/config/config_writer.cpp) dispatch through the reflected binding graph. `[board]` and `[metrics]` are custom-codec sections and must stay special because their config keys are not a fixed field list.
- [src/config/color_resolver.cpp](../src/config/color_resolver.cpp) scans runtime field descriptors to resolve `ColorConfig` values in `[theme.<name>]`, `[colors]`, and `[layout_guide_sheet]`.
- [src/widget/layout_edit_parameter_id.h](../src/widget/layout_edit_parameter_id.h), [src/layout_model/layout_edit_parameter_metadata.cpp](../src/layout_model/layout_edit_parameter_metadata.cpp), and [src/layout_edit/layout_edit_parameter_edit.cpp](../src/layout_edit/layout_edit_parameter_edit.cpp) derive layout-edit parameters from editable field lenses. `LayoutEditParameter` order is also hit-test priority order.
- [src/layout_edit/layout_edit_tree.cpp](../src/layout_edit/layout_edit_tree.cpp) uses the embedded `resources/config.ini` order for the editor tree, then filters leaves through layout-edit metadata.
- [resources/config.ini](../resources/config.ini) remains the spelling authority for shipped config sections and keys. [docs/layout.md](layout.md), [docs/theme_configuration.md](theme_configuration.md), and [docs/layout_edit.md](layout_edit.md) own the user-visible language and edit behavior.
- CMake already runs Python generators into `build/cmake/generated`. Config metadata codegen should follow that generated-output pattern instead of committing generated C++ files.

The old sketch was directionally right, but it missed several source-backed requirements and target constraints:

- The target schema treats every persisted field as editable by default. Fields that the current UI does not edit, such as `[display]`, `[gpu]`, `[network]`, `[storage]`, `[layout_guide_sheet]`, `[layout.<name>]`, and `[card.<id>]` fields, should use the same generated offset metadata instead of a separate non-editable path.
- Dynamic sections need explicit suffix-key ownership. `[theme.<name>]`, `[layout.<name>]`, and `[card.<id>]` map to vectors keyed by `name` or `id`.
- Custom sections need explicit codec ownership. `[board]` writes logical board bindings from runtime-discovered requested metric names, and `[metrics]` validates metric ids through `ConfigMetricCatalog`.
- Editable metadata needs root offsets or dynamic-item offsets, value formats, value kinds, policies, and stable priority order for fields that already participate in active-region hit testing.
- Theme token colors are editable fields in a dynamic section. Their owner is the active dynamic theme item selected by `config.display.theme`, so generated metadata must support dynamic field access as well as fixed root offsets.
- Strict field-to-key derivation requires source renames for the few fields whose member names do not currently match their config keys. The config spelling is the contract; C++ member names should move to match it.

## Target Shape

`src/config/config_desc.h` becomes the maintained schema input. It is a C++-like descriptor file parsed only by `tools/config_meta_gen.py`; it is not compiled directly.

CMake generates:

- `build/cmake/generated/config/config_meta.generated.h`
- `build/cmake/generated/config/config_meta.generated.cpp`
- an optional `build/cmake/generated/config/config_meta.generated.json` manifest for generator tests and review diffs

The generated header is included by `src/config/config.h`. The generated `.cpp` is compiled into `CaseDash`, `CaseDashTests`, and `CaseDashBenchmarks`.

Hand-authored config value types stay in source. This includes `ColorConfig`, `UiFontConfig`, `LogicalPointConfig`, `LogicalSizeConfig`, `LayoutNodeConfig`, `MetricDefinitionConfig`, telemetry selection DTOs, config parser context types, color expression helpers, and custom codec behavior. The generated files own only schema-shaped structs, section descriptors, field descriptors, container binding descriptors, root descriptors, and layout-edit parameter metadata.

Generated metadata should use plain runtime tables and small generated typed helper functions. Making all fields editable should not add a second metadata family or materially increase code size; parser, writer, color, and edit paths should share the same field-offset descriptors. The rework should not reintroduce template reflection, field-count probing, friend `consteval` functions, or macro-expanded field descriptors.

## Descriptor Language

The descriptor uses ordinary struct and field declarations plus `// config_meta:` directives. The parser is intentionally line-oriented and fail-fast; it is not a general C++ parser.

Static section types use a descriptor marker but do not name their section explicitly. The section name is derived from the type name by fixed generator rules, such as stripping `Config`, `SectionConfig`, or `WidgetConfig` suffixes and converting the remaining name to snake case. If an existing type name cannot express the shipped section name cleanly, the source type should be renamed rather than annotated.

```cpp
// config_meta: static
struct FontsConfig {
    UiFontConfig title;
    UiFontConfig big;
    UiFontConfig value;
    UiFontConfig label;
    UiFontConfig text;
    UiFontConfig small;
    UiFontConfig footer;
    UiFontConfig clockTime;
    UiFontConfig clockDate;
};
```

Dynamic sections name their suffix field in the struct directive. The key member is derived from `key=name`; it does not need a separate marker on the field.

```cpp
// config_meta: dynamic_section [theme.$name] key=name
struct ThemeConfig {
    std::string name;
    std::string description;
    ColorConfig background;
    ColorConfig foreground;
    ColorConfig accent;
    ColorConfig guide;
};
```

Every declared field is a config metadata field by default. Use `// config_meta: runtime_only` only for fields that have no config key, save behavior, parser behavior, or editor behavior.

Container directives describe ownership rather than config keys:

```cpp
// config_meta: container
struct LayoutConfig {
    ColorsConfig colors;
    LayoutGuideSheetConfig layoutGuideSheet;
    std::vector<ThemeConfig> themes;
    DashboardSectionConfig dashboard;
    CardStyleConfig cardStyle;
    MetricListWidgetConfig metricList;
    DriveUsageListWidgetConfig driveUsageList;
    ThroughputWidgetConfig throughput;
    GaugeWidgetConfig gauge;
    TextWidgetConfig text;
    NetworkFooterWidgetConfig networkFooter;
    LayoutEditorConfig layoutEditor;
    FontsConfig fonts;
    BoardConfig board;
    MetricsSectionConfig metrics;
    std::vector<LayoutCardConfig> cards;
    std::vector<LayoutSectionConfig> layouts;

    LayoutSectionConfig structure; // config_meta: runtime_only
};
```

`AppConfig` is the only root descriptor. Nested structured owners such as `LayoutConfig` use `container`.

```cpp
// config_meta: root
struct AppConfig {
    DisplayConfig display;
    GpuConfig gpu;
    NetworkConfig network;
    StorageConfig storage;
    LayoutConfig layout;
};
```

Section membership follows from field type. A field whose type has a static section descriptor is a static section, a `std::vector<T>` whose item type has a dynamic section descriptor is a dynamic section collection, a custom-codec type is a custom section, and a container or root type is a recursive binding. Root fields need no section annotations.

Field keys are derived only by converting the member name from lower camel case to snake case. The generator should not support field-key overrides. Current source should be renamed where needed so the member name matches the config key:

| Current member | Config key | Planned member |
|---|---|---|
| `UiFontSetConfig::smallText` | `small` | `small` |
| `CardStyleConfig::cardBorderWidth` | `card_border` | `cardBorder` |
| `LayoutSectionConfig::cardsLayout` | `cards` | `cards` |

The current runtime-only `LayoutConfig::cardsLayout` member needs an implementation audit before it is generated. Production code reads `layout.structure.cardsLayout`; only the difference check and tests mention `LayoutConfig::cardsLayout` directly. The codegen migration should either remove that member or give it a clear runtime-only purpose before preserving it.

Type names should also preserve section spelling through the generator's fixed type-to-section rules. If the current `UiFontSetConfig` name cannot derive `[fonts]` without a special case, rename it to a section-shaped type such as `FontsConfig` during the migration.

## Generated Runtime Metadata

The generator emits these runtime descriptor shapes:

- field tables with key pointer, key length, owner offset, value kind, policy, and value format
- static section descriptors with section name, owner offset from `AppConfig`, codec kind, and field span
- dynamic section descriptors with prefix, vector owner offset from `AppConfig`, key member offset, item size, codec kind, field span, and generated helper callbacks to find, ensure, and iterate items
- custom section descriptors for `[board]` and `[metrics]` that route to hand-authored parser and writer functions
- color field spans for `ColorConfig` fields in `[theme.<name>]`, `[colors]`, and `[layout_guide_sheet]`
- editable field metadata for every generated config field
- stable active-region priority metadata for fields that are used by layout-edit hit testing
- dynamic field metadata for active named layout, active theme, card, metrics, and other dynamic-section editors

Parser and writer dispatch should become table-driven from a flattened `AppConfig` section descriptor list. That removes the current recursive template binding walk while preserving the same section order:

1. `[display]`
2. `[gpu]`
3. `[network]`
4. `[storage]`
5. nested `LayoutConfig` sections in the order currently declared there

Dynamic section helpers may be generated as small typed functions in `config_meta.generated.cpp`. The public call sites should see only descriptor tables and non-template helper APIs.

## Generated Editable Metadata

The descriptor does not mark individual fields as editable. Every generated config field is editable unless it is explicitly marked `runtime_only`.

For each static-section field, the generator emits:

- section name and parameter name
- root offset from `AppConfig`
- runtime value kind
- layout-edit value format
- clamp policy

For each dynamic-section field, the generator emits:

- dynamic section prefix
- item key field
- field offset from the dynamic item
- runtime value kind
- layout-edit value format
- clamp policy

`LayoutEditParameter` identifiers are generated from fixed rules instead of per-field annotations. Existing active-region parameters must keep their current relative priority because hit testing uses priority order. Newly editable fields that are not active-region targets can be appended in deterministic config order unless a later UI change gives them interactive hit regions.

Theme token colors should use generated dynamic field metadata rather than a hard-coded list in `layout_edit_tree.cpp` and `layout_edit_dialog/impl/editors.cpp`. They are ordinary editable fields in the active `[theme.<name>]` item, not a separately annotated theme-token list.

## Implementation Plan

### Prepare The Schema Boundary

Create hand-authored config support headers so generated code can include only stable value types and small descriptor declarations:

- Move or isolate `ColorConfig`, `UiFontConfig`, `LogicalPointConfig`, `LogicalSizeConfig`, `LayoutNodeConfig`, `MetricDefinitionConfig`, `BoardConfig`, and `MetricsSectionConfig` into a source-owned header such as `src/config/config_types.h`.
- Keep config behavior functions in `src/config/config.h` and `src/config/config.cpp`.
- Move policy and value-kind enums that still belong to runtime dispatch out of `config_schema.h` and into a small non-template header.
- Leave existing macros in place during this preparation step so behavior and tests still pass.

### Add The Generator Skeleton

Add `tools/config_meta_gen.py` and `src/config/config_desc.h`.

The first generator version should:

- parse static section, dynamic section, container, root, field, policy, custom codec, and runtime-only directives
- treat every non-runtime field as a generated config field and editable field
- validate duplicate section names, duplicate keys, missing dynamic key fields, unknown policy names, unsupported field types, and any field name whose derived snake-case key does not match the intended config key
- write outputs only when content changes
- emit a JSON manifest that can be asserted in tests without compiling generated C++

Add CMake custom command inputs and outputs under `build/cmake/generated/config/`, and add a `CaseDashGeneratedConfigMeta` target. Make `CaseDash`, `CaseDashTests`, and `CaseDashBenchmarks` depend on it.

### Generate Runtime Field Tables First

Keep the current structs and macro reflection temporarily, but switch `config_runtime_fields.cpp` to consume generated field arrays instead of building them through `consteval` reflection.

This phase proves:

- field key spelling matches `resources/config.ini`
- offsets match the current structs
- parser and writer behavior stay unchanged
- color resolver can scan generated color field tables
- generated editable metadata covers fields that the current editor does not expose without adding duplicate descriptor tables
- custom sections still route through hand-authored handlers

Add tests that compare representative generated field metadata for `fonts`, `colors`, `theme`, `layout`, `card`, `board`, and `metrics`.

### Generate Section Dispatch

Replace the template binding walk in parser and writer with generated flattened section descriptors.

Parser work:

- find static sections by exact name
- find dynamic sections by prefix and ensure keyed vector items through generated callbacks
- dispatch structured sections through generated field spans
- dispatch custom sections through `BoardSectionCodec` and `MetricsSectionCodec`

Writer work:

- iterate the generated section descriptors in the same save order as today
- write static section differences through generated field spans
- write dynamic section differences for each keyed item
- preserve the existing insertion rules for `[gpu]`, `[network]`, `[storage]`, `[board]`, and `[metrics]`
- keep custom save logic for board and metrics

Run parser and writer tests after this step before touching the config structs.

### Move Schema Structs To Generated Code

Once the generated tables drive behavior, move schema-owned structs from `src/config/config.h` into `config_meta.generated.h`.

Do this in small commits or patches:

- generate `DisplayConfig`, `GpuConfig`, `NetworkConfig`, `StorageConfig`, `ThemeConfig`, `FontsConfig`, style/widget config sections, `LayoutSectionConfig`, `LayoutCardConfig`, `LayoutConfig`, and `AppConfig`
- keep custom value and custom section payload types hand-authored
- rename fields whose member names do not match derived config keys
- update all source and tests for `small`, `cardBorder`, and `LayoutSectionConfig::cards`
- delete `CONFIG_VALUE`, `CONFIG_EDITABLE_VALUE`, `CONFIG_SECTION`, `CONFIG_DYNAMIC_SECTION`, root binding macros, and reflected binding macros after no call sites remain

The generated structs should stay default-initialized and must not introduce C++ fallback defaults that duplicate `resources/config.ini`.

### Generate Editable Metadata

Move the layout-edit parameter list and editable field metadata table into generated output.

Implementation details:

- replace `CASEDASH_LAYOUT_EDIT_PARAMETER_ITEMS` with a generated enum declaration and generated metadata table
- preserve current enum order as hit priority
- generate `GetLayoutEditParameterInfo`, `GetLayoutEditConfigFieldMetadata`, `FindLayoutEditParameterByConfigField`, tooltip descriptor data, and generic editable-field lookup data from the same metadata table
- update `layout_edit_parameter_edit.cpp` to use generated root offsets exactly as it does today
- replace manual color-role switches in dashboard and dialog code with metadata-based color field lookup where practical
- replace hard-coded theme token lists with generated dynamic field metadata for `ThemeConfig`

Tests should continue to verify metadata names, root-offset application, dynamic-field application, color field access, font field access, display names, and enum/table order.

### Clean Up The Old Reflection Layer

After generated structs, section dispatch, runtime fields, and layout-edit metadata are all live:

- remove `src/config/config_schema.h` or reduce it to any genuinely shared non-template descriptor declarations that were not moved
- remove stale macros from config and widget headers
- remove template `RuntimeConfigFieldDescriptors<Section>()` declarations and replace them with non-template generated accessors
- update architecture notes to say config schema metadata is generated from `src/config/config_desc.h`
- update build documentation to mention the config metadata generator alongside the existing compressed-resource generator

### Validate

Use the maintained validation entrypoints:

```bat
build.cmd
test.cmd
format.cmd
```

Run focused headless diagnostics against the fresh build after the parser, writer, color, or layout-edit metadata paths change:

```bat
build\CaseDash.exe /default-config /fake /exit /trace:build\config_meta_trace.txt /save-full-config:build\config_meta_full.ini
build\CaseDash.exe /default-config /fake /exit /edit-layout /trace:build\config_meta_edit_trace.txt /screenshot:build\config_meta_edit.png
build\CaseDash.exe /default-config /fake /exit /edit-layout /trace:build\config_meta_guide_trace.txt /layout-guide-sheet:build\config_meta_guide.png
```

Inspect generated config text for section order, dynamic section names, derived color expressions, omitted runtime-only placeholder metric metadata, and preserved board or metrics custom behavior. Inspect layout-edit traces for the expected config paths and active-region metadata after the layout-edit parameter phase.

Do not run `lint.cmd tidy` locally unless explicitly requested.
