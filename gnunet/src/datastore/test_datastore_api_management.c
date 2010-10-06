/*
     This file is part of GNUnet.
     (C) 2004, 2005, 2006, 2007, 2009 Christian Grothoff (and other contributing authors)

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
/*
 * @file datastore/test_datastore_api_management.c
 * @brief Test for the space management functions of the datastore implementation.
 * @author Christian Grothoff
 */

#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_protocols.h"
#include "gnunet_datastore_service.h"

#define VERBOSE GNUNET_NO

/**
 * How long until we give up on transmitting the message?
 */
#define TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 60)

/**
 * Number of iterations to run; must be large enough
 * so that the quota will be exceeded!
 */
#define ITERATIONS 5000

static struct GNUNET_DATASTORE_Handle *datastore;

static struct GNUNET_TIME_Absolute now;

static int ok;

static const char* plugin_name;

static size_t
get_size (int i)
{
  return 8 + 8 * (i % 256);
}


static const void *
get_data (int i)
{
  static char buf[60000]; 
  memset (buf, i, 8 + 8 * (i % 256));
  return buf;
}


static int
get_type(int i)
{
  return 1;
}


static int 
get_priority (int i)
{
  return i+1;
}


static int
get_anonymity(int i)
{
  return i;
}


static struct GNUNET_TIME_Absolute 
get_expiration (int i)
{
  struct GNUNET_TIME_Absolute av;

  av.value = now.value + i * 1000;
  return av;
}

enum RunPhase
  {
    RP_DONE = 0,
    RP_PUT,
    RP_GET,
    RP_GET_FAIL
  };


struct CpsRunContext
{
  GNUNET_HashCode key;
  int i;
  int found;
  struct GNUNET_SCHEDULER_Handle *sched;
  const struct GNUNET_CONFIGURATION_Handle *cfg;
  void *data;
  enum RunPhase phase;
};


static void
run_continuation (void *cls,
		  const struct GNUNET_SCHEDULER_TaskContext *tc);


static void
check_success (void *cls,
	       int success,
	       const char *msg)
{
  struct CpsRunContext *crc = cls;
  if (GNUNET_OK != success)
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		"%s\n", msg);
  GNUNET_assert (GNUNET_OK == success);
  GNUNET_free_non_null (crc->data);
  crc->data = NULL;
  GNUNET_SCHEDULER_add_continuation (crc->sched,
				     &run_continuation,
				     crc,
				     GNUNET_SCHEDULER_REASON_PREREQ_DONE);
}


static void 
check_value (void *cls,
	     const GNUNET_HashCode * key,
	     size_t size,
	     const void *data,
	     enum GNUNET_BLOCK_Type type,
	     uint32_t priority,
	     uint32_t anonymity,
	     struct GNUNET_TIME_Absolute
	     expiration, uint64_t uid)
{
  struct CpsRunContext *crc = cls;
  int i;

  if (key == NULL)
    {
      crc->i--;
      if (crc->found == GNUNET_YES)
	{
	  crc->phase = RP_GET;
	  crc->found = GNUNET_NO;
	}
      else
	{
	  fprintf (stderr,
		   "First not found was %u\n", crc->i);
	  crc->phase = RP_GET_FAIL;
	}
      if (0 == crc->i)
	crc->phase = RP_DONE;
      GNUNET_SCHEDULER_add_continuation (crc->sched,
					 &run_continuation,
					 crc,
					 GNUNET_SCHEDULER_REASON_PREREQ_DONE);
      return;
    }
  i = crc->i;
  crc->found = GNUNET_YES;
  GNUNET_assert (size == get_size (i));
  GNUNET_assert (0 == memcmp (data, get_data(i), size));
  GNUNET_assert (type == get_type (i));
  GNUNET_assert (priority == get_priority (i));
  GNUNET_assert (anonymity == get_anonymity(i));
  GNUNET_assert (expiration.value == get_expiration(i).value);
  GNUNET_DATASTORE_get_next (datastore, GNUNET_YES);
}


static void 
check_nothing (void *cls,
	       const GNUNET_HashCode * key,
	       size_t size,
	       const void *data,
	       enum GNUNET_BLOCK_Type type,
	       uint32_t priority,
	       uint32_t anonymity,
	       struct GNUNET_TIME_Absolute
	       expiration, uint64_t uid)
{
  struct CpsRunContext *crc = cls;

  GNUNET_assert (key == NULL);
  if (0 == --crc->i)
    crc->phase = RP_DONE;
  GNUNET_SCHEDULER_add_continuation (crc->sched,
				     &run_continuation,
				     crc,
				     GNUNET_SCHEDULER_REASON_PREREQ_DONE);
}


static void
run_continuation (void *cls,
		  const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct CpsRunContext *crc = cls;
  ok = (int) crc->phase;
  switch (crc->phase)
    {
    case RP_PUT:
#if VERBOSE
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "Executing `%s' number %u\n",
		  "PUT",
		  crc->i);
#endif
      GNUNET_CRYPTO_hash (&crc->i, sizeof (int), &crc->key);
      GNUNET_DATASTORE_put (datastore,
			    0,
			    &crc->key,
			    get_size (crc->i),
			    get_data (crc->i),
			    get_type (crc->i),
			    get_priority (crc->i),
			    get_anonymity (crc->i),
			    get_expiration (crc->i),
			    1, 1, TIMEOUT,
			    &check_success,
			    crc);
      crc->i++;
      if (crc->i == ITERATIONS)
	{
	  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
		      "Sleeping to give datastore time to clean up\n");
	  sleep (5);
	  crc->phase = RP_GET;
	  crc->i--;
	}
      break;
    case RP_GET:
#if VERBOSE
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "Executing `%s' number %u\n",
		  "GET",
		  crc->i);
#endif
      GNUNET_CRYPTO_hash (&crc->i, sizeof (int), &crc->key);
      GNUNET_DATASTORE_get (datastore, 
			    &crc->key,
			    get_type (crc->i),
			    1, 1, TIMEOUT,
			    &check_value,
			    crc);
      break;
    case RP_GET_FAIL:
#if VERBOSE
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "Executing `%s' number %u\n",
		  "GET",
		  crc->i);
#endif
      GNUNET_CRYPTO_hash (&crc->i, sizeof (int), &crc->key);
      GNUNET_DATASTORE_get (datastore, 
			    &crc->key,
			    get_type (crc->i),
			    1, 1, TIMEOUT,
			    &check_nothing,
			    crc);
      break;
    case RP_DONE:
      GNUNET_assert (0 == crc->i);
#if VERBOSE
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "Finished, disconnecting\n");
#endif
      GNUNET_DATASTORE_disconnect (datastore, GNUNET_YES);
      GNUNET_free (crc);
      ok = 0;
    }
}


static void
run_tests (void *cls,
	   int success,
	   const char *msg)
{
  struct CpsRunContext *crc = cls;

  if (success != GNUNET_YES)
    {
      fprintf (stderr,
	       "Test 'put' operation failed with error `%s' database likely not setup, skipping test.",
	       msg);
      GNUNET_free (crc);
      return;
    }
  GNUNET_SCHEDULER_add_continuation (crc->sched,
				     &run_continuation,
				     crc,
				     GNUNET_SCHEDULER_REASON_PREREQ_DONE);
}


static void
run (void *cls,
     struct GNUNET_SCHEDULER_Handle *sched,
     char *const *args,
     const char *cfgfile,
     const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  struct CpsRunContext *crc;
  static GNUNET_HashCode zkey;

  crc = GNUNET_malloc(sizeof(struct CpsRunContext));
  crc->sched = sched;
  crc->cfg = cfg;
  crc->phase = RP_PUT;
  now = GNUNET_TIME_absolute_get ();
  datastore = GNUNET_DATASTORE_connect (cfg, sched);
  if (NULL ==
      GNUNET_DATASTORE_put (datastore, 0,
			    &zkey, 4, "TEST",
			    GNUNET_BLOCK_TYPE_TEST,
			    0, 0, GNUNET_TIME_relative_to_absolute (GNUNET_TIME_UNIT_SECONDS),
			    0, 1, GNUNET_TIME_UNIT_MINUTES,
			    &run_tests, crc))
    {
      fprintf (stderr,
	       "Test 'put' operation failed.\n");
      GNUNET_free (crc);
      ok = 1;
    }
}



static int
check ()
{
  pid_t pid;
  char cfg_name[128];
  char *const argv[] = { 
    "test-datastore-api-management",
    "-c",
    cfg_name,
#if VERBOSE
    "-L", "DEBUG",
#endif
    NULL
  };
  struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_OPTION_END
  };
  GNUNET_snprintf (cfg_name,
		   sizeof (cfg_name),
		   "test_datastore_api_data_%s.conf",
		   plugin_name);
  pid = GNUNET_OS_start_process (NULL, NULL, "gnunet-service-arm",
                                 "gnunet-service-arm",
#if VERBOSE
                                 "-L", "DEBUG",
#endif
                                 "-c", cfg_name, NULL);
  GNUNET_PROGRAM_run ((sizeof (argv) / sizeof (char *)) - 1,
                      argv, "test-datastore-api", "nohelp",
                      options, &run, NULL);
  if (0 != PLIBC_KILL (pid, SIGTERM))
    {
      GNUNET_log_strerror (GNUNET_ERROR_TYPE_WARNING, "kill");
      ok = 1;
    }
  GNUNET_OS_process_wait(pid);
  if (ok != 0)
    fprintf (stderr, "Missed some testcases: %u\n", ok);
  return ok;
}

int
main (int argc, char *argv[])
{
  int ret;
  
  const char *pos;
  char dir_name[128];

  /* determine name of plugin to use */
  plugin_name = argv[0];
  while (NULL != (pos = strstr(plugin_name, "_")))
    plugin_name = pos+1;

  GNUNET_snprintf (dir_name,
		   sizeof (dir_name),
		   "/tmp/test-gnunet-datastore-%s",
		   plugin_name);
  GNUNET_DISK_directory_remove (dir_name);
  GNUNET_log_setup ("test-datastore-api-management",
#if VERBOSE
                    "DEBUG",
#else
                    "WARNING",
#endif
                    NULL);
  ret = check ();
  GNUNET_DISK_directory_remove (dir_name);
  return ret;
}

/* end of test_datastore_api_management.c */
