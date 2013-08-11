/*
     This file is part of GNUnet.
     (C) 2002-2013 Christian Grothoff (and other contributing authors)

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
 * @file util/test_crypto_ecc.c
 * @brief testcase for ECC public key crypto
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_signatures.h"
#include <gcrypt.h>

#define ITER 25

#define KEYFILE "/tmp/test-gnunet-crypto-ecc.key"

#define PERF GNUNET_YES


static struct GNUNET_CRYPTO_EccPrivateKey *key;


static int
testSignVerify ()
{
  struct GNUNET_CRYPTO_EccSignature sig;
  struct GNUNET_CRYPTO_EccSignaturePurpose purp;
  struct GNUNET_CRYPTO_EccPublicKey pkey;
  int i;
  struct GNUNET_TIME_Absolute start;
  int ok = GNUNET_OK;

  FPRINTF (stderr, "%s",  "W");
  GNUNET_CRYPTO_ecc_key_get_public (key, &pkey);
  start = GNUNET_TIME_absolute_get ();
  purp.size = htonl (sizeof (struct GNUNET_CRYPTO_EccSignaturePurpose));
  purp.purpose = htonl (GNUNET_SIGNATURE_PURPOSE_TEST);

  for (i = 0; i < ITER; i++)
  {
    FPRINTF (stderr, "%s",  ".");
    if (GNUNET_SYSERR == GNUNET_CRYPTO_ecc_sign (key, &purp, &sig))
    {
      FPRINTF (stderr, "%s",  "GNUNET_CRYPTO_ecc_sign returned SYSERR\n");
      ok = GNUNET_SYSERR;
      continue;
    }
    if (GNUNET_SYSERR ==
        GNUNET_CRYPTO_ecc_verify (GNUNET_SIGNATURE_PURPOSE_TEST, &purp, &sig,
                                  &pkey))
    {
      printf ("GNUNET_CRYPTO_ecc_verify failed!\n");
      ok = GNUNET_SYSERR;
      continue;
    }
    if (GNUNET_SYSERR !=
        GNUNET_CRYPTO_ecc_verify (GNUNET_SIGNATURE_PURPOSE_TRANSPORT_PONG_OWN,
                                  &purp, &sig, &pkey))
    {
      printf ("GNUNET_CRYPTO_ecc_verify failed to fail!\n");
      ok = GNUNET_SYSERR;
      continue;
    }
  }
  printf ("%d ECC sign/verify operations %s\n", ITER,
          GNUNET_STRINGS_relative_time_to_string (GNUNET_TIME_absolute_get_duration (start), GNUNET_YES));
  return ok;
}


static int
testDeriveSignVerify ()
{
  struct GNUNET_CRYPTO_EccSignature sig;
  struct GNUNET_CRYPTO_EccSignaturePurpose purp;
  struct GNUNET_CRYPTO_EccPrivateKey *dpriv;
  struct GNUNET_CRYPTO_EccPublicKey pkey;
  struct GNUNET_CRYPTO_EccPublicKey dpub;

  dpriv = GNUNET_CRYPTO_ecc_key_derive (key, "test-derive", "test-CTX");
  GNUNET_CRYPTO_ecc_key_get_public (key, &pkey);
  GNUNET_CRYPTO_ecc_public_key_derive (&pkey, "test-derive", "test-CTX", &dpub);
  purp.size = htonl (sizeof (struct GNUNET_CRYPTO_EccSignaturePurpose));
  purp.purpose = htonl (GNUNET_SIGNATURE_PURPOSE_TEST);

  if (GNUNET_SYSERR == GNUNET_CRYPTO_ecc_sign (dpriv, &purp, &sig))
  {
    FPRINTF (stderr, "%s",  "GNUNET_CRYPTO_ecc_sign returned SYSERR\n");
    GNUNET_CRYPTO_ecc_key_free (dpriv);
    return GNUNET_SYSERR;
  }
  if (GNUNET_SYSERR ==
      GNUNET_CRYPTO_ecc_verify (GNUNET_SIGNATURE_PURPOSE_TEST, 
				&purp, &sig,
				&dpub))
  {
    printf ("GNUNET_CRYPTO_ecc_verify failed!\n");
    GNUNET_CRYPTO_ecc_key_free (dpriv);
    return GNUNET_SYSERR;
  }
  if (GNUNET_SYSERR !=
      GNUNET_CRYPTO_ecc_verify (GNUNET_SIGNATURE_PURPOSE_TEST, 
				&purp, &sig,
				&pkey))
  {
    printf ("GNUNET_CRYPTO_ecc_verify failed to fail!\n");
    GNUNET_CRYPTO_ecc_key_free (dpriv);
    return GNUNET_SYSERR;
  }
  if (GNUNET_SYSERR !=
      GNUNET_CRYPTO_ecc_verify (GNUNET_SIGNATURE_PURPOSE_TRANSPORT_PONG_OWN,
				&purp, &sig, &dpub))
  {
    printf ("GNUNET_CRYPTO_ecc_verify failed to fail!\n");
    GNUNET_CRYPTO_ecc_key_free (dpriv);
    return GNUNET_SYSERR;
  }
  GNUNET_CRYPTO_ecc_key_free (dpriv);
  return GNUNET_OK;
}




#if PERF
static int
testSignPerformance ()
{
  struct GNUNET_CRYPTO_EccSignaturePurpose purp;
  struct GNUNET_CRYPTO_EccSignature sig;
  struct GNUNET_CRYPTO_EccPublicKey pkey;
  int i;
  struct GNUNET_TIME_Absolute start;
  int ok = GNUNET_OK;

  purp.size = htonl (sizeof (struct GNUNET_CRYPTO_EccSignaturePurpose));
  purp.purpose = htonl (GNUNET_SIGNATURE_PURPOSE_TEST);
  FPRINTF (stderr, "%s",  "W");
  GNUNET_CRYPTO_ecc_key_get_public (key, &pkey);
  start = GNUNET_TIME_absolute_get ();
  for (i = 0; i < ITER; i++)
  {
    FPRINTF (stderr, "%s",  ".");
    if (GNUNET_SYSERR == GNUNET_CRYPTO_ecc_sign (key, &purp, &sig))
    {
      FPRINTF (stderr, "%s",  "GNUNET_CRYPTO_ecc_sign returned SYSERR\n");
      ok = GNUNET_SYSERR;
      continue;
    }
  }
  printf ("%d ECC sign operations %s\n", ITER,
          GNUNET_STRINGS_relative_time_to_string (GNUNET_TIME_absolute_get_duration (start),
						  GNUNET_YES));
  return ok;
}
#endif


static int
testCreateFromFile ()
{
  struct GNUNET_CRYPTO_EccPublicKey p1;
  struct GNUNET_CRYPTO_EccPublicKey p2;

  key = GNUNET_CRYPTO_ecc_key_create_from_file (KEYFILE);
  GNUNET_assert (NULL != key);
  GNUNET_CRYPTO_ecc_key_get_public (key, &p1);
  GNUNET_CRYPTO_ecc_key_free (key);
  key = GNUNET_CRYPTO_ecc_key_create_from_file (KEYFILE);
  GNUNET_assert (NULL != key);
  GNUNET_CRYPTO_ecc_key_get_public (key, &p2);
  GNUNET_assert (0 == memcmp (&p1, &p2, sizeof (p1)));
  GNUNET_CRYPTO_ecc_key_free (key);
  GNUNET_assert (0 == UNLINK (KEYFILE));
  key = GNUNET_CRYPTO_ecc_key_create_from_file (KEYFILE);
  GNUNET_assert (NULL != key);
  GNUNET_CRYPTO_ecc_key_get_public (key, &p2);
  GNUNET_assert (0 != memcmp (&p1, &p2, sizeof (p1)));
  GNUNET_CRYPTO_ecc_key_free (key);
  return GNUNET_OK;
}


static void
test_ecdh ()
{
  struct GNUNET_CRYPTO_EccPrivateKey *priv1;
  struct GNUNET_CRYPTO_EccPrivateKey *priv2;
  struct GNUNET_CRYPTO_EccPublicKey pub1;
  struct GNUNET_CRYPTO_EccPublicKey pub2;
  struct GNUNET_HashCode ecdh1;
  struct GNUNET_HashCode ecdh2;

  priv1 = GNUNET_CRYPTO_ecc_key_create ();
  priv2 = GNUNET_CRYPTO_ecc_key_create ();
  GNUNET_CRYPTO_ecc_key_get_public (priv1, &pub1);
  GNUNET_CRYPTO_ecc_key_get_public (priv2, &pub2);
  GNUNET_CRYPTO_ecc_ecdh (priv1, &pub2, &ecdh1);
  GNUNET_CRYPTO_ecc_ecdh (priv2, &pub1, &ecdh2);
  GNUNET_assert (0 == memcmp (&ecdh1, &ecdh2,
			      sizeof (struct GNUNET_HashCode)));
  GNUNET_CRYPTO_ecc_key_free (priv1);
  GNUNET_CRYPTO_ecc_key_free (priv2);
}


static void
perf_keygen ()
{
  struct GNUNET_TIME_Absolute start;
  struct GNUNET_CRYPTO_EccPrivateKey *pk;
  int i;

  start = GNUNET_TIME_absolute_get ();
  for (i=0;i<10;i++)
  {
    fprintf (stderr, ".");
    pk = GNUNET_CRYPTO_ecc_key_create ();
    GNUNET_CRYPTO_ecc_key_free (pk);
  }
  fprintf (stderr, "\n");
  printf ("Creating 10 ECC keys took %s\n",
          GNUNET_STRINGS_relative_time_to_string (GNUNET_TIME_absolute_get_duration (start), GNUNET_YES));
}


int
main (int argc, char *argv[])
{
  int failure_count = 0;

  if (! gcry_check_version ("1.5.0"))
  {
    FPRINTF (stderr,
             _
             ("libgcrypt has not the expected version (version %s is required).\n"),
             "1.5.0");
    return 0;
  }
  GNUNET_log_setup ("test-crypto-ecc", "WARNING", NULL);
  key = GNUNET_CRYPTO_ecc_key_create ();
  if (GNUNET_OK != testDeriveSignVerify ())
  {
    failure_count++;
    fprintf (stderr,
	     "\n\n%d TESTS FAILED!\n\n", failure_count);
    return -1;
  }
#if PERF
  if (GNUNET_OK != testSignPerformance ())
    failure_count++;
#endif
  if (GNUNET_OK != testSignVerify ())
    failure_count++;
  GNUNET_CRYPTO_ecc_key_free (key);
  if (GNUNET_OK != testCreateFromFile ())
    failure_count++;
  GNUNET_assert (0 == UNLINK (KEYFILE));
  test_ecdh ();
  perf_keygen ();

  if (0 != failure_count)
  {
    fprintf (stderr,
	     "\n\n%d TESTS FAILED!\n\n", 
	     failure_count);
    return -1;
  }
  return 0;
}

/* end of test_crypto_ecc.c */
