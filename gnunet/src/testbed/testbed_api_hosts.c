/*
      This file is part of GNUnet
      (C) 2008--2012 Christian Grothoff (and other contributing authors)

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
 * @file testbed/testbed_api_hosts.c
 * @brief API for manipulating 'hosts' controlled by the GNUnet testing service;
 *        allows parsing hosts files, starting, stopping and communicating (via
 *        SSH/stdin/stdout) with the remote (or local) processes
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_testbed_service.h"
#include "gnunet_core_service.h"
#include "gnunet_transport_service.h"

#include "testbed_api.h"
#include "testbed_api_hosts.h"
#include "testbed_helper.h"
#include "testbed_api_operations.h"
#include "testbed_api_sd.h"

#include <zlib.h>

/**
 * Generic logging shorthand
 */
#define LOG(kind, ...)                          \
  GNUNET_log_from (kind, "testbed-api-hosts", __VA_ARGS__);

/**
 * Debug logging shorthand
 */
#define LOG_DEBUG(...)                          \
  LOG (GNUNET_ERROR_TYPE_DEBUG, __VA_ARGS__);

/**
 * Number of extra elements we create space for when we grow host list
 */
#define HOST_LIST_GROW_STEP 10


/**
 * A list entry for registered controllers list
 */
struct RegisteredController
{
  /**
   * The controller at which this host is registered
   */
  const struct GNUNET_TESTBED_Controller *controller;

  /**
   * The next ptr for DLL
   */
  struct RegisteredController *next;

  /**
   * The prev ptr for DLL
   */
  struct RegisteredController *prev;
};


/**
 * A slot to record time taken by an overlay connect operation
 */
struct TimeSlot
{
  /**
   * A key to identify this timeslot
   */
  void *key;

  /**
   * Time
   */
  struct GNUNET_TIME_Relative time;

  /**
   * Number of timing values accumulated
   */
  unsigned int nvals;
};


/**
 * Opaque handle to a host running experiments managed by the testing framework.
 * The master process must be able to SSH to this host without password (via
 * ssh-agent).
 */
struct GNUNET_TESTBED_Host
{

  /**
   * The next pointer for DLL
   */
  struct GNUNET_TESTBED_Host *next;

  /**
   * The prev pointer for DLL
   */
  struct GNUNET_TESTBED_Host *prev;

  /**
   * The hostname of the host; NULL for localhost
   */
  const char *hostname;

  /**
   * The username to be used for SSH login
   */
  const char *username;

  /**
   * the configuration to use as a template while starting a controller on this
   * host.  Operation queue size specific to a host are also read from this
   * configuration handle
   */
  struct GNUNET_CONFIGURATION_Handle *cfg;

  /**
   * The head for the list of controllers where this host is registered
   */
  struct RegisteredController *rc_head;

  /**
   * The tail for the list of controllers where this host is registered
   */
  struct RegisteredController *rc_tail;

  /**
   * Operation queue for simultaneous overlay connect operations target at this
   * host
   */
  struct OperationQueue *opq_parallel_overlay_connect_operations;

  /**
   * An array of timing slots; size should be equal to the current number of parallel
   * overlay connects
   */
  struct TimeSlot *tslots;

  /**
   * Handle for SD calculations amount parallel overlay connect operation finish
   * times
   */
  struct SDHandle *poc_sd;  

  /**
   * The number of parallel overlay connects we do currently
   */
  unsigned int num_parallel_connects;

  /**
   * Counter to indicate when all the available time slots are filled
   */
  unsigned int tslots_filled;

  /**
   * Is a controller started on this host?
   */
  int controller_started;

  /**
   * Global ID we use to refer to a host on the network
   */
  uint32_t id;

  /**
   * The port which is to be used for SSH
   */
  uint16_t port;

};


/**
 * Array of available hosts
 */
static struct GNUNET_TESTBED_Host **host_list;

/**
 * The size of the available hosts list
 */
static unsigned int host_list_size;


/**
 * Lookup a host by ID.
 *
 * @param id global host ID assigned to the host; 0 is
 *        reserved to always mean 'localhost'
 * @return handle to the host, NULL if host not found
 */
struct GNUNET_TESTBED_Host *
GNUNET_TESTBED_host_lookup_by_id_ (uint32_t id)
{
  if (host_list_size <= id)
    return NULL;
  return host_list[id];
}


/**
 * Create a host by ID; given this host handle, we could not
 * run peers at the host, but we can talk about the host
 * internally.
 *
 * @param id global host ID assigned to the host; 0 is
 *        reserved to always mean 'localhost'
 * @param cfg the configuration to use as a template while starting a controller
 *          on this host.  Operation queue sizes specific to a host are also
 *          read from this configuration handle
 * @return handle to the host, NULL on error
 */
struct GNUNET_TESTBED_Host *
GNUNET_TESTBED_host_create_by_id_ (uint32_t id,
                                   const struct GNUNET_CONFIGURATION_Handle
                                   *cfg)
{
  return GNUNET_TESTBED_host_create_with_id (id, NULL, NULL, cfg, 0);
}


/**
 * Obtain the host's unique global ID.
 *
 * @param host handle to the host, NULL means 'localhost'
 * @return id global host ID assigned to the host (0 is
 *         'localhost', but then obviously not globally unique)
 */
uint32_t
GNUNET_TESTBED_host_get_id_ (const struct GNUNET_TESTBED_Host * host)
{
  return host->id;
}


/**
 * Obtain the host's hostname.
 *
 * @param host handle to the host, NULL means 'localhost'
 * @return hostname of the host
 */
const char *
GNUNET_TESTBED_host_get_hostname (const struct GNUNET_TESTBED_Host *host)
{
  return host->hostname;
}


/**
 * Obtain the host's username
 *
 * @param host handle to the host, NULL means 'localhost'
 * @return username to login to the host
 */
const char *
GNUNET_TESTBED_host_get_username_ (const struct GNUNET_TESTBED_Host *host)
{
  return host->username;
}


/**
 * Obtain the host's ssh port
 *
 * @param host handle to the host, NULL means 'localhost'
 * @return username to login to the host
 */
uint16_t
GNUNET_TESTBED_host_get_ssh_port_ (const struct GNUNET_TESTBED_Host * host)
{
  return host->port;
}


/**
 * Check whether a controller is already started on the given host
 *
 * @param host the handle to the host
 * @return GNUNET_YES if the controller is already started; GNUNET_NO if not
 */
int
GNUNET_TESTBED_host_controller_started (const struct GNUNET_TESTBED_Host *host)
{
  return host->controller_started;
}


/**
 * Obtain the host's configuration template
 *
 * @param host handle to the host
 * @return the host's configuration template
 */
const struct GNUNET_CONFIGURATION_Handle *
GNUNET_TESTBED_host_get_cfg_ (const struct GNUNET_TESTBED_Host *host)
{
  return host->cfg;
}


/**
 * Create a host to run peers and controllers on.
 *
 * @param id global host ID assigned to the host; 0 is
 *        reserved to always mean 'localhost'
 * @param hostname name of the host, use "NULL" for localhost
 * @param username username to use for the login; may be NULL
 * @param cfg the configuration to use as a template while starting a controller
 *          on this host.  Operation queue sizes specific to a host are also
 *          read from this configuration handle
 * @param port port number to use for ssh; use 0 to let ssh decide
 * @return handle to the host, NULL on error
 */
struct GNUNET_TESTBED_Host *
GNUNET_TESTBED_host_create_with_id (uint32_t id, const char *hostname,
                                    const char *username, 
                                    const struct GNUNET_CONFIGURATION_Handle
                                    *cfg,
                                    uint16_t port)
{
  struct GNUNET_TESTBED_Host *host;
  unsigned int new_size;

  if ((id < host_list_size) && (NULL != host_list[id]))
  {
    LOG (GNUNET_ERROR_TYPE_WARNING, "Host with id: %u already created\n", id);
    return NULL;
  }
  host = GNUNET_malloc (sizeof (struct GNUNET_TESTBED_Host));
  host->hostname = (NULL != hostname) ? GNUNET_strdup (hostname) : NULL;
  host->username = (NULL != username) ? GNUNET_strdup (username) : NULL;
  host->id = id;
  host->port = (0 == port) ? 22 : port;
  host->cfg = GNUNET_CONFIGURATION_dup (cfg);
  host->opq_parallel_overlay_connect_operations =
      GNUNET_TESTBED_operation_queue_create_ (0);
  GNUNET_TESTBED_set_num_parallel_overlay_connects_ (host, 1);
  host->poc_sd = GNUNET_TESTBED_SD_init_ (10);
  new_size = host_list_size;
  while (id >= new_size)
    new_size += HOST_LIST_GROW_STEP;
  if (new_size != host_list_size)
    GNUNET_array_grow (host_list, host_list_size, new_size);
  GNUNET_assert (id < host_list_size);
  LOG (GNUNET_ERROR_TYPE_DEBUG, "Adding host with id: %u\n", host->id);
  host_list[id] = host;
  return host;
}


/**
 * Create a host to run peers and controllers on.
 *
 * @param hostname name of the host, use "NULL" for localhost
 * @param username username to use for the login; may be NULL
 * @param cfg the configuration to use as a template while starting a controller
 *          on this host.  Operation queue sizes specific to a host are also
 *          read from this configuration handle
 * @param port port number to use for ssh; use 0 to let ssh decide
 * @return handle to the host, NULL on error
 */
struct GNUNET_TESTBED_Host *
GNUNET_TESTBED_host_create (const char *hostname, const char *username,
                            const struct GNUNET_CONFIGURATION_Handle *cfg,
                            uint16_t port)
{
  static uint32_t uid_generator;

  if (NULL == hostname)
    return GNUNET_TESTBED_host_create_with_id (0, hostname, username, 
                                               cfg, port);
  return GNUNET_TESTBED_host_create_with_id (++uid_generator, hostname,
                                             username, cfg, port);
}


/**
 * Load a set of hosts from a configuration file.
 *
 * @param filename file with the host specification
 * @param cfg the configuration to use as a template while starting a controller
 *          on any of the loaded hosts.  Operation queue sizes specific to a host
 *          are also read from this configuration handle
 * @param hosts set to the hosts found in the file; caller must free this if
 *          number of hosts returned is greater than 0
 * @return number of hosts returned in 'hosts', 0 on error
 */
unsigned int
GNUNET_TESTBED_hosts_load_from_file (const char *filename,
                                     const struct GNUNET_CONFIGURATION_Handle
                                     *cfg,
                                     struct GNUNET_TESTBED_Host ***hosts)
{
  //struct GNUNET_TESTBED_Host **host_array;
  struct GNUNET_TESTBED_Host *starting_host;
  char *data;
  char *buf;
  char username[256];
  char hostname[256];
  uint64_t fs;
  short int port;
  int ret;
  unsigned int offset;
  unsigned int count;


  GNUNET_assert (NULL != filename);
  if (GNUNET_YES != GNUNET_DISK_file_test (filename))
  {
    LOG (GNUNET_ERROR_TYPE_WARNING, _("Hosts file %s not found\n"), filename);
    return 0;
  }
  if (GNUNET_OK !=
      GNUNET_DISK_file_size (filename, &fs, GNUNET_YES, GNUNET_YES))
    fs = 0;
  if (0 == fs)
  {
    LOG (GNUNET_ERROR_TYPE_WARNING, _("Hosts file %s has no data\n"), filename);
    return 0;
  }
  data = GNUNET_malloc (fs);
  if (fs != GNUNET_DISK_fn_read (filename, data, fs))
  {
    GNUNET_free (data);
    LOG (GNUNET_ERROR_TYPE_WARNING, _("Hosts file %s cannot be read\n"),
         filename);
    return 0;
  }
  buf = data;
  offset = 0;
  starting_host = NULL;
  count = 0;
  while (offset < (fs - 1))
  {
    offset++;
    if (((data[offset] == '\n')) && (buf != &data[offset]))
    {
      data[offset] = '\0';
      ret =
          SSCANF (buf, "%255[a-zA-Z0-9_]@%255[a-zA-Z0-9.]:%5hd", username,
                  hostname, &port);
      if (3 == ret)
      {
        GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                    "Successfully read host %s, port %d and user %s from file\n",
                    hostname, port, username);
        /* We store hosts in a static list; hence we only require the starting
         * host pointer in that list to access the newly created list of hosts */
        if (NULL == starting_host)
          starting_host = GNUNET_TESTBED_host_create (hostname, username, cfg,
                                                      port);
        else
          (void) GNUNET_TESTBED_host_create (hostname, username, cfg, port);
        count++;
      }
      else
        GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                    "Error reading line `%s' in hostfile\n", buf);
      buf = &data[offset + 1];
    }
    else if ((data[offset] == '\n') || (data[offset] == '\0'))
      buf = &data[offset + 1];
  }
  GNUNET_free (data);
  if (NULL == starting_host)
    return 0;
  *hosts = GNUNET_malloc (sizeof (struct GNUNET_TESTBED_Host *) * count);
  memcpy (*hosts, &host_list[GNUNET_TESTBED_host_get_id_ (starting_host)],
          sizeof (struct GNUNET_TESTBED_Host *) * count);
  return count;
}


/**
 * Destroy a host handle.  Must only be called once everything
 * running on that host has been stopped.
 *
 * @param host handle to destroy
 */
void
GNUNET_TESTBED_host_destroy (struct GNUNET_TESTBED_Host *host)
{
  struct RegisteredController *rc;
  uint32_t id;

  GNUNET_assert (host->id < host_list_size);
  GNUNET_assert (host_list[host->id] == host);
  host_list[host->id] = NULL;
  /* clear registered controllers list */
  for (rc = host->rc_head; NULL != rc; rc = host->rc_head)
  {
    GNUNET_CONTAINER_DLL_remove (host->rc_head, host->rc_tail, rc);
    GNUNET_free (rc);
  }
  GNUNET_free_non_null ((char *) host->username);
  GNUNET_free_non_null ((char *) host->hostname);
  GNUNET_TESTBED_operation_queue_destroy_
      (host->opq_parallel_overlay_connect_operations);
  GNUNET_TESTBED_SD_destroy_ (host->poc_sd);
  GNUNET_free_non_null (host->tslots);
  GNUNET_CONFIGURATION_destroy (host->cfg);
  GNUNET_free (host);
  while (host_list_size >= HOST_LIST_GROW_STEP)
  {
    for (id = host_list_size - 1; id > host_list_size - HOST_LIST_GROW_STEP;
         id--)
      if (NULL != host_list[id])
        break;
    if (id != host_list_size - HOST_LIST_GROW_STEP)
      break;
    if (NULL != host_list[id])
      break;
    host_list_size -= HOST_LIST_GROW_STEP;
  }
  host_list =
      GNUNET_realloc (host_list,
                      sizeof (struct GNUNET_TESTBED_Host *) * host_list_size);
}


/**
 * Marks a host as registered with a controller
 *
 * @param host the host to mark
 * @param controller the controller at which this host is registered
 */
void
GNUNET_TESTBED_mark_host_registered_at_ (struct GNUNET_TESTBED_Host *host,
                                         const struct GNUNET_TESTBED_Controller
                                         *const controller)
{
  struct RegisteredController *rc;

  for (rc = host->rc_head; NULL != rc; rc = rc->next)
  {
    if (controller == rc->controller)   /* already registered at controller */
    {
      GNUNET_break (0);
      return;
    }
  }
  rc = GNUNET_malloc (sizeof (struct RegisteredController));
  rc->controller = controller;
  GNUNET_CONTAINER_DLL_insert_tail (host->rc_head, host->rc_tail, rc);
}


/**
 * Checks whether a host has been registered
 *
 * @param host the host to check
 * @param controller the controller at which host's registration is checked
 * @return GNUNET_YES if registered; GNUNET_NO if not
 */
int
GNUNET_TESTBED_is_host_registered_ (const struct GNUNET_TESTBED_Host *host,
                                    const struct GNUNET_TESTBED_Controller
                                    *const controller)
{
  struct RegisteredController *rc;

  for (rc = host->rc_head; NULL != rc; rc = rc->next)
  {
    if (controller == rc->controller)   /* already registered at controller */
    {
      return GNUNET_YES;
    }
  }
  return GNUNET_NO;
}


/**
 * Handle for controller process
 */
struct GNUNET_TESTBED_ControllerProc
{
  /**
   * The process handle
   */
  struct GNUNET_HELPER_Handle *helper;

  /**
   * The arguments used to start the helper
   */
  char **helper_argv;

  /**
   * The host where the helper is run
   */
  struct GNUNET_TESTBED_Host *host;

  /**
   * The controller error callback
   */
  GNUNET_TESTBED_ControllerStatusCallback cb;

  /**
   * The closure for the above callback
   */
  void *cls;

  /**
   * The send handle for the helper
   */
  struct GNUNET_HELPER_SendHandle *shandle;

  /**
   * The message corresponding to send handle
   */
  struct GNUNET_MessageHeader *msg;

  /**
   * The configuration of the running testbed service
   */
  struct GNUNET_CONFIGURATION_Handle *cfg;

};


/**
 * Function to copy NULL terminated list of arguments
 *
 * @param argv the NULL terminated list of arguments. Cannot be NULL.
 * @return the copied NULL terminated arguments
 */
static char **
copy_argv (const char *const *argv)
{
  char **argv_dup;
  unsigned int argp;

  GNUNET_assert (NULL != argv);
  for (argp = 0; NULL != argv[argp]; argp++) ;
  argv_dup = GNUNET_malloc (sizeof (char *) * (argp + 1));
  for (argp = 0; NULL != argv[argp]; argp++)
    argv_dup[argp] = strdup (argv[argp]);
  return argv_dup;
}


/**
 * Function to join NULL terminated list of arguments
 *
 * @param argv1 the NULL terminated list of arguments. Cannot be NULL.
 * @param argv2 the NULL terminated list of arguments. Cannot be NULL.
 * @return the joined NULL terminated arguments
 */
static char **
join_argv (const char *const *argv1, const char *const *argv2)
{
  char **argvj;
  char *argv;
  unsigned int carg;
  unsigned int cnt;

  carg = 0;
  argvj = NULL;
  for (cnt = 0; NULL != argv1[cnt]; cnt++)
  {
    argv = GNUNET_strdup (argv1[cnt]);
    GNUNET_array_append (argvj, carg, argv);
  }
  for (cnt = 0; NULL != argv2[cnt]; cnt++)
  {
    argv = GNUNET_strdup (argv2[cnt]);
    GNUNET_array_append (argvj, carg, argv);
  }
  GNUNET_array_append (argvj, carg, NULL);
  return argvj;
}


/**
 * Frees the given NULL terminated arguments
 *
 * @param argv the NULL terminated list of arguments
 */
static void
free_argv (char **argv)
{
  unsigned int argp;

  for (argp = 0; NULL != argv[argp]; argp++)
    GNUNET_free (argv[argp]);
  GNUNET_free (argv);
}


/**
 * Generates arguments for opening a remote shell. Builds up the arguments
 * from the environment variable GNUNET_TESTBED_RSH_CMD. The variable
 * should not mention `-p' (port) option and destination address as these will
 * be set locally in the function from its parameteres. If the environmental
 * variable is not found then it defaults to `ssh -o BatchMode=yes -o
 * NoHostAuthenticationForLocalhost=yes'
 *
 * @param port the destination port number
 * @param dst the destination address
 * @return NULL terminated list of arguments
 */
static char **
gen_rsh_args (const char *port, const char *dst)
{
  static const char *default_ssh_args[] = {
    "ssh",
    "-o",
    "BatchMode=yes",
    "-o",
    "NoHostAuthenticationForLocalhost=yes",
    NULL
  };
  char **ssh_args;
  char *ssh_cmd;
  char *ssh_cmd_cp;
  char *arg;
  unsigned int cnt;

  ssh_args = NULL;
  if (NULL != (ssh_cmd = getenv ("GNUNET_TESTBED_RSH_CMD")))
  {
    ssh_cmd = GNUNET_strdup (ssh_cmd);
    ssh_cmd_cp = ssh_cmd;
    for (cnt = 0; NULL != (arg = strtok (ssh_cmd, " ")); ssh_cmd = NULL)
      GNUNET_array_append (ssh_args, cnt, GNUNET_strdup (arg));
    GNUNET_free (ssh_cmd_cp);
  }
  else
  {
    ssh_args = copy_argv (default_ssh_args);
    cnt = (sizeof (default_ssh_args)) / (sizeof (const char *));
    GNUNET_array_grow (ssh_args, cnt, cnt - 1);
  }
  GNUNET_array_append (ssh_args, cnt, GNUNET_strdup ("-p"));
  GNUNET_array_append (ssh_args, cnt, GNUNET_strdup (port));
  GNUNET_array_append (ssh_args, cnt, GNUNET_strdup (dst));
  GNUNET_array_append (ssh_args, cnt, NULL);
  return ssh_args;
}


/**
 * Generates the arguments needed for executing the given binary in a remote
 * shell. Builds the arguments from the environmental variable
 * GNUNET_TETSBED_RSH_CMD_SUFFIX. If the environmental variable is not found,
 * only the given binary name will be present in the returned arguments
 *
 * @param append_args the arguments to append after generating the suffix
 *          arguments. Can be NULL; if not must be NULL terminated 'char *' array
 * @return NULL-terminated args
 */
static char **
gen_rsh_suffix_args (const char * const *append_args)
{
  char **rshell_args;
  char *rshell_cmd;
  char *rshell_cmd_cp;
  char *arg;
  unsigned int cnt;
  unsigned int append_cnt;

  rshell_args = NULL;
  cnt = 0;
  if (NULL != (rshell_cmd = getenv ("GNUNET_TESTBED_RSH_CMD_SUFFIX")))
  {
    rshell_cmd = GNUNET_strdup (rshell_cmd);
    rshell_cmd_cp = rshell_cmd;
    for (; NULL != (arg = strtok (rshell_cmd, " ")); rshell_cmd = NULL)
      GNUNET_array_append (rshell_args, cnt, GNUNET_strdup (arg));
    GNUNET_free (rshell_cmd_cp);
  }
  if (NULL != append_args)
  {
    for (append_cnt = 0; NULL != append_args[append_cnt]; append_cnt++)      
      GNUNET_array_append (rshell_args, cnt, GNUNET_strdup (append_args[append_cnt]));
  }
  GNUNET_array_append (rshell_args, cnt, NULL);
  return rshell_args;
}


/**
 * Functions with this signature are called whenever a
 * complete message is received by the tokenizer.
 *
 * Do not call GNUNET_SERVER_mst_destroy in callback
 *
 * @param cls closure
 * @param client identification of the client
 * @param message the actual message
 *
 * @return GNUNET_OK on success, GNUNET_SYSERR to stop further processing
 */
static int
helper_mst (void *cls, void *client, const struct GNUNET_MessageHeader *message)
{
  struct GNUNET_TESTBED_ControllerProc *cp = cls;
  const struct GNUNET_TESTBED_HelperReply *msg;
  const char *hostname;
  char *config;
  uLongf config_size;
  uLongf xconfig_size;

  msg = (const struct GNUNET_TESTBED_HelperReply *) message;
  GNUNET_assert (sizeof (struct GNUNET_TESTBED_HelperReply) <
                 ntohs (msg->header.size));
  GNUNET_assert (GNUNET_MESSAGE_TYPE_TESTBED_HELPER_REPLY ==
                 ntohs (msg->header.type));
  config_size = (uLongf) ntohs (msg->config_size);
  xconfig_size =
      (uLongf) (ntohs (msg->header.size) -
                sizeof (struct GNUNET_TESTBED_HelperReply));
  config = GNUNET_malloc (config_size);
  GNUNET_assert (Z_OK ==
                 uncompress ((Bytef *) config, &config_size,
                             (const Bytef *) &msg[1], xconfig_size));
  GNUNET_assert (NULL == cp->cfg);
  cp->cfg = GNUNET_CONFIGURATION_create ();
  GNUNET_assert (GNUNET_CONFIGURATION_deserialize
                 (cp->cfg, config, config_size, GNUNET_NO));
  GNUNET_free (config);
  if ((NULL == cp->host) ||
      (NULL == (hostname = GNUNET_TESTBED_host_get_hostname (cp->host))))
    hostname = "localhost";
  /* Change the hostname so that we can connect to it */
  GNUNET_CONFIGURATION_set_value_string (cp->cfg, "testbed", "hostname",
                                         hostname);
  cp->cb (cp->cls, cp->cfg, GNUNET_OK);
  return GNUNET_OK;
}


/**
 * Continuation function from GNUNET_HELPER_send()
 *
 * @param cls closure
 * @param result GNUNET_OK on success,
 *               GNUNET_NO if helper process died
 *               GNUNET_SYSERR during GNUNET_HELPER_stop
 */
static void
clear_msg (void *cls, int result)
{
  struct GNUNET_TESTBED_ControllerProc *cp = cls;

  GNUNET_assert (NULL != cp->shandle);
  cp->shandle = NULL;
  GNUNET_free (cp->msg);
}


/**
 * Callback that will be called when the helper process dies. This is not called
 * when the helper process is stoped using GNUNET_HELPER_stop()
 *
 * @param cls the closure from GNUNET_HELPER_start()
 */
static void
helper_exp_cb (void *cls)
{
  struct GNUNET_TESTBED_ControllerProc *cp = cls;
  GNUNET_TESTBED_ControllerStatusCallback cb;
  void *cb_cls;

  cb = cp->cb;
  cb_cls = cp->cls;
  cp->helper = NULL;
  GNUNET_TESTBED_controller_stop (cp);
  if (NULL != cb)
    cb (cb_cls, NULL, GNUNET_SYSERR);
}


/**
 * Starts a controller process at the given host
 *
 * @param trusted_ip the ip address of the controller which will be set as TRUSTED
 *          HOST(all connections form this ip are permitted by the testbed) when
 *          starting testbed controller at host. This can either be a single ip
 *          address or a network address in CIDR notation.
 * @param host the host where the controller has to be started; NULL for
 *          localhost
 * @param cfg template configuration to use for the remote controller; the
 *          remote controller will be started with a slightly modified
 *          configuration (port numbers, unix domain sockets and service home
 *          values are changed as per TESTING library on the remote host)
 * @param cb function called when the controller is successfully started or
 *          dies unexpectedly; GNUNET_TESTBED_controller_stop shouldn't be
 *          called if cb is called with GNUNET_SYSERR as status. Will never be
 *          called in the same task as 'GNUNET_TESTBED_controller_start'
 *          (synchronous errors will be signalled by returning NULL). This
 *          parameter cannot be NULL.
 * @param cls closure for above callbacks
 * @return the controller process handle, NULL on errors
 */
struct GNUNET_TESTBED_ControllerProc *
GNUNET_TESTBED_controller_start (const char *trusted_ip,
                                 struct GNUNET_TESTBED_Host *host,
                                 const struct GNUNET_CONFIGURATION_Handle *cfg,
                                 GNUNET_TESTBED_ControllerStatusCallback cb,
                                 void *cls)
{
  struct GNUNET_TESTBED_ControllerProc *cp;
  struct GNUNET_TESTBED_HelperInit *msg;
  const char *hostname;

  static char *const binary_argv[] = {
    HELPER_TESTBED_BINARY, NULL
  };

  hostname = NULL;  
  cp = GNUNET_malloc (sizeof (struct GNUNET_TESTBED_ControllerProc));
  if ((NULL == host) || (0 == GNUNET_TESTBED_host_get_id_ (host)))
  {
    cp->helper =
        GNUNET_HELPER_start (GNUNET_YES, HELPER_TESTBED_BINARY, binary_argv,
                             &helper_mst, &helper_exp_cb, cp);
  }
  else
  {
    char *helper_binary_path_args[2];
    char **rsh_args;
    char **rsh_suffix_args;
    const char *username;
    char *port;
    char *dst;

    username = GNUNET_TESTBED_host_get_username_ (host);
    hostname = GNUNET_TESTBED_host_get_hostname (host);
    GNUNET_asprintf (&port, "%u", GNUNET_TESTBED_host_get_ssh_port_ (host));
    if (NULL == username)
      GNUNET_asprintf (&dst, "%s", hostname);
    else
      GNUNET_asprintf (&dst, "%s@%s", username, hostname);
    LOG_DEBUG ("Starting SSH to destination %s\n", dst);

    if (GNUNET_OK !=
        GNUNET_CONFIGURATION_get_value_string (cfg, "testbed",
                                               "HELPER_BINARY_PATH",
                                               &helper_binary_path_args[0]))
      helper_binary_path_args[0] =
          GNUNET_OS_get_libexec_binary_path (HELPER_TESTBED_BINARY);
    helper_binary_path_args[1] = NULL;
    rsh_args = gen_rsh_args (port, dst);
    rsh_suffix_args = gen_rsh_suffix_args ((const char **) helper_binary_path_args);
    cp->helper_argv =
        join_argv ((const char **) rsh_args, (const char **) rsh_suffix_args);
    free_argv (rsh_args);
    free_argv (rsh_suffix_args);
    GNUNET_free (port);
    GNUNET_free (dst);
    cp->helper =
        GNUNET_HELPER_start (GNUNET_NO, cp->helper_argv[0], cp->helper_argv, &helper_mst,
                             &helper_exp_cb, cp);
    GNUNET_free (helper_binary_path_args[0]);
  }
  if (NULL == cp->helper)
  {
    if (NULL != cp->helper_argv)
      free_argv (cp->helper_argv);
    GNUNET_free (cp);
    return NULL;
  }
  cp->host = host;
  cp->cb = cb;
  cp->cls = cls;
  msg = GNUNET_TESTBED_create_helper_init_msg_ (trusted_ip, hostname, cfg);
  cp->msg = &msg->header;
  cp->shandle =
      GNUNET_HELPER_send (cp->helper, &msg->header, GNUNET_NO, &clear_msg, cp);
  if (NULL == cp->shandle)
  {
    GNUNET_free (msg);
    GNUNET_TESTBED_controller_stop (cp);
    return NULL;
  }
  return cp;
}


/**
 * Stop the controller process (also will terminate all peers and controllers
 * dependent on this controller).  This function blocks until the testbed has
 * been fully terminated (!). The controller status cb from
 * GNUNET_TESTBED_controller_start() will not be called.
 *
 * @param cproc the controller process handle
 */
void
GNUNET_TESTBED_controller_stop (struct GNUNET_TESTBED_ControllerProc *cproc)
{
  if (NULL != cproc->shandle)
    GNUNET_HELPER_send_cancel (cproc->shandle);
  if (NULL != cproc->helper)
    GNUNET_HELPER_soft_stop (cproc->helper);
  if (NULL != cproc->cfg)
    GNUNET_CONFIGURATION_destroy (cproc->cfg);
  if (NULL != cproc->helper_argv)
    free_argv (cproc->helper_argv);
  GNUNET_free (cproc);
}


/**
 * The handle for whether a host is habitable or not
 */
struct GNUNET_TESTBED_HostHabitableCheckHandle
{
  /**
   * The host to check
   */
  const struct GNUNET_TESTBED_Host *host;

  /* /\** */
  /*  * the configuration handle to lookup the path of the testbed helper */
  /*  *\/ */
  /* const struct GNUNET_CONFIGURATION_Handle *cfg; */

  /**
   * The callback to call once we have the status
   */
  GNUNET_TESTBED_HostHabitableCallback cb;

  /**
   * The callback closure
   */
  void *cb_cls;

  /**
   * The process handle for the SSH process
   */
  struct GNUNET_OS_Process *auxp;

  /**
   * The arguments used to start the helper
   */
  char **helper_argv;

  /**
   * Task id for the habitability check task
   */
  GNUNET_SCHEDULER_TaskIdentifier habitability_check_task;

  /**
   * How long we wait before checking the process status. Should grow
   * exponentially
   */
  struct GNUNET_TIME_Relative wait_time;

};


/**
 * Task for checking whether a host is habitable or not
 *
 * @param cls GNUNET_TESTBED_HostHabitableCheckHandle
 * @param tc the scheduler task context
 */
static void
habitability_check (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_TESTBED_HostHabitableCheckHandle *h = cls;
  void *cb_cls;
  GNUNET_TESTBED_HostHabitableCallback cb;
  const struct GNUNET_TESTBED_Host *host;
  unsigned long code;
  enum GNUNET_OS_ProcessStatusType type;
  int ret;

  h->habitability_check_task = GNUNET_SCHEDULER_NO_TASK;
  ret = GNUNET_OS_process_status (h->auxp, &type, &code);
  if (GNUNET_SYSERR == ret)
  {
    GNUNET_break (0);
    ret = GNUNET_NO;
    goto call_cb;
  }
  if (GNUNET_NO == ret)
  {
    h->wait_time = GNUNET_TIME_STD_BACKOFF (h->wait_time);
    h->habitability_check_task =
        GNUNET_SCHEDULER_add_delayed (h->wait_time, &habitability_check, h);
    return;
  }
  GNUNET_OS_process_destroy (h->auxp);
  h->auxp = NULL;
  ret = (0 != code) ? GNUNET_NO : GNUNET_YES;

call_cb:
  if (NULL != h->auxp)
    GNUNET_OS_process_destroy (h->auxp);
  cb = h->cb;
  cb_cls = h->cb_cls;
  host = h->host;
  GNUNET_free (h);
  if (NULL != cb)
    cb (cb_cls, host, ret);
}


/**
 * Checks whether a host can be used to start testbed service
 *
 * @param host the host to check
 * @param config the configuration handle to lookup the path of the testbed
 *          helper
 * @param cb the callback to call to inform about habitability of the given host
 * @param cb_cls the closure for the callback
 * @return NULL upon any error or a handle which can be passed to
 *           GNUNET_TESTBED_is_host_habitable_cancel()
 */
struct GNUNET_TESTBED_HostHabitableCheckHandle *
GNUNET_TESTBED_is_host_habitable (const struct GNUNET_TESTBED_Host *host,
                                  const struct GNUNET_CONFIGURATION_Handle
                                  *config,
                                  GNUNET_TESTBED_HostHabitableCallback cb,
                                  void *cb_cls)
{
  struct GNUNET_TESTBED_HostHabitableCheckHandle *h;
  char **rsh_args;
  char **rsh_suffix_args;
  char *stat_args[3];
  const char *hostname;
  char *port;
  char *dst;

  h = GNUNET_malloc (sizeof (struct GNUNET_TESTBED_HostHabitableCheckHandle));
  h->cb = cb;
  h->cb_cls = cb_cls;
  h->host = host;
  hostname = (NULL == host->hostname) ? "127.0.0.1" : host->hostname;
  if (NULL == host->username)
    dst = GNUNET_strdup (hostname);
  else
    GNUNET_asprintf (&dst, "%s@%s", host->username, hostname);
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_string (config, "testbed",
                                             "HELPER_BINARY_PATH",
                                             &stat_args[1]))
    stat_args[1] =
        GNUNET_OS_get_libexec_binary_path (HELPER_TESTBED_BINARY);  
  GNUNET_asprintf (&port, "%u", host->port);
  rsh_args = gen_rsh_args (port, dst);
  GNUNET_free (port);
  GNUNET_free (dst);
  port = NULL;
  dst = NULL;
  stat_args[0] = "stat";
  stat_args[2] = NULL;
  rsh_suffix_args = gen_rsh_suffix_args ((const char **) stat_args);
  h->helper_argv = join_argv ((const char **) rsh_args,
                              (const char **) rsh_suffix_args);
  GNUNET_free (rsh_suffix_args);
  GNUNET_free (rsh_args);
  h->auxp =
      GNUNET_OS_start_process_vap (GNUNET_NO, GNUNET_OS_INHERIT_STD_ERR, NULL,
                                   NULL, h->helper_argv[0], h->helper_argv);
  if (NULL == h->auxp)
  {
    GNUNET_break (0);           /* Cannot exec SSH? */
    GNUNET_free (h);
    return NULL;
  }
  h->wait_time = GNUNET_TIME_STD_BACKOFF (h->wait_time);
  h->habitability_check_task =
      GNUNET_SCHEDULER_add_delayed (h->wait_time, &habitability_check, h);
  return h;
}


/**
 * Function to cancel a request started using GNUNET_TESTBED_is_host_habitable()
 *
 * @param handle the habitability check handle
 */
void
GNUNET_TESTBED_is_host_habitable_cancel (struct
                                         GNUNET_TESTBED_HostHabitableCheckHandle
                                         *handle)
{
  GNUNET_SCHEDULER_cancel (handle->habitability_check_task);
  (void) GNUNET_OS_process_kill (handle->auxp, SIGTERM);
  (void) GNUNET_OS_process_wait (handle->auxp);
  GNUNET_OS_process_destroy (handle->auxp);
  free_argv (handle->helper_argv);
  GNUNET_free (handle);
}


/**
 * handle for host registration
 */
struct GNUNET_TESTBED_HostRegistrationHandle
{
  /**
   * The host being registered
   */
  struct GNUNET_TESTBED_Host *host;

  /**
   * The controller at which this host is being registered
   */
  struct GNUNET_TESTBED_Controller *c;

  /**
   * The Registartion completion callback
   */
  GNUNET_TESTBED_HostRegistrationCompletion cc;

  /**
   * The closure for above callback
   */
  void *cc_cls;
};


/**
 * Register a host with the controller
 *
 * @param controller the controller handle
 * @param host the host to register
 * @param cc the completion callback to call to inform the status of
 *          registration. After calling this callback the registration handle
 *          will be invalid. Cannot be NULL.
 * @param cc_cls the closure for the cc
 * @return handle to the host registration which can be used to cancel the
 *           registration
 */
struct GNUNET_TESTBED_HostRegistrationHandle *
GNUNET_TESTBED_register_host (struct GNUNET_TESTBED_Controller *controller,
                              struct GNUNET_TESTBED_Host *host,
                              GNUNET_TESTBED_HostRegistrationCompletion cc,
                              void *cc_cls)
{
  struct GNUNET_TESTBED_HostRegistrationHandle *rh;
  struct GNUNET_TESTBED_AddHostMessage *msg;
  const char *username;
  const char *hostname;
  char *config;
  char *cconfig;
  void *ptr;
  size_t cc_size;
  size_t config_size;
  uint16_t msg_size;
  uint16_t username_length;
  uint16_t hostname_length;

  if (NULL != controller->rh)
    return NULL;
  hostname = GNUNET_TESTBED_host_get_hostname (host);
  if (GNUNET_YES == GNUNET_TESTBED_is_host_registered_ (host, controller))
  {
    LOG (GNUNET_ERROR_TYPE_WARNING, "Host hostname: %s already registered\n",
         (NULL == hostname) ? "localhost" : hostname);
    return NULL;
  }
  rh = GNUNET_malloc (sizeof (struct GNUNET_TESTBED_HostRegistrationHandle));
  rh->host = host;
  rh->c = controller;
  GNUNET_assert (NULL != cc);
  rh->cc = cc;
  rh->cc_cls = cc_cls;
  controller->rh = rh;
  username = GNUNET_TESTBED_host_get_username_ (host);
  username_length = 0;
  if (NULL != username)
    username_length = strlen (username);
  GNUNET_assert (NULL != hostname); /* Hostname must be present */
  hostname_length = strlen (hostname);
  GNUNET_assert (NULL != host->cfg);
  config = GNUNET_CONFIGURATION_serialize (host->cfg, &config_size);
  cc_size = GNUNET_TESTBED_compress_config_ (config, config_size, &cconfig);
  GNUNET_free (config);
  msg_size = (sizeof (struct GNUNET_TESTBED_AddHostMessage));
  msg_size += username_length;
  msg_size += hostname_length;
  msg_size += cc_size;
  msg = GNUNET_malloc (msg_size);
  msg->header.size = htons (msg_size);
  msg->header.type = htons (GNUNET_MESSAGE_TYPE_TESTBED_ADD_HOST);
  msg->host_id = htonl (GNUNET_TESTBED_host_get_id_ (host));
  msg->ssh_port = htons (GNUNET_TESTBED_host_get_ssh_port_ (host));
  ptr = &msg[1];
  if (NULL != username)
  {
    msg->username_length = htons (username_length);
    ptr = memcpy (ptr, username, username_length);
    ptr += username_length;
  }
  msg->hostname_length = htons (hostname_length);
  ptr = memcpy (ptr, hostname, hostname_length);
  ptr += hostname_length;
  msg->config_size = htons (config_size);
  ptr = memcpy (ptr, cconfig, cc_size);
  ptr += cc_size;
  GNUNET_assert ((ptr - (void *) msg) == msg_size);
  GNUNET_free (cconfig);
  GNUNET_TESTBED_queue_message_ (controller,
                                 (struct GNUNET_MessageHeader *) msg);
  return rh;
}


/**
 * Cancel the pending registration. Note that if the registration message is
 * already sent to the service the cancellation has only the effect that the
 * registration completion callback for the registration is never called.
 *
 * @param handle the registration handle to cancel
 */
void
GNUNET_TESTBED_cancel_registration (struct GNUNET_TESTBED_HostRegistrationHandle
                                    *handle)
{
  if (handle != handle->c->rh)
  {
    GNUNET_break (0);
    return;
  }
  handle->c->rh = NULL;
  GNUNET_free (handle);
}


/**
 * Initializes the operation queue for parallel overlay connects
 *
 * @param h the host handle
 * @param npoc the number of parallel overlay connects - the queue size
 */
void
GNUNET_TESTBED_set_num_parallel_overlay_connects_ (struct
                                                   GNUNET_TESTBED_Host *h,
                                                   unsigned int npoc)
{
  //fprintf (stderr, "%d", npoc);
  GNUNET_free_non_null (h->tslots);
  h->tslots_filled = 0;
  h->num_parallel_connects = npoc;
  h->tslots = GNUNET_malloc (npoc * sizeof (struct TimeSlot));
  GNUNET_TESTBED_operation_queue_reset_max_active_
      (h->opq_parallel_overlay_connect_operations, npoc);
}


/**
 * Returns a timing slot which will be exclusively locked
 *
 * @param h the host handle
 * @param key a pointer which is associated to the returned slot; should not be
 *          NULL. It serves as a key to determine the correct owner of the slot
 * @return the time slot index in the array of time slots in the controller
 *           handle
 */
unsigned int
GNUNET_TESTBED_get_tslot_ (struct GNUNET_TESTBED_Host *h, void *key)
{
  unsigned int slot;

  GNUNET_assert (NULL != h->tslots);
  GNUNET_assert (NULL != key);
  for (slot = 0; slot < h->num_parallel_connects; slot++)
    if (NULL == h->tslots[slot].key)
    {
      h->tslots[slot].key = key;
      return slot;
    }
  GNUNET_assert (0);            /* We should always find a free tslot */
}


/**
 * Decides whether any change in the number of parallel overlay connects is
 * necessary to adapt to the load on the system
 *
 * @param h the host handle
 */
static void
decide_npoc (struct GNUNET_TESTBED_Host *h)
{
  struct GNUNET_TIME_Relative avg;
  int sd;
  unsigned int slot;
  unsigned int nvals;

  if (h->tslots_filled != h->num_parallel_connects)
    return;
  avg = GNUNET_TIME_UNIT_ZERO;
  nvals = 0;
  for (slot = 0; slot < h->num_parallel_connects; slot++)
  {
    avg = GNUNET_TIME_relative_add (avg, h->tslots[slot].time);
    nvals += h->tslots[slot].nvals;
  }
  GNUNET_assert (nvals >= h->num_parallel_connects);
  avg = GNUNET_TIME_relative_divide (avg, nvals);
  GNUNET_assert (GNUNET_TIME_UNIT_FOREVER_REL.rel_value != avg.rel_value);
  sd = GNUNET_TESTBED_SD_deviation_factor_ (h->poc_sd, (unsigned int) avg.rel_value);
  if ( (sd <= 5) ||
       (0 == GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK,
				       h->num_parallel_connects)) )
    GNUNET_TESTBED_SD_add_data_ (h->poc_sd, (unsigned int) avg.rel_value);
  if (GNUNET_SYSERR == sd)
  {
    GNUNET_TESTBED_set_num_parallel_overlay_connects_ (h,
                                                       h->num_parallel_connects);
    return;
  }
  GNUNET_assert (0 <= sd);
  if (0 == sd)
  {
    GNUNET_TESTBED_set_num_parallel_overlay_connects_ (h,
                                                       h->num_parallel_connects
                                                       * 2);
    return;
  }
  if (1 == sd)
  {
    GNUNET_TESTBED_set_num_parallel_overlay_connects_ (h,
                                                       h->num_parallel_connects
                                                       + 1);
    return;
  }
  if (1 == h->num_parallel_connects)
  {
    GNUNET_TESTBED_set_num_parallel_overlay_connects_ (h, 1);
    return;
  }
  if (2 == sd)
  {
    GNUNET_TESTBED_set_num_parallel_overlay_connects_ (h,
                                                       h->num_parallel_connects
                                                       - 1);
    return;
  }
  GNUNET_TESTBED_set_num_parallel_overlay_connects_ (h,
                                                     h->num_parallel_connects /
                                                     2);
}


/**
 * Releases a time slot thus making it available for be used again
 *
 * @param h the host handle
 * @param index the index of the the time slot
 * @param key the key to prove ownership of the timeslot
 * @return GNUNET_YES if the time slot is successfully removed; GNUNET_NO if the
 *           time slot cannot be removed - this could be because of the index
 *           greater than existing number of time slots or `key' being different
 */
int
GNUNET_TESTBED_release_time_slot_ (struct GNUNET_TESTBED_Host *h,
                                   unsigned int index, void *key)
{
  struct TimeSlot *slot;

  GNUNET_assert (NULL != key);
  if (index >= h->num_parallel_connects)
    return GNUNET_NO;
  slot = &h->tslots[index];
  if (key != slot->key)
    return GNUNET_NO;
  slot->key = NULL;
  return GNUNET_YES;
}


/**
 * Function to update a time slot
 *
 * @param h the host handle
 * @param index the index of the time slot to update
 * @param key the key to identify ownership of the slot
 * @param time the new time
 * @param failed should this reading be treated as coming from a fail event
 */
void
GNUNET_TESTBED_update_time_slot_ (struct GNUNET_TESTBED_Host *h,
                                  unsigned int index, void *key,
                                  struct GNUNET_TIME_Relative time, int failed)
{
  struct TimeSlot *slot;

  if (GNUNET_YES == failed)
  {
    if (1 == h->num_parallel_connects)
    {
      GNUNET_TESTBED_set_num_parallel_overlay_connects_ (h, 1);
      return;
    }
    GNUNET_TESTBED_set_num_parallel_overlay_connects_ (h,
                                                       h->num_parallel_connects
                                                       - 1);
  }
  if (GNUNET_NO == GNUNET_TESTBED_release_time_slot_ (h, index, key))
    return;
  slot = &h->tslots[index];
  slot->nvals++;
  if (GNUNET_TIME_UNIT_ZERO.rel_value == slot->time.rel_value)
  {
    slot->time = time;
    h->tslots_filled++;
    decide_npoc (h);
    return;
  }
  slot->time = GNUNET_TIME_relative_add (slot->time, time);
}


/**
 * Queues the given operation in the queue for parallel overlay connects of the
 * given host
 *
 * @param h the host handle
 * @param op the operation to queue in the given host's parally overlay connect
 *          queue 
 */
void
GNUNET_TESTBED_host_queue_oc_ (struct GNUNET_TESTBED_Host *h, 
                               struct GNUNET_TESTBED_Operation *op)
{  
  GNUNET_TESTBED_operation_queue_insert_
      (h->opq_parallel_overlay_connect_operations, op);
}


/**
 * Handler for GNUNET_MESSAGE_TYPE_TESTBED_ADDHOSTCONFIRM message from
 * controller (testbed service)
 *
 * @param c the controller handler
 * @param msg message received
 * @return GNUNET_YES if we can continue receiving from service; GNUNET_NO if
 *           not
 */
int
GNUNET_TESTBED_host_handle_addhostconfirm_ (struct GNUNET_TESTBED_Controller *c,
                                            const struct
                                            GNUNET_TESTBED_HostConfirmedMessage
                                            *msg)
{
  struct GNUNET_TESTBED_HostRegistrationHandle *rh;
  char *emsg;
  uint16_t msg_size;

  rh = c->rh;
  if (NULL == rh)
  {
    return GNUNET_OK;
  }
  if (GNUNET_TESTBED_host_get_id_ (rh->host) != ntohl (msg->host_id))
  {
    LOG_DEBUG ("Mismatch in host id's %u, %u of host confirm msg\n",
               GNUNET_TESTBED_host_get_id_ (rh->host), ntohl (msg->host_id));
    return GNUNET_OK;
  }
  c->rh = NULL;
  msg_size = ntohs (msg->header.size);
  if (sizeof (struct GNUNET_TESTBED_HostConfirmedMessage) == msg_size)
  {
    LOG_DEBUG ("Host %u successfully registered\n", ntohl (msg->host_id));
    GNUNET_TESTBED_mark_host_registered_at_ (rh->host, c);
    rh->cc (rh->cc_cls, NULL);
    GNUNET_free (rh);
    return GNUNET_OK;
  }
  /* We have an error message */
  emsg = (char *) &msg[1];
  if ('\0' !=
      emsg[msg_size - sizeof (struct GNUNET_TESTBED_HostConfirmedMessage)])
  {
    GNUNET_break (0);
    GNUNET_free (rh);
    return GNUNET_NO;
  }
  LOG (GNUNET_ERROR_TYPE_ERROR, _("Adding host %u failed with error: %s\n"),
       ntohl (msg->host_id), emsg);
  rh->cc (rh->cc_cls, emsg);
  GNUNET_free (rh);
  return GNUNET_OK;
}

/* end of testbed_api_hosts.c */
