/*
     This file is part of GNUnet.
     (C) 2005--2013 Christian Grothoff (and other contributing authors)

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
 * @file util/test_pseudonym.c
 * @brief testcase for pseudonym.c
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_common.h"
#include "gnunet_container_lib.h"
#include "gnunet_crypto_lib.h"
#include "gnunet_disk_lib.h"
#include "gnunet_pseudonym_lib.h"

#define CHECK(a) do { if (!(a)) { ok = GNUNET_NO; GNUNET_break(0); goto FAILURE; } } while (0)

static struct GNUNET_CONTAINER_MetaData *meta;

static struct GNUNET_PseudonymIdentifier id1;


static int
iter (void *cls, const struct GNUNET_PseudonymIdentifier * pseudonym,
      const char *name, const char *unique_name,
      const struct GNUNET_CONTAINER_MetaData *md, int32_t rating)
{
  int *ok = cls;

  if ((0 == memcmp (pseudonym, &id1, sizeof (struct GNUNET_PseudonymIdentifier))) &&
      (!GNUNET_CONTAINER_meta_data_test_equal (md, meta)))
  {
    *ok = GNUNET_NO;
    GNUNET_break (0);
  }
  return GNUNET_OK;
}


static int
noti_callback (void *cls, const struct GNUNET_PseudonymIdentifier * pseudonym,
               const char *name, const char *unique_name,
               const struct GNUNET_CONTAINER_MetaData *md, int32_t rating)
{
  int *ret = cls;

  (*ret)++;
  return GNUNET_OK;
}


static int
fake_noti_callback (void *cls, const struct GNUNET_PseudonymIdentifier * pseudonym,
                    const char *name, const char *unique_name,
                    const struct GNUNET_CONTAINER_MetaData *md, int32_t rating)
{
  int *ret = cls;

  (*ret)++;
  return GNUNET_OK;
}


static void
create_pseu (struct GNUNET_PseudonymIdentifier *pseu)
{
  struct GNUNET_PseudonymHandle *ph;

  ph = GNUNET_PSEUDONYM_create (NULL);
  GNUNET_PSEUDONYM_get_identifier (ph, pseu);
  GNUNET_PSEUDONYM_destroy (ph);
}


/**
 * Testcase for meta data / ranking IO routines.
 */
static int
test_io ()
{
  int ok;
  struct GNUNET_PseudonymIdentifier rid1;
  struct GNUNET_PseudonymIdentifier id2;
  struct GNUNET_PseudonymIdentifier rid2;
  struct GNUNET_PseudonymIdentifier fid;
  struct GNUNET_PseudonymIdentifier id3;
  int old;
  int newVal;
  struct GNUNET_CONFIGURATION_Handle *cfg;
  char *name1;
  char *name2;
  char *name3;
  char *name1_unique;
  char *name2_unique;
  char *noname;
  int noname_is_a_dup;
  int notiCount, fakenotiCount;
  static char m[1024 * 1024 * 10];
  struct GNUNET_PSEUDONYM_DiscoveryHandle *dh1;
  struct GNUNET_PSEUDONYM_DiscoveryHandle *dh2;

  memset (m, 'b', sizeof (m));
  m[sizeof (m) - 1] = '\0';

  GNUNET_log_setup ("test-pseudonym", "WARNING", NULL);
  ok = GNUNET_YES;
  (void) GNUNET_DISK_directory_remove ("/tmp/gnunet-pseudonym-test");
  cfg = GNUNET_CONFIGURATION_create ();
  if (-1 == GNUNET_CONFIGURATION_parse (cfg, "test_pseudonym_data.conf"))
  {
    GNUNET_CONFIGURATION_destroy (cfg);
    GNUNET_break (0);
    return -1;
  }
  notiCount = 0;
  fakenotiCount = 0;
  dh1 = GNUNET_PSEUDONYM_discovery_callback_register (cfg, &fake_noti_callback,
						      &fakenotiCount);
  dh2 = GNUNET_PSEUDONYM_discovery_callback_register (cfg, &noti_callback,
						      &notiCount);
  GNUNET_PSEUDONYM_discovery_callback_unregister (dh1);

  /* ACTUAL TEST CODE */
  old = GNUNET_PSEUDONYM_list_all (cfg, NULL, NULL);
  meta = GNUNET_CONTAINER_meta_data_create ();
  GNUNET_CONTAINER_meta_data_insert (meta, "<test>", EXTRACTOR_METATYPE_TITLE,
                                     EXTRACTOR_METAFORMAT_UTF8, "text/plain",
                                     "test", strlen ("test") + 1);
  create_pseu (&id1);
  GNUNET_PSEUDONYM_add (cfg, &id1, meta);
  CHECK (notiCount == 1);
  GNUNET_PSEUDONYM_add (cfg, &id1, meta);
  CHECK (notiCount == 2);
  newVal = GNUNET_PSEUDONYM_list_all (cfg, &iter, &ok);
  CHECK (old < newVal);
  old = newVal;
  create_pseu (&id2);
  GNUNET_PSEUDONYM_add (cfg, &id2, meta);
  CHECK (notiCount == 3);
  newVal = GNUNET_PSEUDONYM_list_all (cfg, &iter, &ok);
  CHECK (old < newVal);
  GNUNET_assert (GNUNET_OK ==
                 GNUNET_CONTAINER_meta_data_insert (meta, "<test>",
                                                    EXTRACTOR_METATYPE_COMMENT,
                                                    EXTRACTOR_METAFORMAT_UTF8,
                                                    "text/plain", m,
                                                    strlen (m) + 1));
  create_pseu (&id3);
  GNUNET_PSEUDONYM_add (cfg, &id3, meta);
  GNUNET_PSEUDONYM_get_info (cfg, &id3, NULL, NULL, &name3, NULL);
  CHECK (name3 != NULL);
  GNUNET_PSEUDONYM_get_info (cfg, &id2, NULL, NULL, &name2, NULL);
  CHECK (name2 != NULL);
  GNUNET_PSEUDONYM_get_info (cfg, &id1, NULL, NULL, &name1, NULL);
  CHECK (name1 != NULL);
  CHECK (0 == strcmp (name1, name2));
  name1_unique = GNUNET_PSEUDONYM_name_uniquify (cfg, &id1, name1, NULL);
  name2_unique = GNUNET_PSEUDONYM_name_uniquify (cfg, &id2, name2, NULL);
  CHECK (0 != strcmp (name1_unique, name2_unique));
  CHECK (GNUNET_SYSERR == GNUNET_PSEUDONYM_name_to_id (cfg, "fake", &rid2));
  CHECK (GNUNET_SYSERR == GNUNET_PSEUDONYM_name_to_id (cfg, name2, &rid2));
  CHECK (GNUNET_SYSERR == GNUNET_PSEUDONYM_name_to_id (cfg, name1, &rid1));
  CHECK (GNUNET_OK == GNUNET_PSEUDONYM_name_to_id (cfg, name2_unique, &rid2));
  CHECK (GNUNET_OK == GNUNET_PSEUDONYM_name_to_id (cfg, name1_unique, &rid1));
  CHECK (0 == memcmp (&id1, &rid1, sizeof (struct GNUNET_PseudonymIdentifier)));
  CHECK (0 == memcmp (&id2, &rid2, sizeof (struct GNUNET_PseudonymIdentifier)));

  create_pseu (&fid);
  GNUNET_log_skip (1, GNUNET_NO);
  CHECK (0 == GNUNET_PSEUDONYM_rank (cfg, &fid, 0));
  GNUNET_log_skip (0, GNUNET_NO);
  CHECK (GNUNET_OK == GNUNET_PSEUDONYM_get_info (cfg, &fid, NULL, NULL, &noname, &noname_is_a_dup));
  CHECK (noname != NULL);
  CHECK (noname_is_a_dup == GNUNET_YES);
  CHECK (0 == GNUNET_PSEUDONYM_rank (cfg, &id1, 0));
  CHECK (5 == GNUNET_PSEUDONYM_rank (cfg, &id1, 5));
  CHECK (-5 == GNUNET_PSEUDONYM_rank (cfg, &id1, -10));
  CHECK (0 == GNUNET_PSEUDONYM_rank (cfg, &id1, 5));
  GNUNET_free (name1);
  GNUNET_free (name2);
  GNUNET_free (name1_unique);
  GNUNET_free (name2_unique);
  GNUNET_free (name3);
  GNUNET_free (noname);
  /* END OF TEST CODE */
FAILURE:
  GNUNET_PSEUDONYM_discovery_callback_unregister (dh2);
  GNUNET_CONTAINER_meta_data_destroy (meta);
  GNUNET_CONFIGURATION_destroy (cfg);
  GNUNET_break (GNUNET_OK ==
                GNUNET_DISK_directory_remove ("/tmp/gnunet-pseudonym-test"));
  return (ok == GNUNET_YES) ? 0 : 1;
}


static int
test_crypto ()
{
  struct GNUNET_PseudonymHandle *ph;

  ph = GNUNET_PSEUDONYM_create (NULL);
  // FIXME: call sign, verify APIs...
  GNUNET_PSEUDONYM_destroy (ph);  
  return 0;
}


int
main (int argc, char *argv[])
{
  if (0 != test_io ())
    return 1;
  if (0 != test_crypto ())
    return 1;
  
  return 0;
}


/* end of test_pseudoynm.c */
