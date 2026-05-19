# Config Metadata Generation

This document describes the maintained config metadata generator and its ownership boundaries. `src/config/config_desc.h` is the single source for schema-shaped config structs, section names, dynamic-section prefixes, runtime field metadata, and generated layout-edit field metadata.

## Generated Outputs

CMake runs `tools/config_meta_gen.py` during normal builds. The generator writes:

- `build/cmake/generated/config/config_meta.generated.h`
- `build/cmake/generated/config/config_meta.generated.cpp`
- `build/cmake/generated/config/config_meta.generated.json`
- `build/cmake/generated/layout_model/layout_edit_parameter_metadata.generated.h`
- `build/cmake/generated/layout_model/layout_edit_parameter_metadata.generated.cpp`

`src/config/config.h` owns hand-authored value types and custom codec payloads, then includes `config/config_meta.generated.h` for the generated schema structs. `ColorConfig`, `UiFontConfig`, `LogicalPointConfig`, `LogicalSizeConfig`, `LayoutNodeConfig`, `MetricDefinitionConfig`, `BoardConfig`, and `MetricsSectionConfig` stay hand-authored because they carry behavior or codec-owned storage that is not a fixed schema field list.

The generated config `.cpp` owns the runtime field tables and flattened section descriptor table consumed by the parser, writer, color resolver, and metadata tests. The generated layout-edit `.cpp` owns only layout-edit config field metadata. The `LayoutEditParameter` enum stays hand-authored in `src/widget/layout_edit_parameter_id.h` because its order is a UI contract for hit-test priority.

## Descriptor Input

`src/config/config_desc.h` is a C++-like descriptor file parsed by the generator. It is readable next to the real config source but is not compiled directly. The parser is intentionally line-oriented and accepts only the small descriptor shape used by the project:

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
field_attr        := "runtime_only" | "policy=" policy_name | "rename=" field_key
policy_name       := "none" | "positive_int" | "non_negative_int" | "font_size" | "degrees"
section_pattern   := "[" literal_prefix "$" identifier "]"
section_name      := "[" literal "]"
codec_name        := "board" | "metrics"
```

The generator validates duplicate section names, duplicate field keys, missing dynamic key fields, malformed directives, unknown policies, unknown custom codecs, unsupported field types, and generated config spellings that do not match `resources/config.ini`.

## Section Kinds

Static sections map one generated struct to one persisted section:

```cpp
// config_meta: static [fonts]
struct FontsConfig {
    UiFontConfig title;
    UiFontConfig smallText;  // config_meta: rename=small
};
```

Field keys are derived by converting lower-camel C++ member names to snake case. Use `rename=` only when a C++ identifier must differ from the persisted key. `FontsConfig::smallText` uses `rename=small` so config files keep the `[fonts] small` key while source avoids the Win32 RPC `small` token.

Dynamic sections map a vector item type to a section prefix and key field:

```cpp
// config_meta: dynamic_section [theme.$name] key=name
struct ThemeConfig {
    std::string name;
    std::string description;
    ColorConfig background;
};
```

The generated section table includes typed callbacks to find, ensure, and enumerate dynamic items. Parser dispatch uses ensure semantics because a parsed dynamic section creates or updates a keyed item. Writer dispatch enumerates existing keyed items.

Custom sections describe codecs that own their own key space:

```cpp
// config_meta: custom_section [board] codec=board
struct BoardConfig {};

// config_meta: custom_section [metrics] codec=metrics
struct MetricsSectionConfig {};
```

`[board]` and `[metrics]` participate in the generated section order, but they do not receive generic field tables or generated `Section` aliases. Their parse and save behavior stays in hand-authored codec functions because board sensor bindings and metric definitions are not fixed fields.

Container and root structs describe ownership and traversal order. `AppConfig` is the only root. Nested owners such as `LayoutConfig` use `container`. Fields marked `runtime_only` stay in the generated schema struct but are excluded from parser, writer, manifest, and layout-edit field metadata.

## Runtime Metadata

Generated runtime metadata is table-driven. Public consumers use:

- `RuntimeConfigSectionDescriptors()`
- `RuntimeConfigFields(section)`
- `FindRuntimeConfigSection(sectionName)`
- `FindRuntimeConfigDynamicSection(sectionName)`
- `FindRuntimeConfigSectionByName(sectionName)`

The flattened section descriptor table records section name or prefix, section kind, codec kind, root owner offset, field span, and dynamic callbacks. Parser and writer dispatch through this table instead of recursive template metadata.

Field descriptors record key text, owner offset, value kind, and clamp policy. The generated layout-edit metadata separately records value format and the `LayoutEditParameter` entry that uses the field.

`src/config/config_schema.h` contains only shared non-template metadata enums. Config schema fields and section metadata live in generated tables, not macro-expanded field lists or type-level descriptors.

## Layout-Edit Metadata

The layout-edit generator output maps hand-authored `LayoutEditParameter` values to generated config field metadata. It keeps metadata separate from the runtime section table because only layout edit needs parameter ordering, display names, tooltip formats, and hit-target field lookup.

The generator preserves the current active parameter order from `src/widget/layout_edit_parameter_id.h`. New active parameters must be added to the enum in the exact hit-test priority position expected by layout edit, then the descriptor metadata can map them to section and field keys.

Layout edit reads generated metadata through:

- `GetLayoutEditParameterInfo`
- `GetLayoutEditConfigFieldMetadata`
- `FindLayoutEditParameterByConfigField`
- `FindLayoutEditTooltipDescriptor`

## Validation

Use the maintained validation entrypoints after config metadata changes:

```bat
build.cmd
test.cmd
format.cmd
```

Run focused headless diagnostics against the fresh build when parser, writer, color, or layout-edit metadata behavior changes:

```bat
build\CaseDash.exe /default-config /fake /exit /trace:build\config_meta_trace.txt /save-full-config:build\config_meta_full.ini
build\CaseDash.exe /default-config /fake /exit /edit-layout /trace:build\config_meta_edit_trace.txt /screenshot:build\config_meta_edit.png
build\CaseDash.exe /default-config /fake /exit /edit-layout /trace:build\config_meta_guide_trace.txt /layout-guide-sheet:build\config_meta_guide.png
```

Inspect the saved full config for section order, dynamic section names, derived color expressions, and custom `[board]` or `[metrics]` behavior. Inspect layout-edit traces for expected config paths, active-region metadata, and generated field names.
