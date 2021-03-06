/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * Installer for Schillix
 * (c) Copyright 2013 - Andrew Stormont <andyjstormont@gmail.com>
 */

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <dirent.h>
#include <libzfs.h>

#include "config.h"
#include "disk.h"
#include "copy.h"

char program_name[] = "schillix-install";
char temp_mount[PATH_MAX] = DEFAULT_MNT_POINT;
char cdrom_path[PATH_MAX] = DEFAULT_CDROM_PATH;

/*
 * Print usage and exit
 */
void
usage (int retval)
{
	FILE *out;

	/*
	 * Print to stderr only on error
	 */
	if (retval == 0)
		out = stdout;
	else
		out = stderr;

	/*
	 * Add some space between error messages
	 */
	if (retval != 0)
		fprintf (out, "\n");

	fprintf (out, "Installer for Schillix\n");
	fprintf (out, "(c) Copyright 2013 - Andrew Stormont\n");
	fprintf (out, "\n");
	fprintf (out, "usage: schillix-install [opts] /path/to/disk or devname\n");
	fprintf (out, "\n");
	fprintf (out, "Where opts is:\n");
	fprintf (out, "\t-r name or new rpool (default is " DEFAULT_RPOOL_NAME ")\n");
	fprintf (out, "\t-m temporary mountpoint (default is " DEFAULT_MNT_POINT ")\n");
	fprintf (out, "\t-c path to livecd contents (default is " DEFAULT_CDROM_PATH ")\n");
	fprintf (out, "\t-u don't unmount or export rpool after install\n");
	fprintf (out, "\t-? print this message and exit\n");

	exit (retval);
}

int
main (int argc, char **argv)
{
	char c, disk[PATH_MAX] = { '\0' }, rpool[ZPOOL_MAXNAMELEN] = DEFAULT_RPOOL_NAME;
	int i;
	DIR *dir;
	libzfs_handle_t *libzfs_handle;
	boolean_t unmount = B_TRUE;

	/*
	 * Parse command line arguments
	 */
	while ((c = getopt (argc, argv, "r:m:c:u?")) != -1)
	{
		switch (c)
		{
			case 'r':
				/*
				 * Set alternative rpool name
				 */
				if (strlen (optarg) >= ZPOOL_MAXNAMELEN)
				{
					fprintf (stderr, "Error: rpool name too long\n");
					usage (EXIT_FAILURE);
				}

				strcpy (rpool, optarg);
				break;

			case 'm':
				/*
				 * Set alternative temp mountpoint
				 */
				if (strlen (optarg) >= PATH_MAX)
				{
					fprintf (stderr, "Error: mountpoint path too long\n");
					usage (EXIT_FAILURE);
				}

				strcpy (temp_mount, optarg);
				break;

			case 'c':
				/*
				 * Set alternative path to livecd contents
				 */
				if (strlen (optarg) >= PATH_MAX)
				{
					fprintf (stderr, "Error: livecd path too long\n");
					usage (EXIT_FAILURE);
				}

				strcpy (cdrom_path, optarg);
				break;

			case 'u':
				/*
				 * Don't unmount or export zpool with done
				 */
				unmount = B_FALSE;
				break;

			case '?':
				/*
				 * We get here if an argument is missing or
				 * the user gave -?
				 */
				if (optopt == '?')
					usage (EXIT_SUCCESS);
				else
					usage (EXIT_FAILURE);
				break;

			default:
				/*
				 * Ignoring opts is bad!
				 */
				abort();
		}
	}

	/*
	 * Fix any given disk paths
	 * TODO: Support for creating mirrored rpools!
	 */
#define DISK_PATH 	"/dev/dsk"
#define RDISK_PATH	"/dev/rdsk"
#define DISK_LEN	8
#define RDISK_LEN	9

	for (i = optind; i < argc; i++)
	{
		if (disk[0] == '\0')
		{
			/*
			 * Disk path is too long
			 */
			if (strlen (disk) >= PATH_MAX)
			{
				fprintf (stderr, "Error: disk path is too long\n");
				usage (EXIT_FAILURE);
			}
			/*
			 * Path is correct already
			 */
			else if (strncmp (RDISK_PATH, argv[i], RDISK_LEN) == 0)
				strcpy (disk, argv[i]);
			/*
			 * Replace /dev/dsk with /dev/rdsk
			 */
			else if (strncmp (DISK_PATH, argv[i], DISK_LEN) == 0)
				sprintf (disk, RDISK_PATH "/%s", argv[i] + DISK_LEN + 1);
			/*
			 * Need to append /dev/rdsk
			 */
			else
				sprintf (disk, RDISK_PATH "/%s", argv[i]);
		}
		else
		{
			fprintf (stderr, "Error: Please specify only one disk\n");
			usage (EXIT_FAILURE);
		}
	}

	if (disk[0] == '\0')
	{
		fprintf (stderr, "Error: No disk specified\n");
		usage (EXIT_FAILURE);
	}

	/*
	 * Ensure that the path to the livecd contents is a directory
	 * and that it can be opened.
	 */
	if ((dir = opendir (cdrom_path)) == NULL)
	{
		fprintf (stderr, "Error: unable to open %s: %s\n", cdrom_path, strerror (errno));
		usage (EXIT_FAILURE);
	}

	(void) closedir (dir);

	/*
	 * Get libzfs handle before outputting anything to stdout/stderr
	 * otherwise we won't be able to get it later (seriously)
	 */
	if ((libzfs_handle = libzfs_init ()) == NULL)
	{
		fprintf (stderr, "Error: Unable to get libzfs handle\n");
		return EXIT_FAILURE;
	}

	/*
	 * Warn the user before touching the disk
	 */
	printf ("All data on %s will be destroyed.  Continue? [yn] ", disk);
	while (scanf ("%c", &c) == 0 || (c != 'y' && c != 'n'))
		printf ("\rContinue? [yn] ");

	if (c == 'n')
	{
		fprintf (stderr, "User aborted format\n");
		return EXIT_FAILURE;
	}

	if (disk_in_use (libzfs_handle, disk) == B_TRUE)
		return EXIT_FAILURE;

	/*
	 * Reformat disk
	 */
	puts ("Reformatting disk...");

	if (create_root_partition (disk) == B_FALSE)
		return EXIT_FAILURE;

	if (create_root_vtoc (disk) == B_FALSE)
		return EXIT_FAILURE;

	/*
	 * Create new ZFS filesystem
	 */
	puts ("Creating new filesystem...");

	if (create_root_pool (libzfs_handle, disk, rpool, temp_mount) == B_FALSE)
		return EXIT_FAILURE;

	if (create_root_datasets (libzfs_handle, rpool) == B_FALSE)
		return EXIT_FAILURE;

	if (set_root_bootfs (libzfs_handle, rpool) == B_FALSE)
		return EXIT_FAILURE;

	/*
	 * Mount new filesystem and copy files
	 */
	puts ("Mounting filesystem...");

	if (mount_root_datasets (libzfs_handle, rpool) == B_FALSE)
		return EXIT_FAILURE;

	printf ("Copying files...\n");

	if (copy_files () == B_FALSE)
		return EXIT_FAILURE;

	if (copy_grub (temp_mount, rpool) == B_FALSE)
		return EXIT_FAILURE;

	/*
	 * Install grub to mbr, create boot archive, etc
	 */
	puts ("Finishing up...");

	if (config_grub (temp_mount, disk) == B_FALSE)
		return EXIT_FAILURE;

	if (config_devfs (temp_mount) == B_FALSE)
		return EXIT_FAILURE;

	if (config_bootadm (temp_mount) == B_FALSE)
		return EXIT_FAILURE;

	/*
	 * Unmount and export new rpool
	 */
	if (unmount == B_TRUE)
	{
		puts ("Unmounting filesystem...");

		if (unmount_root_datasets (libzfs_handle, rpool) == B_FALSE)
			return EXIT_FAILURE;

		if (export_root_pool (libzfs_handle, rpool) == B_FALSE)
			return EXIT_FAILURE;
	}

	(void) libzfs_fini (libzfs_handle);

	puts ("Done.");

	return EXIT_SUCCESS;
}
