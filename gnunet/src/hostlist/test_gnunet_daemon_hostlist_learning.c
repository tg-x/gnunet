/*
     This file is part of GNUnet
     (C) 2009 Christian Grothoff (and other contributing authors)

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
 * @file hostlist/test_gnunet_daemon_hostlist.c
 * @brief test for gnunet_daemon_hostslist.c
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_arm_service.h"
#include "gnunet_core_service.h"
#include "gnunet_transport_service.h"
#include "gnunet_resolver_service.h"

#define VERBOSE GNUNET_YES

#define START_ARM GNUNET_YES
#define MAX_URL_LEN 1000

/**
 * How long until wait until testcases fails
 */
#define TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 20)
#define CHECK_INTERVALL GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 2)

static int timeout;
static int adv_arrived;

static struct GNUNET_SCHEDULER_Handle *sched;

static GNUNET_SCHEDULER_TaskIdentifier timeout_task;
static GNUNET_SCHEDULER_TaskIdentifier check_task;
    
struct PeerContext
{
  struct GNUNET_CONFIGURATION_Handle *cfg;
  struct GNUNET_TRANSPORT_Handle *th;
  struct GNUNET_MessageHeader *hello;
  struct GNUNET_ARM_Handle *arm;
  struct GNUNET_CORE_Handle *core;
#if START_ARM
  pid_t arm_pid;
#endif
};

static struct PeerContext adv_peer;

static struct PeerContext learn_peer;

static void
clean_up (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  if (adv_peer.th != NULL)
  {
    GNUNET_TRANSPORT_disconnect (adv_peer.th);
    adv_peer.th = NULL;
  }
  if (learn_peer.th != NULL)
  {
    GNUNET_TRANSPORT_disconnect (learn_peer.th);
    learn_peer.th = NULL;
  }
  if (adv_peer.core != NULL)
  {
    GNUNET_CORE_disconnect (adv_peer.core);
    adv_peer.core = NULL;
  }
  if (learn_peer.core != NULL)
  {
    GNUNET_CORE_disconnect (learn_peer.core);
    learn_peer.core = NULL;
  }
  GNUNET_SCHEDULER_shutdown (sched);
}

static void shutdown_testcase()
{
  if (timeout_task != GNUNET_SCHEDULER_NO_TASK)
  {
    GNUNET_SCHEDULER_cancel (sched,
                             timeout_task);
    timeout_task = GNUNET_SCHEDULER_NO_TASK;
  }
  if (check_task != GNUNET_SCHEDULER_NO_TASK)
  {
    GNUNET_SCHEDULER_cancel (sched,
        check_task);
    check_task = GNUNET_SCHEDULER_NO_TASK;
  }
  GNUNET_SCHEDULER_add_now (sched,
                            &clean_up, NULL);
}

/**
 * Timeout, give up.
 */
static void
timeout_error (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  timeout_task = GNUNET_SCHEDULER_NO_TASK;
  GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
              "Timeout while executing testcase, test failed.\n");
  timeout = GNUNET_YES;
  clean_up (NULL, tc);
}

/**
 * Check the server statistics regularly
 */
static void
check_statistics (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Checking server stats...\n");
  check_task = GNUNET_SCHEDULER_add_delayed (sched,
                                CHECK_INTERVALL,
                                &check_statistics,
                                NULL);
}

/**
 * Core handler for p2p hostlist advertisements
 */
static int ad_arrive_handler (void *cls,
                             const struct GNUNET_PeerIdentity * peer,
                             const struct GNUNET_MessageHeader * message,
                             struct GNUNET_TIME_Relative latency,
                             uint32_t distance)
{
  char *hostname;
  char *expected_uri = GNUNET_malloc (MAX_URL_LEN);
  char *recv_uri;

  unsigned long long port;
  size_t size;
  const struct GNUNET_MessageHeader * incoming;

  if (-1 == GNUNET_CONFIGURATION_get_value_number (adv_peer.cfg,
                                                   "HOSTLIST",
                                                   "HTTPPORT",
                                                   &port))
    {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Could not read advertising server's configuration\n" );
    return GNUNET_SYSERR;
    }
  hostname = GNUNET_RESOLVER_local_hostname_get ();
  if (NULL != hostname)
    {
      size = strlen (hostname);
      if (size + 15 > MAX_URL_LEN)
        {
          GNUNET_break (0);
        }
      else
        {
          GNUNET_asprintf (&expected_uri,
                           "http://%s:%u/",
                           hostname,
                           (unsigned int) port);
        }
    }

  incoming = (const struct GNUNET_MessageHeader *) message;
  recv_uri = (char*) &incoming[1];
  if ( 0 == strcmp( expected_uri, recv_uri ) )
  {
    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                "Recieved hostlist advertisement with URI `%s'as expected\n", recv_uri);
    adv_arrived = GNUNET_YES;
  }
  else
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Expected URI `%s' and recieved URI `%s' differ\n", expected_uri, recv_uri);
  GNUNET_free ( expected_uri );
  GNUNET_free ( hostname );
  shutdown_testcase();
  return GNUNET_OK;
}

/**
 * List of handlers if we are learning.
 */
static struct GNUNET_CORE_MessageHandler learn_handlers[] = {
  { &ad_arrive_handler, GNUNET_MESSAGE_TYPE_HOSTLIST_ADVERTISEMENT, 0},
  { NULL, 0, 0 }
};

static void
setup_learn_peer (struct PeerContext *p, const char *cfgname)
{
  char * filename;
  unsigned int result;
  p->cfg = GNUNET_CONFIGURATION_create ();
#if START_ARM
  p->arm_pid = GNUNET_OS_start_process (NULL, NULL, "gnunet-service-arm",
                                        "gnunet-service-arm",
#if VERBOSE
                                        "-L", "DEBUG",
#endif
                                        "-c", cfgname, NULL);
#endif
  GNUNET_assert (GNUNET_OK == GNUNET_CONFIGURATION_load (p->cfg, cfgname));
  if (GNUNET_OK == GNUNET_CONFIGURATION_get_value_string (p->cfg,
                                                          "HOSTLIST",
                                                          "HOSTLISTFILE",
                                                          &filename))
  {
  if ( GNUNET_YES == GNUNET_DISK_file_test (filename) )
    {
      result = remove (filename);
      if (result == 0)
      GNUNET_log (GNUNET_ERROR_TYPE_INFO,
            _("Hostlist hostlist file `%s' was removed\n"),filename);
    }
  }
  GNUNET_free ( filename );
  GNUNET_ARM_start_services (p->cfg, sched, "core", NULL);

  p->core = GNUNET_CORE_connect (sched, p->cfg,
                              GNUNET_TIME_UNIT_FOREVER_REL,
                              NULL,
                              NULL,
                              NULL, NULL,
                              NULL, GNUNET_NO,
                              NULL, GNUNET_NO,
                              learn_handlers );
  GNUNET_assert ( NULL != p->core );
}


static void
setup_adv_peer (struct PeerContext *p, const char *cfgname)
{
  p->cfg = GNUNET_CONFIGURATION_create ();
#if START_ARM
  p->arm_pid = GNUNET_OS_start_process (NULL, NULL, "gnunet-service-arm",
                                        "gnunet-service-arm",
#if VERBOSE
                                        "-L", "DEBUG",
#endif
                                        "-c", cfgname, NULL);
#endif
  GNUNET_assert (GNUNET_OK == GNUNET_CONFIGURATION_load (p->cfg, cfgname));
  GNUNET_ARM_start_services (p->cfg, sched, "core", NULL);
}



static void
waitpid_task (void *cls, 
              const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct PeerContext *p = cls;

#if START_ARM 
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Killing ARM process.\n");
  if (0 != PLIBC_KILL (p->arm_pid, SIGTERM))
    GNUNET_log_strerror (GNUNET_ERROR_TYPE_WARNING, "kill");
  if (GNUNET_OS_process_wait(p->arm_pid) != GNUNET_OK)
    GNUNET_log_strerror (GNUNET_ERROR_TYPE_WARNING, "waitpid");
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "ARM process %u stopped\n", p->arm_pid);
#endif
  GNUNET_CONFIGURATION_destroy (p->cfg);
}


static void
stop_cb (void *cls, 
         int success)
{
  struct PeerContext *p = cls;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              success
              ? "ARM stopped core service\n"
              : "ARM failed to stop core service\n");
  GNUNET_ARM_disconnect (p->arm);
  p->arm = NULL;
  /* make sure this runs after all other tasks are done */
  GNUNET_SCHEDULER_add_delayed (sched,
                                GNUNET_TIME_UNIT_SECONDS,
                                &waitpid_task, p);
}


static void
stop_arm (struct PeerContext *p)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Asking ARM to stop core service\n");
  p->arm = GNUNET_ARM_connect (p->cfg, sched, NULL);
  GNUNET_ARM_stop_service (p->arm, "core", GNUNET_TIME_UNIT_SECONDS,
                           &stop_cb, p);
}


/**
 * Try again to connect to transport service.
 */
static void
shutdown_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  stop_arm (&adv_peer);
  stop_arm (&learn_peer);
}


static void
run (void *cls,
     struct GNUNET_SCHEDULER_Handle *s,
     char *const *args,
     const char *cfgfile, 
     const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  timeout = GNUNET_NO;
  adv_arrived = GNUNET_NO;
  sched = s;
  timeout_task = GNUNET_SCHEDULER_add_delayed (sched,
                                               TIMEOUT,
                                               &timeout_error,
                                               NULL);
  check_task = GNUNET_SCHEDULER_add_delayed (sched,
                                CHECK_INTERVALL,
                                &check_statistics,
                                NULL);
  GNUNET_SCHEDULER_add_delayed (sched,
                                GNUNET_TIME_UNIT_FOREVER_REL,
                                &shutdown_task,
                                NULL);
  setup_adv_peer (&adv_peer, "test_learning_adv_peer.conf");
  setup_learn_peer (&learn_peer, "test_learning_learn_peer.conf");
}


static int
check ()
{
  char *const argv[] = { "test-gnunet-daemon-hostlist",
    "-c", "learning_data.conf",
#if VERBOSE
    "-L", "DEBUG",
#endif
    NULL
  };
  struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_OPTION_END
  };

  GNUNET_PROGRAM_run ((sizeof (argv) / sizeof (char *)) - 1,
                      argv, "test-gnunet-daemon-hostlist",
                      "nohelp", options, &run, NULL);

  if ( (timeout == GNUNET_YES) || (adv_arrived == GNUNET_NO))
    return GNUNET_YES;
  else
    return GNUNET_NO;
}

int
main (int argc, char *argv[])
{
  
  int ret;

  GNUNET_DISK_directory_remove ("/tmp/test-gnunetd-hostlist-peer-1");
  GNUNET_DISK_directory_remove ("/tmp/test-gnunetd-hostlist-peer-2");
  GNUNET_log_setup ("test-gnunet-daemon-hostlist",
#if VERBOSE
                    "DEBUG",
#else
                    "WARNING",
#endif
                    NULL);
  ret = check ();
  GNUNET_DISK_directory_remove ("/tmp/test-gnunetd-hostlist-peer-1");
  GNUNET_DISK_directory_remove ("/tmp/test-gnunetd-hostlist-peer-2");
  return ret; 
}

/* end of test_gnunet_daemon_hostlist.c */
