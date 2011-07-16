/*
     This file is part of GNUnet.
     (C) 2009 Christian Grothoff (and other contributing authors)

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
 * @file nse/test_nse_multipeer.c
 *
 * @brief Testcase for the network size estimation service.  Starts
 *        a peergroup with a given number of peers, then waits to
 *        receive size estimates from each peer.  Expects to wait
 *        for one message from each peer.
 */
#include "platform.h"
#include "gnunet_testing_lib.h"
#include "gnunet_nse_service.h"

#define VERBOSE GNUNET_NO

#define NUM_PEERS 4

struct NSEPeer
{
  struct NSEPeer *prev;

  struct NSEPeer *next;

  struct GNUNET_TESTING_Daemon *daemon;

  struct GNUNET_NSE_Handle *nse_handle;
};

struct NSEPeer *peer_head;

struct NSEPeer *peer_tail;

/**
 * How long until we give up on connecting the peers?
 */
#define TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 1500)

static int ok;

static int peers_left;

static unsigned int num_peers;

static struct GNUNET_TESTING_PeerGroup *pg;

/**
 * Check whether peers successfully shut down.
 */
void
shutdown_callback (void *cls, const char *emsg)
{
  if (emsg != NULL)
    {
#if VERBOSE
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Shutdown of peers failed!\n");
#endif
      if (ok == 0)
        ok = 666;
    }
  else
    {
#if VERBOSE
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "All peers successfully shut down!\n");
#endif
      ok = 0;
    }
}

static void
shutdown_task (void *cls,
               const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct NSEPeer *pos;
#if VERBOSE
  fprintf(stderr, "Ending test.\n");
#endif

  while (NULL != (pos = peer_head))
    {
      GNUNET_NSE_disconnect(pos->nse_handle);
      GNUNET_CONTAINER_DLL_remove(peer_head, peer_tail, pos);
      GNUNET_free(pos);
    }

  GNUNET_TESTING_daemons_stop (pg, TIMEOUT, &shutdown_callback, NULL);
}

/**
 * Callback to call when network size estimate is updated.
 *
 * @param cls closure
 * @param estimate the value of the current network size estimate
 * @param std_dev standard deviation (rounded down to nearest integer)
 *                of the size estimation values seen
 *
 */
static void
handle_estimate (void *cls, double estimate, double std_dev)
{
  struct NSEPeer *peer = cls;
  fprintf(stderr, "Received network size estimate from peer %s. Size: %f std.dev. %f\n", GNUNET_i2s(&peer->daemon->id), estimate, std_dev);
}

static void
connect_nse_service (void *cls,
                     const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct NSEPeer *current_peer;
  unsigned int i;
#if VERBOSE
  fprintf(stderr, "TEST_NSE_MULTIPEER: connecting to nse service of peers\n");
#endif
  for (i = 0; i < num_peers; i++)
    {
      current_peer = GNUNET_malloc(sizeof(struct NSEPeer));
      current_peer->daemon = GNUNET_TESTING_daemon_get(pg, i);
      current_peer->nse_handle = GNUNET_NSE_connect (current_peer->daemon->cfg, &handle_estimate, current_peer);
      GNUNET_assert(current_peer->nse_handle != NULL);

      GNUNET_CONTAINER_DLL_insert (peer_head, peer_tail, current_peer);
    }
}

static void
my_cb (void *cls,
       const char *emsg)
{
  if (emsg != NULL)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Peergroup callback called with error, aborting test!\n");
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Error from testing: `%s'\n");
      ok = 1;
      GNUNET_TESTING_daemons_stop (pg, TIMEOUT, &shutdown_callback, NULL);
      return;
    }
#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Peer Group started successfully, connecting to NSE service for each peer!\n");
#endif

  GNUNET_SCHEDULER_add_now(&connect_nse_service, NULL);
}


static void
run (void *cls,
     char *const *args,
     const char *cfgfile, const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  struct GNUNET_CONFIGURATION_Handle *testing_cfg;
  unsigned long long total_peers;
  ok = 1;
  testing_cfg = GNUNET_CONFIGURATION_create();
  GNUNET_assert(GNUNET_OK == GNUNET_CONFIGURATION_load(testing_cfg, cfgfile));
#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Starting daemons.\n");
  GNUNET_CONFIGURATION_set_value_string (testing_cfg, "testing",
                                           "use_progressbars",
                                           "YES");
#endif
  if (GNUNET_OK != GNUNET_CONFIGURATION_get_value_number (testing_cfg, "testing", "num_peers", &total_peers))
    total_peers = NUM_PEERS;

  peers_left = total_peers;
  num_peers = peers_left;
  pg = GNUNET_TESTING_peergroup_start(testing_cfg,
                                      peers_left,
                                      TIMEOUT,
                                      NULL,
                                      &my_cb, NULL,
                                      NULL);
  GNUNET_assert (pg != NULL);
  GNUNET_SCHEDULER_add_delayed (TIMEOUT, &shutdown_task, NULL);
}

static int
check ()
{
  char *const argv[] = { "test-nse-multipeer",
    "-c",
    "test_nse.conf",
#if VERBOSE
    "-L", "DEBUG",
#endif
    NULL
  };
  struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_OPTION_END
  };
  GNUNET_PROGRAM_run ((sizeof (argv) / sizeof (char *)) - 1,
                      argv, "test-nse-multipeer", "nohelp",
                      options, &run, &ok);
  return ok;
}

int
main (int argc, char *argv[])
{
  int ret;

  GNUNET_log_setup ("test-nse-multipeer",
#if VERBOSE
                    "DEBUG",
#else
                    "WARNING",
#endif
                    NULL);
  ret = check ();
  GNUNET_DISK_directory_remove ("/tmp/test-nse-multipeer");
  return ret;
}

/* end of test_nse_multipeer.c */
