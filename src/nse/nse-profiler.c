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
 * @file nse/nse-profiler.c
 *
 * @brief Profiling driver for the network size estimation service.
 *        Generally, the profiler starts a given number of peers,
 *        then churns some off, waits a certain amount of time, then
 *        churns again, and repeats.
 */
#include "platform.h"
#include "gnunet_testing_lib.h"
#include "gnunet_nse_service.h"

#define VERBOSE GNUNET_NO

struct NSEPeer
{
  struct NSEPeer *prev;

  struct NSEPeer *next;

  struct GNUNET_TESTING_Daemon *daemon;

  struct GNUNET_NSE_Handle *nse_handle;
};

struct StatsContext
{
  GNUNET_SCHEDULER_Task task;
  GNUNET_SCHEDULER_TaskIdentifier *task_id;
  void *task_cls;
  unsigned long long total_nse_bytes;
};

struct NSEPeer *peer_head;

struct NSEPeer *peer_tail;

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
struct GNUNET_CONFIGURATION_Handle *testing_cfg;

/**
 * Total number of currently running peers.
 */
static unsigned long long peers_running;

/**
 * Current round we are in.
 */
static unsigned long long current_round;

/**
 * Peers desired in the next round.
 */
static unsigned long long peers_next_round;

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
 * How many data points to capture before triggering next round?
 */
static struct GNUNET_TIME_Relative wait_time;

/**
 * Task called to disconnect peers.
 */
static GNUNET_SCHEDULER_TaskIdentifier disconnect_task;

/**
 * Task called to shutdown test.
 */
static GNUNET_SCHEDULER_TaskIdentifier shutdown_handle;

/**
 * Task used to churn the network.
 */
static GNUNET_SCHEDULER_TaskIdentifier churn_task;

static char *topology_file;

static char *data_filename;

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

  if (disconnect_task != GNUNET_SCHEDULER_NO_TASK)
    {
      GNUNET_SCHEDULER_cancel(disconnect_task);
      disconnect_task = GNUNET_SCHEDULER_NO_TASK;
    }
  while (NULL != (pos = peer_head))
    {
      if (pos->nse_handle != NULL)
        GNUNET_NSE_disconnect(pos->nse_handle);
      GNUNET_CONTAINER_DLL_remove(peer_head, peer_tail, pos);
      GNUNET_free(pos);
    }

  if (data_file != NULL)
    GNUNET_DISK_file_close(data_file);
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
  char *output_buffer;
  int size;
  //fprintf(stderr, "Received network size estimate from peer %s. Size: %f std.dev. %f\n", GNUNET_i2s(&peer->daemon->id), estimate, std_dev);
  if (output_file != NULL)
    {
      size = GNUNET_asprintf(&output_buffer, "%s %u %f %f\n", GNUNET_i2s(&peer->daemon->id), peers_running, estimate, std_dev);
      if (size != GNUNET_DISK_file_write(output_file, output_buffer, size))
        GNUNET_log(GNUNET_ERROR_TYPE_WARNING, "%s: Unable to write to file!\n", "nse-profiler");
    }
  else
    fprintf(stderr, "Received network size estimate from peer %s. Size: %f std.dev. %f\n", GNUNET_i2s(&peer->daemon->id), estimate, std_dev);

}

static void
connect_nse_service (void *cls,
                     const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct NSEPeer *current_peer;
  unsigned int i;
#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "TEST_NSE_MULTIPEER: connecting to nse service of peers\n");
#endif
  GNUNET_log (GNUNET_ERROR_TYPE_WARNING, "TEST_NSE_MULTIPEER: connecting to nse service of peers\n");
  for (i = 0; i < num_peers; i++)
    {
      current_peer = GNUNET_malloc(sizeof(struct NSEPeer));
      current_peer->daemon = GNUNET_TESTING_daemon_get(pg, i);
      if (GNUNET_YES == GNUNET_TESTING_daemon_running(GNUNET_TESTING_daemon_get(pg, i)))
        {
          current_peer->nse_handle = GNUNET_NSE_connect (current_peer->daemon->cfg, &handle_estimate, current_peer);
          GNUNET_assert(current_peer->nse_handle != NULL);
        }
      GNUNET_CONTAINER_DLL_insert (peer_head, peer_tail, current_peer);
    }
}

static void
churn_peers (void *cls,
             const struct GNUNET_SCHEDULER_TaskContext *tc);

/**
 * Continuation called by the "get_all" and "get" functions.
 *
 * @param cls struct StatsContext
 * @param success GNUNET_OK if statistics were
 *        successfully obtained, GNUNET_SYSERR if not.
 */
static void stats_finished_callback (void *cls, int success)
{
  struct StatsContext *stats_context = (struct StatsContext *)cls;
  char *buf;
  int buf_len;

  if ((GNUNET_OK == success) && (data_file != NULL)) /* Stats lookup successful, write out data */
    {
      buf = NULL;
      buf_len = GNUNET_asprintf(&buf, "TOTAL_NSE_BYTES: %u\n", stats_context->total_nse_bytes);
      if (buf_len > 0)
        {
          GNUNET_DISK_file_write(data_file, buf, buf_len);
        }
      GNUNET_free_non_null(buf);
    }

  if (stats_context->task != NULL)
    (*stats_context->task_id) = GNUNET_SCHEDULER_add_now(stats_context->task, stats_context->task_cls);

  GNUNET_free(stats_context);
}

/**
 * Callback function to process statistic values.
 *
 * @param cls struct StatsContext
 * @param peer the peer the statistics belong to
 * @param subsystem name of subsystem that created the statistic
 * @param name the name of the datum
 * @param value the current value
 * @param is_persistent GNUNET_YES if the value is persistent, GNUNET_NO if not
 * @return GNUNET_OK to continue, GNUNET_SYSERR to abort iteration
 */
static int statistics_iterator (void *cls,
                                const struct GNUNET_PeerIdentity *peer,
                                const char *subsystem,
                                const char *name,
                                uint64_t value,
                                int is_persistent)
{
  struct StatsContext *stats_context = (struct StatsContext *)cls;
  char *buf;

  GNUNET_assert(0 < GNUNET_asprintf(&buf, "bytes of messages of type %d received", GNUNET_MESSAGE_TYPE_NSE_P2P_FLOOD));
  if ((0 == strstr(subsystem, "core")) && (0 == strstr(name, buf)))
    {
      stats_context->total_nse_bytes += value;
    }
  GNUNET_free(buf);
  return GNUNET_OK;
}

/**
 * @param cls struct StatsContext
 * @param tc task context
 */
static void
get_statistics (void *cls,
                const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct StatsContext *stats_context = (struct StatsContext *)cls;
  GNUNET_TESTING_get_statistics(pg, &stats_finished_callback, &statistics_iterator, stats_context);
}

static void
disconnect_nse_peers (void *cls,
                      const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct NSEPeer *pos;
  char *buf;
  struct StatsContext *stats_context;
  disconnect_task = GNUNET_SCHEDULER_NO_TASK;
  pos = peer_head;

  GNUNET_log (GNUNET_ERROR_TYPE_WARNING, "TEST_NSE_MULTIPEER: disconnecting nse service of peers\n");
  while (pos != NULL)
    {
      if (pos->nse_handle != NULL)
        {
          GNUNET_NSE_disconnect(pos->nse_handle);
          pos->nse_handle = NULL;
        }
      pos = pos->next;
    }

  GNUNET_asprintf(&buf, "round%llu", current_round);
  if (GNUNET_OK == GNUNET_CONFIGURATION_get_value_number (testing_cfg, "nse-profiler", buf, &peers_next_round))
    {
      current_round++;
      GNUNET_assert(churn_task == GNUNET_SCHEDULER_NO_TASK);
      churn_task = GNUNET_SCHEDULER_add_now(&churn_peers, NULL);
    }
  else /* No more rounds, let's shut it down! */
    {
      stats_context = GNUNET_malloc(sizeof(struct StatsContext));
      GNUNET_SCHEDULER_cancel(shutdown_handle);
      shutdown_handle = GNUNET_SCHEDULER_NO_TASK;
      stats_context->task = &shutdown_task;
      stats_context->task_cls = NULL;
      stats_context->task_id = &shutdown_handle;
      GNUNET_SCHEDULER_add_now(&get_statistics, stats_context);
      //shutdown_handle = GNUNET_SCHEDULER_add_now(&shutdown_task, NULL);
    }
  GNUNET_free(buf);
}

/**
 * Prototype of a function that will be called when a
 * particular operation was completed the testing library.
 *
 * @param cls unused
 * @param emsg NULL on success
 */
void topology_output_callback (void *cls, const char *emsg)
{
  struct StatsContext *stats_context;
  stats_context = GNUNET_malloc(sizeof(struct StatsContext));

  disconnect_task = GNUNET_SCHEDULER_add_delayed(wait_time, &disconnect_nse_peers, NULL);
  GNUNET_SCHEDULER_add_now(&connect_nse_service, NULL);
}


/**
 * Prototype of a function that will be called when a
 * particular operation was completed the testing library.
 *
 * @param cls closure
 * @param emsg NULL on success
 */
static void
churn_callback (void *cls, const char *emsg)
{
  char *temp_output_file;

  if (emsg == NULL) /* Everything is okay! */
    {
      peers_running = GNUNET_TESTING_daemons_running(pg);
      GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                  "Round %lu, churn finished successfully.\n", current_round);
      GNUNET_assert(disconnect_task == GNUNET_SCHEDULER_NO_TASK);
      GNUNET_asprintf(&temp_output_file, "%s%lu.dot", topology_file, current_round);
      GNUNET_TESTING_peergroup_topology_to_file(pg,
                                                temp_output_file,
                                                &topology_output_callback,
                                                NULL);
      GNUNET_log(GNUNET_ERROR_TYPE_WARNING, "Writing topology to file %s\n", temp_output_file);
      GNUNET_free(temp_output_file);
    }
  else
    {
      GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                  "Round %lu, churn FAILED!!\n", current_round);
      GNUNET_SCHEDULER_cancel(shutdown_handle);
      GNUNET_SCHEDULER_add_now(&shutdown_task, NULL);
    }
}

static void
churn_peers (void *cls,
                      const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  peers_running = GNUNET_TESTING_daemons_running(pg);
  churn_task = GNUNET_SCHEDULER_NO_TASK;
  if (peers_next_round == peers_running)
    {
      /* Nothing to do... */
      GNUNET_SCHEDULER_add_now(&connect_nse_service, NULL);
      GNUNET_assert(disconnect_task == GNUNET_SCHEDULER_NO_TASK);
      disconnect_task = GNUNET_SCHEDULER_add_delayed(wait_time, &disconnect_nse_peers, NULL);
      GNUNET_log(GNUNET_ERROR_TYPE_WARNING, "Round %lu, doing nothing!\n", current_round);
    }
  else
    {
      if (peers_next_round > num_peers)
        {
          GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Asked to turn on more peers than have!!\n");
          GNUNET_SCHEDULER_cancel(shutdown_handle);
          GNUNET_SCHEDULER_add_now(&shutdown_task, NULL);
        }
      GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                  "Round %lu, turning off %lu peers, turning on %lu peers!\n",
                  current_round,
                  (peers_running > peers_next_round) ? peers_running
                      - peers_next_round : 0,
                  (peers_next_round > peers_running) ? peers_next_round
                      - peers_running : 0);
      GNUNET_TESTING_daemons_churn (pg,
                                    (peers_running > peers_next_round) ? peers_running
                                        - peers_next_round
                                        : 0,
                                    (peers_next_round > peers_running) ? peers_next_round
                                        - peers_running
                                        : 0, wait_time, &churn_callback,
                                    NULL);
    }
}


static void
my_cb (void *cls,
       const char *emsg)
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
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Peer Group started successfully, connecting to NSE service for each peer!\n");
#endif
  GNUNET_log (GNUNET_ERROR_TYPE_WARNING, "Have %u connections\n", total_connections);
  if (data_file != NULL)
    {
      buf = NULL;
      buf_len = GNUNET_asprintf(&buf, "CONNECTIONS_0: %u\n", total_connections);
      if (buf_len > 0)
        GNUNET_DISK_file_write(data_file, buf, buf_len);
      GNUNET_free_non_null(buf);
    }
  peers_running = GNUNET_TESTING_daemons_running(pg);
  GNUNET_SCHEDULER_add_now(&connect_nse_service, NULL);
  disconnect_task = GNUNET_SCHEDULER_add_delayed(wait_time, &disconnect_nse_peers, NULL);
}

/**
 * Prototype of a function that will be called whenever
 * two daemons are connected by the testing library.
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
void connect_cb (void *cls,
                 const struct GNUNET_PeerIdentity *first,
                 const struct GNUNET_PeerIdentity *second,
                 uint32_t distance,
                 const struct GNUNET_CONFIGURATION_Handle *first_cfg,
                 const struct GNUNET_CONFIGURATION_Handle *second_cfg,
                 struct GNUNET_TESTING_Daemon *first_daemon,
                 struct GNUNET_TESTING_Daemon *second_daemon,
                 const char *emsg)
{
  char *second_id;

  second_id = GNUNET_strdup(GNUNET_i2s(second));
  if (emsg == NULL)
    total_connections++;
}


static void
run (void *cls,
     char *const *args,
     const char *cfgfile, const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  char *temp_str;
  unsigned long long temp_wait;
  ok = 1;
  testing_cfg = GNUNET_CONFIGURATION_create();
  GNUNET_assert(GNUNET_OK == GNUNET_CONFIGURATION_load(testing_cfg, cfgfile));
#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Starting daemons.\n");
  GNUNET_CONFIGURATION_set_value_string (testing_cfg, "testing",
                                           "use_progressbars",
                                           "YES");
#endif
  if (GNUNET_OK != GNUNET_CONFIGURATION_get_value_number (testing_cfg, "testing", "num_peers", &num_peers))
    {
      GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Option TESTING:NUM_PEERS is required!\n");
      return;
    }

  if (GNUNET_OK != GNUNET_CONFIGURATION_get_value_number (testing_cfg, "nse-profiler", "wait_time", &temp_wait))
    {
      GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Option nse-profiler:wait_time is required!\n");
      return;
    }

  if (GNUNET_OK != GNUNET_CONFIGURATION_get_value_string (testing_cfg, "nse-profiler", "topology_output_file", &topology_file))
    {
      GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Option nse-profiler:topology_output_file is required!\n");
      return;
    }

  if (GNUNET_OK != GNUNET_CONFIGURATION_get_value_string (testing_cfg, "nse-profiler", "data_output_file", &data_filename))
    {
      GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Option nse-profiler:data_output_file is required!\n");
      return;
    }


  data_file = GNUNET_DISK_file_open (data_filename, GNUNET_DISK_OPEN_READWRITE
                                                  | GNUNET_DISK_OPEN_CREATE,
                                                  GNUNET_DISK_PERM_USER_READ |
                                                  GNUNET_DISK_PERM_USER_WRITE);
  if (data_file == NULL)
    GNUNET_log(GNUNET_ERROR_TYPE_WARNING, "Failed to open %s for output!\n", data_filename);
  GNUNET_free(data_filename);

  wait_time = GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_SECONDS, temp_wait);

  if (GNUNET_YES == GNUNET_CONFIGURATION_get_value_string(cfg, "nse-profiler", "output_file", &temp_str))
    {
      output_file = GNUNET_DISK_file_open (temp_str, GNUNET_DISK_OPEN_READWRITE
                                                      | GNUNET_DISK_OPEN_CREATE,
                                                      GNUNET_DISK_PERM_USER_READ |
                                                      GNUNET_DISK_PERM_USER_WRITE);
      if (output_file == NULL)
        GNUNET_log(GNUNET_ERROR_TYPE_WARNING, "Failed to open %s for output!\n", temp_str);
    }
  GNUNET_free_non_null(temp_str);

  pg = GNUNET_TESTING_peergroup_start(testing_cfg,
                                      num_peers,
                                      TIMEOUT,
                                      &connect_cb,
                                      &my_cb, NULL,
                                      NULL);
  GNUNET_assert (pg != NULL);
  shutdown_handle = GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_relative_get_forever(), &shutdown_task, NULL);
}



/**
 * nse-profiler command line options
 */
static struct GNUNET_GETOPT_CommandLineOption options[] = {
  {'V', "verbose", NULL,
   gettext_noop ("be verbose (print progress information)"),
   0, &GNUNET_GETOPT_set_one, &verbose},
  GNUNET_GETOPT_OPTION_END
};

int
main (int argc, char *argv[])
{
  int ret;

  GNUNET_log_setup ("nse-profiler",
#if VERBOSE
                    "DEBUG",
#else
                    "WARNING",
#endif
                    NULL);
  ret = 1;
  GNUNET_PROGRAM_run (argc,
                      argv, "nse-profiler", gettext_noop
                      ("Run a test of the NSE service."),
                      options, &run, &ok);

  GNUNET_DISK_directory_remove ("/tmp/nse-profiler");
  return ret;
}

/* end of nse-profiler.c */
