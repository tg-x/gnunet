/*
     This file is part of GNUnet.
     (C) 2010 Christian Grothoff

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
 * @file vpn/gnunet-daemon-vpn-helper.h
 * @brief
 * @author Philipp Toelke
 */
#ifndef GNUNET_DAEMON_VPN_HELPER_H
#define GNUNET_DAEMON_VPN_HELPER_H

/**
 * The process id of the helper
 */
struct GNUNET_OS_Process *helper_proc;

/**
 * The Message-Tokenizer that tokenizes the messages comming from the helper
 */
struct GNUNET_SERVER_MessageStreamTokenizer* mst;

/**
 * Start the helper-process
 */
void start_helper_and_schedule(void *cls,
			       const struct GNUNET_SCHEDULER_TaskContext *tc);

/**
 * Restart the helper-process
 */
void restart_helper(void* cls, const struct GNUNET_SCHEDULER_TaskContext* tskctx);

/**
 * Read from the helper-process
 */
void helper_read(void* cls, const struct GNUNET_SCHEDULER_TaskContext* tsdkctx);

/**
 * Send an dns-answer-packet to the helper
 */
void helper_write(void* cls, const struct GNUNET_SCHEDULER_TaskContext* tsdkctx);

/**
 * Receive packets from the helper-process
 */
void message_token(void *cls,
		   void *client,
		   const struct GNUNET_MessageHeader *message);

void write_to_helper(void* buf, size_t len);
// GNUNET_DISK_file_write(fh_to_helper, response, ntohs(response->shdr.size));

void schedule_helper_write(struct GNUNET_TIME_Relative, void* cls);
// GNUNET_SCHEDULER_add_write_file (GNUNET_TIME_UNIT_FOREVER_REL, fh_to_helper, &helper_write, NULL);

#endif /* end of include guard: GNUNET-DAEMON-VPN-HELPER_H */
