/*
      This file is part of GNUnet
      (C) 2012-2013 Christian Grothoff (and other contributing authors)

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
 * @file include/gnunet_gns_service.h
 * @brief API to the GNS service
 * @author Martin Schanzenbach
 */
#ifndef GNUNET_GNS_SERVICE_H
#define GNUNET_GNS_SERVICE_H

#include "gnunet_util_lib.h"
#include "gnunet_dnsparser_lib.h"
#include "gnunet_namestore_service.h"

#ifdef __cplusplus
extern "C"
{
#if 0                           /* keep Emacsens' auto-indent happy */
}
#endif
#endif


/**
 * String we use to indicate the local master zone or a
 * root entry in the current zone.
 */
#define GNUNET_GNS_MASTERZONE_STR "+"

/**
 * Connection to the GNS service.
 */
struct GNUNET_GNS_Handle;

/**
 * Handle to control a lookup operation.
 */
struct GNUNET_GNS_LookupRequest;


/**
 * Initialize the connection with the GNS service.
 *
 * @param cfg configuration to use
 * @return handle to the GNS service, or NULL on error
 */
struct GNUNET_GNS_Handle *
GNUNET_GNS_connect (const struct GNUNET_CONFIGURATION_Handle *cfg);


/**
 * Shutdown connection with the GNS service.
 *
 * @param handle connection to shut down
 */
void
GNUNET_GNS_disconnect (struct GNUNET_GNS_Handle *handle);


/**
 * Iterator called on obtained result for a GNS lookup.
 *
 * @param cls closure
 * @param rd_count number of records in @a rd
 * @param rd the records in reply
 */
typedef void (*GNUNET_GNS_LookupResultProcessor) (void *cls,
						  uint32_t rd_count,
						  const struct GNUNET_NAMESTORE_RecordData *rd);


/**
 * Perform an asynchronous lookup operation on the GNS.
 *
 * @param handle handle to the GNS service
 * @param name the name to look up
 * @param zone zone to look in
 * @param type the GNS record type to look for
 * @param only_cached #GNUNET_YES to only check locally (not in the DHT)
 * @param shorten_zone_key the private key of the shorten zone (can be NULL);
 *                    specify to enable automatic shortening (given a PSEU
 *                    record, if a given pseudonym is not yet used in the
 *                    shorten zone, we automatically add the respective zone
 *                    under that name)
 * @param proc function to call on result
 * @param proc_cls closure for processor
 * @return handle to the queued request
 */
struct GNUNET_GNS_LookupRequest *
GNUNET_GNS_lookup (struct GNUNET_GNS_Handle *handle,
		   const char *name,
		   const struct GNUNET_CRYPTO_EccPublicKey *zone,
		   int type,
		   int only_cached,
		   const struct GNUNET_CRYPTO_EccPrivateKey *shorten_zone_key,
		   GNUNET_GNS_LookupResultProcessor proc,
		   void *proc_cls);


/**
 * Cancel pending lookup request
 *
 * @param lr the lookup request to cancel
 */
void
GNUNET_GNS_lookup_cancel (struct GNUNET_GNS_LookupRequest *lr);


#if 0                           /* keep Emacsens' auto-indent happy */
{
#endif
#ifdef __cplusplus
}
#endif


#endif
/* gnunet_gns_service.h */
