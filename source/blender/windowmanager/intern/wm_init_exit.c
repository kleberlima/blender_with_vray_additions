/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2007 Blender Foundation.
 * All rights reserved.
 *
 *
 * Contributor(s): Blender Foundation
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/intern/wm_init_exit.c
 *  \ingroup wm
 *
 * Manage initializing resources and correctly shutting down.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#endif

#include "MEM_guardedalloc.h"

#include "CLG_log.h"

#include "DNA_genfile.h"
#include "DNA_scene_types.h"
#include "DNA_userdef_types.h"
#include "DNA_windowmanager_types.h"

#include "BLI_callbacks.h"
#include "BLI_listbase.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"

#include "BLO_writefile.h"
#include "BLO_undofile.h"

#include "BKE_blender.h"
#include "BKE_blender_undo.h"
#include "BKE_context.h"
#include "BKE_screen.h"
#include "BKE_DerivedMesh.h"
#include "BKE_global.h"
#include "BKE_icons.h"
#include "BKE_library.h"
#include "BKE_library_remap.h"
#include "BKE_main.h"
#include "BKE_mball_tessellate.h"
#include "BKE_node.h"
#include "BKE_report.h"
#include "BKE_font.h"

#include "BKE_addon.h"
#include "BKE_appdir.h"
#include "BKE_sequencer.h" /* free seq clipboard */
#include "BKE_material.h" /* clear_matcopybuf */
#include "BKE_tracking.h" /* free tracking clipboard */
#include "BKE_mask.h" /* free mask clipboard */

#include "RE_engine.h"
#include "RE_pipeline.h"        /* RE_ free stuff */

#ifdef WITH_PYTHON
#include "BPY_extern.h"
#endif

#ifdef WITH_GAMEENGINE
#  include "BL_System.h"
#endif
#include "GHOST_Path-api.h"
#include "GHOST_C-api.h"

#include "RNA_define.h"

#include "WM_api.h"
#include "WM_types.h"

#include "wm_cursors.h"
#include "wm_event_system.h"
#include "wm.h"
#include "wm_files.h"
#include "wm_window.h"

#include "ED_anim_api.h"
#include "ED_armature.h"
#include "ED_gpencil.h"
#include "ED_keyframing.h"
#include "ED_keyframes_edit.h"
#include "ED_node.h"
#include "ED_render.h"
#include "ED_space_api.h"
#include "ED_screen.h"
#include "ED_util.h"
#include "ED_undo.h"

#include "UI_interface.h"
#include "BLF_api.h"
#include "BLT_lang.h"

#include "GPU_buffers.h"
#include "GPU_draw.h"
#include "GPU_init_exit.h"

#include "BKE_depsgraph.h"
#include "BKE_sound.h"
#include "COM_compositor.h"

#ifdef WITH_OPENSUBDIV
#  include "BKE_subsurf.h"
#endif

CLG_LOGREF_DECLARE_GLOBAL(WM_LOG_OPERATORS, "wm.operator");
CLG_LOGREF_DECLARE_GLOBAL(WM_LOG_HANDLERS, "wm.handler");
CLG_LOGREF_DECLARE_GLOBAL(WM_LOG_EVENTS, "wm.event");
CLG_LOGREF_DECLARE_GLOBAL(WM_LOG_KEYMAPS, "wm.keymap");

static void wm_init_reports(bContext *C)
{
	ReportList *reports = CTX_wm_reports(C);

	BLI_assert(!reports || BLI_listbase_is_empty(&reports->list));

	BKE_reports_init(reports, RPT_STORE);
}
static void wm_free_reports(bContext *C)
{
	ReportList *reports = CTX_wm_reports(C);

	BKE_reports_clear(reports);
}

bool wm_start_with_console = false; /* used in creator.c */

/* only called once, for startup */
void WM_init(bContext *C, int argc, const char **argv)
{

	if (!G.background) {
		wm_ghost_init(C);   /* note: it assigns C to ghost! */
		wm_init_cursor_data();
	}
	GHOST_CreateSystemPaths();

	BKE_addon_pref_type_init();

	wm_operatortype_init();
	wm_operatortypes_register();

	WM_menutype_init();
	WM_uilisttype_init();

	ED_undosys_type_init();

	BKE_library_callback_free_window_manager_set(wm_close_and_free);   /* library.c */
	BKE_library_callback_free_notifier_reference_set(WM_main_remove_notifier_reference);   /* library.c */
	BKE_library_callback_remap_editor_id_reference_set(WM_main_remap_editor_id_reference);   /* library.c */
	BKE_blender_callback_test_break_set(wm_window_testbreak); /* blender.c */
	BKE_spacedata_callback_id_remap_set(ED_spacedata_id_remap); /* screen.c */
	DAG_editors_update_cb(ED_render_id_flush_update,
	                      ED_render_scene_update,
	                      ED_render_scene_update_pre); /* depsgraph.c */

	ED_spacetypes_init();   /* editors/space_api/spacetype.c */

	ED_file_init();         /* for fsmenu */
	ED_node_init_butfuncs();

	BLF_init(); /* Please update source/gamengine/GamePlayer/GPG_ghost.cpp if you change this */

	BLT_lang_init();
	/* Must call first before doing any .blend file reading, since versionning code may create new IDs... See T57066. */
	BLT_lang_set(NULL);

	/* reports cant be initialized before the wm,
	 * but keep before file reading, since that may report errors */
	wm_init_reports(C);

	/* get the default database, plus a wm */
	wm_homefile_read(C, NULL, G.factory_startup, false, true, NULL, WM_init_state_app_template_get());

	/* Call again to set from userpreferences... */
	BLT_lang_set(NULL);

	if (!G.background) {

#ifdef WITH_INPUT_NDOF
		/* sets 3D mouse deadzone */
		WM_ndof_deadzone_set(U.ndof_deadzone);
#endif

		GPU_init();

		GPU_set_mipmap(G_MAIN, !(U.gameflags & USER_DISABLE_MIPMAP));
		GPU_set_linear_mipmap(true);
		GPU_set_anisotropic(G_MAIN, U.anisotropic_filter);
		GPU_set_gpu_mipmapping(G_MAIN, U.use_gpu_mipmap);

#ifdef WITH_OPENSUBDIV
		BKE_subsurf_osd_init();
#endif

		UI_init();
	}
	else {
		/* Note: Currently only inits icons, which we now want in background mode too
		 * (scripts could use those in background processing...).
		 * In case we do more later, we may need to pass a 'background' flag.
		 * Called from 'UI_init' above */
		BKE_icons_init(1);
	}


	ED_spacemacros_init();

	/* note: there is a bug where python needs initializing before loading the
	 * startup.blend because it may contain PyDrivers. It also needs to be after
	 * initializing space types and other internal data.
	 *
	 * However cant redo this at the moment. Solution is to load python
	 * before wm_homefile_read() or make py-drivers check if python is running.
	 * Will try fix when the crash can be repeated. - campbell. */

#ifdef WITH_PYTHON
	BPY_context_set(C); /* necessary evil */
	BPY_python_start(argc, argv);

	BPY_python_reset(C);
#else
	(void)argc; /* unused */
	(void)argv; /* unused */
#endif

	if (!G.background && !wm_start_with_console)
		GHOST_toggleConsole(3);

	clear_matcopybuf();
	ED_render_clear_mtex_copybuf();

	// glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	wm_history_file_read();

	/* allow a path of "", this is what happens when making a new file */
#if 0
	if (BKE_main_blendfile_path_from_global()[0] == '\0')
		BLI_make_file_string("/", G_MAIN->name, BKE_appdir_folder_default(), "untitled.blend");
#endif

	BLI_strncpy(G.lib, BKE_main_blendfile_path_from_global(), sizeof(G.lib));

#ifdef WITH_COMPOSITOR
	if (1) {
		extern void *COM_linker_hack;
		COM_linker_hack = COM_execute;
	}
#endif

	/* load last session, uses regular file reading so it has to be in end (after init py etc) */
	if (U.uiflag2 & USER_KEEP_SESSION) {
		/* calling WM_recover_last_session(C, NULL) has been moved to creator.c */
		/* that prevents loading both the kept session, and the file on the command line */
	}
	else {
		Main *bmain = CTX_data_main(C);
		/* note, logic here is from wm_file_read_post,
		 * call functions that depend on Python being initialized. */

		/* normally 'wm_homefile_read' will do this,
		 * however python is not initialized when called from this function.
		 *
		 * unlikely any handlers are set but its possible,
		 * note that recovering the last session does its own callbacks. */
		CTX_wm_window_set(C, CTX_wm_manager(C)->windows.first);

		BLI_callback_exec(bmain, NULL, BLI_CB_EVT_VERSION_UPDATE);
		BLI_callback_exec(bmain, NULL, BLI_CB_EVT_LOAD_POST);

		wm_file_read_report(C, bmain);

		if (!G.background) {
			CTX_wm_window_set(C, NULL);
		}
	}
}

void WM_init_splash(bContext *C)
{
	if ((U.uiflag & USER_SPLASH_DISABLE) == 0) {
		wmWindowManager *wm = CTX_wm_manager(C);
		wmWindow *prevwin = CTX_wm_window(C);

		if (wm->windows.first) {
			CTX_wm_window_set(C, wm->windows.first);
			WM_operator_name_call(C, "WM_OT_splash", WM_OP_INVOKE_DEFAULT, NULL);
			CTX_wm_window_set(C, prevwin);
		}
	}
}

bool WM_init_game(bContext *C)
{
	wmWindowManager *wm = CTX_wm_manager(C);
	wmWindow *win;

	ScrArea *sa;
	ARegion *ar = NULL;

	Scene *scene = CTX_data_scene(C);

	if (!scene) {
		/* XXX, this should not be needed. */
		Main *bmain = CTX_data_main(C);
		scene = bmain->scene.first;
	}

	win = wm->windows.first;

	/* first to get a valid window */
	if (win)
		CTX_wm_window_set(C, win);

	sa = BKE_screen_find_big_area(CTX_wm_screen(C), SPACE_VIEW3D, 0);
	ar = BKE_area_find_region_type(sa, RGN_TYPE_WINDOW);

	/* if we have a valid 3D view */
	if (sa && ar) {
		ARegion *arhide;

		CTX_wm_area_set(C, sa);
		CTX_wm_region_set(C, ar);

		/* disable quad view */
		if (ar->alignment == RGN_ALIGN_QSPLIT)
			WM_operator_name_call(C, "SCREEN_OT_region_quadview", WM_OP_EXEC_DEFAULT, NULL);

		/* toolbox, properties panel and header are hidden */
		for (arhide = sa->regionbase.first; arhide; arhide = arhide->next) {
			if (arhide->regiontype != RGN_TYPE_WINDOW) {
				if (!(arhide->flag & RGN_FLAG_HIDDEN)) {
					ED_region_toggle_hidden(C, arhide);
				}
			}
		}

		/* full screen the area */
		if (!sa->full) {
			ED_screen_state_toggle(C, win, sa, SCREENMAXIMIZED);
		}

		/* Fullscreen */
		if ((scene->gm.playerflag & GAME_PLAYER_FULLSCREEN)) {
			WM_operator_name_call(C, "WM_OT_window_fullscreen_toggle", WM_OP_EXEC_DEFAULT, NULL);
			wm_get_screensize(&ar->winrct.xmax, &ar->winrct.ymax);
			ar->winx = ar->winrct.xmax + 1;
			ar->winy = ar->winrct.ymax + 1;
		}
		else {
			GHOST_RectangleHandle rect = GHOST_GetClientBounds(win->ghostwin);
			ar->winrct.ymax = GHOST_GetHeightRectangle(rect);
			ar->winrct.xmax = GHOST_GetWidthRectangle(rect);
			ar->winx = ar->winrct.xmax + 1;
			ar->winy = ar->winrct.ymax + 1;
			GHOST_DisposeRectangle(rect);
		}

		WM_operator_name_call(C, "VIEW3D_OT_game_start", WM_OP_EXEC_DEFAULT, NULL);

		BKE_sound_exit();

		return true;
	}
	else {
		ReportTimerInfo *rti;

		BKE_report(&wm->reports, RPT_ERROR, "No valid 3D View found, game auto start is not possible");

		/* After adding the report to the global list, reset the report timer. */
		WM_event_remove_timer(wm, NULL, wm->reports.reporttimer);

		/* Records time since last report was added */
		wm->reports.reporttimer = WM_event_add_timer(wm, CTX_wm_window(C), TIMER, 0.02);

		rti = MEM_callocN(sizeof(ReportTimerInfo), "ReportTimerInfo");
		wm->reports.reporttimer->customdata = rti;

		return false;
	}
}

/* free strings of open recent files */
static void free_openrecent(void)
{
	struct RecentFile *recent;

	for (recent = G.recent_files.first; recent; recent = recent->next)
		MEM_freeN(recent->filepath);

	BLI_freelistN(&(G.recent_files));
}


#ifdef WIN32
/* Read console events until there is a key event.  Also returns on any error. */
static void wait_for_console_key(void)
{
	HANDLE hConsoleInput = GetStdHandle(STD_INPUT_HANDLE);

	if (!ELEM(hConsoleInput, NULL, INVALID_HANDLE_VALUE) && FlushConsoleInputBuffer(hConsoleInput)) {
		for (;;) {
			INPUT_RECORD buffer;
			DWORD ignored;

			if (!ReadConsoleInput(hConsoleInput, &buffer, 1, &ignored)) {
				break;
			}

			if (buffer.EventType == KEY_EVENT) {
				break;
			}
		}
	}
}
#endif

static int wm_exit_handler(bContext *C, const wmEvent *event, void *userdata)
{
	WM_exit(C);

	UNUSED_VARS(event, userdata);
	return WM_UI_HANDLER_BREAK;
}

/**
 * Cause a delayed WM_exit() call to avoid leaking memory when trying to exit from within operators.
 */
void wm_exit_schedule_delayed(const bContext *C)
{
	/* What we do here is a little bit hacky, but quite simple and doesn't require bigger
	 * changes: Add a handler wrapping WM_exit() to cause a delayed call of it. */

	wmWindow *win = CTX_wm_window(C);

	/* Use modal UI handler for now. Could add separate WM handlers or so, but probably not worth it. */
	WM_event_add_ui_handler(C, &win->modalhandlers, wm_exit_handler, NULL, NULL, 0);
	WM_event_add_mousemove(C); /* ensure handler actually gets called */
}

/**
 * \note doesn't run exit() call #WM_exit() for that.
 */
void WM_exit_ext(bContext *C, const bool do_python)
{
	wmWindowManager *wm = C ? CTX_wm_manager(C) : NULL;

	/* first wrap up running stuff, we assume only the active WM is running */
	/* modal handlers are on window level freed, others too? */
	/* note; same code copied in wm_files.c */
	if (C && wm) {
		wmWindow *win;

		if (!G.background) {
			struct MemFile *undo_memfile = wm->undo_stack ? ED_undosys_stack_memfile_get_active(wm->undo_stack) : NULL;
			if ((U.uiflag2 & USER_KEEP_SESSION) || (undo_memfile != NULL)) {
				/* save the undo state as quit.blend */
				char filename[FILE_MAX];
				bool has_edited;
				int fileflags = G.fileflags & ~(G_FILE_COMPRESS | G_FILE_AUTOPLAY | G_FILE_HISTORY);

				BLI_make_file_string("/", filename, BKE_tempdir_base(), BLENDER_QUIT_FILE);

				has_edited = ED_editors_flush_edits(C, false);

				if ((has_edited && BLO_write_file(CTX_data_main(C), filename, fileflags, NULL, NULL)) ||
				    (undo_memfile && BLO_memfile_write_file(undo_memfile, filename)))
				{
					printf("Saved session recovery to '%s'\n", filename);
				}
			}
		}

		WM_jobs_kill_all(wm);

		for (win = wm->windows.first; win; win = win->next) {

			CTX_wm_window_set(C, win);  /* needed by operator close callbacks */
			WM_event_remove_handlers(C, &win->handlers);
			WM_event_remove_handlers(C, &win->modalhandlers);
			ED_screen_exit(C, win, win->screen);
		}
	}

	BKE_addon_pref_type_free();
	wm_operatortype_free();
	wm_dropbox_free();
	WM_menutype_free();
	WM_uilisttype_free();

	/* all non-screen and non-space stuff editors did, like editmode */
	if (C)
		ED_editors_exit(C);

	ED_undosys_type_free();

//	XXX
//	BIF_GlobalReebFree();
//	BIF_freeRetarget();
	BIF_freeTemplates(C);

	free_openrecent();

	BKE_mball_cubeTable_free();

	/* render code might still access databases */
	RE_FreeAllRender();
	RE_engines_exit();

	ED_preview_free_dbase();  /* frees a Main dbase, before BKE_blender_free! */

	if (C && wm)
		wm_free_reports(C);  /* before BKE_blender_free! - since the ListBases get freed there */

	BKE_sequencer_free_clipboard(); /* sequencer.c */
	BKE_tracking_clipboard_free();
	BKE_mask_clipboard_free();
	BKE_vfont_clipboard_free();

#ifdef WITH_COMPOSITOR
	COM_deinitialize();
#endif

	BKE_blender_free();  /* blender.c, does entire library and spacetypes */
//	free_matcopybuf();
	ANIM_fcurves_copybuf_free();
	ANIM_drivers_copybuf_free();
	ANIM_driver_vars_copybuf_free();
	ANIM_fmodifiers_copybuf_free();
	ED_gpencil_anim_copybuf_free();
	ED_gpencil_strokes_copybuf_free();
	BKE_node_clipboard_clear();

	BLF_exit();

#ifdef WITH_INTERNATIONAL
	BLF_free_unifont();
	BLF_free_unifont_mono();
	BLT_lang_free();
#endif

	ANIM_keyingset_infos_exit();

//	free_txt_data();


#ifdef WITH_PYTHON
	/* option not to close python so we can use 'atexit' */
	if (do_python && ((C == NULL) || CTX_py_init_get(C))) {
		/* XXX - old note */
		/* before BKE_blender_free so py's gc happens while library still exists */
		/* needed at least for a rare sigsegv that can happen in pydrivers */

		/* Update for blender 2.5, move after BKE_blender_free because blender now holds references to PyObject's
		 * so decref'ing them after python ends causes bad problems every time
		 * the pyDriver bug can be fixed if it happens again we can deal with it then */
		BPY_python_end();
	}
#else
	(void)do_python;
#endif

	if (!G.background) {
#ifdef WITH_OPENSUBDIV
		BKE_subsurf_osd_cleanup();
#endif

		GPU_global_buffer_pool_free();
		GPU_free_unused_buffers(G_MAIN);

		GPU_exit();
	}

	ED_file_exit(); /* for fsmenu */

	UI_exit();
	BKE_blender_userdef_data_free(&U, false);

	RNA_exit(); /* should be after BPY_python_end so struct python slots are cleared */

	wm_ghost_exit();

	CTX_free(C);
#ifdef WITH_GAMEENGINE
	SYS_DeleteSystem(SYS_GetSystem());
#endif

	GHOST_DisposeSystemPaths();

	DNA_sdna_current_free();

	BLI_threadapi_exit();

	/* No need to call this early, rather do it late so that other pieces of Blender using sound may exit cleanly,
	 * see also T50676. */
	BKE_sound_exit();

	CLG_exit();

	BKE_blender_atexit();

	if (MEM_get_memory_blocks_in_use() != 0) {
		size_t mem_in_use = MEM_get_memory_in_use() + MEM_get_memory_in_use();
		printf("Error: Not freed memory blocks: %u, total unfreed memory %f MB\n",
		       MEM_get_memory_blocks_in_use(),
		       (double)mem_in_use / 1024 / 1024);
		MEM_printmemlist();
	}
	wm_autosave_delete();

	BKE_tempdir_session_purge();
}

/**
 * \brief Main exit function to close Blender ordinarily.
 * \note Use #wm_exit_schedule_delayed() to close Blender from an operator. Might leak memory otherwise.
 */
void WM_exit(bContext *C)
{
	BLI_callback_exec(NULL, NULL, BLI_CB_EVT_QUIT);

	WM_exit_ext(C, 1);

	printf("\nBlender quit\n");

#ifdef WIN32
	/* ask user to press a key when in debug mode */
	if (G.debug & G_DEBUG) {
		printf("Press any key to exit . . .\n\n");
		wait_for_console_key();
	}
#endif

	exit(G.is_break == true);
}
