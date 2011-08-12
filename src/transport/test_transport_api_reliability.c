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
 * @file transport/test_transport_api_reliability.c
 * @brief base test case for transport implementations
 *
 * This test case serves as a base for tcp and http
 * transport test cases to check that the transports
 * achieve reliable message delivery.
 */
#include "platform.h"
#include "gnunet_common.h"
#include "gnunet_hello_lib.h"
#include "gnunet_getopt_lib.h"
#include "gnunet_os_lib.h"
#include "gnunet_program_lib.h"
#include "gnunet_scheduler_lib.h"
#include "gnunet_server_lib.h"
#include "gnunet_transport_service.h"
#include "gauger.h"
#include "transport.h"
#include "transport-testing.h"

#define VERBOSE GNUNET_NO

#define VERBOSE_ARM GNUNET_NO

#define START_ARM GNUNET_YES

/**
 * How long until we give up on transmitting the message?
 */
#define TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 1500)

static int ok;

static  GNUNET_SCHEDULER_TaskIdentifier die_task;

struct PeerContext * p1;

struct PeerContext * p2;

struct GNUNET_TRANSPORT_TransmitHandle * th;

char * cfg_file_p1;

char * cfg_file_p2;

/*
 * Testcase specific declarations
 */

/**
 * Note that this value must not significantly exceed
 * 'MAX_PENDING' in 'gnunet-service-transport.c', otherwise
 * messages may be dropped even for a reliable transport.
 */
#define TOTAL_MSGS (1024 * 2)

#define MTYPE 12345

struct TestMessage
{
  struct GNUNET_MessageHeader header;
  uint32_t num;
};

static int msg_scheduled;
static int msg_sent;
static int msg_recv_expected;
static int msg_recv;

static int test_failed;

static unsigned long long total_bytes;

static struct GNUNET_TIME_Absolute start_time;

/*
 * END Testcase specific declarations
 */

#if VERBOSE
#define OKPP do { ok++; fprintf (stderr, "Now at stage %u at %s:%u\n", ok, __FILE__, __LINE__); } while (0)
#else
#define OKPP do { ok++; } while (0)
#endif


static void
end ()
{
  unsigned long long delta;
  //char *value_name;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Stopping peers\n");

  delta = GNUNET_TIME_absolute_get_duration (start_time).rel_value;
  fprintf (stderr,
           "\nThroughput was %llu kb/s\n",
           total_bytes * 1000 / 1024 / delta);
  //GNUNET_asprintf(&value_name, "reliable_%s", test_name);
  //GAUGER ("TRANSPORT", value_name, (int)(total_bytes * 1000 / 1024 /delta), "kb/s");
  //GNUNET_free(value_name);

  if (die_task != GNUNET_SCHEDULER_NO_TASK)
    GNUNET_SCHEDULER_cancel(die_task);

  if (th != NULL)
    GNUNET_TRANSPORT_notify_transmit_ready_cancel(th);
  th = NULL;

  GNUNET_TRANSPORT_TESTING_stop_peer(p1);
  GNUNET_TRANSPORT_TESTING_stop_peer(p2);
}

static void
end_badly ()
{
  die_task = GNUNET_SCHEDULER_NO_TASK;
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Fail! Stopping peers\n");

  if (th != NULL)
    GNUNET_TRANSPORT_notify_transmit_ready_cancel(th);
  th = NULL;

  if (p1 != NULL)
    GNUNET_TRANSPORT_TESTING_stop_peer(p1);
  if (p2 != NULL)
    GNUNET_TRANSPORT_TESTING_stop_peer(p2);

  ok = GNUNET_SYSERR;
}


static unsigned int
get_size (unsigned int iter)
{
  unsigned int ret;

  ret = (iter * iter * iter);
  return sizeof (struct TestMessage) + (ret % 60000);
}


static void
notify_receive (void *cls,
                const struct GNUNET_PeerIdentity *peer,
                const struct GNUNET_MessageHeader *message,
                const struct GNUNET_TRANSPORT_ATS_Information *ats,
                uint32_t ats_count)
{
  static int n;
  unsigned int s;
  char cbuf[GNUNET_SERVER_MAX_MESSAGE_SIZE - 1];
  const struct TestMessage *hdr;

  hdr = (const struct TestMessage*) message;
  s = get_size (n);
  if (MTYPE != ntohs (message->type))
    return;
  msg_recv_expected = n;
  msg_recv = ntohl(hdr->num);
  if (ntohs (message->size) != (s))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Expected message %u of size %u, got %u bytes of message %u\n",
                  n, s,
                  ntohs (message->size),
                  ntohl (hdr->num));
      if (die_task != GNUNET_SCHEDULER_NO_TASK)
        GNUNET_SCHEDULER_cancel (die_task);
      test_failed = GNUNET_YES;
      die_task = GNUNET_SCHEDULER_add_now (&end_badly, NULL);
      return;
    }
  if (ntohl (hdr->num) != n)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Expected message %u of size %u, got %u bytes of message %u\n",
                  n, s,
                  ntohs (message->size),
                  ntohl (hdr->num));
      if (die_task != GNUNET_SCHEDULER_NO_TASK)
        GNUNET_SCHEDULER_cancel (die_task);
      test_failed = GNUNET_YES;
      die_task = GNUNET_SCHEDULER_add_now (&end_badly, NULL);
      return;
    }
  memset (cbuf, n, s - sizeof (struct TestMessage));
  if (0 != memcmp (cbuf,
                   &hdr[1],
                   s - sizeof (struct TestMessage)))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Expected message %u with bits %u, but body did not match\n",
                  n, (unsigned char) n);
      if (die_task != GNUNET_SCHEDULER_NO_TASK)
        GNUNET_SCHEDULER_cancel (die_task);
      test_failed = GNUNET_YES;
      die_task = GNUNET_SCHEDULER_add_now (&end_badly, NULL);
      return;
    }
#if VERBOSE
  if (ntohl(hdr->num) % 5000 == 0)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Got message %u of size %u\n",
                  ntohl (hdr->num),
                  ntohs (message->size));
    }
#endif
  n++;
  if (0 == (n % (TOTAL_MSGS/100)))
    {
      fprintf (stderr, ".");
      if (die_task != GNUNET_SCHEDULER_NO_TASK)
        GNUNET_SCHEDULER_cancel (die_task);
      die_task = GNUNET_SCHEDULER_add_delayed (TIMEOUT,
                                               &end_badly,
                                               NULL);
    }
  if (n == TOTAL_MSGS)
  {
    ok = 0;
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,"All messages received\n");
    end ();
  }
}


static size_t
notify_ready (void *cls, size_t size, void *buf)
{
  static int n;
  char *cbuf = buf;
  struct TestMessage hdr;
  unsigned int s;
  unsigned int ret;

  if (buf == NULL)
    {
      GNUNET_break (0);
      ok = 42;
      return 0;
    }
  th = NULL;
  ret = 0;
  s = get_size (n);
  GNUNET_assert (size >= s);
  GNUNET_assert (buf != NULL);
  cbuf = buf;
  do
    {
      hdr.header.size = htons (s);
      hdr.header.type = htons (MTYPE);
      hdr.num = htonl (n);
      msg_sent = n;
      memcpy (&cbuf[ret], &hdr, sizeof (struct TestMessage));
      ret += sizeof (struct TestMessage);
      memset (&cbuf[ret], n, s - sizeof (struct TestMessage));
      ret += s - sizeof (struct TestMessage);
#if VERBOSE
      if (n % 5000 == 0)
        {
          GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                      "Sending message %u of size %u\n",
                      n,
                      s);
        }
#endif
      n++;
      s = get_size (n);
      if (0 == GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, 16))
        break; /* sometimes pack buffer full, sometimes not */
    }
  while (size - ret >= s);
  if (n < TOTAL_MSGS)
  {
    if (th == NULL)
      th = GNUNET_TRANSPORT_notify_transmit_ready (p2->th,
                                            &p1->id,
                                            s, 0, TIMEOUT,
                                            &notify_ready,
                                            NULL);
    msg_scheduled = n;
  }
  if (n % 5000 == 0)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Returning total message block of size %u\n",
                  ret);
    }
  total_bytes += ret;
  if (n == TOTAL_MSGS)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,"All messages sent\n");
  }
  return ret;
}


static void
notify_connect (void *cls,
                const struct GNUNET_PeerIdentity *peer,
                const struct GNUNET_TRANSPORT_ATS_Information *ats,
                uint32_t ats_count)
{

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Peer `%4s' connected to us (%p)!\n",
              GNUNET_i2s (peer),
              cls);

  if (cls == p1)
    {
      GNUNET_TRANSPORT_set_quota (p1->th,
                                  &p2->id,
                                  GNUNET_BANDWIDTH_value_init (1024 * 1024 * 1024),
                                  GNUNET_BANDWIDTH_value_init (1024 * 1024 * 1024));
    }
  else  if (cls == p2)
    {
      GNUNET_TRANSPORT_set_quota (p2->th,
                                  &p1->id,
                                  GNUNET_BANDWIDTH_value_init (1024 * 1024 * 1024),
                                  GNUNET_BANDWIDTH_value_init (1024 * 1024 * 1024));
    }
}


static void
notify_disconnect (void *cls,
                   const struct GNUNET_PeerIdentity *peer)
{
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Peer `%4s' disconnected (%p)!\n",
              GNUNET_i2s (peer), cls);
}

static void
sendtask ()
{
  start_time = GNUNET_TIME_absolute_get ();
  th = GNUNET_TRANSPORT_notify_transmit_ready (p2->th,
                                          &p1->id,
                                          get_size (0), 0, TIMEOUT,
                                          &notify_ready,
                                          NULL);
}

static void
testing_connect_cb (struct PeerContext * p1, struct PeerContext * p2, void *cls)
{
  char * p1_c = strdup (GNUNET_i2s(&p1->id));
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Peers connected: %s <-> %s\n",
       p1_c,
       GNUNET_i2s (&p2->id));
  GNUNET_free (p1_c);

  // FIXME: THIS IS REQUIRED! SEEMS TO BE A BUG!
  GNUNET_SCHEDULER_add_delayed(GNUNET_TIME_UNIT_SECONDS, &sendtask, NULL);
}

static void
run (void *cls,
     char *const *args,
     const char *cfgfile, const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  die_task = GNUNET_SCHEDULER_add_delayed (TIMEOUT,
                                           &end_badly, NULL);

  p1 = GNUNET_TRANSPORT_TESTING_start_peer(cfg_file_p1,
      &notify_receive,
      &notify_connect,
      &notify_disconnect,
      NULL);
  p2 = GNUNET_TRANSPORT_TESTING_start_peer(cfg_file_p2,
      &notify_receive,
      &notify_connect,
      &notify_disconnect,
      NULL);

  GNUNET_TRANSPORT_TESTING_connect_peers(p1, p2, &testing_connect_cb, NULL);
}

static int
check ()
{
  static char *const argv[] = { "test-transport-api",
    "-c",
    "test_transport_api_data.conf",
#if VERBOSE
    "-L", "DEBUG",
#endif
    NULL
  };
  static struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_OPTION_END
  };

#if WRITECONFIG
  setTransportOptions("test_transport_api_data.conf");
#endif
  ok = 1;
  GNUNET_PROGRAM_run ((sizeof (argv) / sizeof (char *)) - 1,
                      argv, "test-transport-api", "nohelp",
                      options, &run, &ok);

  return ok;
}

/**
 * Return the actual path to a file found in the current
 * PATH environment variable.
 *
 * @param binary the name of the file to find
 */
static char *
get_path_from_PATH (char *binary)
{
  char *path;
  char *pos;
  char *end;
  char *buf;
  const char *p;

  p = getenv ("PATH");
  if (p == NULL)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  _("PATH environment variable is unset.\n"));
      return NULL;
    }
  path = GNUNET_strdup (p);     /* because we write on it */
  buf = GNUNET_malloc (strlen (path) + 20);
  pos = path;

  while (NULL != (end = strchr (pos, PATH_SEPARATOR)))
    {
      *end = '\0';
      sprintf (buf, "%s/%s", pos, binary);
      if (GNUNET_DISK_file_test (buf) == GNUNET_YES)
        {
          GNUNET_free (path);
          return buf;
        }
      pos = end + 1;
    }
  sprintf (buf, "%s/%s", pos, binary);
  if (GNUNET_DISK_file_test (buf) == GNUNET_YES)
    {
      GNUNET_free (path);
      return buf;
    }
  GNUNET_free (buf);
  GNUNET_free (path);
  return NULL;
}

/**
 * Check whether the suid bit is set on a file.
 * Attempts to find the file using the current
 * PATH environment variable as a search path.
 *
 * @param binary the name of the file to check
 *
 * @return GNUNET_YES if the binary is found and
 *         can be run properly, GNUNET_NO otherwise
 */
static int
check_gnunet_nat_binary(char *binary)
{
  struct stat statbuf;
  char *p;
#ifdef MINGW
  SOCKET rawsock;
#endif

#ifdef MINGW
  char *binaryexe;
  GNUNET_asprintf (&binaryexe, "%s.exe", binary);
  p = get_path_from_PATH (binaryexe);
  free (binaryexe);
#else
  p = get_path_from_PATH (binary);
#endif
  if (p == NULL)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  _("Could not find binary `%s' in PATH!\n"),
                  binary);
      return GNUNET_NO;
    }
  if (0 != STAT (p, &statbuf))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                  _("stat (%s) failed: %s\n"),
                  p,
                  STRERROR (errno));
      GNUNET_free (p);
      return GNUNET_SYSERR;
    }
  GNUNET_free (p);
#ifndef MINGW
  if ( (0 != (statbuf.st_mode & S_ISUID)) &&
       (statbuf.st_uid == 0) )
    return GNUNET_YES;
  return GNUNET_NO;
#else
  rawsock = socket (AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (INVALID_SOCKET == rawsock)
    {
      DWORD err = GetLastError ();
      GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                  "socket (AF_INET, SOCK_RAW, IPPROTO_ICMP) have failed! GLE = %d\n", err);
      return GNUNET_NO; /* not running as administrator */
    }
  closesocket (rawsock);
  return GNUNET_YES;
#endif
}

int
main (int argc, char *argv[])
{
  int ret;

  GNUNET_log_setup ("test-transport-api",
#if VERBOSE
                    "DEBUG",
#else
                    "WARNING",
#endif
                    NULL);

  char * pch = strdup(argv[0]);
  char * backup = pch;
  char * filename = NULL;
  char *dotexe;

  /* get executable filename */
  pch = strtok (pch,"/");
  while (pch != NULL)
  {
    pch = strtok (NULL, "/");
    if (pch != NULL)
      filename = pch;
  }
  /* remove "lt-" */
  filename = strstr(filename, "tes");
  if (NULL != (dotexe = strstr (filename, ".exe")))
    dotexe[0] = '\0';

  /* create cfg filename */
  GNUNET_asprintf(&cfg_file_p1, "%s_peer1.conf",filename);
  GNUNET_asprintf(&cfg_file_p2, "%s_peer2.conf", filename);
  GNUNET_free (backup);

  if (strstr(argv[0], "tcp_nat") != NULL)
    {
      if (GNUNET_YES != check_gnunet_nat_binary("gnunet-nat-server"))
        {
          GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                      "`%s' not properly installed, cannot run NAT test!\n",
                      "gnunet-nat-server");
          return 0;
        }
    }
  else if (strstr(argv[0], "udp_nat") != NULL)
    {
      if (GNUNET_YES != check_gnunet_nat_binary("gnunet-nat-server"))
        {
          GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                      "`%s' not properly installed, cannot run NAT test!\n",
                      "gnunet-nat-server");
          return 0;
        }
    }

  ret = check ();

  GNUNET_free (cfg_file_p1);
  GNUNET_free (cfg_file_p2);

  return ret;
}


/* end of test_transport_api_reliability.c */
