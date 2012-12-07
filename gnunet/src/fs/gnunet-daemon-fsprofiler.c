/*
     This file is part of GNUnet.
     (C) 2012 Christian Grothoff

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
 * @file fs/gnunet-daemon-fsprofiler.c
 * @brief daemon that publishes and downloads (random) files
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_fs_service.h"
#include "gnunet_statistics_service.h"

/**
 * We use 'patterns' of the form (x,y,t) to specify desired download/publish
 * activities of a peer.  They are stored in a DLL.
 */
struct Pattern
{
  /**
   * Kept in a DLL.
   */
  struct Pattern *next;

  /**
   * Kept in a DLL.
   */
  struct Pattern *prev;

  /**
   * Execution context for the pattern (FS-handle to the operation).
   */
  void *ctx;

  /**
   * Secondary execution context for the pattern (FS-handle to the operation).
   */
  void *sctx;

  /**
   * When did the operation start?
   */
  struct GNUNET_TIME_Absolute start_time;

  /**
   * With how much delay should this operation be started?
   */
  struct GNUNET_TIME_Relative delay;

  /**
   * Task to run the operation.
   */
  GNUNET_SCHEDULER_TaskIdentifier task;

  /**
   * X-value.
   */
  unsigned long long x;

  /**
   * Y-value.
   */
  unsigned long long y;
};


/**
 * Return value from 'main'.
 */
static int global_ret;

/**
 * Configuration we use.
 */
static const struct GNUNET_CONFIGURATION_Handle *cfg;

/**
 * Handle to the statistics service.
 */
static struct GNUNET_STATISTICS_Handle *stats_handle;

/**
 * Peer's FS handle.
 */
static struct GNUNET_FS_Handle *fs_handle;

/**
 * Unique number for this peer in the testbed.
 */
static unsigned long long my_peerid;

/**
 * Desired anonymity level.
 */
static unsigned long long anonymity_level;

/**
 * Desired replication level.
 */
static unsigned long long replication_level;

/**
 * String describing which publishing operations this peer should
 * perform.  The format is "(SIZE,SEED,TIME)*", for example:
 * "(1,5,0)(7,3,13)" means to publish a file with 1 byte and
 * seed/keyword 5 immediately and another file with 7 bytes and
 * seed/keyword 3 after 13 ms.
 */
static char *publish_pattern;

/**
 * Head of the DLL of publish patterns.
 */
static struct Pattern *publish_head;

/**
 * Tail of the DLL of publish patterns.
 */
static struct Pattern *publish_tail;

/**
 * String describing which download operations this peer should
 * perform. The format is "(KEYWORD,SIZE,DELAY)*"; for example,
 * "(1,7,3)(3,8,8)" means to download one file of 7 bytes under
 * keyword "1" starting the search after 3 ms; and another one of 8
 * bytes under keyword '3' starting after 8 ms.  The file size is
 * used to determine which search result(s) should be used or ignored.
 */
static char *download_pattern;

/**
 * Head of the DLL of publish patterns.
 */
static struct Pattern *download_head;

/**
 * Tail of the DLL of publish patterns.
 */
static struct Pattern *download_tail;


/**
 * Parse a pattern string and store the corresponding 
 * 'struct Pattern' in the given head/tail.
 *
 * @param head where to store the head
 * @param tail where to store the tail
 * @param pattern pattern to parse
 * @return GNUNET_OK on success
 */
static int
parse_pattern (struct Pattern **head,
	       struct Pattern **tail,
	       const char *pattern)
{
  struct Pattern *p;
  unsigned long long x;
  unsigned long long y;
  unsigned long long t;

  while (3 == sscanf (pattern,
		      "(%llu,%llu,%llu)",
		      &x, &y, &t))
  {
    p = GNUNET_malloc (sizeof (struct Pattern));
    p->x = x;
    p->y = y;
    p->delay.rel_value = (uint64_t) t;
    GNUNET_CONTAINER_DLL_insert (*head, *tail, p);
    pattern = strstr (pattern, ")");
    GNUNET_assert (NULL != pattern);
    pattern++;
  }
  return (0 == strlen (pattern)) ? GNUNET_OK : GNUNET_SYSERR;
}


/**
 * Create a file of the given length with a deterministic amount
 * of data to be published under keyword 'kval'.
 *
 * @param length number of bytes in the file
 * @param kval keyword value and seed for the data of the file
 * @param ctx context to pass to 'fi'
 * @return file information handle for the file
 */
static struct GNUNET_FS_FileInformation *
make_file (uint64_t length,
	   uint64_t kval,
	   void *ctx)
{
  struct GNUNET_FS_FileInformation *fi;
  struct GNUNET_FS_BlockOptions bo;
  char *data;
  struct GNUNET_FS_Uri *keywords;
  char kw[128];
  unsigned long long i;
  uint64_t xor;

  data = NULL; /* to make compilers happy */
  if ( (0 != length) &&
       (NULL == (data = GNUNET_malloc_large ((size_t) length))) )
      return NULL;
  /* initialize data with 'unique' data only depending on 'kval' and 'size',
     making sure that blocks do not repeat */
  for (i=0;i<length; i+=8)  
  {
    xor = length ^ kval ^ (uint64_t) (i / 32 / 1024);
    memcpy (&data[i], &xor, GNUNET_MIN (length - i, sizeof (uint64_t)));
  }
  bo.expiration_time = GNUNET_TIME_relative_to_absolute (GNUNET_TIME_UNIT_DAYS);
  bo.anonymity_level = (uint32_t) anonymity_level;
  bo.content_priority = 128;
  bo.replication_level = (uint32_t) replication_level;
  GNUNET_snprintf (kw, sizeof (kw),
		   "%llu", (unsigned long long) kval);
  keywords = GNUNET_FS_uri_ksk_create (kw, NULL);
  fi = GNUNET_FS_file_information_create_from_data (fs_handle,
						    ctx,
						    length,
						    data, keywords,
						    NULL, GNUNET_NO, &bo);
  GNUNET_FS_uri_destroy (keywords);
  return fi;
}


/**
 * Task run during shutdown.
 *
 * @param cls unused
 * @param tc unused
 */
static void
shutdown_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct Pattern *p;

  while (NULL != (p = publish_head))
  {
    if (GNUNET_SCHEDULER_NO_TASK != p->task)
      GNUNET_SCHEDULER_cancel (p->task);
    if (NULL != p->ctx)
      GNUNET_FS_publish_stop (p->ctx);
    GNUNET_CONTAINER_DLL_remove (publish_head, publish_tail, p);
    GNUNET_free (p);
  }
  while (NULL != (p = download_head))
  {
    if (GNUNET_SCHEDULER_NO_TASK != p->task)
      GNUNET_SCHEDULER_cancel (p->task);
    if (NULL != p->ctx)
      GNUNET_FS_download_stop (p->ctx, GNUNET_YES);
    if (NULL != p->sctx)
      GNUNET_FS_search_stop (p->sctx);
    GNUNET_CONTAINER_DLL_remove (download_head, download_tail, p);
    GNUNET_free (p);
  }
  if (NULL != fs_handle)
  {
    GNUNET_FS_stop (fs_handle);
    fs_handle = NULL;
  }
  if (NULL != stats_handle)
  {
    GNUNET_STATISTICS_destroy (stats_handle, GNUNET_YES);
    stats_handle = NULL;
  }
}


/**
 * Notification of FS to a client about the progress of an
 * operation.  Callbacks of this type will be used for uploads,
 * downloads and searches.  Some of the arguments depend a bit
 * in their meaning on the context in which the callback is used.
 *
 * @param cls closure
 * @param info details about the event, specifying the event type
 *        and various bits about the event
 * @return client-context (for the next progress call
 *         for this operation; should be set to NULL for
 *         SUSPEND and STOPPED events).  The value returned
 *         will be passed to future callbacks in the respective
 *         field in the GNUNET_FS_ProgressInfo struct.
 */
static void *
progress_cb (void *cls,
	     const struct GNUNET_FS_ProgressInfo *info)
{
  // FIXME:
  // - search result => start download
  // - publishing done => staitstic, terminate 'struct Pattern'
  // - download done => statistic, terminate 'struct Pattern'
  // => all patterns done => then what!? (how do we tell the
  //    drive that we are done!?)
  return NULL;
}


/**
 * Start publish operation.
 *
 * @param cls the 'struct Pattern' specifying the operation to perform
 * @param tc scheduler context
 */
static void 
start_publish (void *cls,
		const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct Pattern *p = cls;
  struct GNUNET_FS_FileInformation *fi;

  p->task = GNUNET_SCHEDULER_NO_TASK;
  if (0 != (tc->reason & GNUNET_SCHEDULER_REASON_SHUTDOWN))
    return;
  fi = make_file (p->x, p->y, p);
  p->ctx = GNUNET_FS_publish_start (fs_handle,
				    fi,
				    NULL, NULL, NULL,
				    GNUNET_FS_PUBLISH_OPTION_NONE);
}


/**
 * Start download operation.
 *
 * @param cls the 'struct Pattern' specifying the operation to perform
 * @param tc scheduler context
 */
static void 
start_download (void *cls,
		const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct Pattern *p = cls;

  p->task = GNUNET_SCHEDULER_NO_TASK;
  if (0 != (tc->reason & GNUNET_SCHEDULER_REASON_SHUTDOWN))
    return;
  // FIXME: start search operation
}


/**
 * @brief Main function that will be run by the scheduler.
 *
 * @param cls closure
 * @param args remaining command-line arguments
 * @param cfgfile name of the configuration file used (for saving, can be NULL!)
 * @param cfg_ configuration
 */
static void
run (void *cls, char *const *args GNUNET_UNUSED,
     const char *cfgfile GNUNET_UNUSED,
     const struct GNUNET_CONFIGURATION_Handle *cfg_)
{
  char myoptname[128];
  struct Pattern *p;

  cfg = cfg_;
  /* Scheduled the task to clean up when shutdown is called */
  GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_FOREVER_REL, &shutdown_task,
                                NULL);

  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_number (cfg, 
					     "TESTBED", "PEERID",
                                             &my_peerid))
  {
    GNUNET_log_config_missing (GNUNET_ERROR_TYPE_ERROR,
			       "TESTBED", "PEERID");
    global_ret = GNUNET_SYSERR;
    GNUNET_SCHEDULER_shutdown ();
    return;
  }
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_number (cfg, 
					     "FSPROFILER", "ANONYMITY_LEVEL",
                                             &anonymity_level))
    anonymity_level = 1;
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_number (cfg, 
					     "FSPROFILER", "REPLICATION_LEVEL",
                                             &replication_level))   
    replication_level = 1;
  GNUNET_snprintf (myoptname, sizeof (myoptname),
		   "DOWNLOAD-PATTERN-%u", my_peerid);
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_string (cfg, 
					     "FSPROFILER", myoptname,
                                             &download_pattern))   
    download_pattern = GNUNET_strdup ("");
  GNUNET_snprintf (myoptname, sizeof (myoptname),
		   "PUBLISH-PATTERN-%u", my_peerid);
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_string (cfg, 
					     "FSPROFILER", myoptname,
                                             &publish_pattern))   
    publish_pattern = GNUNET_strdup ("");
  if ( (GNUNET_OK !=
	parse_pattern (&download_head,
		       &download_tail,
		       download_pattern)) ||
       (GNUNET_OK !=
	parse_pattern (&publish_head,
		       &publish_tail,
		       publish_pattern)) )
  {
    GNUNET_SCHEDULER_shutdown ();
    return;
  }

  stats_handle = GNUNET_STATISTICS_create ("fsprofiler", cfg);
  fs_handle =
    GNUNET_FS_start (cfg,
		     "fsprofiler",
		     &progress_cb, NULL,
		     GNUNET_FS_FLAGS_NONE,
		     GNUNET_FS_OPTIONS_DOWNLOAD_PARALLELISM, 1,
		     GNUNET_FS_OPTIONS_REQUEST_PARALLELISM, 1,
		     GNUNET_FS_OPTIONS_END);
  if (NULL == fs_handle)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Could not acquire FS handle. Exiting.\n");
    global_ret = GNUNET_SYSERR;
    GNUNET_SCHEDULER_shutdown ();
    return;
  }
  for (p = publish_head; NULL != p; p = p->next)
    p->task = GNUNET_SCHEDULER_add_delayed (p->delay,
					    &start_publish, p);
  for (p = download_head; NULL != p; p = p->next)
    p->task = GNUNET_SCHEDULER_add_delayed (p->delay,
					    &start_download, p);
}


/**
 * Program that performs various "random" FS activities.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, 1 on error
 */
int
main (int argc, char *const *argv)
{
  static const struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_OPTION_END
  };

  if (GNUNET_OK != GNUNET_STRINGS_get_utf8_args (argc, argv, &argc, &argv))
    return 2;
  return (GNUNET_OK ==
          GNUNET_PROGRAM_run (argc, argv, "gnunet-daemon-fsprofiler",
                              gettext_noop
                              ("Daemon to use file-sharing to measure its performance."),
                              options, &run, NULL)) ? global_ret : 1;
}

/* end of gnunet-daemon-fsprofiler.c */
