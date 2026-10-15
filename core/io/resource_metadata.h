/**************************************************************************/
/*  resource_metadata.h                                                   */
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

#pragma once

#include "core/io/file_access.h"

// Only resource sidecars are redirected. Imported data, caches and exported
// remaps keep their existing locations and formats.
class ResourceMetadata {
public:
	static bool is_enabled();
	static bool is_metadata_directory(const String &p_path);
	static bool is_excluded(const String &p_path, const String &p_suffix);
	static String get_write_path(const String &p_path, const String &p_suffix);
	static String get_read_path(const String &p_path, const String &p_suffix);
	static Ref<FileAccess> open_file(const String &p_path, const String &p_suffix, Error *r_error = nullptr);
	static Error commit_file(const String &p_path, const String &p_suffix, const Ref<FileAccess> &p_file);
	static Error remove_file(const String &p_path, const String &p_suffix);
	static Error move_file(const String &p_from, const String &p_to, const String &p_suffix);
	static Error migrate_file(const String &p_path, const String &p_suffix);
};
