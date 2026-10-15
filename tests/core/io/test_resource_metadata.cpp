/**************************************************************************/
/*  test_resource_metadata.cpp                                            */
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

TEST_FORCE_LINK(test_resource_metadata)

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_metadata.h"
#include "core/io/resource_saver.h"
#include "tests/test_utils.h"

namespace TestResourceMetadata {

struct Fixture {
	Ref<DirAccess> directory;
	String old_path;
	Variant old_enabled;
	Variant old_root;
	Variant old_patterns;

	Fixture() {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		old_path = TestProjectSettingsInternalsAccessor::resource_path();
		old_enabled = settings->get_setting("filesystem/metadata/enabled");
		old_root = settings->get_setting("filesystem/metadata/root_directory");
		old_patterns = settings->get_setting("filesystem/metadata/exclude_patterns");
		Error err;
		directory = DirAccess::create_temp("godot-metadata-test", false, &err);
		REQUIRE(err == OK);
		TestProjectSettingsInternalsAccessor::resource_path() = directory->get_current_dir();
		settings->set_setting("filesystem/metadata/enabled", true);
		settings->set_setting("filesystem/metadata/root_directory", "res://.metadata");
		settings->set_setting("filesystem/metadata/exclude_patterns", PackedStringArray());
	}

	~Fixture() {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		settings->set_setting("filesystem/metadata/enabled", old_enabled);
		settings->set_setting("filesystem/metadata/root_directory", old_root);
		settings->set_setting("filesystem/metadata/exclude_patterns", old_patterns);
		TestProjectSettingsInternalsAccessor::resource_path() = old_path;
	}

	static void write(const String &p_path, const String &p_text) {
		REQUIRE(DirAccess::make_dir_recursive_absolute(p_path.get_base_dir()) == OK);
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		REQUIRE(file->store_string(p_text));
	}

	static Error save(const String &p_path, const String &p_text) {
		Error err;
		Ref<FileAccess> file = ResourceMetadata::open_file(p_path, ".uid", &err);
		if (file.is_null()) {
			return err;
		}
		REQUIRE(file->store_string(p_text));
		return ResourceMetadata::commit_file(p_path, ".uid", file);
	}

	static void patterns(std::initializer_list<const char *> p_patterns) {
		PackedStringArray patterns;
		for (const char *pattern : p_patterns) {
			patterns.push_back(pattern);
		}
		ProjectSettings::get_singleton()->set_setting("filesystem/metadata/exclude_patterns", patterns);
	}
};

TEST_CASE("[ResourceMetadata] Disabled ignores root and filters and uses original writes") {
	Fixture fixture;
	Fixture::write("res://scripts/test.gd.uid", "legacy\n");
	Fixture::write("res://.metadata/scripts/test.gd.uid", "external\n");
	ProjectSettings::get_singleton()->set_setting("filesystem/metadata/enabled", false);
	CHECK(ResourceMetadata::get_read_path("res://scripts/test.gd", ".uid") == "res://scripts/test.gd.uid");
	CHECK(ResourceMetadata::get_write_path("res://scripts/test.gd", ".uid") == "res://scripts/test.gd.uid");
	CHECK_FALSE(ResourceMetadata::is_metadata_directory("res://.metadata"));
	REQUIRE(Fixture::save("res://scripts/test.gd", "updated\n") == OK);
	CHECK(FileAccess::get_file_as_string("res://scripts/test.gd.uid") == "updated\n");
	CHECK(FileAccess::get_file_as_string("res://.metadata/scripts/test.gd.uid") == "external\n");
	CHECK_FALSE(FileAccess::exists("res://scripts/test.gd.uid.tmp"));
	CHECK(ResourceMetadata::migrate_file("res://scripts/test.gd", ".uid") == OK);
	CHECK(FileAccess::exists("res://scripts/test.gd.uid"));
}

TEST_CASE("[ResourceMetadata] UID fallback migration and preferred identity") {
	Fixture fixture;
	const String path = "res://scripts/test.gd";
	const String uid = ResourceUID::get_singleton()->id_to_text(ResourceUID::get_singleton()->create_id_for_path(path));
	Fixture::write(path + ".uid", uid + "\n");
	CHECK(ResourceMetadata::get_read_path(path, ".uid") == path + ".uid");
	REQUIRE(ResourceMetadata::migrate_file(path, ".uid") == OK);
	CHECK_FALSE(FileAccess::exists(path + ".uid"));
	CHECK(FileAccess::get_file_as_string("res://.metadata/scripts/test.gd.uid") == uid + "\n");
	Ref<ResourceFormatLoader> loader;
	loader.instantiate();
	CHECK(loader->get_resource_uid(path) == ResourceUID::get_singleton()->text_to_id(uid));
	Fixture::write(path + ".uid", "stale\n");
	REQUIRE(ResourceMetadata::migrate_file(path, ".uid") == OK);
	CHECK_FALSE(FileAccess::exists(path + ".uid"));
	CHECK(loader->get_resource_uid(path) == ResourceUID::get_singleton()->text_to_id(uid));
	const ResourceUID::ID new_uid = ResourceUID::get_singleton()->create_id_for_path("res://other.gd");
	REQUIRE(ResourceSaver::set_uid(path, new_uid) == OK);
	CHECK(loader->get_resource_uid(path) == new_uid);
}

TEST_CASE("[ResourceMetadata] Staging supports editor safe-save backends") {
	Fixture fixture;
	Fixture::write("res://test.gd.uid", "identity\n");
	const bool old_backup = FileAccess::is_backup_save_enabled();
	FileAccess::set_backup_save(true);
	const Error err = ResourceMetadata::migrate_file("res://test.gd", ".uid");
	FileAccess::set_backup_save(old_backup);
	REQUIRE(err == OK);
	CHECK(FileAccess::get_file_as_string("res://.metadata/test.gd.uid") == "identity\n");
	CHECK_FALSE(FileAccess::exists("res://test.gd.uid"));
	CHECK_FALSE(FileAccess::exists("res://.metadata/test.gd.uid.tmp"));
}

TEST_CASE("[ResourceMetadata] Relative absolute roots and scanner exclusion") {
	Fixture fixture;
	ProjectSettings *settings = ProjectSettings::get_singleton();
	settings->set_setting("filesystem/metadata/root_directory", "meta data");
	CHECK(ResourceMetadata::get_write_path("res://a/b.gd", ".uid") == "res://meta data/a/b.gd.uid");
	CHECK(ResourceMetadata::is_metadata_directory("res://meta data"));
	CHECK(ResourceMetadata::is_metadata_directory("res://meta data/a"));
	CHECK_FALSE(ResourceMetadata::is_metadata_directory("res://meta data2"));
	settings->set_setting("filesystem/metadata/root_directory", fixture.directory->get_current_dir().path_join("absolute root"));
	CHECK(ResourceMetadata::is_metadata_directory("res://absolute root"));
	REQUIRE(Fixture::save("res://a/b.gd", "uid://test\n") == OK);
	CHECK(FileAccess::exists("res://absolute root/a/b.gd.uid"));
	CHECK(ResourceMetadata::get_write_path("user://a.gd", ".uid") == "user://a.gd.uid");
	settings->set_setting("filesystem/metadata/root_directory", fixture.directory->get_current_dir());
	CHECK_FALSE(ResourceMetadata::is_metadata_directory("res://a"));
	Fixture::write("res://a/b.gd.uid", "alias\n");
	REQUIRE(ResourceMetadata::migrate_file("res://a/b.gd", ".uid") == OK);
	CHECK(FileAccess::exists("res://a/b.gd.uid"));
}

TEST_CASE("[ResourceMetadata] Gitignore style patterns files directories wildcards and negation") {
	Fixture fixture;
	Fixture::patterns({ "# comment", "vendor/", "/root.gd", "*.tmp", "a/**/test?.[gp]d", "*.uid", "!keep.gd.uid", "\\#literal.gd" });
	CHECK(ResourceMetadata::is_excluded("res://nested/vendor/file.png", ".import"));
	CHECK(ResourceMetadata::is_excluded("res://root.gd", ".import"));
	CHECK_FALSE(ResourceMetadata::is_excluded("res://nested/root.gd", ".import"));
	CHECK(ResourceMetadata::is_excluded("res://nested/file.tmp", ".import"));
	CHECK(ResourceMetadata::is_excluded("res://a/test1.gd", ".import"));
	CHECK(ResourceMetadata::is_excluded("res://a/b/c/test2.pd", ".import"));
	CHECK_FALSE(ResourceMetadata::is_excluded("res://a/b/test12.gd", ".import"));
	CHECK_FALSE(ResourceMetadata::is_excluded("res://a/contest1.gd", ".import"));
	CHECK(ResourceMetadata::is_excluded("res://scripts/test.gd", ".uid"));
	CHECK_FALSE(ResourceMetadata::is_excluded("res://scripts/keep.gd", ".uid"));
	CHECK(ResourceMetadata::is_excluded("res://#literal.gd", ".import"));
	Fixture::patterns({ "a/*/file.gd", "a/[!x].gd" });
	CHECK(ResourceMetadata::is_excluded("res://a/b/file.gd", ".uid"));
	CHECK_FALSE(ResourceMetadata::is_excluded("res://a/b/c/file.gd", ".uid"));
	CHECK(ResourceMetadata::is_excluded("res://a/y.gd", ".uid"));
	CHECK_FALSE(ResourceMetadata::is_excluded("res://a/x.gd", ".uid"));
}

TEST_CASE("[ResourceMetadata] Move across filter boundaries preserves identity and removes duplicates") {
	Fixture fixture;
	Fixture::patterns({ "vendor/" });
	Fixture::write("res://vendor/test.gd.uid", "identity\n");
	REQUIRE(ResourceMetadata::move_file("res://vendor/test.gd", "res://scripts/test.gd", ".uid") == OK);
	CHECK_FALSE(FileAccess::exists("res://vendor/test.gd.uid"));
	CHECK(FileAccess::get_file_as_string("res://.metadata/scripts/test.gd.uid") == "identity\n");
	Fixture::write("res://scripts/test.gd.uid", "stale\n");
	REQUIRE(ResourceMetadata::move_file("res://scripts/test.gd", "res://vendor/new.gd", ".uid") == OK);
	CHECK_FALSE(FileAccess::exists("res://scripts/test.gd.uid"));
	CHECK_FALSE(FileAccess::exists("res://.metadata/scripts/test.gd.uid"));
	CHECK(FileAccess::get_file_as_string("res://vendor/new.gd.uid") == "identity\n");
	REQUIRE(ResourceMetadata::remove_file("res://vendor/new.gd", ".uid") == OK);
	CHECK_FALSE(FileAccess::exists("res://vendor/new.gd.uid"));
}

TEST_CASE("[ResourceMetadata] Failed migration and move retain the only valid copy") {
	Fixture fixture;
	Fixture::write("res://scripts/test.gd.uid", "identity\n");
	Fixture::write("res://blocker", "not a directory");
	ProjectSettings::get_singleton()->set_setting("filesystem/metadata/root_directory", "res://blocker/nested");
	ERR_PRINT_OFF;
	CHECK(ResourceMetadata::migrate_file("res://scripts/test.gd", ".uid") != OK);
	CHECK(ResourceMetadata::move_file("res://scripts/test.gd", "res://other.gd", ".uid") != OK);
	ERR_PRINT_ON;
	CHECK(FileAccess::get_file_as_string("res://scripts/test.gd.uid") == "identity\n");
}

} // namespace TestResourceMetadata
