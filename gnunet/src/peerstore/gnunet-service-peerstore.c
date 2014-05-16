/*
     This file is part of GNUnet.
     (C) 

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
 * @file peerstore/gnunet-service-peerstore.c
 * @brief peerstore service implementation
 * @author Omar Tarabai
 */
#include "platform.h"
#include "gnunet_util_lib.h"
#include "peerstore.h"
#include "gnunet_peerstore_plugin.h"
#include "peerstore_common.h"

//TODO: GNUNET_SERVER_receive_done() ?
//TODO: implement value lifetime

/**
 * Our configuration.
 */
static const struct GNUNET_CONFIGURATION_Handle *cfg;

/**
 * Database plugin library name
 */
char *db_lib_name;

/**
 * Database handle
 */
static struct GNUNET_PEERSTORE_PluginFunctions *db;

/**
 * Task run during shutdown.
 *
 * @param cls unused
 * @param tc unused
 */
static void
shutdown_task (void *cls,
	       const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  if(NULL != db_lib_name)
  {
    GNUNET_break (NULL == GNUNET_PLUGIN_unload (db_lib_name, db));
    GNUNET_free (db_lib_name);
    db_lib_name = NULL;
  }
}


/**
 * A client disconnected.  Remove all of its data structure entries.
 *
 * @param cls closure, NULL
 * @param client identification of the client
 */
static void
handle_client_disconnect (void *cls,
			  struct GNUNET_SERVER_Client
			  * client)
{
}

/**
 * Function called by for each matching record.
 *
 * @param cls closure
 * @param peer peer identity
 * @param sub_system name of the GNUnet sub system responsible
 * @param value stored value
 * @param size size of stored value
 *
int record_iterator(void *cls,
    const char *sub_system,
    const struct GNUNET_PeerIdentity *peer,
    const char *key,
    const void *value,
    size_t size,
    struct GNUNET_TIME_Absolute expiry)
{
  struct GNUNET_SERVER_TransmitContext *tc = cls;
  struct StoreRecordMessage *srm;

  srm = PEERSTORE_create_record_message(sub_system,
      peer,
      key,
      value,
      size,
      expiry,
      GNUNET_MESSAGE_TYPE_PEERSTORE_ITERATE);
  GNUNET_SERVER_transmit_context_append_message(tc, (const struct GNUNET_MessageHeader *)srm);
  return GNUNET_YES;
}*/

/**
 * Handle an iterate request from client
 *
 * @param cls unused
 * @param client identification of the client
 * @param message the actual message
 *
void handle_iterate (void *cls,
    struct GNUNET_SERVER_Client *client,
    const struct GNUNET_MessageHeader *message)
{
  struct GNUNET_PEERSTORE_Record *record;
  struct GNUNET_SERVER_TransmitContext *tc;

  record = PEERSTORE_parse_record_message(message);
  if(NULL == record)
  {
    GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Malformed iterate request from client\n");
    GNUNET_SERVER_receive_done(client, GNUNET_SYSERR);
    return;
  }
  if(NULL == record->sub_system)
  {
    GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Sub system not supplied in client iterate request\n");
    GNUNET_SERVER_receive_done(client, GNUNET_SYSERR);
    return;
  }
  tc = GNUNET_SERVER_transmit_context_create (client);
  if(GNUNET_OK == db->iterate_records(db->cls,
      record->sub_system,
      record->peer,
      record->key,
      &record_iterator,
      tc))
  {

  }
}*/

/**
 * Handle a store request from client
 *
 * @param cls unused
 * @param client identification of the client
 * @param message the actual message
 */
void handle_store (void *cls,
    struct GNUNET_SERVER_Client *client,
    const struct GNUNET_MessageHeader *message)
{
  struct GNUNET_PEERSTORE_Record *record;
  uint16_t response_type;
  struct GNUNET_SERVER_TransmitContext *tc;

  record = PEERSTORE_parse_record_message(message);
  if(NULL == record)
  {
    GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Malformed store request from client\n");
    GNUNET_SERVER_receive_done(client, GNUNET_SYSERR);
    return;
  }
  if(NULL == record->sub_system
      || NULL == record->peer
      || NULL == record->key)
  {
    GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Full key not supplied in client store request\n");
    GNUNET_SERVER_receive_done(client, GNUNET_SYSERR);
    return;
  }
  GNUNET_log(GNUNET_ERROR_TYPE_INFO, "Received a store request (size: %lu) for sub system `%s', peer `%s', key `%s'\n",
      record->value_size,
      record->sub_system,
      GNUNET_i2s (record->peer),
      record->key);
  if(GNUNET_OK == db->store_record(db->cls,
      record->sub_system,
      record->peer,
      record->key,
      record->value,
      record->value_size,
      *record->expiry))
  {
    response_type = GNUNET_MESSAGE_TYPE_PEERSTORE_STORE_RESULT_OK;
  }
  else
  {
    GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Failed to store requested value, sqlite database error.");
    response_type = GNUNET_MESSAGE_TYPE_PEERSTORE_STORE_RESULT_FAIL;
  }

  tc = GNUNET_SERVER_transmit_context_create (client);
  GNUNET_SERVER_transmit_context_append_data(tc, NULL, 0, response_type);
  GNUNET_SERVER_transmit_context_run (tc, GNUNET_TIME_UNIT_FOREVER_REL);

}

/**
 * Peerstore service runner.
 *
 * @param cls closure
 * @param server the initialized server
 * @param c configuration to use
 */
static void
run (void *cls,
     struct GNUNET_SERVER_Handle *server,
     const struct GNUNET_CONFIGURATION_Handle *c)
{
  static const struct GNUNET_SERVER_MessageHandler handlers[] = {
      {&handle_store, NULL, GNUNET_MESSAGE_TYPE_PEERSTORE_STORE, 0},
//      {&handle_iterate, NULL, GNUNET_MESSAGE_TYPE_PEERSTORE_ITERATE, 0},
      {NULL, NULL, 0, 0}
  };
  char *database;

  cfg = c;
  if (GNUNET_OK !=
        GNUNET_CONFIGURATION_get_value_string (cfg, "peerstore", "DATABASE",
                                               &database))
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "No database backend configured\n");

  else
  {
    GNUNET_asprintf (&db_lib_name, "libgnunet_plugin_peerstore_%s", database);
    db = GNUNET_PLUGIN_load(db_lib_name, (void *) cfg);
    GNUNET_free(database);
  }
  if(NULL == db)
	  GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Could not load database backend `%s'\n", db_lib_name);
  else
  {
    GNUNET_SERVER_add_handlers (server, handlers);
    GNUNET_SERVER_disconnect_notify (server,
             &handle_client_disconnect,
             NULL);
  }
  GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_FOREVER_REL,
				&shutdown_task,
				NULL);
}


/**
 * The main function for the peerstore service.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0 ok, 1 on error
 */
int
main (int argc, char *const *argv)
{
  return (GNUNET_OK ==
          GNUNET_SERVICE_run (argc,
                              argv,
                              "peerstore",
			      GNUNET_SERVICE_OPTION_NONE,
			      &run, NULL)) ? 0 : 1;
}

/* end of gnunet-service-peerstore.c */
