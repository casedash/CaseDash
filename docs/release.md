# Release Workflow

This document owns CaseDash versioning and release publication.
See also: [docs/build.md](build.md) for local build setup and [docs/project.md](project.md) for repository policy.

## Version Source

- [VERSION](../VERSION) is the base product version.
- Version text uses `major.minor` or `major.minor.patch`.
- CMake reads `VERSION` and Git metadata during configure, then generates build metadata under `build\cmake\generated`.
- The generated metadata is compiled into the executable as C++ constants, a Windows `VERSIONINFO` resource, and the application manifest assembly version.

## Build Kinds

- An official release build comes from a clean commit whose exact Git tag is `v<VERSION>`.
- Official release builds report the plain version from `VERSION`.
- Other builds are development builds and report `VERSION-dev+g<commit>`.
- Locally modified builds append `.dirty`.
- Development builds set the Windows version resource prerelease flag.

## User-Visible Version

- Windows file properties show the numeric file version plus the full CaseDash version string.
- The application manifest uses the four-part Windows numeric version derived from `VERSION`.
- The dashboard menu exposes `About CaseDash` immediately before `Exit`.
- The About dialog shows the full version, release kind, and commit when available.

## Official Release Steps

Use the repository release entrypoint:

```bat
release.cmd <version>
```

`release.cmd` asks for keyboard confirmation, updates [VERSION](../VERSION), commits the version change, runs format, lint, build, and tests, creates the matching annotated `v<VERSION>` tag, pushes the current branch, and pushes the tag.

The `Release` GitHub Actions workflow checks that the tag matches `VERSION`, builds and tests CaseDash, packages the executable, writes SHA-256 checksums, and creates the GitHub Release.

The workflow can also be started manually with a tag input, but the tag must still match `VERSION`.
