/*
     This file is part of GNUnet.
     (C) 2009, 2010 Christian Grothoff (and other contributing authors)

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
 * @file core/test_core_quota_compliance.c
 * @brief testcase for core_api.c focusing quota compliance on core level
 *
 * FIXME:
 * - make sure connect callback is invoked properly as well!
 */
#include "platform.h"
#include "gnunet_common.h"
#include "gnunet_constants.h"
#include "gnunet_arm_service.h"
#include "gnunet_core_service.h"
#include "gnunet_getopt_lib.h"
#include "gnunet_os_lib.h"
#include "gnunet_program_lib.h"
#include "gnunet_scheduler_lib.h"
#include "gnunet_transport_service.h"
#include "gnunet_statistics_service.h"

#define VERBOSE GNUNET_NO
#define DEBUG_TRANSMISSION GNUNET_NO

#define SYMMETRIC 0
#define ASYMMETRIC_SEND_LIMITED 1
#define ASYMMETRIC_RECV_LIMITED 2

#define START_ARM GNUNET_YES

/**
 * Note that this value must not significantly exceed
 * 'MAX_PENDING' in 'gnunet-service-transport.c', otherwise
 * messages may be dropped even for a reliable transport.
 */
#define TOTAL_MSGS (60000 * 10)

/**
 * How long until we give up on transmitting the message?
 */
#define TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 30)

/**
 * What delay do we request from the core service for transmission?
 * Any value smaller than the CORK delay will disable CORKing, which
 * is what we want here.
 */
#define FAST_TIMEOUT GNUNET_TIME_relative_divide (GNUNET_CONSTANTS_MAX_CORK_DELAY, 2)

#define MTYPE 12345
#define MSIZE 1024
#define MEASUREMENT_LENGTH GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 5)

static unsigned long long total_bytes_sent;
static unsigned long long total_bytes_recv;

static struct GNUNET_TIME_Absolute start_time;

static GNUNET_SCHEDULER_TaskIdentifier err_task;

static GNUNET_SCHEDULER_TaskIdentifier measure_task;


struct PeerContext
{
  struct GNUNET_CONFIGURATION_Handle *cfg;
  struct GNUNET_CORE_Handle *ch;
  struct GNUNET_PeerIdentity id;   
  struct GNUNET_TRANSPORT_Handle *th;
  struct GNUNET_MessageHeader *hello;
  struct GNUNET_STATISTICS_Handle *stats;
  int connect_status;
#if START_ARM
  struct GNUNET_OS_Process *arm_proc;
#endif
};

static struct PeerContext p1;
static struct PeerContext p2;

static unsigned long long current_quota_p1_in;
static unsigned long long current_quota_p1_out;
static unsigned long long current_quota_p2_in;
static unsigned long long current_quota_p2_out;

static int ok;
static int test;
static int32_t tr_n;

static int running;


#if VERBOSE
#define OKPP do { ok++; fprintf (stderr, "Now at stage %u at %s:%u\n", ok, __FILE__, __LINE__); } while (0)
#else
#define OKPP do { ok++; } while (0)
#endif

struct TestMessage 
{
  struct GNUNET_MessageHeader header;
  uint32_t num;
};

static void
terminate_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  GNUNET_CORE_disconnect (p1.ch);
  p1.ch = NULL;
  GNUNET_CORE_disconnect (p2.ch);
  p2.ch = NULL;
  GNUNET_TRANSPORT_disconnect (p1.th);
  p1.th = NULL;
  GNUNET_TRANSPORT_disconnect (p2.th);
  p2.th = NULL;
}


static void
terminate_task_error (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Testcase timout, exit!\n");
  GNUNET_break (0);

  if (measure_task != GNUNET_SCHEDULER_NO_TASK)
	  GNUNET_SCHEDULER_cancel(measure_task);

  GNUNET_CORE_disconnect (p1.ch);
  p1.ch = NULL;
  GNUNET_CORE_disconnect (p2.ch);
  p2.ch = NULL;
  GNUNET_TRANSPORT_disconnect (p1.th);
  p1.th = NULL;
  GNUNET_TRANSPORT_disconnect (p2.th);
  p2.th = NULL;
  ok = 42;
}


/**
 * Callback function to process statistic values.
 *
 * @param cls closure
 * @param subsystem name of subsystem that created the statistic
 * @param name the name of the datum
 * @param value the current value
 * @param is_persistent GNUNET_YES if the value is persistent, GNUNET_NO if not
 * @return GNUNET_OK to continue, GNUNET_SYSERR to abort iteration
 */
static int
print_stat (void *cls,
	    const char *subsystem,
	    const char *name,
	    uint64_t value,
	    int is_persistent)
{
  if (cls==&p1)
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	   "Peer1 %50s = %12llu\n",
	   name,
	   (unsigned long long) value);
  if (cls==&p2)
	  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		   "Peer2 %50s = %12llu\n",
		   name,
		   (unsigned long long) value);
  return GNUNET_OK;
}

static void
measurement_stop (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  unsigned long long int delta;
  unsigned long long int throughput_out;
  unsigned long long int throughput_in;
  unsigned long long int max_quota_in;
  unsigned long long int max_quota_out;
  unsigned long long int quota_delta;

  measure_task = GNUNET_SCHEDULER_NO_TASK;
  fprintf(stdout,"\n");
  running = GNUNET_NO;

  delta = GNUNET_TIME_absolute_get_duration (start_time).rel_value;

  throughput_out = total_bytes_sent * 1000 / 1024 / delta;
  throughput_in = total_bytes_recv * 1000 / 1024 / delta;

  if (current_quota_p1_in < current_quota_p2_in)
	  max_quota_in = current_quota_p1_in;
  else
	  max_quota_in = current_quota_p2_in;
  if (current_quota_p1_out < current_quota_p2_out)
	  max_quota_out = current_quota_p1_out;
  else
	  max_quota_out = current_quota_p2_out;

  if (max_quota_out < max_quota_in)
	  quota_delta = max_quota_in / 10;
  else
	  quota_delta = max_quota_out / 10;

  if ((throughput_out < (max_quota_out/1024)) && (throughput_out < (max_quota_in/1024)))
	  ok = 0;
  else
	  ok = 1;

  GNUNET_STATISTICS_get (p1.stats,
		     "core",
		     "# discarded CORE_SEND requests",
		     GNUNET_TIME_UNIT_FOREVER_REL,
		     NULL,
		     &print_stat, &p1);

  GNUNET_STATISTICS_get (p1.stats,
		     "core",
		     "# discarded CORE_SEND request bytes",
		     GNUNET_TIME_UNIT_FOREVER_REL,
		     NULL,
		     &print_stat, &p1);
  GNUNET_STATISTICS_get (p1.stats,
		     "core",
		     "# discarded lower priority CORE_SEND requests",
		     GNUNET_TIME_UNIT_FOREVER_REL,
		     NULL,
		     &print_stat, NULL);
  GNUNET_STATISTICS_get (p1.stats,
		     "core",
		     "# discarded lower priority CORE_SEND request bytes",
		     GNUNET_TIME_UNIT_FOREVER_REL,
		     NULL,
		     &print_stat, &p1);
  GNUNET_STATISTICS_get (p2.stats,
		     "core",
		     "# discarded CORE_SEND requests",
		     GNUNET_TIME_UNIT_FOREVER_REL,
		     NULL,
		     &print_stat, &p2);

  GNUNET_STATISTICS_get (p2.stats,
		     "core",
		     "# discarded CORE_SEND request bytes",
		     GNUNET_TIME_UNIT_FOREVER_REL,
		     NULL,
		     &print_stat, &p2);
  GNUNET_STATISTICS_get (p2.stats,
		     "core",
		     "# discarded lower priority CORE_SEND requests",
		     GNUNET_TIME_UNIT_FOREVER_REL,
		     NULL,
		     &print_stat, &p2);
  GNUNET_STATISTICS_get (p2.stats,
		     "core",
		     "# discarded lower priority CORE_SEND request bytes",
		     GNUNET_TIME_UNIT_FOREVER_REL,
		     NULL,
		     &print_stat, &p2);

  enum GNUNET_ErrorType kind = GNUNET_ERROR_TYPE_DEBUG;
  if (ok==1)
  {
	  kind = GNUNET_ERROR_TYPE_ERROR;
  }
  switch (test)
  {
  case SYMMETRIC:
	  GNUNET_log (kind,"Core quota compliance test with symmetric quotas: %s\n", (ok==0)?"PASSED":"FAILED");
	  break;
  case ASYMMETRIC_SEND_LIMITED:
	  GNUNET_log (kind,"Core quota compliance test with limited sender quota: %s\n", (ok==0)?"PASSED":"FAILED");
	  break;
  case ASYMMETRIC_RECV_LIMITED:
	  GNUNET_log (kind,"Core quota compliance test with limited receiver quota: %s\n", (ok==0)?"PASSED":"FAILED");
	  break;
  };
  GNUNET_log (kind,"Peer 1 send  rate: %llu kB/s (%llu Bytes in %u sec.)\n",throughput_out,total_bytes_sent, delta/1000);
  GNUNET_log (kind,"Peer 1 send quota: %llu kB/s\n",current_quota_p1_out / 1024);
  GNUNET_log (kind,"Peer 2 receive  rate: %llu kB/s (%llu Bytes in %u sec.)\n",throughput_in,total_bytes_recv, delta/1000);
  GNUNET_log (kind,"Peer 2 receive quota: %llu kB/s\n",current_quota_p2_in / 1024);
/*
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,"Max. inbound  quota allowed: %llu kB/s\n",max_quota_in /1024);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,"Max. outbound quota allowed: %llu kB/s\n",max_quota_out/1024);
*/
  GNUNET_SCHEDULER_cancel (err_task);
  GNUNET_SCHEDULER_add_now (&terminate_task, NULL);

}

static size_t
transmit_ready (void *cls, size_t size, void *buf)
{
  char *cbuf = buf;
  struct TestMessage hdr;
  unsigned int ret;

  GNUNET_assert (size <= GNUNET_CONSTANTS_MAX_ENCRYPTED_MESSAGE_SIZE);
  if (buf == NULL)
    {
      if (p1.ch != NULL)
	GNUNET_break (NULL !=
		      GNUNET_CORE_notify_transmit_ready (p1.ch,
							 0,
							 FAST_TIMEOUT,
							 &p2.id,
							 MSIZE,
							 &transmit_ready, &p1));
      return 0;
    }
  GNUNET_assert (tr_n < TOTAL_MSGS);
  ret = 0;
  GNUNET_assert (size >= MSIZE);
  GNUNET_assert (buf != NULL);
  cbuf = buf;
  do
    {
#if DEBUG_TRANSMISSION
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "Sending message %u of size %u at offset %u\n",
		  tr_n,
		  MSIZE,
		  ret);
#endif
      hdr.header.size = htons (MSIZE);
      hdr.header.type = htons (MTYPE);
      hdr.num = htonl (tr_n);
      memcpy (&cbuf[ret], &hdr, sizeof (struct TestMessage));
      ret += sizeof (struct TestMessage);
      memset (&cbuf[ret], tr_n, MSIZE - sizeof (struct TestMessage));
      ret += MSIZE - sizeof (struct TestMessage);
      tr_n++;
      if (0 == GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, 16))
	break; /* sometimes pack buffer full, sometimes not */
    }
  while (size - ret >= MSIZE);
  GNUNET_SCHEDULER_cancel (err_task);
  err_task = GNUNET_SCHEDULER_add_delayed (TIMEOUT,
				  &terminate_task_error,
				  NULL);

  total_bytes_sent += ret;
  return ret;
}



static void
connect_notify (void *cls,
                const struct GNUNET_PeerIdentity *peer,
                const struct GNUNET_TRANSPORT_ATS_Information *atsi)
{
  struct PeerContext *pc = cls;

  GNUNET_assert (pc->connect_status == 0);
  pc->connect_status = 1;
  if (pc == &p1)
    {
#if DEBUG_TRANSMISSION
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		  "Encrypted connection established to peer `%4s'\n",
		  GNUNET_i2s (peer));
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Asking core (1) for transmission to peer `%4s'\n",
                  GNUNET_i2s (&p2.id));
#endif
      if (err_task != GNUNET_SCHEDULER_NO_TASK) GNUNET_SCHEDULER_cancel (err_task);
      err_task = GNUNET_SCHEDULER_add_delayed (TIMEOUT,
				      &terminate_task_error,
				      NULL);
      start_time = GNUNET_TIME_absolute_get ();
      running = GNUNET_YES;
      measure_task = GNUNET_SCHEDULER_add_delayed(MEASUREMENT_LENGTH, &measurement_stop, NULL);

      GNUNET_break (NULL != GNUNET_CORE_notify_transmit_ready (p1.ch,
						       0,
						       TIMEOUT,
						       &p2.id,
						       MSIZE,
						       &transmit_ready, &p1));
    }
}


static void
disconnect_notify (void *cls,
                   const struct GNUNET_PeerIdentity *peer)
{
  struct PeerContext *pc = cls;
  pc->connect_status = 0;
#if DEBUG_TRANSMISSION
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Encrypted connection to `%4s' cut\n", GNUNET_i2s (peer));
#endif
}


static int
inbound_notify (void *cls,
                const struct GNUNET_PeerIdentity *other,
                const struct GNUNET_MessageHeader *message,
		const struct GNUNET_TRANSPORT_ATS_Information *atsi)
{
#if DEBUG_TRANSMISSION
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Core provides inbound data from `%4s' %llu.\n", GNUNET_i2s (other), ntohs(message->size));
#endif
  total_bytes_recv += ntohs(message->size);
  return GNUNET_OK;
}


static int
outbound_notify (void *cls,
                 const struct GNUNET_PeerIdentity *other,
                 const struct GNUNET_MessageHeader *message,
		 const struct GNUNET_TRANSPORT_ATS_Information *atsi)
{
#if DEBUG_TRANSMISSION
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Core notifies about outbound data for `%4s'.\n",
              GNUNET_i2s (other));
#endif
  return GNUNET_OK;
}


static size_t
transmit_ready (void *cls, size_t size, void *buf);

static int
process_mtype (void *cls,
               const struct GNUNET_PeerIdentity *peer,
               const struct GNUNET_MessageHeader *message,
	       const struct GNUNET_TRANSPORT_ATS_Information *atsi)
{
  static int n;
  const struct TestMessage *hdr;

  hdr = (const struct TestMessage*) message;
  if (MTYPE != ntohs (message->type))
    return GNUNET_SYSERR;
  if (ntohs (message->size) != MSIZE)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		  "Expected message %u of size %u, got %u bytes of message %u\n",
		  n, MSIZE,
		  ntohs (message->size),
		  ntohl (hdr->num));
      GNUNET_SCHEDULER_cancel (err_task);
      err_task = GNUNET_SCHEDULER_add_now (&terminate_task_error, NULL);
      return GNUNET_SYSERR;
    }
  if (ntohl (hdr->num) != n)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
		  "Expected message %u of size %u, got %u bytes of message %u\n",
		  n, MSIZE,
		  ntohs (message->size),
		  ntohl (hdr->num));
      GNUNET_SCHEDULER_cancel (err_task);
      err_task = GNUNET_SCHEDULER_add_now (&terminate_task_error, NULL);
      return GNUNET_SYSERR;
    }
#if DEBUG_TRANSMISSION
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "Got message %u of size %u\n",
	      ntohl (hdr->num),
	      ntohs (message->size));	      
#endif
  n++;
  if (0 == (n % 10))
    fprintf (stderr, ".");
  if (n == TOTAL_MSGS)
    {
      GNUNET_SCHEDULER_cancel (err_task);
      GNUNET_SCHEDULER_add_now (&terminate_task, NULL);
    }
  else
    {
      if (running == GNUNET_YES)
	GNUNET_break (NULL !=
		      GNUNET_CORE_notify_transmit_ready (p1.ch,
							 0,
							 FAST_TIMEOUT,
							 &p2.id,
							 MSIZE,
							 &transmit_ready, &p1));
    }
  return GNUNET_OK;
}


static struct GNUNET_CORE_MessageHandler handlers[] = {
  {&process_mtype, MTYPE, 0},
  {NULL, 0, 0}
};



static void
init_notify (void *cls,
             struct GNUNET_CORE_Handle *server,
             const struct GNUNET_PeerIdentity *my_identity,
             const struct GNUNET_CRYPTO_RsaPublicKeyBinaryEncoded *publicKey)
{
  struct PeerContext *p = cls;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Connection to CORE service of `%4s' established\n",
              GNUNET_i2s (my_identity));
  GNUNET_assert (server != NULL);
  p->id = *my_identity;
  p->ch = server;
  if (cls == &p1)
    {
      GNUNET_assert (ok == 2);
      OKPP;
      /* connect p2 */
      GNUNET_CORE_connect (p2.cfg, 1,
                           &p2,
                           &init_notify,			 
                           &connect_notify,
                           &disconnect_notify,
			   NULL,
                           &inbound_notify,
                           GNUNET_YES,
                           &outbound_notify, GNUNET_YES, handlers);
    }
  else
    {
      GNUNET_assert (ok == 3);
      OKPP;
      GNUNET_assert (cls == &p2);
#if DEBUG_TRANSMISSION
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Asking core (1) to connect to peer `%4s'\n",
                  GNUNET_i2s (&p2.id));
#endif
      GNUNET_CORE_peer_request_connect (p1.ch,
					GNUNET_TIME_UNIT_SECONDS,
					&p2.id,
					NULL, NULL);
    }
}


static void
process_hello (void *cls,
               const struct GNUNET_MessageHeader *message)
{
  struct PeerContext *p = cls;

  GNUNET_TRANSPORT_get_hello_cancel (p->th, &process_hello, p);

#if DEBUG_TRANSMISSION
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Received (my) `%s' from transport service\n",
              "HELLO");
#endif
  GNUNET_assert (message != NULL);
  p->hello = GNUNET_malloc (ntohs (message->size));
  memcpy (p->hello, message, ntohs (message->size));
  if ((p == &p1) && (p2.th != NULL))
    GNUNET_TRANSPORT_offer_hello (p2.th, message);
  if ((p == &p2) && (p1.th != NULL))
    GNUNET_TRANSPORT_offer_hello (p1.th, message);

  if ((p == &p1) && (p2.hello != NULL))
    GNUNET_TRANSPORT_offer_hello (p1.th, p2.hello);
  if ((p == &p2) && (p1.hello != NULL))
    GNUNET_TRANSPORT_offer_hello (p2.th, p1.hello);
}



static void
setup_peer (struct PeerContext *p, const char *cfgname)
{
  p->cfg = GNUNET_CONFIGURATION_create ();
#if START_ARM
  p->arm_proc = GNUNET_OS_start_process (NULL, NULL, "gnunet-service-arm",
                                        "gnunet-service-arm",
#if VERBOSE
                                        "-L", "DEBUG",
#endif
                                        "-c", cfgname, NULL);
#endif
  GNUNET_assert (GNUNET_OK == GNUNET_CONFIGURATION_load (p->cfg, cfgname));
  p->stats = GNUNET_STATISTICS_create ("core",p->cfg);
  GNUNET_assert (p->stats!=NULL);
  p->th = GNUNET_TRANSPORT_connect (p->cfg, NULL, p, NULL, NULL, NULL);
  GNUNET_assert (p->th != NULL);
  GNUNET_TRANSPORT_get_hello (p->th, &process_hello, p);
}


static void
run (void *cls,
     char *const *args,
     const char *cfgfile,
     const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  GNUNET_assert (ok == 1);
  OKPP;
  err_task = GNUNET_SCHEDULER_add_delayed (TIMEOUT,
			      &terminate_task_error,
			      NULL);
  if (test == SYMMETRIC)
    {
      setup_peer (&p1, "test_core_quota_peer1.conf");
      setup_peer (&p2, "test_core_quota_peer2.conf");
    }
  else if (test == ASYMMETRIC_SEND_LIMITED)
    {
      setup_peer (&p1, "test_core_quota_asymmetric_send_limited_peer1.conf");
      setup_peer (&p2, "test_core_quota_asymmetric_send_limited_peer2.conf");
    }
  else if (test == ASYMMETRIC_RECV_LIMITED)
    {
      setup_peer (&p1, "test_core_quota_asymmetric_recv_limited_peer1.conf");
      setup_peer (&p2, "test_core_quota_asymmetric_recv_limited_peer2.conf");
    }

  GNUNET_assert (test != -1);
  GNUNET_assert (GNUNET_SYSERR != GNUNET_CONFIGURATION_get_value_number (p1.cfg,
                                         "CORE",
                                         "TOTAL_QUOTA_IN",
                                         &current_quota_p1_in));
  GNUNET_assert (GNUNET_SYSERR != GNUNET_CONFIGURATION_get_value_number (p2.cfg,
                                         "CORE",
                                         "TOTAL_QUOTA_IN",
                                         &current_quota_p2_in));
  GNUNET_assert (GNUNET_SYSERR != GNUNET_CONFIGURATION_get_value_number (p1.cfg,
                                         "CORE",
                                         "TOTAL_QUOTA_OUT",
                                         &current_quota_p1_out));
  GNUNET_assert (GNUNET_SYSERR != GNUNET_CONFIGURATION_get_value_number (p2.cfg,
                                         "CORE",
                                         "TOTAL_QUOTA_OUT",
                                         &current_quota_p2_out));

  GNUNET_CORE_connect (p1.cfg, 1,
                       &p1,
                       &init_notify,
                       &connect_notify,
                       &disconnect_notify,
                       NULL,
                       &inbound_notify,
                       GNUNET_YES, &outbound_notify, GNUNET_YES, handlers);
}


static void
stop_arm (struct PeerContext *p)
{
#if START_ARM
  if (0 != GNUNET_OS_process_kill (p->arm_proc, SIGTERM))
    GNUNET_log_strerror (GNUNET_ERROR_TYPE_WARNING, "kill");
  if (GNUNET_OS_process_wait(p->arm_proc) != GNUNET_OK)
    GNUNET_log_strerror (GNUNET_ERROR_TYPE_WARNING, "waitpid");
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "ARM process %u stopped\n", GNUNET_OS_process_get_pid (p->arm_proc));
  GNUNET_OS_process_close (p->arm_proc);
  p->arm_proc = NULL;
#endif
  GNUNET_CONFIGURATION_destroy (p->cfg);
}

static int
check ()
{


  char *const argv[] = { "test-core-quota-compliance",
    "-c",
    "test_core_api_data.conf",
#if VERBOSE
    "-L", "DEBUG",
#endif
    NULL
  };
  struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_OPTION_END
  };
  ok = 1;
  GNUNET_PROGRAM_run ((sizeof (argv) / sizeof (char *)) - 1,
                      argv, "test-core-quota-compliance", "nohelp", options, &run, &ok);
  stop_arm (&p1);
  stop_arm (&p2);
  return ok;
}

int
main (int argc, char *argv[])
{
  int ret;

  test = -1;
  if (strstr(argv[0], "_symmetric") != NULL)
    {
      test = SYMMETRIC;
    }
  else if (strstr(argv[0], "_asymmetric_send") != NULL)
    {
	  test = ASYMMETRIC_SEND_LIMITED;
    }
  else if (strstr(argv[0], "_asymmetric_recv") != NULL)
    {
	  test = ASYMMETRIC_RECV_LIMITED;
    }
  GNUNET_assert (test != -1);
  GNUNET_log_setup ("test-core-quota-compliance",
#if VERBOSE
                    "DEBUG",
#else
                    "WARNING",
#endif
                    NULL);
  ret = check ();
  GNUNET_DISK_directory_remove ("/tmp/test-gnunet-core-peer-1");
  GNUNET_DISK_directory_remove ("/tmp/test-gnunet-core-peer-2");

  return ret;
}
/* end of test_core_api_reliability.c */
