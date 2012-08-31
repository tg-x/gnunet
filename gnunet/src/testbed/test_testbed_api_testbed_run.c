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
 * @file testbed/test_testbed_api_testbed_run.c
 * @brief Test cases for testing high-level testbed management
 * @author Sree Harsha Totakura <sreeharsha@totakura.in> 
 */

#include "platform.h"
#include "gnunet_common.h"
#include "gnunet_testbed_service.h"

/**
 * Number of peers we want to start
 */
#define NUM_PEERS 2

/**
 * The array of peers; we fill this as the peers are given to us by the testbed
 */
static struct GNUNET_TESTBED_Peer *peers[NUM_PEERS];

/**
 * Abort task identifier
 */
static GNUNET_SCHEDULER_TaskIdentifier abort_task;

/**
 * Current peer id
 */
unsigned int peer_id;

/**
 * Testing result
 */
static int result;


/**
 * Shutdown nicely
 *
 * @param cls NULL
 * @param tc the task context
 */
static void
do_shutdown (void *cls, const const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  if (GNUNET_SCHEDULER_NO_TASK != abort_task)
    GNUNET_SCHEDULER_cancel (abort_task);
  GNUNET_SCHEDULER_shutdown ();
}


/**
 * abort task to run on test timed out
 *
 * @param cls NULL
 * @param tc the task context
 */
static void
do_abort (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  GNUNET_log (GNUNET_ERROR_TYPE_WARNING, "Test timedout -- Aborting\n");
  abort_task = GNUNET_SCHEDULER_NO_TASK;
  GNUNET_SCHEDULER_add_now (&do_shutdown, NULL);
  GNUNET_SCHEDULER_shutdown (); /* Stop the scheduler */
}


/**
 * Task to be executed when peers are ready
 *
 * @param cls NULL
 * @param tc the task context
 */
static void
master_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  result = GNUNET_OK;
  /* Artificial delay */
  GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_UNIT_SECONDS, &do_shutdown, NULL);
}


/**
 * Controller event callback
 *
 * @param cls NULL
 * @param event the controller event
 */
static void
controller_event_cb (void *cls,
                     const struct GNUNET_TESTBED_EventInformation *event)
{
  
  switch (event->type)
  {
  case GNUNET_TESTBED_ET_PEER_START:
    GNUNET_assert (NULL == peers[peer_id]);
    GNUNET_assert (NULL != event->details.peer_start.peer);
    peers[peer_id++] = event->details.peer_start.peer;
    break;
  default:
    GNUNET_break (0);
  }
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
  uint64_t event_mask;
  
  event_mask = 0;
  event_mask |= (1LL << GNUNET_TESTBED_ET_PEER_START);
  event_mask |= (1LL << GNUNET_TESTBED_ET_PEER_STOP);
  event_mask |= (1LL << GNUNET_TESTBED_ET_CONNECT);
  event_mask |= (1LL << GNUNET_TESTBED_ET_DISCONNECT);
  event_mask |= (1LL << GNUNET_TESTBED_ET_OPERATION_FINISHED);
  GNUNET_TESTBED_run (NULL, config, 2, event_mask, &controller_event_cb,
                           NULL, &master_task, NULL);
  abort_task = GNUNET_SCHEDULER_add_delayed 
    (GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 5), &do_abort, NULL);
}


/**
 * Main function
 */
int main (int argc, char **argv)
{
  int ret;
  char *const argv2[] = { 
    "test_testbed_api_testbed_run",
    "-c", "test_testbed_api.conf",
    NULL
  };
  struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_OPTION_END
  };

  result = GNUNET_SYSERR;
  ret = GNUNET_PROGRAM_run ((sizeof (argv2) / sizeof (char *)) - 1, argv2,
			    "test_testbed_api_testbed_run", "nohelp", options,
                            &run, NULL);
  if ((GNUNET_OK != ret) || (GNUNET_OK != result))
    return 1;
  return 0;
}
