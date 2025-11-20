I inspected src/gui_main.c and src/gui_batch.c and traced how the GUI components interact with the core modules (image_io, payload, metadata, aes_wrapper, stego_core, batch). Below is a concise, step‑by‑step working flow and the data path through the code for Encode, Decode, and Batch features.

What I looked at: gui_main.c (main window, Encode/Decode tabs) and gui_batch.c (batch task UI and orchestration). Now the flow.

High‑level GUI lifecycle
1. gui_init() initializes GTK.
2. gui_show_main_window() -> build_main_ui():
   - Builds three tabs: Encode, Decode, Batch (gui_batch_create_tab).
   - Presents main window and runs the GTK main loop.

Encode flow (single file)
1. User selects input image and output directory:
   - Buttons call gtk_file_dialog_open/select_folder; callbacks on_encode_input_file_selected/on_encode_output_file_selected store GFile* (encode_selected_input_file / encode_selected_output_file) and update button labels.
   - Data: GFile representing filesystem paths.

2. User chooses payload type:
   - If "Text Message" the text is read from encode_text_view_message; if "File" a payload file GFile* is chosen via on_encode_payload_file_selected.

3. User clicks Encode (on_encode_clicked):
   - Validations: input and output selected; if payload type = file ensure payload selected.
   - Reads password from encode_entry_password and LSB depth from encode_combo_lsb_depth.

4. JPEG handling:
   - image_is_jpeg(input_path) check. If JPEG, a modal warning is shown; image is converted to PNG via image_convert_jpeg_to_png into a temp path (/tmp/stego_converted_<pid>.png). actual_cover_path points to converted PNG or original path.

5. Load cover image:
   - image_load(actual_cover_path, &cover) loads the cover into struct Image.

6. Create payload data:
   - For text: extract text buffer -> payload_from_text(text, &payload).
   - For file: payload_load_from_file(payload_path, &payload).
   - Payload becomes a struct Payload (bytes + size + possibly metadata flags).

7. Optional encryption:
   - If password provided, aes_encrypt_inplace(&payload, password) modifies payload in memory and sets encrypted flag to true.

8. Create metadata:
   - metadata_create_from_payload(payload_filename, payload.size, lsb_depth, encrypted) -> struct Metadata containing original filename, size, LSB depth, encrypted flag, etc.

9. Embed payload:
   - stego_embed(&cover, &payload, &meta, lsb_depth, &stego) produces a stego image struct Image.
   - If embedding fails (image too small), flow aborts and shows error.

10. Save output:
    - Generate output filename (input_basename + "_stego_<rand>.png"), build output_path using output_dir (from GFile).
    - image_save(output_path, &stego).
    - Cleanup: free images/payload, unlink temporary PNG if conversion was done.

Data path summary (Encode)
- UI -> GFile* (input/output/payload) / text buffer / entry buffer (password)
- GFile -> file path string (g_file_get_path)
- image_io: image_load(input) -> struct Image cover
- payload module: payload_from_text or payload_load_from_file -> struct Payload (bytes)
- aes_wrapper: aes_encrypt_inplace modifies Payload (optional)
- metadata: metadata_create_from_payload -> struct Metadata
- stego_core: stego_embed(cover, payload, metadata, lsb_depth) -> struct Image stego
- image_io: image_save(stego, output_path)
- UI notified via progress bar updates sprinkled through the function (explicit fractions are set)

Decode flow (single file)
1. User selects stego image and output directory, enters optional password (callbacks store GFile* and label updates).

2. User clicks Decode (on_decode_clicked):
   - Load stego image: image_load(input_path, &stego).
   - stego_extract(&stego, &meta, &payload) extracts struct Metadata and struct Payload. If fails, show error.
   - If payload.encrypted and password provided: aes_decrypt_inplace(&payload, password).
   - Build output_path using output_dir and meta.original_filename.
   - payload_write_to_file(&payload, output_path) writes raw payload bytes to disk.
   - Cleanup and show completion message.

Data path summary (Decode)
- UI -> GFile* (input/output), entry buffer (password)
- image_io: image_load -> struct Image stego
- stego_core: stego_extract -> struct Metadata + struct Payload
- aes_wrapper: aes_decrypt_inplace -> modifies Payload (optional)
- payload: payload_write_to_file -> writes bytes to disk
- UI progress bar updated at key steps

Batch flow (multiple tasks, asynchronous)
1. User opens Batch tab: gui_batch_create_tab creates UI with mode combo (Encode/Decode), "+ Add Task" and Start All Tasks button. task_panels is a GHashTable task_id -> BatchTaskPanel*.

2. Add Task:
   - on_add_task_clicked reads mode and calls create_batch_task_panel(is_encode).
   - create_batch_task_panel builds a collapsible panel (BatchTaskPanel) with widgets similar to single encode/decode panels (input chooser, output chooser, payload controls when encode, password, lsb, progress label/remove).
   - Panel stores GFile* references for selected files, password string pointer, lsb depth, is_encode flag, and a GTask* running_task.

3. User configures many tasks: each panel uses file dialogs and callbacks to set panel->input_file, panel->output_folder, panel->payload_file, and UI labels. task_panel_is_ready() checks required fields.

4. Start All Tasks:
   - on_start_all_clicked -> start_all_tasks() iterates task_panels and calls start_task_panel(panel) for ready, not currently processing panels.
   - start_task_panel disables UI elements for that panel, updates progress/status, collects parameters (paths, password, lsb), and prepares GuiBatchUserData containing task_id and weak panel pointer.

5. Launch asynchronous work:
   - For decode: panel->running_task = batch_decode_async(stego_path, output_dir, password, gui_batch_progress_cb, gui_batch_finished_cb, ud).
   - For encode: either creates a temp file for text payload or uses selected payload file, constructs output_path, then panel->running_task = batch_encode_async(cover_path, payload_path, output_path, lsb_depth, password, gui_batch_progress_cb, gui_batch_finished_cb, ud).
   - The batch module runs operations asynchronously (not shown here). It repeatedly calls gui_batch_progress_cb (on main thread) and calls gui_batch_finished_cb when done.

6. Progress and Finish callbacks:
   - gui_batch_progress_cb(user_data, fraction) updates panel->progress_bar and panel->status_label text.
   - gui_batch_finished_cb(user_data, success, message) marks panel as not processing, updates status_label (green/red), re-enables remove button, frees GuiBatchUserData, and calls update_start_button_sensitivity() so Start All Tasks can be re-enabled if other ready tasks exist.
   - Removing a task uses on_remove_task_clicked which prevents removal if panel->is_processing; otherwise g_hash_table_remove(task_panels, task_id) triggers batch_task_panel_free to free resources and removes the widget from the UI.

Batch data path summary
- Per-panel: UI -> GFile*/text/pointers -> on start convert to char* paths (g_file_get_path)
- batch module (async) receives path strings + password + lsb + callbacks -> internally calls same core modules (image_io, payload, metadata, stego_core) to perform encode/decode
- Progress updates routed back to GUI via gui_batch_progress_cb and gui_batch_finished_cb with GuiBatchUserData linking the async result to the BatchTaskPanel UI elements

Memory/cleanup notes (as seen in code)
- Files selected are stored as GFile* and unref'd on panel free or when replaced.
- Temporary files (converted PNG for JPEG or temp text payload) are unlinked/removed after use.
- GuiBatchUserData is allocated per submitted task and freed in gui_batch_finished_cb.
- Hash table task_panels owns keys (g_strdup) and values freed by batch_task_panel_free via g_hash_table_new_full.

In short
- GUI collects user inputs (GFile*, text, password, choices).
- Paths/text are converted to in‑memory payloads or temp files.
- Core modules (image_io, payload, metadata, aes_wrapper, stego_core) perform heavy lifting: image load/save, payload serialization, optional AES encrypt/decrypt, metadata creation, and stego embed/extract.
- Batch uses async APIs and reports progress back to the GUI via callbacks; single encode/decode happens synchronously in GUI handlers with explicit progress fractions.

If you want, I can draw a compact sequence diagram or summarize the exact functions involved per major data handoff (e.g., which function returns/accepts struct Payload, struct Image, etc.). Which would you prefer next?
