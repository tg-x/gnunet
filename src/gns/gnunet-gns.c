/*
     This file is part of GNUnet.
     (C) 2012 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 3, or (at your
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
 * @file gnunet-gns.c
 * @brief command line tool to access distributed GNS
 * @author Christian Grothoff
 *
 * TODO:
 * - everything
 */
#include "platform.h"
#include <gnunet_util_lib.h>
#include <gnunet_dnsparser_lib.h>
#include <gnunet_namestore_service.h>
#include <gnunet_gns_service.h>

/**
 * Handle to GNS service.
 */
static struct GNUNET_GNS_Handle *gns;

/**
 * GNS name to shorten. (-s option)
 */
static char *shorten_name;

/**
 * GNS name to lookup. (-u option)
 */
static char *lookup_name;


/**
 * record type to look up (-t option)
 */
static char *lookup_type;

/**
 * name to look up authority for (-a option)
 */
static char *auth_name;

static enum GNUNET_GNS_RecordType rtype;

/**
 * Task run on shutdown.  Cleans up everything.
 *
 * @param cls unused
 * @param tc scheduler context
 */
static void
do_shutdown (void *cls,
	     const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  if (NULL != gns)
  {
    GNUNET_GNS_disconnect (gns);
    gns = NULL;
  }
}


static void
process_shorten_result(void* cls, const char* nshort)
{
  printf("%s shortened to %s\n", (char*) cls, nshort);
  GNUNET_SCHEDULER_add_now (&do_shutdown, NULL);
}

static void
process_lookup_result(void* cls, uint32_t rd_count,
                      const struct GNUNET_NAMESTORE_RecordData *rd)
{
  int i;
  char* addr;
  char* name = (char*) cls;

  if (rd_count == 0)
    printf("No results.\n");

  for (i=0; i<rd_count; i++)
  {
    if (rd[i].record_type != rtype)
      continue;
    if (rd[i].record_type == GNUNET_GNS_RECORD_TYPE_A)
    {
      addr = inet_ntoa(*((struct in_addr*)rd[i].data));
      printf("Got A record for %s: %s\n", name, addr);
    }
    if (rd[i].record_type == GNUNET_GNS_RECORD_MX)
    {
      printf("Got MX record for %s: %s\n", name, (char*)rd[i].data);
    }

    //FIXME others? maybe to string method for records?
  }

  GNUNET_SCHEDULER_add_now (&do_shutdown, NULL);
}

static void
process_auth_result(void* cls, const char* auth)
{
  printf ("%s\n", auth);
  GNUNET_SCHEDULER_add_now (&do_shutdown, NULL);
}

/**
 * Main function that will be run.
 *
 * @param cls closure
 * @param args remaining command-line arguments
 * @param cfgfile name of the configuration file used (for saving, can be NULL!)
 * @param cfg configuration
 */
static void
run (void *cls, char *const *args, const char *cfgfile,
     const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  gns = GNUNET_GNS_connect (cfg);
  rtype = GNUNET_GNS_RECORD_TYPE_A;

  if (NULL == gns)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		_("Failed to connect to GNS\n"));
    return;
  }
  
  if (shorten_name != NULL)
  {
    /** shorten name */
    GNUNET_GNS_shorten(gns, shorten_name, &process_shorten_result,
                       shorten_name);
  }

  if (lookup_name != NULL)
  {
    GNUNET_GNS_lookup(gns, lookup_name, rtype,
                      &process_lookup_result, lookup_name);
  }

  if (auth_name != NULL)
  {
    GNUNET_GNS_get_authority(gns, auth_name, &process_auth_result, auth_name);
  }
  
  // FIXME: do work here...
  //GNUNET_SCHEDULER_add_now (&do_shutdown, NULL);
}


/**
 * The main function for gnunet-gns.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, 1 on error
 */
int
main (int argc, char *const *argv)
{
  static const struct GNUNET_GETOPT_CommandLineOption options[] = {
    {'s', "shorten", NULL,
     gettext_noop ("try to shorten a given GNS name"), 1,
     &GNUNET_GETOPT_set_string, &shorten_name},
    {'u', "lookup", NULL,
      gettext_noop ("Lookup a record using GNS (NOT IMPLEMENTED)"), 1,
      &GNUNET_GETOPT_set_string, &lookup_name},
    {'a', "authority", NULL,
      gettext_noop ("Get the authority of a particular name"), 1,
      &GNUNET_GETOPT_set_string, &auth_name},
    {'t', "type", NULL,
      gettext_noop ("Specify the type of the record lookup"), 1,
      &GNUNET_GETOPT_set_string, &lookup_type},
    GNUNET_GETOPT_OPTION_END
  };

  int ret;

  GNUNET_log_setup ("gnunet-gns", "WARNING", NULL);
  ret =
      (GNUNET_OK ==
       GNUNET_PROGRAM_run (argc, argv, "gnunet-gns",
                           _("GNUnet GNS access tool"), 
			   options,
                           &run, NULL)) ? 0 : 1;

  return ret;
}

/* end of gnunet-gns.c */
