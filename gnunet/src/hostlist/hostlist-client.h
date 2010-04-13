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
 * @file hostlist/hostlist-client.h
 * @brief hostlist support.  Downloads HELLOs via HTTP.
 * @author Christian Grothoff
 */

#ifndef HOSTLIST_CLIENT_H
#define HOSTLIST_CLIENT_H

#include "gnunet_core_service.h"
#include "gnunet_statistics_service.h"
#include "gnunet_util_lib.h"
#include "gnunet_time_lib.h"

/*
 * a single hostlist obtained by hostlist advertisements
 */
struct GNUNET_Hostlist
{

  struct GNUNET_PeerIdentity    peer;
  char *                        hostlist_uri;
  unsigned long                 hello_count;
  unsigned long                 times_used;
  struct GNUNET_TIME_Absolute   time_creation;
  struct GNUNET_TIME_Absolute   time_last_usage;
  uint64_t                      quality;
};


/**
 * Start downloading hostlists from hostlist servers as necessary.
 *
 * @param c the configuration to use
 * @param s the scheduler to use
 * @param st hande for publishing statistics
 * @param ch set to handler for connect notifications
 * @param dh set to handler for disconnect notifications
 * @param msgh set to handler for message handler notifications
 * @param learn set if client is learning new hostlists
 * @return GNUNET_OK on success
 */
int
GNUNET_HOSTLIST_client_start (const struct GNUNET_CONFIGURATION_Handle *c,
			      struct GNUNET_SCHEDULER_Handle *s,
			      struct GNUNET_STATISTICS_Handle *st,
			      GNUNET_CORE_ConnectEventHandler *ch,
			      GNUNET_CORE_DisconnectEventHandler *dh,
			      GNUNET_CORE_MessageCallback *msgh,
			      int learn);


/**
 * Stop downloading hostlists from hostlist servers as necessary.
 */
void
GNUNET_HOSTLIST_client_stop (void);


#endif
/* end of hostlist-client.h */
