/**************************************************************************/
/*  resource_metadata.cpp                                                 */
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

#include "resource_metadata.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"

namespace {

// Match a gitignore-style glob without letting ordinary '*' or '?' cross '/'.
// Dynamic programming bounds the work even for patterns with many wildcards.
bool match_glob(const String &p_pattern, const String &p_path) {
	const int n = p_pattern.length();
	const int m = p_path.length();
	Vector<uint8_t> table;
	table.resize((n + 1) * (m + 1));
	table.fill(0);
	uint8_t *t = table.ptrw();
	t[n * (m + 1) + m] = 1;
	for (int i = n - 1; i >= 0; i--) {
		bool directory_tail = false;
		for (int j = m; j >= 0; j--) {
			bool match = false;
			const char32_t c = p_pattern[i];
			if (c == '*') {
				const bool recursive = i + 1 < n && p_pattern[i + 1] == '*';
				int next = i + 1;
				while (next < n && p_pattern[next] == '*') {
					next++;
				}
				match = t[next * (m + 1) + j];
				if (recursive && next < n && p_pattern[next] == '/') {
					// '**/' consumes whole directories, never a partial name.
					if (j < m && p_path[j] == '/') {
						directory_tail = directory_tail || t[i * (m + 1) + j + 1];
					}
					match = t[(next + 1) * (m + 1) + j] || directory_tail;
				} else if (j < m && (recursive || p_path[j] != '/')) {
					match = match || t[i * (m + 1) + j + 1];
				}
			} else if (j < m) {
				if (c == '?' && p_path[j] != '/') {
					match = t[(i + 1) * (m + 1) + j + 1];
				} else if (c == '\\' && i + 1 < n) {
					match = p_pattern[i + 1] == p_path[j] && t[(i + 2) * (m + 1) + j + 1];
				} else if (c == '[' && p_path[j] != '/') {
					int k = i + 1;
					const bool negate = k < n && (p_pattern[k] == '!' || p_pattern[k] == '^');
					k += negate ? 1 : 0;
					bool found = false;
					for (; k < n && p_pattern[k] != ']'; k++) {
						if (k + 2 < n && p_pattern[k + 1] == '-' && p_pattern[k + 2] != ']') {
							found = found || (p_path[j] >= p_pattern[k] && p_path[j] <= p_pattern[k + 2]);
							k += 2;
						} else {
							found = found || p_pattern[k] == p_path[j];
						}
					}
					match = k < n && (found != negate) && t[(k + 1) * (m + 1) + j + 1];
				} else {
					match = c == p_path[j] && t[(i + 1) * (m + 1) + j + 1];
				}
			}
			t[i * (m + 1) + j] = match;
		}
	}
	return t[0];
}

bool match_rule(String p_pattern, const String &p_path) {
	const bool directory = p_pattern.ends_with("/");
	p_pattern = p_pattern.trim_suffix("/");
	const bool anchored = p_pattern.begins_with("/") || p_pattern.contains_char('/');
	p_pattern = p_pattern.trim_prefix("/");
	for (int end = 0; end <= p_path.length(); end++) {
		if (end < p_path.length() && p_path[end] != '/') {
			continue;
		}
		if (directory && end == p_path.length()) {
			continue;
		}
		const String candidate = p_path.substr(0, end);
		if (match_glob(p_pattern, anchored ? candidate : candidate.get_file())) {
			return true;
		}
	}
	return false;
}

String metadata_root() {
	const String root = GLOBAL_GET("filesystem/metadata/root_directory");
	if (root.is_empty() || root.begins_with("user://")) {
		return String();
	}
	return (root.is_relative_path() ? String("res://").path_join(root) : root).simplify_path();
}

bool equivalent(const String &p_a, const String &p_b) {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const String a = settings->globalize_path(p_a);
	const String b = settings->globalize_path(p_b);
	return a == b || DirAccess::create(DirAccess::ACCESS_FILESYSTEM)->is_equivalent(a, b);
}

} // namespace

bool ResourceMetadata::is_enabled() {
	return ProjectSettings::get_singleton() && bool(GLOBAL_GET("filesystem/metadata/enabled"));
}

bool ResourceMetadata::is_metadata_directory(const String &p_path) {
	if (!is_enabled()) {
		return false;
	}
	const String root = metadata_root();
	if (root.is_empty()) {
		return false;
	}
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const String local_root = settings->localize_path(root).trim_suffix("/");
	// A root alias must not hide the entire resource tree.
	return local_root != "res:/" && local_root.begins_with("res://") && (p_path == local_root || p_path.begins_with(local_root + "/"));
}

bool ResourceMetadata::is_excluded(const String &p_path, const String &p_suffix) {
	const PackedStringArray patterns = GLOBAL_GET("filesystem/metadata/exclude_patterns");
	const String path = ProjectSettings::get_singleton()->localize_path(p_path).trim_prefix("res://");
	bool excluded = false;
	for (const String &entry : patterns) {
		for (String pattern : entry.split("\n")) {
			pattern = pattern.trim_suffix("\r");
			if (pattern.is_empty() || pattern.begins_with("#")) {
				continue;
			}
			const bool negate = pattern.begins_with("!");
			if (negate) {
				pattern = pattern.substr(1);
			}
			if (!pattern.is_empty() && (match_rule(pattern, path) || match_rule(pattern, path + p_suffix))) {
				excluded = !negate;
			}
		}
	}
	return excluded;
}

String ResourceMetadata::get_write_path(const String &p_path, const String &p_suffix) {
	if (!is_enabled()) {
		return p_path + p_suffix;
	}
	const String root = metadata_root();
	const String path = ProjectSettings::get_singleton()->localize_path(p_path).simplify_path();
	if (root.is_empty() || !path.begins_with("res://") || is_excluded(path, p_suffix)) {
		return p_path + p_suffix;
	}
	const String relative = path.trim_prefix("res://");
	ERR_FAIL_COND_V_MSG(relative.is_empty() || relative == ".." || relative.begins_with("../"), String(), "Metadata source must stay inside the resource root.");
	return root.path_join(relative) + p_suffix;
}

String ResourceMetadata::get_read_path(const String &p_path, const String &p_suffix) {
	if (!is_enabled()) {
		return p_path + p_suffix;
	}
	const String preferred = get_write_path(p_path, p_suffix);
	return FileAccess::exists(preferred) ? preferred : p_path + p_suffix;
}

Ref<FileAccess> ResourceMetadata::open_file(const String &p_path, const String &p_suffix, Error *r_error) {
	const String path = get_write_path(p_path, p_suffix);
	if (!is_enabled()) {
		return FileAccess::open(path, FileAccess::WRITE, r_error);
	}
	Error err = path.is_empty() ? ERR_INVALID_PARAMETER : DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	if (err != OK) {
		if (r_error) {
			*r_error = err;
		}
		return Ref<FileAccess>();
	}
	return FileAccess::open(path + ".tmp", FileAccess::WRITE, r_error);
}

Error ResourceMetadata::commit_file(const String &p_path, const String &p_suffix, const Ref<FileAccess> &p_file) {
	ERR_FAIL_COND_V(p_file.is_null(), ERR_INVALID_PARAMETER);
	// The disabled path is the original direct FileAccess write.
	if (!p_file->get_path().ends_with(p_suffix + ".tmp")) {
		return OK;
	}
	// Editor safe-save backends expose a private temporary name through
	// get_path_absolute(), then rename it to get_path() on close.
	const String staged_path = ProjectSettings::get_singleton()->globalize_path(p_file->get_path());
	const String path = staged_path.trim_suffix(".tmp");
	const uint64_t expected_size = p_file->get_position();
	p_file->flush();
	const Error write_error = p_file->get_error();
	p_file->close();
	if (write_error != OK) {
		DirAccess::remove_absolute(staged_path);
		return write_error;
	}
	Error err;
	Ref<FileAccess> staged = FileAccess::open(staged_path, FileAccess::READ, &err);
	if (staged.is_null()) {
		return err;
	}
	if (staged->get_length() != expected_size) {
		staged->close();
		DirAccess::remove_absolute(staged_path);
		return ERR_FILE_CANT_WRITE;
	}
	staged->close();
	err = DirAccess::rename_absolute(staged_path, path);
	if (err != OK) {
		DirAccess::remove_absolute(staged_path);
		return err;
	}
	const String legacy = p_path + p_suffix;
	if (FileAccess::exists(legacy) && !equivalent(path, legacy)) {
		err = DirAccess::remove_absolute(legacy);
		ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Saved metadata to '%s', but could not remove legacy sidecar '%s'.", path, legacy));
	}
	return OK;
}

Error ResourceMetadata::remove_file(const String &p_path, const String &p_suffix) {
	const String preferred = get_write_path(p_path, p_suffix);
	if (FileAccess::exists(preferred)) {
		Error err = DirAccess::remove_absolute(preferred);
		if (err != OK) {
			return err;
		}
	}
	const String legacy = p_path + p_suffix;
	return FileAccess::exists(legacy) ? DirAccess::remove_absolute(legacy) : OK;
}

Error ResourceMetadata::move_file(const String &p_from, const String &p_to, const String &p_suffix) {
	if (p_from == p_to) {
		return OK;
	}
	String source_path = get_read_path(p_from, p_suffix);
	if (!FileAccess::exists(source_path)) {
		// Directory renames already carry adjacent files to the destination.
		source_path = p_to + p_suffix;
		if (!FileAccess::exists(source_path) || equivalent(source_path, get_write_path(p_to, p_suffix))) {
			return OK;
		}
	}
	if (equivalent(source_path, get_write_path(p_to, p_suffix))) {
		return OK; // Case-only rename on a case-insensitive filesystem.
	}
	Error err;
	Ref<FileAccess> source = FileAccess::open(source_path, FileAccess::READ, &err);
	ERR_FAIL_COND_V(source.is_null(), err);
	const Vector<uint8_t> data = source->get_buffer(source->get_length());
	ERR_FAIL_COND_V(source->get_error() != OK, source->get_error());
	source->close();
	Ref<FileAccess> target = open_file(p_to, p_suffix, &err);
	ERR_FAIL_COND_V(target.is_null(), err);
	ERR_FAIL_COND_V(!target->store_buffer(data), ERR_FILE_CANT_WRITE);
	err = commit_file(p_to, p_suffix, target);
	target->close();
	if (err != OK) {
		return err;
	}
	const String destination = get_write_path(p_to, p_suffix);
	const String old_paths[] = { source_path, get_write_path(p_from, p_suffix), p_from + p_suffix };
	for (const String &old_path : old_paths) {
		if (FileAccess::exists(old_path) && !equivalent(old_path, destination)) {
			err = DirAccess::remove_absolute(old_path);
			if (err != OK) {
				return err;
			}
		}
	}
	return OK;
}

Error ResourceMetadata::migrate_file(const String &p_path, const String &p_suffix) {
	if (!is_enabled()) {
		return OK;
	}
	const String path = get_write_path(p_path, p_suffix);
	const String legacy = p_path + p_suffix;
	if (!FileAccess::exists(legacy) || equivalent(path, legacy)) {
		return OK;
	}
	// Rewrite the preferred copy when both exist; never let a stale adjacent
	// duplicate overwrite it, and never delete anything before a successful save.
	const String source_path = get_read_path(p_path, p_suffix);
	Error err;
	Ref<FileAccess> source = FileAccess::open(source_path, FileAccess::READ, &err);
	ERR_FAIL_COND_V(source.is_null(), err);
	const Vector<uint8_t> data = source->get_buffer(source->get_length());
	ERR_FAIL_COND_V(source->get_error() != OK, source->get_error());
	source->close();
	Ref<FileAccess> target = open_file(p_path, p_suffix, &err);
	ERR_FAIL_COND_V(target.is_null(), err);
	ERR_FAIL_COND_V(!target->store_buffer(data), ERR_FILE_CANT_WRITE);
	return commit_file(p_path, p_suffix, target);
}
