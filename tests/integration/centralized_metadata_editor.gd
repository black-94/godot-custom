@tool
extends EditorPlugin
## Drives the editor's existing UI signal callbacks, not a test-only file API.
## Installed into a temporary project by test_centralized_metadata.py.

var dock: FileSystemDock
var file_tree: Tree

func _enter_tree():
	if FileAccess.file_exists("res://editor_test_done"):
		return
	_run.call_deferred()
	_watchdog.call_deferred()

func _watchdog():
	await get_tree().create_timer(90).timeout
	push_error("Editor metadata regression did not finish")
	get_tree().quit(1)

func descendants(node: Node) -> Array[Node]:
	var result: Array[Node] = [node]
	for child in node.get_children(true):
		result.append_array(descendants(child))
	return result

func callback(node: Node, signal_name: String, method: String) -> Callable:
	for connection in node.get_signal_connection_list(signal_name):
		var action: Callable = connection["callable"]
		if String(action.get_method()).ends_with(method):
			return action
	return Callable()

func settle():
	await get_tree().create_timer(0.5).timeout
	while EditorInterface.get_resource_filesystem().is_scanning() or EditorInterface.get_resource_filesystem().is_importing():
		await get_tree().process_frame
	await get_tree().create_timer(0.2).timeout

func select_path(path: String):
	# The real tree's multi-selection callbacks consume row metadata. A synthetic
	# row lets this test work in either filesystem dock layout, also headlessly.
	file_tree.deselect_all()
	var row = file_tree.create_item(file_tree.get_root())
	row.set_text(0, path.trim_suffix("/").get_file())
	row.set_metadata(0, path)
	row.select(0, true)
	file_tree.grab_focus()

func option(id: int):
	print("EDITOR selected: ", file_tree.get_selected().get_metadata(0))
	for node in descendants(dock):
		if node is PopupMenu:
			var action = callback(node, "id_pressed", "_tree_rmb_option")
			if action.is_valid():
				action.call(id)
				return
	assert(false, "Missing filesystem context menu")

func move_path(path: String, destination: String):
	print("EDITOR move: ", path, " -> ", destination)
	select_path(path)
	option(9) # FILE_MENU_MOVE
	for node in descendants(dock):
		if node.has_signal("move_pressed") and node.visible:
			node.emit_signal("move_pressed", destination)
			node.hide()
			await settle()
			return
	assert(false, "Missing move dialog")

func duplicate_path(path: String, new_name: String):
	print("EDITOR duplicate: ", path, " -> ", new_name)
	select_path(path)
	option(12) # FILE_MENU_DUPLICATE
	for node in descendants(dock):
		if node.is_class("DirectoryCreateDialog") and node.visible:
			print("EDITOR dialog: ", node.title)
			for child in descendants(node):
				if child is LineEdit:
					child.text = new_name
					child.text_changed.emit(new_name)
			# AcceptDialog's button dispatches the virtual ok_pressed() callback.
			node.get_ok_button().pressed.emit()
			await settle()
			return
	assert(false, "Missing duplicate dialog")

func rename_path(path: String, new_name: String):
	print("EDITOR rename: ", path, " -> ", new_name)
	select_path(path)
	option(10) # FILE_MENU_RENAME
	for node in descendants(file_tree):
		if node is LineEdit and node.text == path.trim_suffix("/").get_file():
			node.text = new_name
			node.text_submitted.emit(new_name)
			await settle()
			return
	assert(false, "Missing tree rename editor")

func remove_path(path: String):
	print("EDITOR remove: ", path)
	select_path(path)
	option(11) # FILE_MENU_REMOVE
	for node in descendants(dock):
		if node.is_class("DependencyRemoveDialog") and node.visible:
			node.get_ok_button().pressed.emit()
			await settle()
			return
	assert(false, "Missing dependency removal dialog")

func metadata(path: String, suffix: String) -> String:
	if not ProjectSettings.get_setting("filesystem/metadata/enabled"):
		return path + suffix
	var root: String = ProjectSettings.get_setting("filesystem/metadata/root_directory")
	return root.path_join(path.trim_prefix("res://")) + suffix

func text(path: String) -> String:
	return FileAccess.get_file_as_string(path)

func _run():
	await settle()
	dock = EditorInterface.get_file_system_dock()
	for node in descendants(dock):
		if node is Tree and callback(node, "multi_selected", "_tree_multi_selected").is_valid():
			file_tree = node
			break
	assert(file_tree != null)
	# Scanner must exclude even a non-hidden metadata directory.
	var fs = EditorInterface.get_resource_filesystem().get_filesystem()
	for i in fs.get_subdir_count():
		assert(fs.get_subdir(i).get_name() != "metadata visible")

	var original = text(metadata("res://scripts/player.gd", ".uid"))
	await duplicate_path("res://scripts/player.gd", "copy.gd")
	assert(FileAccess.file_exists("res://scripts/copy.gd"))
	assert(text(metadata("res://scripts/copy.gd", ".uid")) != original)
	if ProjectSettings.get_setting("filesystem/metadata/enabled"):
		assert(not FileAccess.file_exists("res://scripts/copy.gd.uid"))
	await rename_path("res://scripts/copy.gd", "renamed.gd")
	var copy_uid = text(metadata("res://scripts/renamed.gd", ".uid"))
	assert(not FileAccess.file_exists(metadata("res://scripts/copy.gd", ".uid")))
	await move_path("res://scripts/renamed.gd", "res://vendor")
	assert(text("res://vendor/renamed.gd.uid") == copy_uid)
	assert(not FileAccess.file_exists(metadata("res://scripts/renamed.gd", ".uid")))
	await move_path("res://vendor/renamed.gd", "res://scripts")
	assert(text(metadata("res://scripts/renamed.gd", ".uid")) == copy_uid)
	assert(not FileAccess.file_exists("res://vendor/renamed.gd.uid"))

	var config = ConfigFile.new()
	assert(config.load(metadata("res://textures/icon.png", ".import")) == OK)
	var texture_uid = config.get_value("remap", "uid")
	await duplicate_path("res://textures/icon.png", "copy.png")
	assert(config.load(metadata("res://textures/copy.png", ".import")) == OK)
	assert(config.get_value("remap", "uid") != texture_uid)
	assert(config.get_value("params", "mipmaps/generate") == true)
	var texture_copy_uid = config.get_value("remap", "uid")
	await rename_path("res://textures/copy.png", "renamed.png")
	assert(config.load(metadata("res://textures/renamed.png", ".import")) == OK)
	assert(config.get_value("remap", "uid") == texture_copy_uid)

	# Directory duplication reassigns identities; directory rename preserves them.
	await duplicate_path("res://scripts/", "scripts_copy")
	assert(text(metadata("res://scripts_copy/player.gd", ".uid")) != original)
	var directory_uid = text(metadata("res://scripts_copy/player.gd", ".uid"))
	await rename_path("res://scripts_copy/", "scripts_moved")
	assert(text(metadata("res://scripts_moved/player.gd", ".uid")) == directory_uid)
	assert(not FileAccess.file_exists(metadata("res://scripts_copy/player.gd", ".uid")))
	await duplicate_path("res://textures/", "textures_copy")
	await rename_path("res://textures_copy/", "textures_moved")
	assert(FileAccess.file_exists(metadata("res://textures_moved/icon.png", ".import")))

	await remove_path("res://scripts/renamed.gd")
	assert(not FileAccess.file_exists("res://scripts/renamed.gd"))
	assert(not FileAccess.file_exists(metadata("res://scripts/renamed.gd", ".uid")))
	await remove_path("res://scripts_moved/")
	assert(not FileAccess.file_exists(metadata("res://scripts_moved/player.gd", ".uid")))
	await remove_path("res://textures_moved/")
	assert(not FileAccess.file_exists(metadata("res://textures_moved/icon.png", ".import")))
	await remove_path("res://textures/renamed.png")
	assert(not FileAccess.file_exists(metadata("res://textures/renamed.png", ".import")))
	assert(text(metadata("res://scripts/player.gd", ".uid")) == original)
	FileAccess.open("res://editor_test_done", FileAccess.WRITE).store_string("passed")
	print("METADATA_EDITOR_OK")
	get_tree().quit()
