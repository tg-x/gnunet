/*
     This file is part of GNUnet.
     (C) 2001, 2002, 2004, 2005, 2006, 2007, 2009 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/
/**
 * @file fs/gnunet-directory.c
 * @brief display content of GNUnet directories
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_fs_service.h"

static int ret;

/**
 * Print a meta data entry.
 *
 * @param cls closure (unused)
 * @param type type of the keyword
 * @param data value of the keyword
 */
static int
item_printer (void *cls,
	      EXTRACTOR_KeywordType type, 
	      const char *data)
{
  printf ("\t%20s: %s\n",
          dgettext (LIBEXTRACTOR_GETTEXT_DOMAIN,
                    EXTRACTOR_getKeywordTypeAsString (type)), data);
  return GNUNET_OK;
}



/**
 * Print an entry in a directory.
 *
 * @param cls closure (not used)
 * @param filename name of the file in the directory
 * @param uri URI of the file
 * @param meta metadata for the file; metadata for
 *        the directory if everything else is NULL/zero
 * @param length length of the available data for the file
 *           (of type size_t since data must certainly fit
 *            into memory; if files are larger than size_t
 *            permits, then they will certainly not be
 *            embedded with the directory itself).
 * @param data data available for the file (length bytes)
 */
static void
print_entry (void *cls,
	     const char *filename,
	     const struct GNUNET_FS_Uri *uri,
	     const struct GNUNET_CONTAINER_MetaData *meta,
	     size_t length,
	     const void *data)
{
  char *string;

  string = GNUNET_FS_uri_to_string (uri);
  printf ("%s:\n", string);
  GNUNET_free (string);
  GNUNET_CONTAINER_meta_data_get_contents (meta,
					   &item_printer,
					   NULL);
}


/**
 * Main function that will be run by the scheduler.
 *
 * @param cls closure
 * @param sched the scheduler to use
 * @param args remaining command-line arguments
 * @param cfgfile name of the configuration file used (for saving, can be NULL!)
 * @param cfg configuration
 */
static void
run (void *cls,
     struct GNUNET_SCHEDULER_Handle *sched,
     char *const *args,
     const char *cfgfile,
     const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  struct GNUNET_DISK_MapHandle *map;
  struct GNUNET_DISK_FileHandle *h;
  void *data;
  size_t len;
  uint64_t size;
  const char *filename = args[0];

  if (NULL == filename)
    {
      fprintf (stderr,
	       _("You must specify a filename to inspect."));
      ret = 1;
      return;
    }
  if ( (GNUNET_OK !=
	GNUNET_DISK_file_size (filename,
			       &size,
			       GNUNET_YES)) ||
       (NULL == (h = GNUNET_DISK_file_open (filename,
					    GNUNET_DISK_OPEN_READ))) )
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		  _("Failed to read directory `%s'\n"),
		  filename);
      ret = 1;
      return;
    }
  len = (size_t) size;
  data = GNUNET_DISK_file_map (h,
			       &map,
			       GNUNET_DISK_MAP_TYPE_READ,
			       len);
  GNUNET_assert (NULL != data);
  GNUNET_FS_directory_list_contents (len,
				     data,
				     0, 
				     &print_entry,
				     NULL);
  GNUNET_DISK_file_unmap (map);
  GNUNET_DISK_file_close (h);
}


/**
 * gnunet-directory command line options
 */
static struct GNUNET_GETOPT_CommandLineOption options[] = {
  // FIXME: options!
  GNUNET_GETOPT_OPTION_END
};


/**
 * The main function to inspect GNUnet directories.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, 1 on error
 */
int
main (int argc, char *const *argv)
{
  return (GNUNET_OK ==
          GNUNET_PROGRAM_run (argc,
                              argv,
                              "gnunet-directory",
                              gettext_noop
                              ("Display GNUnet directories."),
                              options, &run, NULL)) ? ret : 1;
}

/* end of gnunet-directory.c */
