#!/usr/bin/env python3
"""Headless sidecar-root import/reimport/export regression test.

Usage: python3 tests/integration/test_import_sidecar_root.py /path/to/godot
Requires an editor binary; export templates are not needed for --export-pack.
"""

import json
import pathlib
import re
import struct
import subprocess
import sys
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
    result = subprocess.run(
        [str(binary), "--headless", "--path", str(project), *arguments],
        capture_output=True,
        text=True,
        timeout=180,
    )
    if result.returncode or "SCRIPT ERROR:" in result.stderr:
        raise AssertionError(f"Command failed: {arguments}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def configure(project, root):
    (project / "project.godot").write_text(
        'config_version=5\n[application]\nconfig/name="Sidecar root test"\n'
        f"[filesystem]\nimport/sidecar_root={json.dumps(str(root))}\n"
        '[rendering]\nrenderer/rendering_method="gl_compatibility"\n',
        encoding="utf-8",
    )


def uid(sidecar):
    return re.search(r'^uid="([^"]+)"', sidecar.read_text(), re.MULTILINE).group(1)


def check_load(binary, project, width, pack=None):
    script = project / "verify.gd"
    script.write_text(
        "extends SceneTree\n"
        "func _initialize():\n"
        '\tvar texture = load("res://textures/icon.png")\n'
        "\tif not texture is Texture2D:\n\t\tquit(1)\n\t\treturn\n"
        f"\tif texture.get_width() != {width}:\n\t\tquit(2)\n\t\treturn\n"
        + (
            '\tif ProjectSettings.get_setting_with_override("filesystem/import/sidecar_root") != "":\n'
            "\t\tquit(3)\n\t\treturn\n"
            if pack
            else ""
        )
        + '\tprint("SIDECAR_LOAD_OK")\n\tquit()\n',
        encoding="utf-8",
    )
    # For a pack, verify.gd must have been created and imported before export.
    arguments = ["--script", "res://verify.gd"]
    if pack:
        arguments = ["--main-pack", str(pack), *arguments]
    assert "SIDECAR_LOAD_OK" in run(binary, project, *arguments)


def main():
    binary = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="godot-sidecar-integration-") as temporary:
        workspace = pathlib.Path(temporary)
        project = workspace / "project"
        textures = project / "textures"
        textures.mkdir(parents=True)
        source = textures / "icon.png"
        legacy = textures / "icon.png.import"
        source.write_bytes(png(1))
        configure(project, "")
        run(binary, project, "--editor", "--import")
        assert legacy.exists(), "Default imports must remain adjacent"
        original_uid = uid(legacy)
        legacy.write_text(legacy.read_text().replace("mipmaps/generate=false", "mipmaps/generate=true"))

        configure(project, "res://.metadata")
        time.sleep(1.1)
        source.write_bytes(png(2))
        run(binary, project, "--editor", "--import")
        sidecar = project / ".metadata/textures/icon.png.import"
        assert sidecar.exists() and not legacy.exists(), "Reimport must migrate the legacy sidecar"
        assert uid(sidecar) == original_uid, "Migration must retain resource identity"
        assert "mipmaps/generate=true" in sidecar.read_text(), "Migration must retain import options"
        check_load(binary, project, 2)

        # A conflicting legacy sidecar must not override the preferred root.
        legacy.write_text('[remap]\nimporter="skip"\n')
        time.sleep(1.1)
        source.write_bytes(png(3))
        run(binary, project, "--editor", "--import")
        assert not legacy.exists()
        assert uid(sidecar) == original_uid
        check_load(binary, project, 3)

        # Move metadata to a filesystem root outside the project, including spaces.
        external = workspace / "external metadata"
        (external / "textures").mkdir(parents=True)
        sidecar.rename(external / "textures/icon.png.import")
        configure(project, external)
        time.sleep(1.1)
        source.write_bytes(png(4))
        run(binary, project, "--editor", "--import")
        assert (external / "textures/icon.png.import").exists() and not legacy.exists()
        assert uid(external / "textures/icon.png.import") == original_uid
        check_load(binary, project, 4)

        # Clear feature overrides as well as the base root in exported projects.
        with (project / "project.godot").open("a", encoding="utf-8") as config:
            config.write(f"\n[filesystem]\nimport/sidecar_root.linux={json.dumps(str(external))}\n")
        (project / "export_presets.cfg").write_text(
            '[preset.0]\nname="Linux"\nplatform="Linux"\nrunnable=true\n'
            'export_filter="all_resources"\ninclude_filter=""\nexclude_filter=""\n'
            '[preset.0.options]\nbinary_format/architecture="x86_64"\n',
            encoding="utf-8",
        )
        # Prepare the runtime assertion in the script that goes into the pack.
        script = project / "verify.gd"
        script.write_text(
            "extends SceneTree\nfunc _initialize():\n"
            '\tvar texture = load("res://textures/icon.png")\n'
            '\tif not texture is Texture2D or texture.get_width() != 4 or ProjectSettings.get_setting_with_override("filesystem/import/sidecar_root") != "":\n'
            "\t\tquit(1)\n\t\treturn\n"
            '\tprint("SIDECAR_LOAD_OK")\n\tquit()\n',
            encoding="utf-8",
        )
        pack = workspace / "test.pck"
        run(binary, project, "--editor", "--export-pack", "Linux", str(pack))
        assert pack.exists()
        # Make the development metadata unavailable: the pack must be self-contained.
        external.rename(workspace / "hidden metadata")
        assert "SIDECAR_LOAD_OK" in run(binary, workspace, "--main-pack", str(pack), "--script", "res://verify.gd")
    print("PASS: default import, migration, root precedence, reimport, absolute root, export and packed load")


if __name__ == "__main__":
    main()
