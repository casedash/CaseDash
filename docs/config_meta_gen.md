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
- `build/cmake/generated/config/config_meta.generated.json` manifest for generator tests and review diffs

The generated header is included by `src/config/config.h`. The generated `.cpp` is compiled into `CaseDash`, `CaseDashTests`, and `CaseDashBenchmarks`.

Hand-authored config value types stay in source. This includes `ColorConfig`, `UiFontConfig`, `LogicalPointConfig`, `LogicalSizeConfig`, `LayoutNodeConfig`, `MetricDefinitionConfig`, telemetry selection DTOs, config parser context types, color expression helpers, and custom codec behavior. The generated files own only schema-shaped structs, section descriptors, field descriptors, container binding descriptors, root descriptors, and layout-edit parameter metadata.

Generated metadata should use plain runtime tables and small generated typed helper functions. Making all fields editable should not add a second metadata family or materially increase code size; parser, writer, color, and edit paths should share the same field-offset descriptors. The rework should not reintroduce template reflection, field-count probing, friend `consteval` functions, or macro-expanded field descriptors.

## Descriptor Language

The descriptor uses ordinary struct and field declarations plus `// config_meta:` directives. The parser is intentionally line-oriented and fail-fast; it is not a general C++ parser.

The grammar is deliberately small:

```text
descriptor        := (blank | comment | include | namespace_alias | struct_decl)*
include           := "#include" quoted_path
namespace_alias   := "namespace" identifier "=" qualified_identifier ";"
struct_decl       := struct_directive? "struct" type_name "{" field_decl* "};"
struct_directive  := "// config_meta:" struct_kind
struct_kind       := "static" section_name
                   | "dynamic_section" section_pattern "key=" identifier
                   | "custom_section" section_name "codec=" codec_name
                   | "container"
                   | "root"
field_decl        := field_type identifier ";" field_directive?
field_type        := qualified_identifier | "std::vector<" qualified_identifier ">"
field_directive   := "// config_meta:" field_attr+
field_attr        := "runtime_only" | "policy=" policy_name
policy_name       := "none" | "positive_int" | "non_negative_int" | "font_size" | "degrees"
section_pattern   := "[" literal_prefix "$" identifier "]"
section_name      := "[" literal "]"
qualified_identifier := identifier ("::" identifier)*
type_name         := identifier
codec_name        := identifier
```

The generator accepts `#include` lines and namespace aliases only so `src/config/config_desc.h` can stay readable beside the real source headers. It does not expand includes, evaluate aliases, parse templates beyond recognized field type text, or evaluate arbitrary C++ expressions. A struct directive applies to the next struct declaration. A field directive applies only to the field on the same line. Directives on unsupported lines are generator errors.

Static section types list their shipped config section name in the descriptor marker. The listed section name must match `resources/config.ini`.

Field keys are derived by converting the member name from lower camel case to snake case. The snake-case conversion inserts a word break before an uppercase letter that follows a lowercase letter or digit, and before the last uppercase letter in an acronym when the next character is lowercase; the result is lowercased. The generator does not support field-key overrides. `resources/config.ini` is the naming authority for shipped sections and keys. When generated names do not match that file, the source and descriptor names must change to match the config language; the generator should fail rather than encode an exception.

```cpp
// config_meta: static [fonts]
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

Custom sections describe hand-authored section codecs. They participate in the root binding order but do not generate fixed field tables.

```cpp
// config_meta: custom_section [board] codec=board
struct BoardConfig {
};

// config_meta: custom_section [metrics] codec=metrics
struct MetricsSectionConfig {
};
```

The generator maps `codec=board` to `BoardSectionCodec` and `codec=metrics` to `MetricsSectionCodec`. Unknown codec names are generator errors.

Every declared field is a config metadata field and an editable field by default. Use `// config_meta: runtime_only` only for fields that have no config key, save behavior, parser behavior, or editor behavior.

Non-default clamping policy is field metadata. The default policy is derived from the field type so previously non-editable fields can become editable without per-field annotations:

| Field type | Default value format | Default policy |
|---|---|---|
| `int` | `Integer` | `positive_int` |
| `double` | `FloatingPoint` | `none` |
| `std::string` | `String` | `none` |
| `std::vector<std::string>` | `String` | `none` |
| `LogicalPointConfig` | `String` | `none` |
| `LogicalSizeConfig` | `String` | `none` |
| `ColorConfig` | `ColorHex` | `none` |
| `UiFontConfig` | `FontSpec` | `font_size` |
| `LayoutNodeConfig` | `String` | `none` |

Use `policy=` only when a field needs behavior different from its type default. The generated parser and layout-edit setters apply the same policy table.

```cpp
// config_meta: static [dashboard]
struct DashboardSectionConfig {
    int outerMargin; // config_meta: policy=non_negative_int
    int rowGap; // config_meta: policy=non_negative_int
    int columnGap; // config_meta: policy=non_negative_int
};

// config_meta: static [gauge]
struct GaugeWidgetConfig {
    int outerPadding; // config_meta: policy=non_negative_int
    double sweepDegrees; // config_meta: policy=degrees
    double segmentGapDegrees; // config_meta: policy=degrees
};
```

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

Current source should be renamed where needed so generated names match `resources/config.ini`:

| Current member | Config key | Planned member |
|---|---|---|
| `UiFontSetConfig::smallText` | `small` | `small` |
| `CardStyleConfig::cardBorderWidth` | `card_border` | `cardBorder` |
| `LayoutSectionConfig::cardsLayout` | `cards` | `cards` |

The current runtime-only `LayoutConfig::cardsLayout` member needs an implementation audit before it is generated. Production code reads `layout.structure.cardsLayout`; only the difference check and tests mention `LayoutConfig::cardsLayout` directly. The codegen migration should either remove that member or give it a clear runtime-only purpose before preserving it.

Type names should stay section-shaped and readable even though static section spelling comes from the directive. During the migration, rename confusing type names such as `UiFontSetConfig` to section-shaped names such as `FontsConfig`.

Generator tests should compare the manifest against `resources/config.ini` for shipped static section order, static keys, dynamic section prefixes, dynamic keys from representative sections, and custom section names. The comparison should explicitly exclude custom section dynamic keys such as `[board]` sensor bindings and `[metrics]` metric ids because those keys are codec-owned.

## Generated Runtime Metadata

The generator emits these runtime descriptor shapes:

- field tables with key pointer, key length, owner offset, value kind, policy, and value format
- static section descriptors with section name, owner offset from `AppConfig`, codec kind, and field span
- dynamic section descriptors with prefix, vector owner offset from `AppConfig`, key member offset, item size, codec kind, field span, and generated helper callbacks to find, ensure, and iterate items
- custom section descriptors for `[board]` and `[metrics]` that route to hand-authored parser and writer functions
- color field spans for `ColorConfig` fields in `[theme.<name>]`, `[colors]`, and `[layout_guide_sheet]`
- editable field metadata for every generated config field
- stable active-region priority metadata for fields that are used by layout-edit hit testing
- dynamic field metadata for active named layout, active theme, card, and other dynamic-section editors
- custom editable adapters for codec-owned sections such as `[metrics]`

Parser and writer dispatch should become table-driven from a flattened `AppConfig` section descriptor list. That removes the current recursive template binding walk while preserving the same section order:

1. `[display]`
2. `[gpu]`
3. `[network]`
4. `[storage]`
5. nested `LayoutConfig` sections in the order currently declared there

Dynamic section helpers may be generated as small typed functions in `config_meta.generated.cpp`. The public call sites should see only descriptor tables and non-template helper APIs.

## Generated Editable Metadata

The descriptor does not mark individual fields as editable. Every generated config field is editable unless it is explicitly marked `runtime_only`.

The generator emits one editable-field table for all generated fields. Each entry has:

- stable field id
- section name or dynamic section prefix
- parameter name
- address kind: root field, dynamic item field, or custom adapter field
- runtime value kind
- layout-edit value format
- clamp policy
- active-region parameter id when the field has a widget hit target
- deterministic tree order from generated section order and field order

For each static-section field, the generator also emits:

- root offset from `AppConfig`

For each dynamic-section field, the generator emits:

- dynamic section descriptor id
- key member name and key member offset
- field offset from the dynamic item
- generated typed callbacks to find and ensure the vector item

Dynamic editable fields are resolved through explicit edit scopes:

- `ActiveTheme` resolves `[theme.<name>]` from `config.display.theme`. Missing themes do not create editor leaves.
- `ActiveNamedLayout` resolves `[layout.<name>]` from `config.display.layout`. Layout-structure editors continue to edit the resolved `layout.structure` mirror and the matching named layout together.
- `CardById` resolves `[card.<id>]` from the card id stored in the tree leaf focus key.
- `ItemKey` resolves a dynamic section item from the key stored in the tree leaf focus key.
- `Custom` routes through a hand-authored adapter.

Mutable dynamic access must be explicit about creation. Parser dispatch uses ensure semantics because a config section creates or updates the keyed vector item. Layout-edit preview and apply paths use find semantics for existing leaves unless a specific editor workflow creates a new item.

Codec-owned sections are not forced into fake field tables. `[metrics]` remains a custom editable adapter keyed by metric id and validated through `ConfigMetricCatalog`; `[board]` remains a custom codec for sensor binding save/load behavior. Custom adapters can expose layout-edit leaves, value formats, and preview/apply functions, but they do not claim generated field offsets.

`LayoutEditParameter` identifiers are generated from fixed rules instead of per-field annotations. Existing active-region parameters must keep their current relative priority because hit testing uses priority order. Newly editable fields that are not active-region targets can be appended in deterministic config order unless a later UI change gives them interactive hit regions.

Theme token colors should use generated dynamic field metadata rather than a hard-coded list in `layout_edit_tree.cpp` and `layout_edit_dialog/impl/editors.cpp`. They are ordinary editable fields in the active `[theme.<name>]` item, not a separately annotated theme-token list.

Layout-node editing remains path-based because `LayoutNodeConfig` values contain a tree. Generated field metadata identifies the owning `cards` or `layout` config field, while existing layout-node edit keys continue to carry child paths, gap anchors, card ids, and weight-edit context.

## Implementation Plan

### Prepare The Schema Boundary

Create hand-authored config support headers so generated code can include only stable value types and small descriptor declarations:

- Move or isolate `ColorConfig`, `UiFontConfig`, `LogicalPointConfig`, `LogicalSizeConfig`, `LayoutNodeConfig`, `MetricDefinitionConfig`, `BoardConfig`, and `MetricsSectionConfig` into a source-owned header such as `src/config/config_types.h`.
- Keep config behavior functions in `src/config/config.h` and `src/config/config.cpp`.
- Move policy and value-kind enums that still belong to runtime dispatch out of `config_schema.h` and into a small non-template header.
- Leave existing macros in place during this preparation step so behavior and tests still pass.
- Keep schema structs hand-authored during this phase. Generated headers may declare metadata tables and accessors that reference those structs, but they must not define duplicate structs.

### Add The Generator Skeleton

Add `tools/config_meta_gen.py` and `src/config/config_desc.h`.

The first generator version should:

- parse the descriptor grammar defined above, including static section, dynamic section, custom section, container, root, policy, and runtime-only directives
- treat every non-runtime field as a generated config field and editable field
- validate duplicate section names, duplicate keys, missing dynamic key fields, unknown policy names, unsupported field types, malformed directives, misplaced directives, and names that do not match `resources/config.ini`
- write outputs only when content changes
- emit a JSON manifest with generated sections, keys, dynamic prefixes, policies, value kinds, value formats, custom codecs, and edit scopes

Add CMake custom command inputs and outputs under `build/cmake/generated/config/`, and add a `CaseDashGeneratedConfigMeta` target. Make `CaseDash`, `CaseDashTests`, and `CaseDashBenchmarks` depend on it.

### Generate Runtime Field Tables First

Keep the current structs and macro reflection temporarily, but switch `config_runtime_fields.cpp` to consume generated field arrays instead of building them through `consteval` reflection.

This phase proves:

- section and field spelling matches `resources/config.ini` without overrides
- offsets match the current structs
- default and explicit policies match the descriptor contract
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

Once the generated tables drive behavior, move schema-owned structs from `src/config/config.h` into `config_meta.generated.h`. Ownership changes one struct family at a time: a struct is either hand-authored or generated, never both. Each handoff removes the hand-authored definition before the generated header defines the same type.

Do this in small commits or patches:

- generate `DisplayConfig`, `GpuConfig`, `NetworkConfig`, `StorageConfig`, `ThemeConfig`, `FontsConfig`, style/widget config sections, `LayoutSectionConfig`, `LayoutCardConfig`, `LayoutConfig`, and `AppConfig`
- keep custom value and custom section payload types hand-authored
- rename fields and types whose derived names do not match `resources/config.ini`
- update all source and tests for `small`, `cardBorder`, and `LayoutSectionConfig::cards`
- delete `CONFIG_VALUE`, `CONFIG_EDITABLE_VALUE`, `CONFIG_SECTION`, `CONFIG_DYNAMIC_SECTION`, root binding macros, and reflected binding macros after no call sites remain

During the transition, `src/config/config.h` includes the generated header after the value types and custom section payloads are available. The generated header supplies declarations and accessors for hand-authored structs until the final ownership handoff, then supplies the schema struct definitions. The generated structs should stay default-initialized and must not introduce C++ fallback defaults that duplicate `resources/config.ini`.

### Generate Editable Metadata

Move the layout-edit parameter list and editable field metadata table into generated output.

Implementation details:

- replace `CASEDASH_LAYOUT_EDIT_PARAMETER_ITEMS` with a generated enum declaration and generated metadata table
- preserve current enum order as hit priority
- generate `GetLayoutEditParameterInfo`, `GetLayoutEditConfigFieldMetadata`, `FindLayoutEditParameterByConfigField`, tooltip descriptor data, dynamic editable-field lookup data, and custom editable adapter routing from the same metadata source
- update `layout_edit_parameter_edit.cpp` to use generated root offsets exactly as it does today
- replace manual color-role switches in dashboard and dialog code with metadata-based color field lookup where practical
- replace hard-coded theme token lists with generated `ActiveTheme` dynamic field metadata for `ThemeConfig`

Tests should continue to verify metadata names, root-offset application, dynamic-field find and apply behavior, custom metric adapter behavior, color field access, font field access, display names, and enum/table order.

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
