#!/usr/bin/env python3
"""Headless centralized metadata regression test; requires an editor binary.

Usage: python3 tests/integration/test_centralized_metadata.py /path/to/godot
No export templates or display server are required.
"""

import argparse
import json
import pathlib
import re
import struct
import subprocess
import tempfile
import time
import zlib


def png(width):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, 1, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(b"\0" + b"\xff\0\0\xff" * width))
        + chunk(b"IEND", b"")
    )


def run(binary, project, *arguments):
    print(f"Running: {' '.join(arguments)}", flush=True)
    try:
        result = subprocess.run(
            [str(binary), "--headless", "--path", str(project), *arguments],
            capture_output=True,
            text=True,
            timeout=240,
        )
    except subprocess.TimeoutExpired as error:
        raise AssertionError(f"Command timed out: {arguments}\n{error.stdout!r}\n{error.stderr!r}") from error
    if result.returncode or "ERROR:" in result.stderr:
        raise AssertionError(f"Command failed: {arguments}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def configure(project, root, enabled=True, patterns=()):
    (project / "project.godot").write_text(
        'config_version=5\n[application]\nconfig/name="Centralized metadata test"\n'
        f"[filesystem]\nmetadata/enabled={str(enabled).lower()}\n"
        f"metadata/root_directory={json.dumps(str(root))}\n"
        f"metadata/exclude_patterns=PackedStringArray({', '.join(json.dumps(p) for p in patterns)})\n"
        '[rendering]\nrenderer/rendering_method="gl_compatibility"\n',
        encoding="utf-8",
    )


def uid(sidecar):
    return re.search(r'^uid="([^"]+)"', sidecar.read_text(), re.MULTILINE).group(1)


def check_load(binary, project, width):
    (project / "verify.gd").write_text(
        "extends SceneTree\nfunc _initialize():\n"
        '\tvar texture = load("res://textures/icon.png")\n'
        f"\tif not texture is Texture2D or texture.get_width() != {width}:\n\t\tquit(1)\n\t\treturn\n"
        '\tprint("METADATA_LOAD_OK")\n\tquit()\n',
        encoding="utf-8",
    )
    assert "METADATA_LOAD_OK" in run(binary, project, "--script", "res://verify.gd")


def check_editor(binary, project):
    addon = project / "addons/metadata_test"
    addon.mkdir(parents=True, exist_ok=True)
    (addon / "plugin.cfg").write_text(
        '[plugin]\nname="Metadata integration test"\ndescription=""\nauthor="Godot"\nversion="1"\nscript="plugin.gd"\n',
        encoding="utf-8",
    )
    (addon / "plugin.gd").write_text(
        pathlib.Path(__file__).with_name("centralized_metadata_editor.gd").read_text(encoding="utf-8"),
        encoding="utf-8",
    )
    (project / "editor_test_done").unlink(missing_ok=True)
    with (project / "project.godot").open("a", encoding="utf-8") as config:
        config.write('\n[editor_plugins]\nenabled=PackedStringArray("res://addons/metadata_test/plugin.cfg")\n')
    output = run(binary, project, "--editor")
    assert "METADATA_EDITOR_OK" in output
    assert (project / "editor_test_done").read_text() == "passed"


def check_export(binary, project, metadata, workspace, script_uid, texture_uid):
    # Clear base settings AND feature overrides; the pack must use canonical remaps.
    with (project / "project.godot").open("a", encoding="utf-8") as config:
        config.write(
            f"\n[filesystem]\nmetadata/root_directory.linux={json.dumps(str(metadata))}\nmetadata/enabled.linux=true\n"
        )
    (project / "export_presets.cfg").write_text(
        '[preset.0]\nname="Linux"\nplatform="Linux"\nrunnable=true\n'
        'export_filter="all_resources"\ninclude_filter="*.uid"\nexclude_filter=""\n'
        '[preset.0.options]\nbinary_format/architecture="x86_64"\n',
        encoding="utf-8",
    )
    (project / "verify.gd").write_text(
        "extends SceneTree\nfunc _initialize():\n"
        f'\tvar texture = load("{texture_uid}")\n'
        '\tif not texture is Texture2D or texture.get_width() != 2 or ProjectSettings.get_setting_with_override("filesystem/metadata/enabled"):\n'
        "\t\tquit(1)\n\t\treturn\n"
        '\tif ProjectSettings.get_setting_with_override("filesystem/metadata/root_directory") != "":\n'
        "\t\tquit(2)\n\t\treturn\n"
        f'\tif not load("{script_uid.strip()}") is Script:\n\t\tquit(3)\n\t\treturn\n'
        '\tif FileAccess.file_exists("res://metadata visible/scripts/player.gd.uid"):\n\t\tquit(4)\n\t\treturn\n'
        '\tprint("METADATA_PACK_OK")\n\tquit()\n',
        encoding="utf-8",
    )
    pack = workspace / "test.pck"
    run(binary, project, "--editor", "--export-pack", "Linux", str(pack))
    assert pack.exists()
    hidden = workspace / "hidden metadata"
    metadata.rename(hidden)
    try:
        assert "METADATA_PACK_OK" in run(binary, workspace, "--main-pack", str(pack), "--script", "res://verify.gd")
    finally:
        hidden.rename(metadata)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument(
        "--require-no-dotnet", action="store_true", help="Also verify the editor is built without the .NET module."
    )
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()
    with tempfile.TemporaryDirectory(prefix="godot-metadata-integration-") as temporary:
        workspace = pathlib.Path(temporary)
        project = workspace / "project"
        (project / "textures").mkdir(parents=True)
        (project / "scripts").mkdir()
        (project / "vendor").mkdir()
        source = project / "textures/icon.png"
        legacy = project / "textures/icon.png.import"
        script = project / "scripts/player.gd"
        script.write_text("extends RefCounted\n", encoding="utf-8")
        (project / "scripts/basic.gdshader").write_text("shader_type canvas_item;\n", encoding="utf-8")
        source.write_bytes(png(1))

        # A configured root alone must not change any original behavior.
        configure(project, "res://metadata visible", enabled=False)
        if arguments.require_no_dotnet:
            probe = project / "verify_no_dotnet.gd"
            probe.write_text(
                "extends SceneTree\nfunc _initialize():\n"
                '\tif ClassDB.class_exists("CSharpScript") or Engine.has_singleton("GodotSharp"):\n'
                '\t\tpush_error("Expected a standalone editor without the .NET module")\n'
                "\t\tquit(1)\n\t\treturn\n"
                '\tprint("NO_DOTNET_MODULE_OK")\n\tquit()\n',
                encoding="utf-8",
            )
            assert "NO_DOTNET_MODULE_OK" in run(binary, project, "--script", "res://verify_no_dotnet.gd")
            probe.unlink()
        run(binary, project, "--editor", "--import")
        assert legacy.exists() and (project / "scripts/player.gd.uid").exists()
        assert (project / "scripts/basic.gdshader.uid").exists()
        assert not (project / "metadata visible").exists()
        original_uid = uid(legacy)
        script_uid = (project / "scripts/player.gd.uid").read_text()
        shader_uid = (project / "scripts/basic.gdshader.uid").read_text()
        legacy.write_text(legacy.read_text().replace("mipmaps/generate=false", "mipmaps/generate=true"))
        check_editor(binary, project)

        # Migration must work on unchanged sources, not only after a reimport.
        configure(project, "res://metadata visible", patterns=("vendor/",))
        run(binary, project, "--editor", "--import")
        metadata = project / "metadata visible"
        sidecar = metadata / "textures/icon.png.import"
        assert sidecar.exists() and not legacy.exists()
        assert uid(sidecar) == original_uid and "mipmaps/generate=true" in sidecar.read_text()
        assert (metadata / "scripts/player.gd.uid").read_text() == script_uid
        assert (metadata / "scripts/basic.gdshader.uid").read_text() == shader_uid
        assert not (project / "scripts/player.gd.uid").exists()
        assert not (metadata / "metadata visible").exists(), "The root must not be recursively scanned"
        check_load(binary, project, 1)

        # Conflicting legacy copies cannot override preferred metadata.
        legacy.write_text('[remap]\nimporter="skip"\n')
        (project / "scripts/player.gd.uid").write_text("uid://stale\n")
        time.sleep(1.1)
        source.write_bytes(png(2))
        run(binary, project, "--editor", "--import")
        assert not legacy.exists() and not (project / "scripts/player.gd.uid").exists()
        assert uid(sidecar) == original_uid
        assert (metadata / "scripts/player.gd.uid").read_text() == script_uid
        check_load(binary, project, 2)
        check_editor(binary, project)
        check_export(binary, project, metadata, workspace, script_uid, original_uid)

        # Absolute external root, spaces, relative layout, new resources and filters.
        external = workspace / "external metadata"
        metadata.rename(external)
        configure(
            project,
            external,
            patterns=("vendor/", "/textures/excluded.png", "*.gdshader.uid", "!scripts/basic.gdshader.uid"),
        )
        (project / "vendor/skip.gd").write_text("extends RefCounted\n")
        (project / "textures/excluded.png").write_bytes(png(1))
        (project / "scripts/new.gd").write_text("extends RefCounted\n")
        run(binary, project, "--editor", "--import")
        assert (project / "vendor/skip.gd.uid").exists()
        assert not (external / "vendor/skip.gd.uid").exists()
        assert (project / "textures/excluded.png.import").exists()
        assert (external / "scripts/new.gd.uid").exists()
        assert (external / "scripts/basic.gdshader.uid").read_text() == shader_uid
        assert (external / "scripts/player.gd.uid").read_text() == script_uid
        check_load(binary, project, 2)
        check_editor(binary, project)

        check_export(binary, project, external, workspace, script_uid, original_uid)

        # Disabling ignores even a conflicting external UID; legacy writes stay legacy.
        configure(project, external, enabled=False)
        (project / "scripts/player.gd.uid").write_text(script_uid)
        run(binary, project, "--editor", "--import")
        assert (project / "scripts/player.gd.uid").read_text() == script_uid
        assert legacy.exists()
    print(
        "PASS: disabled compatibility, UID/import migration, precedence, reimport, filters, editor create/copy/rename/move/delete, absolute root, export, packed load"
    )


if __name__ == "__main__":
    main()
