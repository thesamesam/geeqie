/*
 * Copyright (C) 2006 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: John Ellis
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "main.h"
#include "trash.h"
#include "utilops.h"
#include "window.h"

#include "editors.h"
#include "filedata.h"
#include "ui-fileops.h"

/*
 *--------------------------------------------------------------------------
 * Safe Delete
 *--------------------------------------------------------------------------
 */

static gint file_util_safe_number(gint64 free_space)
{
	gint n = 0;
	gint64 total = 0;
	GList *list;
	GList *work;
	gboolean sorted = FALSE;
	gboolean warned = FALSE;
	FileData *dir_fd;

	dir_fd = file_data_new_dir(options->file_ops.safe_delete_path);
	if (!filelist_read(dir_fd, &list, NULL))
		{
		file_data_unref(dir_fd);
		return 0;
		}
	file_data_unref(dir_fd);

	work = list;
	while (work)
		{
		FileData *fd;
		gint v;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		v = (gint)strtol(fd->name, NULL, 10);
		if (v >= n) n = v + 1;

		total += fd->size;
		}

	while (options->file_ops.safe_delete_folder_maxsize > 0 && list &&
	       (free_space < 0 || total + free_space > (gint64)options->file_ops.safe_delete_folder_maxsize * 1048576) )
		{
		FileData *fd;

		if (!sorted)
			{
			list = filelist_sort(list, SORT_NAME, TRUE);
			sorted = TRUE;
			}

		fd = static_cast<FileData *>(list->data);
		list = g_list_remove(list, fd);

		DEBUG_1("expunging from trash for space: %s", fd->name);
		if (!unlink_file(fd->path) && !warned)
			{
			file_util_warning_dialog(_("Delete failed"),
						 _("Unable to remove old file from trash folder"),
						 GTK_STOCK_DIALOG_WARNING, NULL);
			warned = TRUE;
			}
		total -= fd->size;
		file_data_unref(fd);
		}

	filelist_free(list);

	return n;
}

void file_util_trash_clear(void)
{
	file_util_safe_number(-1);
}

static gchar *file_util_safe_dest(const gchar *path)
{
	gint n;
	gchar *name;
	gchar *dest;

	n = file_util_safe_number(filesize(path));
	name = g_strdup_printf("%06d_%s", n, filename_from_path(path));
	dest = g_build_filename(options->file_ops.safe_delete_path, name, NULL);
	g_free(name);

	return dest;
}

static void move_to_trash_failed_cb(GenericDialog *UNUSED(gd), gpointer UNUSED(data))
{
	help_window_show("TrashFailed.html");
}

gboolean file_util_safe_unlink(const gchar *path)
{
	static GenericDialog *gd = NULL;
	gchar *result = NULL;
	gboolean success = TRUE;
	gchar *message;

	if (!isfile(path)) return FALSE;

	if (options->file_ops.no_trash)
		{
		if (!unlink_file(path))
			{
			file_util_warning_dialog(_("Delete failed"),
						 _("Unable to remove file"),
						 GTK_STOCK_DIALOG_WARNING, NULL);
			success = FALSE;
			}
		}
	else if (!options->file_ops.use_system_trash)
		{
		if (!isdir(options->file_ops.safe_delete_path))
			{
			DEBUG_1("creating trash: %s", options->file_ops.safe_delete_path);
			if (!options->file_ops.safe_delete_path || !mkdir_utf8(options->file_ops.safe_delete_path, 0755))
				{
				result = _("Could not create folder");
				success = FALSE;
				}
			}

		if (success)
			{
			gchar *dest;

			dest = file_util_safe_dest(path);
			if (dest)
				{
				DEBUG_1("safe deleting %s to %s", path, dest);
				success = move_file(path, dest);
				}
			else
				{
				success = FALSE;
				}

			if (!success && !access_file(path, W_OK))
				{
				result = _("Permission denied");
				}
			g_free(dest);
			}

		if (result && !gd)
			{
			gchar *buf;

			buf = g_strdup_printf(_("Unable to access or create the trash folder.\n\"%s\""), options->file_ops.safe_delete_path);
			gd = file_util_warning_dialog(result, buf, GTK_STOCK_DIALOG_WARNING, NULL);
			g_free(buf);
			}
		}
	else
		{
		GFile *tmp = g_file_new_for_path(path);
		GError *error = NULL;

		if (!g_file_trash(tmp, FALSE, &error) )
			{
			message = g_strconcat("See the Help file for a possible workaround.\n\n", error->message, NULL);
			gd = warning_dialog(_("Move to trash failed\n\n"), message, GTK_STOCK_DIALOG_ERROR, NULL);
			generic_dialog_add_button(gd, GTK_STOCK_HELP, "Help", move_to_trash_failed_cb, FALSE);

			g_free(message);
			g_error_free(error);

			/* A second warning dialog is not necessary */
			// success = FALSE;
			}
	}

	return success;
}

gchar *file_util_safe_delete_status(void)
{
	gchar *buf = NULL;

	if (is_valid_editor_command(CMD_DELETE))
		{
		buf = g_strdup(_("Deletion by external command"));
		}
	else if (options->file_ops.no_trash)
		{
		buf = g_strdup(_("Deleting without trash"));
		}
	else if (options->file_ops.safe_delete_enable)
		{
		if (!options->file_ops.use_system_trash)
			{
			gchar *buf2;
			if (options->file_ops.safe_delete_folder_maxsize > 0)
				buf2 = g_strdup_printf(_(" (max. %d MiB)"), options->file_ops.safe_delete_folder_maxsize);
			else
				buf2 = g_strdup("");

			buf = g_strdup_printf(_("Using Geeqie Trash bin\n%s"), buf2);
			g_free(buf2);
			}
		else
			{
			buf = g_strdup(_("Using system Trash bin"));
			}
		}

	return buf;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
