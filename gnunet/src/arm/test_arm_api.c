/*
     This file is part of GNUnet.
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
 * @file arm/test_arm_api.c
 * @brief testcase for arm_api.c
 */
#include "platform.h"
#include "gnunet_common.h"
#include "gnunet_arm_service.h"
#include "gnunet_client_lib.h"
#include "gnunet_configuration_lib.h"
#include "gnunet_getopt_lib.h"
#include "gnunet_program_lib.h"
#include "gnunet_resolver_service.h"

#define VERBOSE GNUNET_NO

#define START_ARM GNUNET_YES

#define TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 10)

static struct GNUNET_SCHEDULER_Handle *sched;

static const struct GNUNET_CONFIGURATION_Handle *cfg;

static int ok = 1;

static void
dns_notify (void *cls, const struct sockaddr *addr, socklen_t addrlen)
{
  if (addr == NULL)
    {
      GNUNET_assert (ok == 0);
#if START_ARM
      GNUNET_ARM_stop_service ("arm", cfg, sched, TIMEOUT, NULL, NULL);
#endif
      return;
    }
  GNUNET_assert (addr != NULL);
  ok = 0;
}


static void
resolver_notify (void *cls, int success)
{
  GNUNET_assert (success == GNUNET_YES);
  sleep (1);                    /* FIXME: that we need to do this is a problem... */
  GNUNET_RESOLVER_ip_get (sched,
                          cfg,
                          "localhost", AF_INET, TIMEOUT, &dns_notify, NULL);
}

static void
arm_notify (void *cls, int success)
{
  GNUNET_assert (success == GNUNET_YES);
#if START_ARM
  sleep (1);                    /* FIXME: that we need to do this is a problem... */
#endif
  GNUNET_ARM_start_service ("resolver",
                            cfg, sched, TIMEOUT, &resolver_notify, NULL);
}


static void
task (void *cls,
      struct GNUNET_SCHEDULER_Handle *s,
      char *const *args,
      const char *cfgfile,
      const struct GNUNET_CONFIGURATION_Handle *c)
{
  cfg = c;
  sched = s;
#if START_ARM
  GNUNET_ARM_start_service ("arm", cfg, sched, TIMEOUT, &arm_notify, NULL);
#else
  arm_notify (NULL, GNUNET_YES);
#endif
}



static int
check ()
{
  char *const argv[] = {
    "test-arm-api",
    "-c", "test_arm_api_data.conf",
#if VERBOSE
    "-L", "DEBUG",
#endif
    NULL
  };
  struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_OPTION_END
  };
  GNUNET_assert (GNUNET_OK ==
                 GNUNET_PROGRAM_run ((sizeof (argv) / sizeof (char *)) - 1,
                                     argv,
                                     "test-arm-api",
                                     "nohelp", options, &task, NULL));
  return ok;
}

int
main (int argc, char *argv[])
{
  int ret;


  GNUNET_log_setup ("test-arm-api",
#if VERBOSE
                    "DEBUG",
#else
                    "WARNING",
#endif
                    NULL);
  ret = check ();

  return ret;
}

/* end of test_arm_api.c */
