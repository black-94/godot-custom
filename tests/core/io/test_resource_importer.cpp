/**************************************************************************/
/*  test_resource_importer.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_resource_importer)

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/resource_importer.h"
#include "tests/test_utils.h"

namespace TestResourceImporter {

struct SidecarFixture {
	Ref<DirAccess> directory;
	String old_resource_path;
	Variant old_root;

	SidecarFixture() {
		old_resource_path = TestProjectSettingsInternalsAccessor::resource_path();
		old_root = ProjectSettings::get_singleton()->get_setting("filesystem/import/sidecar_root");
		Error err;
		directory = DirAccess::create_temp("godot-sidecar-test", false, &err);
		REQUIRE(err == OK);
		TestProjectSettingsInternalsAccessor::resource_path() = directory->get_current_dir();
		set_root("");
	}

	~SidecarFixture() {
		set_root(old_root);
		TestProjectSettingsInternalsAccessor::resource_path() = old_resource_path;
	}

	void set_root(const String &p_root) {
		ProjectSettings::get_singleton()->set_setting("filesystem/import/sidecar_root", p_root);
	}

	static Ref<ConfigFile> config(const String &p_type) {
		Ref<ConfigFile> result;
		result.instantiate();
		result->set_value("remap", "importer", "texture");
		result->set_value("remap", "type", p_type);
		result->set_value("remap", "path", "res://.godot/imported/test.ctex");
		result->set_value("params", "test_option", 42);
		return result;
	}

	static void save_legacy(const String &p_path, const String &p_type = "Texture2D") {
		REQUIRE(DirAccess::make_dir_recursive_absolute(p_path.get_base_dir()) == OK);
		REQUIRE(config(p_type)->save(p_path + ".import") == OK);
	}
};

TEST_CASE("[ResourceImporter] Sidecar default and root path mapping") {
	SidecarFixture fixture;
	CHECK(ResourceFormatImporter::get_import_write_path("res://textures/icon.png") == "res://textures/icon.png.import");
	fixture.set_root("res://.metadata");
	CHECK(ResourceFormatImporter::get_import_write_path("res://textures/icon.png") == "res://.metadata/textures/icon.png.import");
	fixture.set_root(".metadata");
	CHECK(ResourceFormatImporter::get_import_write_path("res://textures/icon.png") == "res://.metadata/textures/icon.png.import");
	fixture.set_root(fixture.directory->get_current_dir().path_join("external"));
	CHECK(ResourceFormatImporter::get_import_write_path("res://textures/icon.png") == fixture.directory->get_current_dir().path_join("external/textures/icon.png.import"));
	CHECK(ResourceFormatImporter::get_import_write_path("user://icon.png") == "user://icon.png.import");
}

TEST_CASE("[ResourceImporter] Sidecar root takes priority with legacy fallback") {
	SidecarFixture fixture;
	SidecarFixture::save_legacy("res://textures/icon.png");
	fixture.set_root("res://.metadata");
	CHECK(ResourceFormatImporter::get_import_path("res://textures/icon.png") == "res://textures/icon.png.import");
	REQUIRE(DirAccess::make_dir_recursive_absolute("res://.metadata/textures") == OK);
	REQUIRE(SidecarFixture::config("ImageTexture")->save("res://.metadata/textures/icon.png.import") == OK);
	CHECK(ResourceFormatImporter::get_import_path("res://textures/icon.png") == "res://.metadata/textures/icon.png.import");
	REQUIRE(ResourceFormatImporter::get_singleton() != nullptr);
	CHECK(ResourceFormatImporter::get_singleton()->recognize_path("res://textures/icon.png"));
	CHECK(ResourceFormatImporter::get_singleton()->get_resource_type("res://textures/icon.png") == "ImageTexture");
	CHECK(ResourceFormatImporter::get_singleton()->get_internal_resource_path("res://textures/icon.png") == "res://.godot/imported/test.ctex");
}

TEST_CASE("[ResourceImporter] Sidecar writes migrate legacy metadata and preserve options") {
	SidecarFixture fixture;
	SidecarFixture::save_legacy("res://textures/icon.png");
	fixture.set_root("res://.metadata");
	Ref<ConfigFile> config;
	config.instantiate();
	REQUIRE(config->load(ResourceFormatImporter::get_import_path("res://textures/icon.png")) == OK);
	REQUIRE(ResourceFormatImporter::save_import_file("res://textures/icon.png", config) == OK);
	CHECK_FALSE(FileAccess::exists("res://textures/icon.png.import"));
	CHECK_FALSE(FileAccess::exists("res://.metadata/textures/icon.png.import.tmp"));
	REQUIRE(config->load("res://.metadata/textures/icon.png.import") == OK);
	CHECK(int(config->get_value("params", "test_option")) == 42);
	// Updating a sidecar must replace the previous file too.
	config->set_value("params", "test_option", 43);
	REQUIRE(ResourceFormatImporter::save_import_file("res://textures/icon.png", config) == OK);
	REQUIRE(config->load("res://.metadata/textures/icon.png.import") == OK);
	CHECK(int(config->get_value("params", "test_option")) == 43);
}

TEST_CASE("[ResourceImporter] Sidecar staging does not remove legacy before commit") {
	SidecarFixture fixture;
	SidecarFixture::save_legacy("res://icon.png");
	fixture.set_root("res://.metadata");
	Ref<FileAccess> file = ResourceFormatImporter::open_import_file("res://icon.png");
	REQUIRE(file.is_valid());
	REQUIRE(file->store_string(SidecarFixture::config("Texture2D")->encode_to_text()));
	CHECK(FileAccess::exists("res://icon.png.import"));
	CHECK_FALSE(FileAccess::exists("res://.metadata/icon.png.import"));
	REQUIRE(ResourceFormatImporter::commit_import_file("res://icon.png", file) == OK);
	CHECK_FALSE(FileAccess::exists("res://icon.png.import"));
}

TEST_CASE("[ResourceImporter] Sidecar failed write preserves legacy metadata") {
	SidecarFixture fixture;
	SidecarFixture::save_legacy("res://icon.png");
	const String original = FileAccess::get_md5("res://icon.png.import");
	Ref<FileAccess> blocker = FileAccess::open("res://blocker", FileAccess::WRITE);
	REQUIRE(blocker.is_valid());
	blocker->close();
	fixture.set_root("res://blocker/nested");
	ERR_PRINT_OFF;
	const Error err = ResourceFormatImporter::save_import_file("res://icon.png", SidecarFixture::config("ImageTexture"));
	ERR_PRINT_ON;
	CHECK(err != OK);
	CHECK(FileAccess::get_md5("res://icon.png.import") == original);
}

TEST_CASE("[ResourceImporter] Sidecar move and removal handle both locations") {
	SidecarFixture fixture;
	SidecarFixture::save_legacy("res://textures/icon.png");
	fixture.set_root("res://.metadata");
	REQUIRE(ResourceFormatImporter::save_import_file("res://textures/icon.png", SidecarFixture::config("ImageTexture")) == OK);
	// A stale adjacent duplicate must be removed along with the preferred copy.
	SidecarFixture::save_legacy("res://textures/icon.png");
	REQUIRE(ResourceFormatImporter::move_import_file("res://textures/icon.png", "res://new/subdir/renamed.png") == OK);
	CHECK_FALSE(FileAccess::exists("res://textures/icon.png.import"));
	CHECK_FALSE(FileAccess::exists("res://.metadata/textures/icon.png.import"));
	CHECK(FileAccess::exists("res://.metadata/new/subdir/renamed.png.import"));
	REQUIRE(ResourceFormatImporter::move_import_file("res://new/subdir/renamed.png", "res://new/subdir/renamed.png") == OK);
	SidecarFixture::save_legacy("res://new/subdir/renamed.png");
	REQUIRE(ResourceFormatImporter::remove_import_file("res://new/subdir/renamed.png") == OK);
	CHECK_FALSE(FileAccess::exists("res://new/subdir/renamed.png.import"));
	CHECK_FALSE(FileAccess::exists("res://.metadata/new/subdir/renamed.png.import"));
}

TEST_CASE("[ResourceImporter] Sidecar directory move migrates already moved legacy file") {
	SidecarFixture fixture;
	SidecarFixture::save_legacy("res://old/icon.png");
	REQUIRE(DirAccess::rename_absolute("res://old", "res://new") == OK);
	fixture.set_root("res://.metadata");
	REQUIRE(ResourceFormatImporter::move_import_file("res://old/icon.png", "res://new/icon.png") == OK);
	CHECK(FileAccess::exists("res://.metadata/new/icon.png.import"));
	CHECK_FALSE(FileAccess::exists("res://new/icon.png.import"));
}

TEST_CASE("[ResourceImporter] Sidecar default write retains adjacent metadata") {
	SidecarFixture fixture;
	REQUIRE(ResourceFormatImporter::save_import_file("res://subdir/icon.png", SidecarFixture::config("Texture2D")) == OK);
	CHECK(FileAccess::exists("res://subdir/icon.png.import"));
	CHECK_FALSE(FileAccess::exists("res://subdir/icon.png.import.tmp"));
	REQUIRE(ResourceFormatImporter::move_import_file("res://subdir/icon.png", "res://new/icon.png") == OK);
	CHECK(FileAccess::exists("res://new/icon.png.import"));
	CHECK_FALSE(FileAccess::exists("res://subdir/icon.png.import"));
}

TEST_CASE("[ResourceImporter] Sidecar failed commit preserves legacy metadata") {
	SidecarFixture fixture;
	SidecarFixture::save_legacy("res://icon.png");
	const String original = FileAccess::get_md5("res://icon.png.import");
	fixture.set_root("res://.metadata");
	Ref<FileAccess> file = ResourceFormatImporter::open_import_file("res://icon.png");
	REQUIRE(file.is_valid());
	REQUIRE(file->store_string(SidecarFixture::config("ImageTexture")->encode_to_text()));
	REQUIRE(DirAccess::make_dir_absolute("res://.metadata/icon.png.import") == OK);
	ERR_PRINT_OFF;
	const Error err = ResourceFormatImporter::commit_import_file("res://icon.png", file);
	ERR_PRINT_ON;
	CHECK(err != OK);
	CHECK(FileAccess::get_md5("res://icon.png.import") == original);
}

TEST_CASE("[ResourceImporter] Sidecar absolute root and resource-root alias") {
	SidecarFixture fixture;
	fixture.set_root(fixture.directory->get_current_dir().path_join("external"));
	REQUIRE(ResourceFormatImporter::save_import_file("res://textures/icon.png", SidecarFixture::config("Texture2D")) == OK);
	CHECK(FileAccess::exists("res://external/textures/icon.png.import"));
	CHECK_FALSE(FileAccess::exists("res://textures/icon.png.import"));
	fixture.set_root(fixture.directory->get_current_dir());
	REQUIRE(ResourceFormatImporter::save_import_file("res://textures/icon.png", SidecarFixture::config("Texture2D")) == OK);
	CHECK(FileAccess::exists("res://textures/icon.png.import"));
}

TEST_CASE("[ResourceImporter] Sidecar UID updates migrate metadata") {
	SidecarFixture fixture;
	SidecarFixture::save_legacy("res://icon.png");
	fixture.set_root("res://.metadata");
	Ref<ResourceFormatImporterSaver> saver;
	saver.instantiate();
	const ResourceUID::ID uid = ResourceUID::get_singleton()->create_id_for_path("res://icon.png");
	REQUIRE(saver->set_uid("res://icon.png", uid) == OK);
	CHECK_FALSE(FileAccess::exists("res://icon.png.import"));
	CHECK(ResourceFormatImporter::get_singleton()->get_resource_uid("res://icon.png") == uid);
}

} // namespace TestResourceImporter
