# Centralized resource metadata

This opt-in feature redirects resource sidecars (`.uid` and `.import`) without
moving source resources, embedded resource UIDs, imported binaries, or `.godot`
caches. The default is disabled and uses the original adjacent read/write paths.

## Configuration

In Project Settings, enable **Filesystem > Metadata > Enabled**, and set
**Root Directory**. Restart the editor after changing metadata settings.

```ini
[filesystem]
metadata/enabled=true
metadata/root_directory="res://.metadata"
metadata/exclude_patterns=PackedStringArray("vendor/", "/icon.svg", "*.uid", "!scripts/**")
```

The root also accepts ordinary project-relative paths and absolute filesystem
paths, including paths with spaces outside the project. An empty root keeps
sidecars adjacent. `user://` is not a metadata root.

For example, `/shared/project-metadata` produces:

```text
res://characters/player.gd       -> /shared/project-metadata/characters/player.gd.uid
res://textures/player.png        -> /shared/project-metadata/textures/player.png.import
```

Use a dedicated root, preferably outside source directories you intend to move.
Roots inside the project are excluded from editor resource scanning and export.
Do not put unrelated source resources in that directory. Add the root to your VCS
ignore rules if these sidecars should not be committed.

## Migration and lifecycle

- Reads prefer the configured root, then fall back to adjacent metadata.
- Opening the editor migrates existing sidecars, including unchanged resources.
  Import options and UID contents are retained, rather than regenerated.
- Enabled writes stage the entire file, flush and validate it, then rename it
  into place. Only then is an old adjacent copy removed. Editor safe-save
  backends are supported. Errors are reported and the previous copy is retained.
- Copying resources assigns fresh identities; moving or renaming them retains
  identities. Directory operations mirror each resource's relative sidecar path.
  Moving across filter boundaries moves sidecars between centralized and adjacent
  locations. Deletion removes metadata from both searched locations.
- Export reads centralized metadata but writes standard package paths, and clears
  metadata settings and their feature overrides. Exported projects require no
  access to the development metadata directory.

Before changing roots, disabling the feature, or adding exclusions for resources
already migrated, move the affected metadata to the new root or back beside its
sources. Previous roots are not searched or silently deleted. Use separate roots
for projects with overlapping relative resource paths.

## Exclusion patterns

Rules match project-relative source **or sidecar** paths, case-sensitively. Entries
may contain multiple newline-separated rules. A matching rule keeps metadata
adjacent; a later `!` rule re-includes it. This is gitignore-style, not a Git index:
unlike Git, re-including an ignored parent first is not required.

- `vendor/`: any directory named `vendor` and its descendants.
- `/assets/raw/`: only this project-root-relative directory.
- `icon.png`: files or directories of that name at any depth.
- `/icon.png`: only the project-root file.
- `*.uid`: UID sidecars only.
- `assets/**/test?.[gp]d`: recursive directories, single-character and class globs.
- `!scripts/**`: re-include sidecars below `scripts`.
- `# comment`: comments; `\#literal` and `\!literal` match literal leading signs.

Ordinary `*` and `?` do not cross directory separators. Exclusions only control
metadata placement; the resources themselves still import and export normally.

## Editor artifacts

Download editor binaries from the tag's GitHub Actions run, under **Artifacts**:

- `linux-editor`: standard Linux x86_64 editor; no .NET SDK/runtime required.
- `linux-editor-mono`: Linux editor with C# support; requires a compatible .NET
  SDK (8.0 or later), even for projects that only use GDScript. Keep the bundled
  `GodotSharp` directory beside the executable. This build has no startup option
  to disable its .NET module.
- `macos-editor`: standard universal editor for Intel and Apple Silicon.

`*-template*` artifacts are game export runtimes, not project editors. For Linux
GDScript-only projects, choose `linux-editor`, not a Mono or minimal template.

## Verification

An editor build with `tests=yes` runs the C++ regression cases:

```sh
godot --headless --test --test-case='[ResourceImporter]*,[ResourceMetadata]*'
python3 tests/integration/test_centralized_metadata.py /path/to/godot
```

The integration test creates disposable projects and drives the existing editor
UI signal callbacks for file/directory duplicate, rename, move, and delete. It
also covers disabled mode, unchanged-file migration, conflicting legacy files,
filters, relative and absolute roots, reimport, and loading an exported PCK after
the metadata directory has become unavailable. No display or export templates
are required. Both standard and Mono Linux export-test CI jobs run this regression
automatically. The standard editor additionally uses `--require-no-dotnet` to
assert that C# classes and the GodotSharp singleton are absent, so a runner's
preinstalled .NET SDK cannot conceal an accidentally published Mono build.

For the requested remote Docker environment, mount the source working directory
at `/home/master/workspace` in `agent-tool:latest`. A headless test build can use:

```sh
python3 -m SCons platform=linuxbsd target=editor tests=yes dev_build=yes \
    debug_symbols=no optimize=none x11=no wayland=no vulkan=no opengl3=no \
    dbus=no speechd=no fontconfig=yes use_sowrap=yes -j6
```

Set writable `XDG_CACHE_HOME`, `XDG_CONFIG_HOME`, and `XDG_DATA_HOME` directories
inside the workspace if the container's defaults are not writable.

The Linux `godot-cpp` CI build retains the response-file archive wrapper from
`feature/import-sidecar-root` to avoid Linux's per-argument command-length limit.
