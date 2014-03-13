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
 * @file mesh/mesh_profiler.c
 *
 * @brief Profiler for mesh experiments.
 */
#include <stdio.h>
#include "platform.h"
#include "mesh_test_lib.h"
#include "gnunet_mesh_service.h"
#include "gnunet_statistics_service.h"
#include <gauger.h>


/**
 * How namy messages to send
 */
#define TOTAL_PACKETS 1000

/**
 * How namy peers to run
 */
#define TOTAL_PEERS 1000

/**
 * How long until we give up on connecting the peers?
 */
#define TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 120)

/**
 * Time to wait for stuff that should be rather fast
 */
#define SHORT_TIME GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 60)


/**
 * How many events have happened
 */
static int ok;

/**
 * Number of events expected to conclude the test successfully.
 */
int ok_goal;

/**
 * Size of each test packet
 */
size_t size_payload = sizeof (struct GNUNET_MessageHeader) + sizeof (uint32_t);

/**
 * Operation to get peer ids.
 */
struct GNUNET_TESTBED_Operation *t_op[TOTAL_PEERS];

/**
 * Peer ids.
 */
struct GNUNET_PeerIdentity *p_id[TOTAL_PEERS];

/**
 * Mesh handle for the root peer
 */
static struct GNUNET_MESH_Handle *mesh_h[TOTAL_PEERS];

/**
 * Channel handle for the root peer
 */
static struct GNUNET_MESH_Channel *ch;

/**
 * Channel handle for the dest peer
 */
static struct GNUNET_MESH_Channel *incoming_ch;

/**
 * Peer ids counter.
 */
unsigned int p_ids;

/**
 * Is the setup initialized?
 */
static int initialized;

/**
 * Number of payload packes sent
 */
static int data_sent;

/**
 * Number of payload packets received
 */
static int data_received;

/**
 * Number of payload packed explicitly (app level) acknowledged
 */
static int data_ack;

/**
 * Total number of currently running peers.
 */
static unsigned long long peers_running;

/**
 * Test context (to shut down).
 */
struct GNUNET_MESH_TEST_Context *test_ctx;

/**
 * Task called to shutdown test.
 */
static GNUNET_SCHEDULER_TaskIdentifier shutdown_handle;

/**
 * Task called to disconnect peers, before shutdown.
 */
static GNUNET_SCHEDULER_TaskIdentifier disconnect_task;

/**
 * Task to perform tests
 */
static GNUNET_SCHEDULER_TaskIdentifier test_task;

/**
 * Time we started the data transmission (after channel has been established
 * and initilized).
 */
static struct GNUNET_TIME_Absolute start_time;

static struct GNUNET_TESTBED_Peer **testbed_peers;

/**
 *
 */
static struct GNUNET_STATISTICS_Handle *stats;
static struct GNUNET_STATISTICS_GetHandle *stats_get;
static struct GNUNET_TESTBED_Operation *stats_op;
static unsigned int ka_sent;
static unsigned int ka_received;


/**
 * Show the results of the test (banwidth acheived) and log them to GAUGER
 */
static void
show_end_data (void)
{
  static struct GNUNET_TIME_Absolute end_time;
  static struct GNUNET_TIME_Relative total_time;

  end_time = GNUNET_TIME_absolute_get();
  total_time = GNUNET_TIME_absolute_get_difference(start_time, end_time);
  FPRINTF (stderr, "\nResults of test \"%s\"\n", test_name);
  FPRINTF (stderr, "Test time %s\n",
	   GNUNET_STRINGS_relative_time_to_string (total_time,
						   GNUNET_YES));
  FPRINTF (stderr, "Test bandwidth: %f kb/s\n",
	   4 * TOTAL_PACKETS * 1.0 / (total_time.rel_value_us / 1000)); // 4bytes * ms
  FPRINTF (stderr, "Test throughput: %f packets/s\n\n",
	   TOTAL_PACKETS * 1000.0 / (total_time.rel_value_us / 1000)); // packets * ms
  GAUGER ("MESH", test_name,
          TOTAL_PACKETS * 1000.0 / (total_time.rel_value_us / 1000),
          "packets/s");
}


/**
 * Shut down peergroup, clean up.
 *
 * @param cls Closure (unused).
 * @param tc Task Context.
 */
static void
shutdown_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Ending test.\n");
  shutdown_handle = GNUNET_SCHEDULER_NO_TASK;
}


/**
 * Disconnect from mesh services af all peers, call shutdown.
 *
 * @param cls Closure (unused).
 * @param tc Task Context.
 */
static void
disconnect_mesh_peers (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  long line = (long) cls;
  unsigned int i;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "disconnecting mesh service of peers, called from line %ld\n",
              line);
  disconnect_task = GNUNET_SCHEDULER_NO_TASK;
  for (i = 0; i < 2; i++)
  {
    GNUNET_TESTBED_operation_done (t_op[i]);
  }
  if (NULL != ch)
  {
    GNUNET_MESH_channel_destroy (ch);
    ch = NULL;
  }
  if (NULL != incoming_ch)
  {
    GNUNET_MESH_channel_destroy (incoming_ch);
    incoming_ch = NULL;
  }
  GNUNET_MESH_TEST_cleanup (test_ctx);
  if (GNUNET_SCHEDULER_NO_TASK != shutdown_handle)
  {
    GNUNET_SCHEDULER_cancel (shutdown_handle);
  }
  if (NULL != stats_get)
    GNUNET_STATISTICS_get_cancel (stats_get);
  shutdown_handle = GNUNET_SCHEDULER_add_now (&shutdown_task, NULL);
}


/**
 * Abort test: schedule disconnect and shutdown immediately
 *
 * @param line Line in the code the abort is requested from (__LINE__).
 */
static void
abort_test (long line)
{
  if (disconnect_task != GNUNET_SCHEDULER_NO_TASK)
  {
    GNUNET_SCHEDULER_cancel (disconnect_task);
    disconnect_task = GNUNET_SCHEDULER_add_now (&disconnect_mesh_peers,
                                                (void *) line);
  }
}

/**
 * Transmit ready callback.
 *
 * @param cls Closure (message type).
 * @param size Size of the tranmist buffer.
 * @param buf Pointer to the beginning of the buffer.
 *
 * @return Number of bytes written to buf.
 */
static size_t
tmt_rdy (void *cls, size_t size, void *buf);


/**
 * Task to schedule a new data transmission.
 *
 * @param cls Closure (peer #).
 * @param tc Task Context.
 */
static void
data_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_MESH_TransmitHandle *th;
  struct GNUNET_MESH_Channel *channel;

  if ((GNUNET_SCHEDULER_REASON_SHUTDOWN & tc->reason) != 0)
    return;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Data task\n");
  if (GNUNET_YES == test_backwards)
  {
    channel = incoming_ch;
  }
  else
  {
    channel = ch;
  }
  th = GNUNET_MESH_notify_transmit_ready (channel, GNUNET_NO,
                                          GNUNET_TIME_UNIT_FOREVER_REL,
                                          size_payload, &tmt_rdy, (void *) 1L);
  if (NULL == th)
  {
    unsigned long i = (unsigned long) cls;

    GNUNET_log (GNUNET_ERROR_TYPE_INFO, "Retransmission\n");
    if (0 == i)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_INFO, "  in 1 ms\n");
      GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_MILLISECONDS,
                                    &data_task, (void *)1UL);
    }
    else
    {
      i++;
      GNUNET_log (GNUNET_ERROR_TYPE_INFO, "in %u ms\n", i);
      GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_relative_multiply(
                                      GNUNET_TIME_UNIT_MILLISECONDS,
                                      i),
                                    &data_task, (void *)i);
    }
  }
}


/**
 * Transmit ready callback
 *
 * @param cls Closure (message type).
 * @param size Size of the buffer we have.
 * @param buf Buffer to copy data to.
 */
size_t
tmt_rdy (void *cls, size_t size, void *buf)
{
  struct GNUNET_MessageHeader *msg = buf;
  uint32_t *data;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "tmt_rdy called, filling buffer\n");
  if (size < size_payload || NULL == buf)
  {
    GNUNET_break (ok >= ok_goal - 2);
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "size %u, buf %p, data_sent %u, data_received %u\n",
                size, buf, data_sent, data_received);
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "ok %u, ok goal %u\n", ok, ok_goal);

    return 0;
  }
  msg->size = htons (size);
  msg->type = htons ((long) cls);
  data = (uint32_t *) &msg[1];
  *data = htonl (data_sent);
  if (GNUNET_NO == initialized)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "sending initializer\n");
  }
  else if (SPEED == test)
  {
    data_sent++;
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              " Sent packet %d\n", data_sent);
    if (data_sent < TOTAL_PACKETS)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              " Scheduling packet %d\n", data_sent + 1);
      GNUNET_SCHEDULER_add_now (&data_task, NULL);
    }
  }

  return size_payload;
}


/**
 * Function is called whenever a message is received.
 *
 * @param cls closure (set from GNUNET_MESH_connect)
 * @param channel connection to the other end
 * @param channel_ctx place to store local state associated with the channel
 * @param message the actual message
 * @return GNUNET_OK to keep the connection open,
 *         GNUNET_SYSERR to close it (signal serious error)
 */
int
data_callback (void *cls, struct GNUNET_MESH_Channel *channel,
               void **channel_ctx,
               const struct GNUNET_MessageHeader *message)
{
  long client = (long) cls;
  long expected_target_client;
  uint32_t *data;

  ok++;

  GNUNET_MESH_receive_done (channel);

  if ((ok % 20) == 0)
  {
    if (GNUNET_SCHEDULER_NO_TASK != disconnect_task)
    {
      GNUNET_SCHEDULER_cancel (disconnect_task);
      disconnect_task = GNUNET_SCHEDULER_add_delayed (SHORT_TIME,
                                                      &disconnect_mesh_peers,
                                                      (void *) __LINE__);
    }
  }

  switch (client)
  {
  case 0L:
    GNUNET_log (GNUNET_ERROR_TYPE_INFO, "Root client got a message!\n");
    break;
  case 4L:
    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                "Leaf client %li got a message.\n",
                client);
    break;
  default:
    GNUNET_assert (0);
    break;
  }
  GNUNET_log (GNUNET_ERROR_TYPE_INFO, " ok: (%d/%d)\n", ok, ok_goal);
  data = (uint32_t *) &message[1];
  GNUNET_log (GNUNET_ERROR_TYPE_INFO, " payload: (%u)\n", ntohl (*data));
  if (SPEED == test && GNUNET_YES == test_backwards)
  {
    expected_target_client = 0L;
  }
  else
  {
    expected_target_client = 4L;
  }

  if (GNUNET_NO == initialized)
  {
    initialized = GNUNET_YES;
    start_time = GNUNET_TIME_absolute_get ();
    if (SPEED == test)
    {
      GNUNET_assert (4L == client);
      GNUNET_SCHEDULER_add_now (&data_task, NULL);
      return GNUNET_OK;
    }
  }

  if (client == expected_target_client) // Normally 4
  {
    data_received++;
    GNUNET_log (GNUNET_ERROR_TYPE_INFO, " received data %u\n", data_received);
    if (SPEED != test || (ok_goal - 2) == ok)
    {
      GNUNET_MESH_notify_transmit_ready (channel, GNUNET_NO,
                                         GNUNET_TIME_UNIT_FOREVER_REL,
                                         size_payload, &tmt_rdy, (void *) 1L);
      return GNUNET_OK;
    }
    else
    {
      if (data_received < TOTAL_PACKETS)
        return GNUNET_OK;
    }
  }
  else // Normally 0
  {
    if (test == SPEED_ACK || test == SPEED)
    {
      data_ack++;
      GNUNET_log (GNUNET_ERROR_TYPE_INFO, " received ack %u\n", data_ack);
      GNUNET_MESH_notify_transmit_ready (channel, GNUNET_NO,
                                         GNUNET_TIME_UNIT_FOREVER_REL,
                                         size_payload, &tmt_rdy, (void *) 1L);
      if (data_ack < TOTAL_PACKETS && SPEED != test)
        return GNUNET_OK;
      if (ok == 2 && SPEED == test)
        return GNUNET_OK;
      show_end_data();
    }
    if (test == P2P_SIGNAL)
    {
      GNUNET_MESH_channel_destroy (incoming_ch);
      incoming_ch = NULL;
    }
    else
    {
      GNUNET_MESH_channel_destroy (ch);
      ch = NULL;
    }
  }

  if (GNUNET_SCHEDULER_NO_TASK != disconnect_task)
  {
    GNUNET_SCHEDULER_cancel (disconnect_task);
    disconnect_task = GNUNET_SCHEDULER_add_delayed (SHORT_TIME,
                                                    &disconnect_mesh_peers,
                                                    (void *) __LINE__);
  }

  return GNUNET_OK;
}


/**
 * Adapter function called to establish a connection to the statistics service.
 *
 * @param cls closure
 * @param cfg configuration of the peer to connect to; will be available until
 *          GNUNET_TESTBED_operation_done() is called on the operation returned
 *          from GNUNET_TESTBED_service_connect()
 * @return service handle to return in 'op_result', NULL on error
 */
static void *
stats_ca (void *cls, const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  return GNUNET_STATISTICS_create ("<test_mesh>", cfg);
}


/**
 * Adapter function called to destroy a connection to
 * statistics service.
 *
 * @param cls Closure (unused).
 * @param op_result service handle returned from the connect adapter
 */
static void
stats_da (void *cls, void *op_result)
{
  GNUNET_assert (op_result == stats);
  GNUNET_STATISTICS_destroy (stats, GNUNET_NO);
  stats = NULL;
}


/**
 * Function called by testbed once we are connected to stats
 * service. Get the statistics of interest.
 *
 * @param cls Closure (unused).
 * @param op connect operation handle
 * @param ca_result handle to stats service
 * @param emsg error message on failure
 */
static void
stats_connect_cb (void *cls,
                  struct GNUNET_TESTBED_Operation *op,
                  void *ca_result,
                  const char *emsg);

/**
 * Stats callback. Finish the stats testbed operation and when all stats have
 * been iterated, shutdown the test.
 *
 * @param cls closure
 * @param success GNUNET_OK if statistics were
 *        successfully obtained, GNUNET_SYSERR if not.
 */
static void
stats_cont (void *cls, int success)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "stats_cont for peer %u\n", cls);
  GNUNET_TESTBED_operation_done (stats_op);
  stats_get = NULL;
  if (NULL == cls)
  {
    stats_op = GNUNET_TESTBED_service_connect (NULL,
                                               testbed_peers[4],
                                               "statistics",
                                               &stats_connect_cb,
                                               (void *)4,
                                               &stats_ca,
                                               &stats_da,
                                               (void *)4);
  }
  else
  {
    if (GNUNET_SCHEDULER_NO_TASK != disconnect_task)
      GNUNET_SCHEDULER_cancel (disconnect_task);
    disconnect_task = GNUNET_SCHEDULER_add_now (&disconnect_mesh_peers,
                                                (void *) __LINE__);
  }
}


/**
 * Process statistic values.
 *
 * @param cls closure
 * @param subsystem name of subsystem that created the statistic
 * @param name the name of the datum
 * @param value the current value
 * @param is_persistent GNUNET_YES if the value is persistent, GNUNET_NO if not
 * @return GNUNET_OK to continue, GNUNET_SYSERR to abort iteration
 */
static int
stats_iterator (void *cls, const char *subsystem, const char *name,
                uint64_t value, int is_persistent)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "  %u - %s [%s]: %llu\n",
              cls, subsystem, name, value);
  if (0 == strncmp("# keepalives sent", name,
                   strlen("# keepalives sent"))
      && 0 == (long) cls)
    ka_sent = value;

  if (0 == strncmp("# keepalives received", name,
                   strlen ("# keepalives received"))
      && 4 == (long) cls)
  {
    ka_received = value;
    GNUNET_log (GNUNET_ERROR_TYPE_INFO, " sent: %u, received: %u\n",
                ka_sent, ka_received);
    if (ka_sent < 2 || ka_sent > ka_received + 1)
      ok--;
  }

  return GNUNET_OK;
}


/**
 * Function called by testbed once we are connected to stats
 * service. Get the statistics of interest.
 *
 * @param cls Closure (unused).
 * @param op connect operation handle
 * @param ca_result handle to stats service
 * @param emsg error message on failure
 */
static void
stats_connect_cb (void *cls,
                  struct GNUNET_TESTBED_Operation *op,
                  void *ca_result,
                  const char *emsg)
{
  if (NULL == ca_result || NULL != emsg)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Failed to connect to statistics service: %s\n", emsg);
    return;
  }

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "stats for peer %u\n", cls);
  stats = ca_result;

  stats_get = GNUNET_STATISTICS_get (stats, "mesh", NULL,
                                     GNUNET_TIME_UNIT_FOREVER_REL,
                                     &stats_cont, &stats_iterator, cls);
  if (NULL == stats_get)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Could not get statistics of peer %u!\n", cls);
  }
}


/**
 * Task check that keepalives were sent and received.
 *
 * @param cls Closure (NULL).
 * @param tc Task Context.
 */
static void
check_keepalives (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  if ((GNUNET_SCHEDULER_REASON_SHUTDOWN & tc->reason) != 0)
    return;

  disconnect_task = GNUNET_SCHEDULER_NO_TASK;
  GNUNET_log (GNUNET_ERROR_TYPE_INFO, "check keepalives\n");
  GNUNET_MESH_channel_destroy (ch);
  stats_op = GNUNET_TESTBED_service_connect (NULL,
                                             testbed_peers[0],
                                             "statistics",
                                             &stats_connect_cb,
                                             NULL,
                                             &stats_ca,
                                             &stats_da,
                                             NULL);
}


/**
 * Handlers, for diverse services
 */
static struct GNUNET_MESH_MessageHandler handlers[] = {
  {&data_callback, 1, sizeof (struct GNUNET_MessageHeader)},
  {NULL, 0, 0}
};


/**
 * Method called whenever another peer has added us to a channel
 * the other peer initiated.
 *
 * @param cls Closure.
 * @param channel New handle to the channel.
 * @param initiator Peer that started the channel.
 * @param port Port this channel is connected to.
 * @param options channel option flags
 * @return Initial channel context for the channel
 *         (can be NULL -- that's not an error).
 */
static void *
incoming_channel (void *cls, struct GNUNET_MESH_Channel *channel,
                 const struct GNUNET_PeerIdentity *initiator,
                 uint32_t port, enum GNUNET_MESH_ChannelOption options)
{
  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
              "Incoming channel from %s to peer %d\n",
              GNUNET_i2s (initiator), (long) cls);
  ok++;
  GNUNET_log (GNUNET_ERROR_TYPE_INFO, " ok: %d\n", ok);
  if ((long) cls == 4L)
    incoming_ch = channel;
  else
  {
    GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                "Incoming channel for unknown client %lu\n", (long) cls);
    GNUNET_break(0);
  }
  if (GNUNET_SCHEDULER_NO_TASK != disconnect_task)
  {
    GNUNET_SCHEDULER_cancel (disconnect_task);
    if (KEEPALIVE == test)
    {
      struct GNUNET_TIME_Relative delay;
      delay = GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS , 5);
      disconnect_task =
        GNUNET_SCHEDULER_add_delayed (delay, &check_keepalives, NULL);
    }
    else
      disconnect_task = GNUNET_SCHEDULER_add_delayed (SHORT_TIME,
                                                      &disconnect_mesh_peers,
                                                      (void *) __LINE__);
  }

  return NULL;
}

/**
 * Function called whenever an inbound channel is destroyed.  Should clean up
 * any associated state.
 *
 * @param cls closure (set from GNUNET_MESH_connect)
 * @param channel connection to the other end (henceforth invalid)
 * @param channel_ctx place where local state associated
 *                   with the channel is stored
 */
static void
channel_cleaner (void *cls, const struct GNUNET_MESH_Channel *channel,
                 void *channel_ctx)
{
  long i = (long) cls;

  GNUNET_log (GNUNET_ERROR_TYPE_INFO,
              "Incoming channel disconnected at peer %d\n",
              i);
  if (4L == i)
  {
    ok++;
    GNUNET_break (channel == incoming_ch);
    incoming_ch = NULL;
  }
  else if (0L == i)
  {
    if (P2P_SIGNAL == test)
    {
      ok ++;
    }
    GNUNET_break (channel == ch);
    ch = NULL;
  }
  else
    GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                "Unknown peer! %d\n", i);
  GNUNET_log (GNUNET_ERROR_TYPE_INFO, " ok: %d\n", ok);

  if (GNUNET_SCHEDULER_NO_TASK != disconnect_task)
  {
    GNUNET_SCHEDULER_cancel (disconnect_task);
    disconnect_task = GNUNET_SCHEDULER_add_now (&disconnect_mesh_peers,
                                                (void *) __LINE__);
  }

  return;
}


/**
 * START THE TESTCASE ITSELF, AS WE ARE CONNECTED TO THE MESH SERVICES.
 *
 * Testcase continues when the root receives confirmation of connected peers,
 * on callback funtion ch.
 *
 * @param cls Closure (unsued).
 * @param tc Task Context.
 */
static void
do_test (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  enum GNUNET_MESH_ChannelOption flags;

  if ((GNUNET_SCHEDULER_REASON_SHUTDOWN & tc->reason) != 0)
    return;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "test_task\n");

  if (GNUNET_SCHEDULER_NO_TASK != disconnect_task)
  {
    GNUNET_SCHEDULER_cancel (disconnect_task);
  }

  flags = GNUNET_MESH_OPTION_DEFAULT;
  if (SPEED_REL == test)
  {
    test = SPEED;
    flags |= GNUNET_MESH_OPTION_RELIABLE;
  }
  ch = GNUNET_MESH_channel_create (h1, NULL, p_id[1], 1, flags);

  disconnect_task = GNUNET_SCHEDULER_add_delayed (SHORT_TIME,
                                                  &disconnect_mesh_peers,
                                                  (void *) __LINE__);
  if (KEEPALIVE == test)
    return; /* Don't send any data. */

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Sending data initializer...\n");
  data_ack = 0;
  data_received = 0;
  data_sent = 0;
  GNUNET_MESH_notify_transmit_ready (ch, GNUNET_NO,
                                     GNUNET_TIME_UNIT_FOREVER_REL,
                                     size_payload, &tmt_rdy, (void *) 1L);
}

/**
 * Callback to be called when the requested peer information is available
 *
 * @param cls the closure from GNUNET_TESTBED_peer_get_information()
 * @param op the operation this callback corresponds to
 * @param pinfo the result; will be NULL if the operation has failed
 * @param emsg error message if the operation has failed;
 *             NULL if the operation is successfull
 */
static void
pi_cb (void *cls,
       struct GNUNET_TESTBED_Operation *op,
       const struct GNUNET_TESTBED_PeerInformation *pinfo,
       const char *emsg)
{
  long i = (long) cls;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "id callback for %ld\n", i);

  if (NULL == pinfo || NULL != emsg)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "pi_cb: %s\n", emsg);
    abort_test (__LINE__);
    return;
  }
  p_id[i] = pinfo->result.id;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "  id: %s\n", GNUNET_i2s (p_id[i]));
  p_ids++;
  if (p_ids < 2)
    return;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Got all IDs, starting test\n");
  test_task = GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_SECONDS,
                                            &do_test, NULL);
}

/**
 * test main: start test when all peers are connected
 *
 * @param cls Closure.
 * @param ctx Argument to give to GNUNET_MESH_TEST_cleanup on test end.
 * @param num_peers Number of peers that are running.
 * @param peers Array of peers.
 * @param meshes Handle to each of the MESHs of the peers.
 */
static void
tmain (void *cls,
       struct GNUNET_MESH_TEST_Context *ctx,
       unsigned int num_peers,
       struct GNUNET_TESTBED_Peer **peers,
       struct GNUNET_MESH_Handle **meshes)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "test main\n");
  ok = 0;
  test_ctx = ctx;
  peers_running = num_peers;
  testbed_peers = peers;
  h1 = meshes[0];
  h2 = meshes[num_peers - 1];
  disconnect_task = GNUNET_SCHEDULER_add_delayed (SHORT_TIME,
                                                  &disconnect_mesh_peers,
                                                  (void *) __LINE__);
  shutdown_handle = GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_FOREVER_REL,
                                                  &shutdown_task, NULL);
  t_op[0] = GNUNET_TESTBED_peer_get_information (peers[0],
                                                 GNUNET_TESTBED_PIT_IDENTITY,
                                                 &pi_cb, (void *) 0L);
  t_op[1] = GNUNET_TESTBED_peer_get_information (peers[num_peers - 1],
                                                 GNUNET_TESTBED_PIT_IDENTITY,
                                                 &pi_cb, (void *) 1L);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "requested peer ids\n");
}


/**
 * Main: start test
 */
int
main (int argc, char *argv[])
{
  initialized = GNUNET_NO;
  uint32_t ports[2];
  const char *config_file;

  GNUNET_log_setup ("test", "DEBUG", NULL);
  config_file = "test_mesh.conf";

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Start\n");
  if (strstr (argv[0], "_small_forward") != NULL)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "FORWARD\n");
    test = FORWARD;
    test_name = "unicast";
    ok_goal = 4;
  }
  else if (strstr (argv[0], "_small_signal") != NULL)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "SIGNAL\n");
    test = P2P_SIGNAL;
    test_name = "signal";
    ok_goal = 4;
  }
  else if (strstr (argv[0], "_small_speed_ack") != NULL)
  {
    /* Test is supposed to generate the following callbacks:
     * 1 incoming channel (@dest)
     * TOTAL_PACKETS received data packet (@dest)
     * TOTAL_PACKETS received data packet (@orig)
     * 1 received channel destroy (@dest)
     */
    ok_goal = TOTAL_PACKETS * 2 + 2;
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "SPEED_ACK\n");
    test = SPEED_ACK;
    test_name = "speed ack";
  }
  else if (strstr (argv[0], "_small_speed") != NULL)
  {
    /* Test is supposed to generate the following callbacks:
     * 1 incoming channel (@dest)
     * 1 initial packet (@dest)
     * TOTAL_PACKETS received data packet (@dest)
     * 1 received data packet (@orig)
     * 1 received channel destroy (@dest)
     */
    ok_goal = TOTAL_PACKETS + 4;
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "SPEED\n");
    if (strstr (argv[0], "_reliable") != NULL)
    {
      test = SPEED_REL;
      test_name = "speed reliable";
      config_file = "test_mesh_drop.conf";
    }
    else
    {
      test = SPEED;
      test_name = "speed";
    }
  }
  else if (strstr (argv[0], "_keepalive") != NULL)
  {
    test = KEEPALIVE;
    /* Test is supposed to generate the following callbacks:
     * 1 incoming channel (@dest)
     * [wait]
     * 1 received channel destroy (@dest)
     */
    ok_goal = 2;
  }
  else
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "UNKNOWN\n");
    test = SETUP;
    ok_goal = 0;
  }

  if (strstr (argv[0], "backwards") != NULL)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "BACKWARDS (LEAF TO ROOT)\n");
    test_backwards = GNUNET_YES;
    GNUNET_asprintf (&test_name, "backwards %s", test_name);
  }

  p_ids = 0;
  ports[0] = 1;
  ports[1] = 0;
  GNUNET_MESH_TEST_run ("test_mesh_small",
                        config_file,
                        5,
                        &tmain,
                        NULL, /* tmain cls */
                        &incoming_channel,
                        &channel_cleaner,
                        handlers,
                        ports);

  if (ok_goal > ok)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "FAILED! (%d/%d)\n", ok, ok_goal);
    return 1;
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "success\n");
  return 0;
}

/* end of test_mesh_small.c */

