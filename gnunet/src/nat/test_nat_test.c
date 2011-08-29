/*
     This file is part of GNUnet.
     (C) 2009, 2011 Christian Grothoff (and other contributing authors)

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
 * Testcase for the NAT testing code.
 *
 * @file nat/test_nat_test.c
 * @brief Testcase for NAT testing functions
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_common.h"
#include "gnunet_util_lib.h"
#include "gnunet_nat_lib.h"


#define VERBOSE GNUNET_NO


/**
 * Time to wait before stopping NAT test, in seconds
 */
#define TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 15)


static int ret = 1;

static struct GNUNET_NAT_Test *tst;

static GNUNET_SCHEDULER_TaskIdentifier end;

static void
end_test (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  GNUNET_NAT_test_stop (tst);
}

static void
report_success (void *cls, int success)
{
  GNUNET_assert (GNUNET_OK == success);
  ret = 0;
  GNUNET_SCHEDULER_cancel (end);
  end = GNUNET_SCHEDULER_add_now (&end_test, NULL);
}

/**
 * Main function run with scheduler.
 */
static void
run (void *cls, char *const *args, const char *cfgfile,
     const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  tst =
      GNUNET_NAT_test_start (cfg, GNUNET_YES, 1285, 1285, &report_success,
                             NULL);
  if (NULL == tst)
    return;
  end = GNUNET_SCHEDULER_add_delayed (TIMEOUT, &end_test, NULL);
}


int
main (int argc, char *const argv[])
{
  struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_OPTION_END
  };
  struct GNUNET_OS_Process *gns;

  char *const argv_prog[] = {
    "test-nat-test",
    "-c",
    "test_nat_test_data.conf",
    "-L",
#if VERBOSE
    "DEBUG",
#else
    "WARNING",
#endif
    NULL
  };

  GNUNET_log_setup ("test-nat-test",
#if VERBOSE
                    "DEBUG",
#else
                    "WARNING",
#endif
                    NULL);
  gns =
      GNUNET_OS_start_process (NULL, NULL, "gnunet-nat-server",
                               "gnunet-nat-server",
#if VERBOSE
                               "-L", "DEBUG",
#endif
                               "-c", "test_nat_test_data.conf", "12345", NULL);
  GNUNET_assert (NULL != gns);
  GNUNET_PROGRAM_run (5, argv_prog, "test-nat-test", "nohelp", options, &run,
                      NULL);
  GNUNET_break (0 == GNUNET_OS_process_kill (gns, SIGTERM));
  GNUNET_break (GNUNET_OK == GNUNET_OS_process_wait (gns));
  GNUNET_OS_process_close (gns);
  return ret;
}

/* end of test_nat_test.c */
