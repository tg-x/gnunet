/*
     This file is part of GNUnet.
     (C) 2011 Christian Grothoff (and other contributing authors)

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
 * @file mesh/test_mesh_small.c
 *
 * @brief Test for the mesh service.
 */
#include "platform.h"
#include "gnunet_testing_lib.h"
#include "gnunet_mesh_service_new.h"

#define VERBOSE GNUNET_YES
#define REMOVE_DIR GNUNET_YES

struct MeshPeer
{
  struct MeshPeer *prev;

  struct MeshPeer *next;

  struct GNUNET_TESTING_Daemon *daemon;

  struct GNUNET_MESH_Handle *mesh_handle;
};


struct StatsContext
{
  unsigned long long total_mesh_bytes;
};


// static struct MeshPeer *peer_head;
//
// static struct MeshPeer *peer_tail;

/**
 * How long until we give up on connecting the peers?
 */
#define TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 1500)

static int ok;

/**
 * Be verbose
 */
static int verbose;

/**
 * Total number of peers in the test.
 */
static unsigned long long num_peers;

/**
 * Global configuration file
 */
static struct GNUNET_CONFIGURATION_Handle *testing_cfg;

/**
 * Total number of currently running peers.
 */
static unsigned long long peers_running;

/**
 * Total number of connections in the whole network.
 */
static unsigned int total_connections;

/**
 * The currently running peer group.
 */
static struct GNUNET_TESTING_PeerGroup *pg;

/**
 * File to report results to.
 */
static struct GNUNET_DISK_FileHandle *output_file;

/**
 * File to log connection info, statistics to.
 */
static struct GNUNET_DISK_FileHandle *data_file;

/**
 * Task called to disconnect peers.
 */
static GNUNET_SCHEDULER_TaskIdentifier disconnect_task;

/**
 * Task called to shutdown test.
 */
static GNUNET_SCHEDULER_TaskIdentifier shutdown_handle;

static char *topology_file;

static char *data_filename;

static struct GNUNET_TIME_Relative time_out;

/**
 * Check whether peers successfully shut down.
 */
static void
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
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "All peers successfully shut down!\n");
#endif
    ok = 0;
  }
}


static void
shutdown_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
#if VERBOSE
  fprintf (stderr, "Ending test.\n");
#endif

  if (disconnect_task != GNUNET_SCHEDULER_NO_TASK)
  {
    GNUNET_SCHEDULER_cancel (disconnect_task);
    disconnect_task = GNUNET_SCHEDULER_NO_TASK;
  }
  if (data_file != NULL)
    GNUNET_DISK_file_close (data_file);
  GNUNET_TESTING_daemons_stop (pg, TIMEOUT, &shutdown_callback, NULL);
  GNUNET_CONFIGURATION_destroy (testing_cfg);
}


/**
 * Handlers, for diverse services
 */
static struct GNUNET_MESH_MessageHandler handlers[] = {
//    {&callback, 1, 0},
  {NULL, 0, 0}
};


static void
disconnect_mesh (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "test: disconnecting mesh service of peers\n");
  disconnect_task = GNUNET_SCHEDULER_NO_TASK;
  GNUNET_SCHEDULER_cancel (shutdown_handle);
  shutdown_handle = GNUNET_SCHEDULER_add_now(&shutdown_task, NULL);
}


/**
 * Function called whenever an inbound tunnel is destroyed.  Should clean up
 * any associated state.
 *
 * @param cls closure (set from GNUNET_MESH_connect)
 * @param tunnel connection to the other end (henceforth invalid)
 * @param tunnel_ctx place where local state associated
 *                   with the tunnel is stored
 */
static void
tunnel_cleaner (void *cls, const struct GNUNET_MESH_Tunnel *tunnel,
                void *tunnel_ctx)
{
#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "tunnel disconnected\n");
#endif
  return;
}

/**
 * Method called whenever a tunnel falls apart.
 *
 * @param cls closure
 * @param peer peer identity the tunnel stopped working with
 */
static void
dh (void *cls, const struct GNUNET_PeerIdentity *peer)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "peer disconnected\n");
  return;
}


/**
 * Method called whenever a tunnel is established.
 *
 * @param cls closure
 * @param peer peer identity the tunnel was created to, NULL on timeout
 * @param atsi performance data for the connection
 */
static void
ch (void *cls, const struct GNUNET_PeerIdentity *peer,
    const struct GNUNET_ATS_Information *atsi)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "peer connected\n");
  return;
}


/**
 * connect_mesh_service: connect to the mesh service of one of the peers
 *
 */
static void
connect_mesh (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_TESTING_Daemon *d;
  struct GNUNET_MESH_Handle *h;
  struct GNUNET_MESH_Tunnel *t;
  GNUNET_MESH_ApplicationType app;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "connect_mesh_service\n");

  d = GNUNET_TESTING_daemon_get (pg, 1);
  app = (GNUNET_MESH_ApplicationType) 0;

#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "connecting to mesh service of peer %s\n", GNUNET_i2s (&d->id));
#endif
  h = GNUNET_MESH_connect (d->cfg, 10, NULL, NULL, &tunnel_cleaner, handlers,
                           &app);
#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "connected to mesh service of peer %s\n",
              GNUNET_i2s (&d->id));
#endif
  t = GNUNET_MESH_tunnel_create (h, NULL, &ch, &dh, NULL);
  GNUNET_MESH_tunnel_destroy (t);
  GNUNET_MESH_disconnect (h);
}


/**
 * peergroup_ready: start test when all peers are connected
 * @param cls closure
 * @param emsg error message
 */
static void
peergroup_ready (void *cls, const char *emsg)
{
  char *buf;
  int buf_len;

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
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Peer Group started successfully!\n");
#endif

  GNUNET_log (GNUNET_ERROR_TYPE_WARNING, "Have %u connections\n",
              total_connections);
  if (data_file != NULL)
  {
    buf = NULL;
    buf_len = GNUNET_asprintf (&buf, "CONNECTIONS_0: %u\n", total_connections);
    if (buf_len > 0)
      GNUNET_DISK_file_write (data_file, buf, buf_len);
    GNUNET_free (buf);
  }
  peers_running = GNUNET_TESTING_daemons_running (pg);

  GNUNET_SCHEDULER_add_now (&connect_mesh, NULL);
  disconnect_task =
      GNUNET_SCHEDULER_add_delayed (time_out, &disconnect_mesh, NULL);

}


/**
 * Function that will be called whenever two daemons are connected by
 * the testing library.
 *
 * @param cls closure
 * @param first peer id for first daemon
 * @param second peer id for the second daemon
 * @param distance distance between the connected peers
 * @param first_cfg config for the first daemon
 * @param second_cfg config for the second daemon
 * @param first_daemon handle for the first daemon
 * @param second_daemon handle for the second daemon
 * @param emsg error message (NULL on success)
 */
static void
connect_cb (void *cls, const struct GNUNET_PeerIdentity *first,
            const struct GNUNET_PeerIdentity *second, uint32_t distance,
            const struct GNUNET_CONFIGURATION_Handle *first_cfg,
            const struct GNUNET_CONFIGURATION_Handle *second_cfg,
            struct GNUNET_TESTING_Daemon *first_daemon,
            struct GNUNET_TESTING_Daemon *second_daemon, const char *emsg)
{
  if (emsg == NULL)
    total_connections++;
}


/**
 * run: load configuration options and schedule test to run (start peergroup)
 * @param cls closure
 * @param args argv
 * @param cfgfile configuration file name (can be NULL)
 * @param cfg configuration handle
 */
static void
run (void *cls, char *const *args, const char *cfgfile,
     const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  char *temp_str;
  struct GNUNET_TESTING_Host *hosts;

  ok = 1;
  testing_cfg = GNUNET_CONFIGURATION_dup (cfg);

  GNUNET_log_setup ("test_mesh_small",
#if VERBOSE
                    "DEBUG",
#else
                    "WARNING",
#endif
                    NULL);

#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Starting daemons.\n");
  GNUNET_CONFIGURATION_set_value_string (testing_cfg, "testing",
                                         "use_progressbars", "YES");
#endif

  time_out = GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 30);

  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_number (testing_cfg, "testing",
                                             "num_peers", &num_peers))
  {
    GNUNET_assert (GNUNET_OK ==
                   GNUNET_CONFIGURATION_load (testing_cfg,
                                              "test_mesh_small.conf"));
    if (GNUNET_OK !=
        GNUNET_CONFIGURATION_get_value_number (testing_cfg, "testing",
                                               "num_peers", &num_peers))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Option TESTING:NUM_PEERS is required!\n");
      return;
    }
  }

  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_string (testing_cfg, "testing",
                                             "topology_output_file",
                                             &topology_file))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Option test_mesh_small:topology_output_file is required!\n");
    return;
  }

  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_string (testing_cfg, "test_mesh_small",
                                             "data_output_file",
                                             &data_filename))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Option test_mesh_small:data_output_file is required!\n");
    return;
  }

  data_file =
      GNUNET_DISK_file_open (data_filename,
                             GNUNET_DISK_OPEN_READWRITE |
                             GNUNET_DISK_OPEN_CREATE,
                             GNUNET_DISK_PERM_USER_READ |
                             GNUNET_DISK_PERM_USER_WRITE);
  if (data_file == NULL)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_WARNING, "Failed to open %s for output!\n",
                data_filename);
    GNUNET_free (data_filename);
  }

  if (GNUNET_YES ==
      GNUNET_CONFIGURATION_get_value_string (cfg, "test_mesh_small",
                                             "output_file", &temp_str))
  {
    output_file =
        GNUNET_DISK_file_open (temp_str,
                               GNUNET_DISK_OPEN_READWRITE |
                               GNUNET_DISK_OPEN_CREATE,
                               GNUNET_DISK_PERM_USER_READ |
                               GNUNET_DISK_PERM_USER_WRITE);
    if (output_file == NULL)
      GNUNET_log (GNUNET_ERROR_TYPE_WARNING, "Failed to open %s for output!\n",
                  temp_str);
  }
  GNUNET_free_non_null (temp_str);

  hosts = GNUNET_TESTING_hosts_load (testing_cfg);

  pg = GNUNET_TESTING_peergroup_start (testing_cfg, num_peers, TIMEOUT,
                                       &connect_cb, &peergroup_ready, NULL,
                                       hosts);
  GNUNET_assert (pg != NULL);
  shutdown_handle =
      GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_relative_get_forever (),
                                    &shutdown_task, NULL);
}



/**
 * test_mesh_small command line options
 */
static struct GNUNET_GETOPT_CommandLineOption options[] = {
  {'V', "verbose", NULL,
   gettext_noop ("be verbose (print progress information)"),
   0, &GNUNET_GETOPT_set_one, &verbose},
  GNUNET_GETOPT_OPTION_END
};


/**
 * Main: start test
 */
int
main (int argc, char *argv[])
{
  GNUNET_PROGRAM_run (argc, argv, "test_mesh_small",
                      gettext_noop ("Test mesh in a small network."), options,
                      &run, NULL);
#if REMOVE_DIR
  GNUNET_DISK_directory_remove ("/tmp/test_mesh_small");
#endif
  return ok;
}

/* end of test_mesh_small.c */
