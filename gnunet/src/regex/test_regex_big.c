/*
     This file is part of GNUnet.
     (C) 2011, 2012 Christian Grothoff (and other contributing authors)

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
 * @file regex/test_regex_big.c
 * @brief Stream API testing between 2 peers using testing API
 * @author Bart Polot
 * @author Max Szengel
 */

#include <string.h>

#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_mesh_service.h"
#include "gnunet_stream_lib.h"
#include "gnunet_testbed_service.h"


#define NUM_HOSTS 2

#define PEER_PER_HOST 1

#define TOTAL_PEERS (NUM_HOSTS * PEER_PER_HOST)

/**
 * Shorthand for Relative time in seconds
 */
#define TIME_REL_SECS(sec) \
  GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, sec)

/**
 * Structure for holding peer's sockets and IO Handles
 */
struct PeerData
{
  /**
   * Handle to testbed peer
   */
  struct GNUNET_TESTBED_Peer *peer;

  /**
   * The service connect operation to stream
   */
  struct GNUNET_TESTBED_Operation *op;

  /**
   * Our Peer id
   */
  struct GNUNET_PeerIdentity our_id;
};


/**
 * Different states in test setup
 */
enum SetupState
{
  /**
   * The initial state
   */
  INIT,

  /**
   * Connecting to slave controller
   */
  LINKING,

  LINKING_SLAVES,

  LINKING_SLAVES_SUCCESS,

  CONNECTING_PEERS,

  CREATING_PEER,

  STARTING_PEER
};


/**
 * Event Mask for operation callbacks
 */
uint64_t event_mask;

/**
 * Testbed operation handle
 */
static struct GNUNET_TESTBED_Operation *op[NUM_HOSTS];

static enum SetupState state[NUM_HOSTS];

static GNUNET_SCHEDULER_TaskIdentifier abort_task;

/**
 * Global test result
 */
static int result;

/**
 * Hosts successfully registered
 */
static unsigned int host_registered;

/**
 * Peers successfully started
 */
static unsigned int peers_started;

/**
 * The master controller host
 */
struct GNUNET_TESTBED_Host *master_host;

/**
 * The master controller process
 */
static struct GNUNET_TESTBED_ControllerProc *master_proc;

/**
 * Handle to master controller
 */
static struct GNUNET_TESTBED_Controller *master_ctrl;

/**
 * Slave host IP addresses
 */

static char *slave_ips[NUM_HOSTS] = { "192.168.1.33", "192.168.1.34" };

/**
 * The slave hosts
 */
struct GNUNET_TESTBED_Host *slave_hosts[NUM_HOSTS];

/**
 * Slave host registration handles
 */
static struct GNUNET_TESTBED_HostRegistrationHandle *rh;

/**
 * The peers
 */
struct GNUNET_TESTBED_Peer *peers[TOTAL_PEERS];

/**
 * Handle to global configuration
 */
static struct GNUNET_CONFIGURATION_Handle *cfg;


/**
 * Completion callback for shutdown
 *
 * @param cls the closure from GNUNET_STREAM_shutdown call
 * @param operation the operation that was shutdown (SHUT_RD, SHUT_WR,
 *          SHUT_RDWR)
 */
// static void
// shutdown_completion (void *cls,
//                      int operation)
// {
//   GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "STREAM shutdown successful\n");
//   GNUNET_SCHEDULER_add_now (&do_close, cls);
// }



/**
 * Shutdown sockets gracefully
 */
// static void
// do_shutdown (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
// {
//   result = GNUNET_OK;
//   peer1.shutdown_handle = GNUNET_STREAM_shutdown (peer1.socket, SHUT_RDWR,
//                                                   &shutdown_completion, cls);
// }


/**
 * Something went wrong and timed out. Kill everything and set error flag
 */
static void
do_abort (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "test: ABORT\n");
  result = GNUNET_SYSERR;
  abort_task = 0;
}


/**
 * Adapter function called to destroy a connection to
 * a service.
 *
 * @param cls closure
 * @param op_result service handle returned from the connect adapter
 */
// static void
// stream_da (void *cls, void *op_result)
// {
//   struct GNUNET_STREAM_ListenSocket *lsocket;
//   struct GNUNET_STREAM_Socket *socket;
//
//   if (&peer1 == cls)
//   {
//     lsocket = op_result;
//     GNUNET_STREAM_listen_close (lsocket);
//     GNUNET_TESTBED_operation_done (peer2.op);
//     return;
//   }
//   if (&peer2 == cls)
//   {
//     socket = op_result;
//     GNUNET_STREAM_close (socket);
//     GNUNET_SCHEDULER_shutdown (); /* Exit point of the test */
//     return;
//   }
//   GNUNET_assert (0);
// }


/**
 * Adapter function called to establish a connection to
 * a service.
 *
 * @param cls closure
 * @param cfg configuration of the peer to connect to; will be available until
 *          GNUNET_TESTBED_operation_done() is called on the operation returned
 *          from GNUNET_TESTBED_service_connect()
 * @return service handle to return in 'op_result', NULL on error
 */
// static void *
// stream_ca (void *cls, const struct GNUNET_CONFIGURATION_Handle *cfg)
// {
//   struct GNUNET_STREAM_ListenSocket *lsocket;
//
//   switch (setup_state)
//   {
//   case PEER1_STREAM_CONNECT:
//     lsocket = GNUNET_STREAM_listen (cfg, 10, &stream_listen_cb, NULL,
//                                     GNUNET_STREAM_OPTION_SIGNAL_LISTEN_SUCCESS,
//                                     &stream_connect, GNUNET_STREAM_OPTION_END);
//     return lsocket;
//   case PEER2_STREAM_CONNECT:
//     peer2.socket = GNUNET_STREAM_open (cfg, &peer1.our_id, 10, &stream_open_cb,
//                                        &peer2, GNUNET_STREAM_OPTION_END);
//     return peer2.socket;
//   default:
//     GNUNET_assert (0);
//   }
// }


/**
 * Listen success callback; connects a peer to stream as client
 */
// static void
// stream_connect (void)
// {
//   GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Stream listen open successful\n");
//   peer2.op = GNUNET_TESTBED_service_connect (&peer2, peer2.peer, "stream",
//                                              NULL, NULL,
//                                              stream_ca, stream_da, &peer2);
//   setup_state = PEER2_STREAM_CONNECT;
// }


/**
 * Callback to be called when the requested peer information is available
 *
 * @param cb_cls the closure from GNUNET_TETSBED_peer_get_information()
 * @param op the operation this callback corresponds to
 * @param pinfo the result; will be NULL if the operation has failed
 * @param emsg error message if the operation has failed; will be NULL if the
 *          operation is successfull
 */
// static void
// peerinfo_cb (void *cb_cls, struct GNUNET_TESTBED_Operation *op_,
//              const struct GNUNET_TESTBED_PeerInformation *pinfo,
//              const char *emsg)
// {
//   GNUNET_assert (NULL == emsg);
//   GNUNET_assert (op == op_);
//   switch (setup_state)
//     {
//     case PEER1_GET_IDENTITY:
//       memcpy (&peer1.our_id, pinfo->result.id,
//               sizeof (struct GNUNET_PeerIdentity));
//       GNUNET_TESTBED_operation_done (op);
//       GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Peer 1 id: %s\n", GNUNET_i2s
//                   (&peer1.our_id));
//       op = GNUNET_TESTBED_peer_get_information (peer2.peer,
//                                                 GNUNET_TESTBED_PIT_IDENTITY,
//                                                 &peerinfo_cb, NULL);
//       setup_state = PEER2_GET_IDENTITY;
//       break;
//     case PEER2_GET_IDENTITY:
//       memcpy (&peer2.our_id, pinfo->result.id,
//               sizeof (struct GNUNET_PeerIdentity));
//       GNUNET_TESTBED_operation_done (op);
//       GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Peer 2 id: %s\n", GNUNET_i2s
//                   (&peer2.our_id));
//       peer1.op = GNUNET_TESTBED_service_connect (&peer1, peer1.peer, "stream",
//                                                  NULL, NULL, stream_ca,
//                                                  stream_da, &peer1);
//       setup_state = PEER1_STREAM_CONNECT;
//       break;
//     default:
//       GNUNET_assert (0);
//     }
// }


/**
 * Signature of a main function for a testcase.
 *
 * @param cls closure
 * @param num_peers number of peers in 'peers'
 * @param peers handle to peers run in the testbed
 */
// static void
// test_master (void *cls, unsigned int num_peers,
//              struct GNUNET_TESTBED_Peer **peers)
// {
//   GNUNET_assert (NULL != peers);
//   GNUNET_assert (NULL != peers[0]);
//   GNUNET_assert (NULL != peers[1]);
//   peer1.peer = peers[0];
//   peer2.peer = peers[1];
//   op = GNUNET_TESTBED_overlay_connect (NULL, NULL, NULL, peer2.peer, peer1.peer);
//   setup_state = INIT;
//   abort_task =
//     GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_relative_multiply
//                                   (GNUNET_TIME_UNIT_SECONDS, 40), &do_abort,
//                                   NULL);
// }

void
mesh_connect_cb (void *cls, struct GNUNET_TESTBED_Operation *op,
                 void *ca_result, const char *emsg)
{
  long i = (long) cls;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "mesh connect callback for peer %i\n",
              i);
}


void *
mesh_ca (void *cls, const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "mesh connect adapter\n");

  return NULL;
}


void
mesh_da (void *cls, void *op_result)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "mesh disconnect adapter\n");
}


/**
 * Functions of this signature are called when a peer has been successfully
 * started or stopped.
 *
 * @param cls the closure from GNUNET_TESTBED_peer_start/stop()
 * @param emsg NULL on success; otherwise an error description
 */
static void
peer_start_cb (void *cls, const char *emsg)
{
  unsigned int cnt;
  long i = (long) cls;

  GNUNET_TESTBED_operation_done (op[i]);
  peers_started++;
  // FIXME create and start rest of PEERS_PER_HOST
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, " %u peer(s) started\n", peers_started);

  if (TOTAL_PEERS == peers_started)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "All peers started.\n");
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Linking slave controllers\n");

    for (cnt = 0; cnt < NUM_HOSTS - 1; cnt++)
    {
      state[cnt] = LINKING_SLAVES;
      op[cnt] =
          GNUNET_TESTBED_get_slave_config ((void *) (long) cnt, master_ctrl,
                                           slave_hosts[cnt + 1]);
    }
  }
}


/**
 * Functions of this signature are called when a peer has been successfully
 * created
 *
 * @param cls the closure from GNUNET_TESTBED_peer_create()
 * @param peer the handle for the created peer; NULL on any error during
 *          creation
 * @param emsg NULL if peer is not NULL; else MAY contain the error description
 */
static void
peer_create_cb (void *cls, struct GNUNET_TESTBED_Peer *peer, const char *emsg)
{
  long i = (long) cls;
  long peer_id;

//   GNUNET_TESTBED_operation_done(op[i]);
  peer_id = i;                  // FIXME  A * i + B
  peers[peer_id] = peer;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, " Peer %i created\n", peer_id);
  op[i] = GNUNET_TESTBED_peer_start (NULL, peer, peer_start_cb, (void *) i);
}


/**
 * Signature of the event handler function called by the
 * respective event controller.
 *
 * @param cls closure
 * @param event information about the event
 */
static void
controller_cb (void *cls, const struct GNUNET_TESTBED_EventInformation *event)
{
  long i;

  switch (event->type)
  {
  case GNUNET_TESTBED_ET_PEER_START:
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, " Peer started\n");
    /* event->details.peer_start.peer; */
    /* event->details.peer_start.host; */

    break;
  case GNUNET_TESTBED_ET_PEER_STOP:
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Peer stopped\n");
    break;
  case GNUNET_TESTBED_ET_CONNECT:
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Overlay Connected\n");
    for (i = 0; i < TOTAL_PEERS; i++)
    {
      GNUNET_TESTBED_service_connect (NULL, peers[i], "mesh", &mesh_connect_cb,
                                      (void *) i, &mesh_ca, &mesh_da, NULL);
    }
    break;
  case GNUNET_TESTBED_ET_OPERATION_FINISHED:
    if (NULL != event->details.operation_finished.emsg)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Testbed error: %s\n",
                  event->details.operation_finished.emsg);
      GNUNET_assert (0);
    }

    i = (long) event->details.operation_finished.op_cls;
    switch (state[i])
    {
    case INIT:
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "  Init: %u\n", i);
      GNUNET_TESTBED_operation_done (event->details.
                                     operation_finished.operation);
      op[i] = NULL;
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "  Operation %u finished\n", i);
      break;
    case LINKING:
      GNUNET_TESTBED_operation_done (event->details.
                                     operation_finished.operation);
      op[i] = NULL;
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "  Operation %u finished\n", i);
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "   Linked host %i\n", i);
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "   Creating peer...\n");

      state[i] = CREATING_PEER;
      op[i] =
          GNUNET_TESTBED_peer_create (master_ctrl, slave_hosts[i], cfg,
                                      peer_create_cb, (void *) i);
      break;
    case CREATING_PEER:
      GNUNET_TESTBED_operation_done (event->details.
                                     operation_finished.operation);
      op[i] = NULL;
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "  Operation %u finished\n", i);
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "  Peer create\n");
      break;
    case LINKING_SLAVES:
    {
      struct GNUNET_CONFIGURATION_Handle *slave_cfg;

      GNUNET_assert (NULL != event->details.operation_finished.generic);
      slave_cfg =
          GNUNET_CONFIGURATION_dup ((struct GNUNET_CONFIGURATION_Handle *)
                                    event->details.operation_finished.generic);
      GNUNET_TESTBED_operation_done (event->details.
                                     operation_finished.operation);
      op[i] = NULL;
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "  Operation %u finished\n", i);
      state[i] = LINKING_SLAVES_SUCCESS;
      op[i] =
          GNUNET_TESTBED_controller_link ((void *) (long) i, master_ctrl,
                                          slave_hosts[i + 1], slave_hosts[i],
                                          slave_cfg, GNUNET_NO);
      GNUNET_CONFIGURATION_destroy (slave_cfg);
      break;
    }
    case LINKING_SLAVES_SUCCESS:
      GNUNET_TESTBED_operation_done (event->details.
                                     operation_finished.operation);
      op[i] = NULL;
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "  Operation %u finished\n", i);
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, " Linking slave %i succeeded\n", i);
      state[0] = CONNECTING_PEERS;
      op[0] =
          GNUNET_TESTBED_overlay_configure_topology (NULL, TOTAL_PEERS, peers,
                                                     GNUNET_TESTBED_TOPOLOGY_LINE);
      GNUNET_assert (NULL != op[0]);
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Connecting peers...\n");
      break;
    case CONNECTING_PEERS:
      GNUNET_TESTBED_operation_done (event->details.
                                     operation_finished.operation);
      op[i] = NULL;
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "  Operation %u finished\n", i);
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Peers connected\n");
      break;
    default:
      GNUNET_break (0);
    }
    break;
  default:
    GNUNET_break (0);
  }
}

/**
 * Callback which will be called to after a host registration succeeded or failed
 *
 * @param cls the host which has been registered
 * @param emsg the error message; NULL if host registration is successful
 */
static void
registration_cont (void *cls, const char *emsg)
{
  if (NULL != emsg)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "%s\n", emsg);
    GNUNET_assert (0);
  }
  state[host_registered] = LINKING;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, " Linking host %u\n", host_registered);
  op[host_registered] =
      GNUNET_TESTBED_controller_link ((void *) (long) host_registered,
                                      master_ctrl, slave_hosts[host_registered],
                                      NULL, cfg, GNUNET_YES);
  host_registered++;
  if (NUM_HOSTS != host_registered)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, " Registering host %u\n",
                host_registered);
    rh = GNUNET_TESTBED_register_host (master_ctrl,
                                       slave_hosts[host_registered],
                                       &registration_cont, NULL);
    return;
  }
}

/**
 * Callback to signal successfull startup of the controller process
 *
 * @param cls the closure from GNUNET_TESTBED_controller_start()
 * @param cfg the configuration with which the controller has been started;
 *          NULL if status is not GNUNET_OK
 * @param status GNUNET_OK if the startup is successfull; GNUNET_SYSERR if not,
 *          GNUNET_TESTBED_controller_stop() shouldn't be called in this case
 */
static void
status_cb (void *cls, const struct GNUNET_CONFIGURATION_Handle *config,
           int status)
{
  unsigned int i;

  if (NULL == config || GNUNET_OK != status)
    return;

  event_mask = 0;
  event_mask |= (1L << GNUNET_TESTBED_ET_PEER_START);
  event_mask |= (1L << GNUNET_TESTBED_ET_PEER_STOP);
  event_mask |= (1L << GNUNET_TESTBED_ET_CONNECT);
  event_mask |= (1L << GNUNET_TESTBED_ET_OPERATION_FINISHED);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Connecting to master controller\n");
  master_ctrl =
      GNUNET_TESTBED_controller_connect (config, master_host, event_mask,
                                         &controller_cb, NULL);
  GNUNET_assert (NULL != master_ctrl);

  for (i = 0; i < NUM_HOSTS; i++)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, " Creating host %u\n", i);
    slave_hosts[i] = GNUNET_TESTBED_host_create (slave_ips[i], NULL, 0);
    GNUNET_assert (NULL != slave_hosts[i]);
  }
  host_registered = 0;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, " Registering host %u\n",
              host_registered);
  rh = GNUNET_TESTBED_register_host (master_ctrl, slave_hosts[0],
                                     &registration_cont, NULL);
  GNUNET_assert (NULL != rh);
}


/**
 * Main run function.
 *
 * @param cls NULL
 * @param args arguments passed to GNUNET_PROGRAM_run
 * @param cfgfile the path to configuration file
 * @param cfg the configuration file handle
 */
static void
run (void *cls, char *const *args, const char *cfgfile,
     const struct GNUNET_CONFIGURATION_Handle *config)
{
  master_host = GNUNET_TESTBED_host_create ("192.168.1.33", NULL, 0);
  GNUNET_assert (NULL != master_host);
  cfg = GNUNET_CONFIGURATION_dup (config);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Starting master controller\n");
  master_proc =
      GNUNET_TESTBED_controller_start ("192.168.1.33", master_host, cfg,
                                       status_cb, NULL);
  abort_task =
      GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_relative_multiply
                                    (GNUNET_TIME_UNIT_MINUTES, 60), &do_abort,
                                    NULL);
}

/**
 * Main function
 */
int
main (int argc, char **argv)
{
  int ret;
  int test_hosts;
  unsigned int i;

  struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_OPTION_END
  };
  char *const argv2[] = { "test_big",
    "-c", "test_regex_big.conf",
    NULL
  };

  test_hosts = GNUNET_OK;
  for (i = 0; i < NUM_HOSTS; i++)
  {
    char *const remote_args[] = {
      "ssh", "-o", "BatchMode=yes", slave_ips[i],
      "gnunet-helper-testbed --help > /dev/null", NULL
    };
    struct GNUNET_OS_Process *auxp;
    enum GNUNET_OS_ProcessStatusType type;
    unsigned long code;

    fprintf (stderr, "Testing host %i\n", i);
    auxp =
        GNUNET_OS_start_process_vap (GNUNET_NO, GNUNET_OS_INHERIT_STD_ALL, NULL,
                                     NULL, "ssh", remote_args);
    GNUNET_assert (NULL != auxp);
    do
    {
      ret = GNUNET_OS_process_status (auxp, &type, &code);
      GNUNET_assert (GNUNET_SYSERR != ret);
      (void) usleep (300);
    }
    while (GNUNET_NO == ret);
    (void) GNUNET_OS_process_wait (auxp);
    GNUNET_OS_process_destroy (auxp);
    if (0 != code)
    {
      fprintf (stderr,
               "Unable to run the test as this system is not configured "
               "to use password less SSH logins to host %s.\n", slave_ips[i]);
      test_hosts = GNUNET_SYSERR;
    }
  }
  if (test_hosts != GNUNET_OK)
  {
    fprintf (stderr, "Some hosts have failed the ssh check. Exiting.\n");
    return 1;
  }
  fprintf (stderr, "START.\n");

  result = GNUNET_SYSERR;

  ret =
      GNUNET_PROGRAM_run ((sizeof (argv2) / sizeof (char *)) - 1, argv2,
                          "test_regex_big", "nohelp", options, &run, NULL);

  fprintf (stderr, "END.\n");

  if (GNUNET_SYSERR == result || GNUNET_OK != ret)
    return 1;
  return 0;
}
