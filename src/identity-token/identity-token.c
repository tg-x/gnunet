/*
      This file is part of GNUnet
      Copyright (C) 2010-2015 Christian Grothoff (and other contributing authors)

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
      Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
      Boston, MA 02110-1301, USA.
 */

/**
 * @file identity-token/identity-token.c
 * @brief helper library to manage identity tokens
 * @author Martin Schanzenbach
 */
#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_signatures.h"
#include "gnunet_identity_provider_lib.h"
#include <jansson.h>


/**
 * Crypto helper functions
 */

static int
create_sym_key_from_ecdh(const struct GNUNET_HashCode *new_key_hash,
                         struct GNUNET_CRYPTO_SymmetricSessionKey *skey,
                         struct GNUNET_CRYPTO_SymmetricInitializationVector *iv)
{
  struct GNUNET_CRYPTO_HashAsciiEncoded new_key_hash_str;

  GNUNET_CRYPTO_hash_to_enc (new_key_hash,
                             &new_key_hash_str);
  GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Creating symmetric rsa key from %s\n", (char*)&new_key_hash_str);
  static const char ctx_key[] = "gnuid-aes-ctx-key";
  GNUNET_CRYPTO_kdf (skey, sizeof (struct GNUNET_CRYPTO_SymmetricSessionKey),
                     new_key_hash, sizeof (struct GNUNET_HashCode),
                     ctx_key, strlen (ctx_key),
                     NULL, 0);
  static const char ctx_iv[] = "gnuid-aes-ctx-iv";
  GNUNET_CRYPTO_kdf (iv, sizeof (struct GNUNET_CRYPTO_SymmetricInitializationVector),
                     new_key_hash, sizeof (struct GNUNET_HashCode),
                     ctx_iv, strlen (ctx_iv),
                     NULL, 0);
  return GNUNET_OK;
}



/**
 * Decrypts metainfo part from a token code
 */
static int
decrypt_str_ecdhe (const struct GNUNET_CRYPTO_EcdsaPrivateKey *priv_key,
                   const struct GNUNET_CRYPTO_EcdhePublicKey *ecdh_key,
                   const char *cyphertext,
                   size_t cyphertext_len,
                   char **result_str)
{
  struct GNUNET_HashCode new_key_hash;
  struct GNUNET_CRYPTO_SymmetricSessionKey enc_key;
  struct GNUNET_CRYPTO_SymmetricInitializationVector enc_iv;

  char *str_buf = GNUNET_malloc (cyphertext_len);
  size_t str_size;

  //Calculate symmetric key from ecdh parameters
  GNUNET_assert (GNUNET_OK == GNUNET_CRYPTO_ecdsa_ecdh (priv_key,
                                                        ecdh_key,
                                                        &new_key_hash));

  create_sym_key_from_ecdh (&new_key_hash,
                            &enc_key,
                            &enc_iv);

  str_size = GNUNET_CRYPTO_symmetric_decrypt (cyphertext,
                                              cyphertext_len,
                                              &enc_key,
                                              &enc_iv,
                                              str_buf);
  GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Decrypted bytes: %d Expected bytes: %d\n", str_size, cyphertext_len);
  if (-1 == str_size)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "ECDH invalid\n");
    GNUNET_free (str_buf);
    return GNUNET_SYSERR;
  }
  *result_str = GNUNET_malloc (str_size+1);
  memcpy (*result_str, str_buf, str_size);
  (*result_str)[str_size] = '\0';
  GNUNET_free (str_buf);
  return GNUNET_OK;

}

/**
 * Decrypt string using pubkey and ECDHE
*/
static int
decrypt_str_ecdhe2 (const struct GNUNET_CRYPTO_EcdhePrivateKey *ecdh_privkey,
                    const struct GNUNET_CRYPTO_EcdsaPublicKey *aud_key,
                    const char *ciphertext,
                    size_t ciphertext_len,
                    char **plaintext)
{
  struct GNUNET_CRYPTO_SymmetricSessionKey skey;
  struct GNUNET_CRYPTO_SymmetricInitializationVector iv;
  struct GNUNET_HashCode new_key_hash;

  //This is true see documentation for GNUNET_CRYPTO_symmetric_encrypt
  *plaintext = GNUNET_malloc (ciphertext_len);

  // Derived key K = H(eB)
  GNUNET_assert (GNUNET_OK == GNUNET_CRYPTO_ecdh_ecdsa (ecdh_privkey,
                                                        aud_key,
                                                        &new_key_hash));
  create_sym_key_from_ecdh(&new_key_hash, &skey, &iv);
  GNUNET_CRYPTO_symmetric_decrypt (ciphertext,
                                   ciphertext_len,
                                   &skey, &iv,
                                   *plaintext);
  return GNUNET_OK;
}


/**
 * Encrypt string using pubkey and ECDHE
 * Returns ECDHE pubkey to be used for decryption
 */
static int
encrypt_str_ecdhe (const char *plaintext,
                   const struct GNUNET_CRYPTO_EcdsaPublicKey *pub_key,
                   char **cyphertext,
                   struct GNUNET_CRYPTO_EcdhePrivateKey **ecdh_privkey,
                   struct GNUNET_CRYPTO_EcdhePublicKey *ecdh_pubkey)
{
  struct GNUNET_CRYPTO_SymmetricSessionKey skey;
  struct GNUNET_CRYPTO_SymmetricInitializationVector iv;
  struct GNUNET_HashCode new_key_hash;
  ssize_t enc_size;

  // ECDH keypair E = eG
  *ecdh_privkey = GNUNET_CRYPTO_ecdhe_key_create();
  GNUNET_CRYPTO_ecdhe_key_get_public (*ecdh_privkey,
                                      ecdh_pubkey);

  //This is true see documentation for GNUNET_CRYPTO_symmetric_encrypt
  *cyphertext = GNUNET_malloc (strlen (plaintext));

  // Derived key K = H(eB)
  GNUNET_assert (GNUNET_OK == GNUNET_CRYPTO_ecdh_ecdsa (*ecdh_privkey,
                                                        pub_key,
                                                        &new_key_hash));
  create_sym_key_from_ecdh(&new_key_hash, &skey, &iv);
  GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Encrypting string %s\n (len=%d)",
              plaintext,
              strlen (plaintext));
  enc_size = GNUNET_CRYPTO_symmetric_encrypt (plaintext, strlen (plaintext),
                                              &skey, &iv,
                                              *cyphertext);
  GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Encrypted (len=%d)", enc_size);
  return GNUNET_OK;
}




/**
 * Identity Token API
 */


/**
 * Create an Identity Token
 *
 * @param type the JSON API resource type
 * @param id the JSON API resource id
 * @return a new JSON API resource or NULL on error.
 */
struct GNUNET_IDENTITY_PROVIDER_Token*
GNUNET_IDENTITY_PROVIDER_token_create (const struct GNUNET_CRYPTO_EcdsaPublicKey* iss,
                                       const struct GNUNET_CRYPTO_EcdsaPublicKey* aud)
{
  struct GNUNET_IDENTITY_PROVIDER_Token *token;
  char* audience;
  char* issuer;

  issuer = GNUNET_STRINGS_data_to_string_alloc (iss,
                                                sizeof (struct GNUNET_CRYPTO_EcdsaPublicKey));
  audience = GNUNET_STRINGS_data_to_string_alloc (aud,
                                                  sizeof (struct GNUNET_CRYPTO_EcdsaPublicKey));



  token = GNUNET_malloc (sizeof (struct GNUNET_IDENTITY_PROVIDER_Token));

  token->header = json_object();
  token->payload = json_object();

  json_object_set_new (token->header, "alg", json_string ("ED512"));
  json_object_set_new (token->header, "typ", json_string ("JWT"));

  json_object_set_new (token->payload, "iss", json_string (issuer));
  json_object_set_new (token->payload, "aud", json_string (audience));

  token->aud_key = *aud;
  GNUNET_free (issuer);
  GNUNET_free (audience);
  return token;
}

void
GNUNET_IDENTITY_PROVIDER_token_destroy (struct GNUNET_IDENTITY_PROVIDER_Token *token)
{
  json_decref (token->header);
  json_decref (token->payload);
  GNUNET_free (token);
}

void
GNUNET_IDENTITY_PROVIDER_token_add_attr (const struct GNUNET_IDENTITY_PROVIDER_Token *token,
                                         const char* key,
                                         const char* value)
{
  GNUNET_assert (NULL != token);
  GNUNET_assert (NULL != token->payload);

  json_object_set_new (token->payload, key, json_string (value));
}

void
GNUNET_IDENTITY_PROVIDER_token_add_json (const struct GNUNET_IDENTITY_PROVIDER_Token *token,
                                         const char* key,
                                         json_t* value)
{
  GNUNET_assert (NULL != token);
  GNUNET_assert (NULL != token->payload);

  json_object_set_new (token->payload, key, value);
}


int
GNUNET_IDENTITY_PROVIDER_token_parse2 (const char* raw_data,
                                       const struct GNUNET_CRYPTO_EcdhePrivateKey *priv_key,
                                       const struct GNUNET_CRYPTO_EcdsaPublicKey *aud_key,
                                       struct GNUNET_IDENTITY_PROVIDER_Token **result)
{
  char *enc_token_str;
  char *tmp_buf;
  char *token_str;
  char *enc_token;
  char *header;
  char *header_base64;
  char *payload;
  char *payload_base64;
  size_t enc_token_len;
  json_error_t err_json;

  GNUNET_asprintf (&tmp_buf, "%s", raw_data);
  strtok (tmp_buf, ",");
  enc_token_str = strtok (NULL, ",");

  enc_token_len = GNUNET_STRINGS_base64_decode (enc_token_str,
                                                strlen (enc_token_str),
                                                &enc_token);
  if (GNUNET_OK != decrypt_str_ecdhe2 (priv_key,
                                       aud_key,
                                       enc_token,
                                       enc_token_len,
                                       &token_str))
  {
    GNUNET_free (tmp_buf);
    GNUNET_free (enc_token);
    return GNUNET_SYSERR;
  }

  header_base64 = strtok (token_str, ".");
  payload_base64 = strtok (NULL, ".");

  GNUNET_STRINGS_base64_decode (header_base64,
                                strlen (header_base64),
                                &header);
  GNUNET_STRINGS_base64_decode (payload_base64,
                                strlen (payload_base64),
                                &payload);
  //TODO signature


  *result = GNUNET_malloc (sizeof (struct GNUNET_IDENTITY_PROVIDER_Token));
  (*result)->aud_key =  *aud_key;
  (*result)->header = json_loads (header, JSON_DECODE_ANY, &err_json);
  (*result)->payload = json_loads (payload, JSON_DECODE_ANY, &err_json);
  GNUNET_free (enc_token);
  GNUNET_free (token_str);
  GNUNET_free (tmp_buf);
  GNUNET_free (payload);
  GNUNET_free (header);
  return GNUNET_OK;
}

int
GNUNET_IDENTITY_PROVIDER_token_parse (const char* raw_data,
                                      const struct GNUNET_CRYPTO_EcdsaPrivateKey *priv_key,
                                      struct GNUNET_IDENTITY_PROVIDER_Token **result)
{
  char *ecdh_pubkey_str;
  char *enc_token_str;
  char *tmp_buf;
  char *token_str;
  char *enc_token;
  char *header;
  char *header_base64;
  char *payload;
  char *payload_base64;
  size_t enc_token_len;
  json_error_t err_json;
  struct GNUNET_CRYPTO_EcdhePublicKey ecdh_pubkey;

  GNUNET_asprintf (&tmp_buf, "%s", raw_data);
  ecdh_pubkey_str = strtok (tmp_buf, ",");
  enc_token_str = strtok (NULL, ",");

  GNUNET_STRINGS_string_to_data (ecdh_pubkey_str,
                                 strlen (ecdh_pubkey_str),
                                 &ecdh_pubkey,
                                 sizeof (struct GNUNET_CRYPTO_EcdhePublicKey));
  enc_token_len = GNUNET_STRINGS_base64_decode (enc_token_str,
                                                strlen (enc_token_str),
                                                &enc_token);
  if (GNUNET_OK != decrypt_str_ecdhe (priv_key,
                                      &ecdh_pubkey,
                                      enc_token,
                                      enc_token_len,
                                      &token_str))
  {
    GNUNET_free (tmp_buf);
    GNUNET_free (enc_token);
    return GNUNET_SYSERR;
  }

  header_base64 = strtok (token_str, ".");
  payload_base64 = strtok (NULL, ".");

  GNUNET_STRINGS_base64_decode (header_base64,
                                strlen (header_base64),
                                &header);
  GNUNET_STRINGS_base64_decode (payload_base64,
                                strlen (payload_base64),
                                &payload);
  //TODO signature and aud key


  *result = GNUNET_malloc (sizeof (struct GNUNET_IDENTITY_PROVIDER_Token));
  (*result)->header = json_loads (header, JSON_DECODE_ANY, &err_json);
  (*result)->payload = json_loads (payload, JSON_DECODE_ANY, &err_json);
  GNUNET_free (enc_token);
  GNUNET_free (token_str);
  GNUNET_free (tmp_buf);
  GNUNET_free (payload);
  GNUNET_free (header);
  return GNUNET_OK;
}

int
GNUNET_IDENTITY_PROVIDER_token_to_string (const struct GNUNET_IDENTITY_PROVIDER_Token *token,
                                          const struct GNUNET_CRYPTO_EcdsaPrivateKey *priv_key,
                                          char **result)
{
  char *payload_str;
  char *header_str;
  char *payload_base64;
  char *header_base64;
  char *padding;
  char *signature_target;
  char *signature_str;
  struct GNUNET_CRYPTO_EccSignaturePurpose *purpose;
  header_str = json_dumps (token->header, JSON_COMPACT);
  GNUNET_STRINGS_base64_encode (header_str,
                                strlen (header_str),
                                &header_base64);
  //Remove GNUNET padding of base64
  padding = strtok(header_base64, "=");
  while (NULL != padding)
    padding = strtok(NULL, "=");

  payload_str = json_dumps (token->payload, JSON_COMPACT);
  GNUNET_STRINGS_base64_encode (payload_str,
                                strlen (payload_str),
                                &payload_base64);

  //Remove GNUNET padding of base64
  padding = strtok(payload_base64, "=");
  while (NULL != padding)
    padding = strtok(NULL, "=");

  GNUNET_asprintf (&signature_target, "%s,%s", header_base64, payload_base64);
  purpose =
    GNUNET_malloc (sizeof (struct GNUNET_CRYPTO_EccSignaturePurpose) +
                   strlen (signature_target));
  purpose->size =
    htonl (strlen (signature_target) + sizeof (struct GNUNET_CRYPTO_EccSignaturePurpose));
  purpose->purpose = htonl(GNUNET_SIGNATURE_PURPOSE_GNUID_TOKEN);
  memcpy (&purpose[1], signature_target, strlen (signature_target));
  if (GNUNET_OK != GNUNET_CRYPTO_ecdsa_sign (priv_key,
                                             purpose,
                                             (struct GNUNET_CRYPTO_EcdsaSignature *)&token->signature))
  {
    GNUNET_free (signature_target);
    GNUNET_free (payload_str);
    GNUNET_free (header_str);
    GNUNET_free (payload_base64);
    GNUNET_free (header_base64);
    GNUNET_free (purpose);
    return GNUNET_SYSERR;
  }

  GNUNET_STRINGS_base64_encode ((const char*)&token->signature,
                                sizeof (struct GNUNET_CRYPTO_EcdsaSignature),
                                &signature_str);
  GNUNET_asprintf (result, "%s.%s.%s",
                   header_base64, payload_base64, signature_str);
  GNUNET_free (signature_target);
  GNUNET_free (payload_str);
  GNUNET_free (header_str);
  GNUNET_free (signature_str);
  GNUNET_free (payload_base64);
  GNUNET_free (header_base64);
  GNUNET_free (purpose);
  return GNUNET_OK;
}

int
GNUNET_IDENTITY_PROVIDER_token_serialize (const struct GNUNET_IDENTITY_PROVIDER_Token *token,
                                          const struct GNUNET_CRYPTO_EcdsaPrivateKey *priv_key,
                                          struct GNUNET_CRYPTO_EcdhePrivateKey **ecdh_privkey,
                                          char **result)
{
  char *token_str;
  char *enc_token;
  char *dh_key_str;
  char *enc_token_base64;
  struct GNUNET_CRYPTO_EcdhePublicKey ecdh_pubkey;

  GNUNET_assert (GNUNET_OK == GNUNET_IDENTITY_PROVIDER_token_to_string (token,
                                                                        priv_key,
                                                                        &token_str));

  GNUNET_assert (GNUNET_OK == encrypt_str_ecdhe (token_str,
                                                 &token->aud_key,
                                                 &enc_token,
                                                 ecdh_privkey,
                                                 &ecdh_pubkey));
  GNUNET_STRINGS_base64_encode (enc_token,
                                strlen (token_str),
                                &enc_token_base64);
  dh_key_str = GNUNET_STRINGS_data_to_string_alloc (&ecdh_pubkey,
                                                    sizeof (struct GNUNET_CRYPTO_EcdhePublicKey));
  GNUNET_asprintf (result, "%s,%s", dh_key_str, enc_token_base64);
  GNUNET_free (dh_key_str);
  GNUNET_free (enc_token_base64);
  GNUNET_free (enc_token);
  GNUNET_free (token_str);
  return GNUNET_OK;
}

struct GNUNET_IDENTITY_PROVIDER_TokenTicketPayload*
GNUNET_IDENTITY_PROVIDER_ticket_payload_create (const char* nonce,
                                                const struct GNUNET_CRYPTO_EcdsaPublicKey* identity_pkey,
                                                const char* lbl_str)
{
  struct GNUNET_IDENTITY_PROVIDER_TokenTicketPayload* payload;

  payload = GNUNET_malloc (sizeof (struct GNUNET_IDENTITY_PROVIDER_TokenTicketPayload));
  GNUNET_asprintf (&payload->nonce, nonce, strlen (nonce));
  payload->identity_key = *identity_pkey;
  GNUNET_asprintf (&payload->label, lbl_str, strlen (lbl_str));
  return payload;
}

void
GNUNET_IDENTITY_PROVIDER_ticket_payload_destroy (struct GNUNET_IDENTITY_PROVIDER_TokenTicketPayload* payload)
{
  GNUNET_free (payload->nonce);
  GNUNET_free (payload->label);
  GNUNET_free (payload);
}

void
GNUNET_IDENTITY_PROVIDER_ticket_payload_serialize (struct GNUNET_IDENTITY_PROVIDER_TokenTicketPayload *payload,
                                                   char **result)
{
  char* identity_key_str;

  identity_key_str = GNUNET_STRINGS_data_to_string_alloc (&payload->identity_key,
                                                          sizeof (struct GNUNET_CRYPTO_EcdsaPublicKey));

  GNUNET_asprintf (result, 
                   "{\"nonce\": \"%u\",\"identity\": \"%s\",\"label\": \"%s\"}",
                   payload->nonce, identity_key_str, payload->label);
  GNUNET_free (identity_key_str);

}


/**
 * Create the token code
 * The metadata is encrypted with a share ECDH derived secret using B (aud_key)
 * and e (ecdh_privkey)
 * The ticket also contains E (ecdh_pubkey) and a signature over the
 * metadata and E
 */
struct GNUNET_IDENTITY_PROVIDER_TokenTicket*
GNUNET_IDENTITY_PROVIDER_ticket_create (const char* nonce_str,
                                        const struct GNUNET_CRYPTO_EcdsaPublicKey* identity_pkey,
                                        const char* lbl_str,
                                        const struct GNUNET_CRYPTO_EcdsaPublicKey *aud_key)
{
  struct GNUNET_IDENTITY_PROVIDER_TokenTicket *ticket;
  struct GNUNET_IDENTITY_PROVIDER_TokenTicketPayload *code_payload;

  ticket = GNUNET_malloc (sizeof (struct GNUNET_IDENTITY_PROVIDER_TokenTicket));
  code_payload = GNUNET_IDENTITY_PROVIDER_ticket_payload_create (nonce_str,
                                                                 identity_pkey,
                                                                 lbl_str);
  ticket->aud_key = *aud_key;
  ticket->payload = code_payload;


  return ticket;
}

void
GNUNET_IDENTITY_PROVIDER_ticket_destroy (struct GNUNET_IDENTITY_PROVIDER_TokenTicket *ticket)
{
  GNUNET_IDENTITY_PROVIDER_ticket_payload_destroy (ticket->payload);
  GNUNET_free (ticket);
}

int
GNUNET_IDENTITY_PROVIDER_ticket_serialize (struct GNUNET_IDENTITY_PROVIDER_TokenTicket *ticket,
                                           const struct GNUNET_CRYPTO_EcdsaPrivateKey *priv_key,
                                           char **result)
{
  char *code_payload_str;
  char *enc_ticket_payload;
  char *ticket_payload_str;
  char *ticket_sig_str;
  char *ticket_str;
  char *dh_key_str;
  char *write_ptr;
  struct GNUNET_CRYPTO_EcdhePrivateKey *ecdhe_privkey;

  struct GNUNET_CRYPTO_EccSignaturePurpose *purpose;

  GNUNET_IDENTITY_PROVIDER_ticket_payload_serialize (ticket->payload,
                                                     &code_payload_str);

  GNUNET_assert (GNUNET_OK == encrypt_str_ecdhe (code_payload_str,
                                                 &ticket->aud_key,
                                                 &enc_ticket_payload,
                                                 &ecdhe_privkey,
                                                 &ticket->ecdh_pubkey));

  GNUNET_free (ecdhe_privkey);

  purpose = 
    GNUNET_malloc (sizeof (struct GNUNET_CRYPTO_EccSignaturePurpose) + 
                   sizeof (struct GNUNET_CRYPTO_EcdhePublicKey) + //E
                   strlen (code_payload_str)); // E_K (code_str)
  purpose->size = 
    htonl (sizeof (struct GNUNET_CRYPTO_EccSignaturePurpose) +
           sizeof (struct GNUNET_CRYPTO_EcdhePublicKey) +
           strlen (code_payload_str));
  purpose->purpose = htonl(GNUNET_SIGNATURE_PURPOSE_GNUID_TICKET);
  write_ptr = (char*) &purpose[1];
  memcpy (write_ptr,
          &ticket->ecdh_pubkey,
          sizeof (struct GNUNET_CRYPTO_EcdhePublicKey));
  write_ptr += sizeof (struct GNUNET_CRYPTO_EcdhePublicKey);
  memcpy (write_ptr, enc_ticket_payload, strlen (code_payload_str));
  GNUNET_assert (GNUNET_OK == GNUNET_CRYPTO_ecdsa_sign (priv_key,
                                                        purpose,
                                                       &ticket->signature));
  GNUNET_STRINGS_base64_encode (enc_ticket_payload,
                                strlen (code_payload_str),
                                &ticket_payload_str);
  ticket_sig_str = GNUNET_STRINGS_data_to_string_alloc (&ticket->signature,
                                                        sizeof (struct GNUNET_CRYPTO_EcdsaSignature));

  dh_key_str = GNUNET_STRINGS_data_to_string_alloc (&ticket->ecdh_pubkey,
                                                    sizeof (struct GNUNET_CRYPTO_EcdhePublicKey));
  GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Using ECDH pubkey %s to encrypt\n", dh_key_str);
  GNUNET_asprintf (&ticket_str, "{\"meta\": \"%s\", \"ecdh\": \"%s\", \"signature\": \"%s\"}",
                   ticket_payload_str, dh_key_str, ticket_sig_str);
  GNUNET_STRINGS_base64_encode (ticket_str, strlen (ticket_str), result);
  GNUNET_free (dh_key_str);
  GNUNET_free (purpose);
  GNUNET_free (ticket_str);
  GNUNET_free (ticket_sig_str);
  GNUNET_free (code_payload_str);
  GNUNET_free (enc_ticket_payload);
  GNUNET_free (ticket_payload_str);
  return GNUNET_OK;
}

int
GNUNET_IDENTITY_PROVIDER_ticket_payload_parse(const char *raw_data,
                                              ssize_t data_len,
                                              const struct GNUNET_CRYPTO_EcdsaPrivateKey *priv_key,
                                              const struct GNUNET_CRYPTO_EcdhePublicKey *ecdhe_pkey,
                                              struct GNUNET_IDENTITY_PROVIDER_TokenTicketPayload **result)
{
  const char* label_str;
  const char* nonce_str;
  const char* identity_key_str;

  json_t *root;
  json_t *label_json;
  json_t *identity_json;
  json_t *nonce_json;
  json_error_t err_json;
  char* meta_str;
  struct GNUNET_CRYPTO_EcdsaPublicKey id_pkey;

  if (GNUNET_OK != decrypt_str_ecdhe (priv_key,
                                      ecdhe_pkey,
                                      raw_data,
                                      data_len,
                                      &meta_str))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Metadata decryption failed\n");
    return GNUNET_SYSERR;
  }

  GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Metadata: %s\n", meta_str);
  root = json_loads (meta_str, JSON_DECODE_ANY, &err_json);
  if (!root)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Error parsing metadata: %s\n", err_json.text);
    GNUNET_free (meta_str);
    return GNUNET_SYSERR;
  }

  identity_json = json_object_get (root, "identity");
  if (!json_is_string (identity_json))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Error parsing metadata: %s\n", err_json.text);
    json_decref (root);
    GNUNET_free (meta_str);
    return GNUNET_SYSERR;
  }
  identity_key_str = json_string_value (identity_json);
  GNUNET_STRINGS_string_to_data (identity_key_str,
                                 strlen (identity_key_str),
                                 &id_pkey,
                                 sizeof (struct GNUNET_CRYPTO_EcdsaPublicKey));


  label_json = json_object_get (root, "label");
  if (!json_is_string (label_json))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Error parsing metadata: %s\n", err_json.text);
    json_decref (root);
    GNUNET_free (meta_str);
    return GNUNET_SYSERR;
  }

  label_str = json_string_value (label_json);
  GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Found label: %s\n", label_str);

  nonce_json = json_object_get (root, "nonce");
  if (!json_is_string (label_json))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Error parsing metadata: %s\n", err_json.text);
    json_decref (root);
    GNUNET_free (meta_str);
    return GNUNET_SYSERR;
  }

  nonce_str = json_string_value (nonce_json);
  GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Found nonce: %s\n", nonce_str);

  *result = GNUNET_IDENTITY_PROVIDER_ticket_payload_create (nonce_str,
                                                            (const struct GNUNET_CRYPTO_EcdsaPublicKey*)&id_pkey,
                                                            label_str);
  GNUNET_free (meta_str);
  json_decref (root);
  return GNUNET_OK;

}

int
GNUNET_IDENTITY_PROVIDER_ticket_parse (const char *raw_data,
                                       const struct GNUNET_CRYPTO_EcdsaPrivateKey *priv_key,
                                       struct GNUNET_IDENTITY_PROVIDER_TokenTicket **result)
{
  const char* enc_meta_str;
  const char* ecdh_enc_str;
  const char* signature_enc_str;

  json_t *root;
  json_t *signature_json;
  json_t *ecdh_json;
  json_t *enc_meta_json;
  json_error_t err_json;
  char* enc_meta;
  char* ticket_decoded;
  char* write_ptr;
  size_t enc_meta_len;
  struct GNUNET_CRYPTO_EccSignaturePurpose *purpose;
  struct GNUNET_IDENTITY_PROVIDER_TokenTicket *ticket;
  struct GNUNET_IDENTITY_PROVIDER_TokenTicketPayload *ticket_payload;

  ticket_decoded = NULL;
  GNUNET_STRINGS_base64_decode (raw_data, strlen (raw_data), &ticket_decoded);
  GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Token Code: %s\n", ticket_decoded);
  root = json_loads (ticket_decoded, JSON_DECODE_ANY, &err_json);
  if (!root)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "%s\n", err_json.text);
    return GNUNET_SYSERR;
  }

  signature_json = json_object_get (root, "signature");
  ecdh_json = json_object_get (root, "ecdh");
  enc_meta_json = json_object_get (root, "meta");

  signature_enc_str = json_string_value (signature_json);
  ecdh_enc_str = json_string_value (ecdh_json);
  enc_meta_str = json_string_value (enc_meta_json);

  ticket = GNUNET_malloc (sizeof (struct GNUNET_IDENTITY_PROVIDER_TokenTicket));

  if (GNUNET_OK != GNUNET_STRINGS_string_to_data (ecdh_enc_str,
                                                  strlen (ecdh_enc_str),
                                                  &ticket->ecdh_pubkey,
                                                  sizeof  (struct GNUNET_CRYPTO_EcdhePublicKey)))
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "ECDH PKEY %s invalid in metadata\n", ecdh_enc_str);
    json_decref (root);
    GNUNET_free (ticket);
    return GNUNET_SYSERR;
  }
  GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Using ECDH pubkey %s for metadata decryption\n", ecdh_enc_str);
  if (GNUNET_OK != GNUNET_STRINGS_string_to_data (signature_enc_str,
                                                  strlen (signature_enc_str),
                                                  &ticket->signature,
                                                  sizeof (struct GNUNET_CRYPTO_EcdsaSignature)))
  {
    json_decref (root);
    GNUNET_free (ticket_decoded);
    GNUNET_free (ticket);
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "ECDH signature invalid in metadata\n");
    return GNUNET_SYSERR;
  }

  enc_meta_len = GNUNET_STRINGS_base64_decode (enc_meta_str,
                                               strlen (enc_meta_str),
                                               &enc_meta);


  GNUNET_IDENTITY_PROVIDER_ticket_payload_parse (enc_meta,
                                                 enc_meta_len,
                                                 priv_key,
                                                 (const struct GNUNET_CRYPTO_EcdhePublicKey*)&ticket->ecdh_pubkey,
                                                 &ticket_payload);

  ticket->payload = ticket_payload;
  //TODO: check signature here
  purpose = 
    GNUNET_malloc (sizeof (struct GNUNET_CRYPTO_EccSignaturePurpose) + 
                   sizeof (struct GNUNET_CRYPTO_EcdhePublicKey) + //E
                   enc_meta_len); // E_K (code_str)
  purpose->size = 
    htonl (sizeof (struct GNUNET_CRYPTO_EccSignaturePurpose) +
           sizeof (struct GNUNET_CRYPTO_EcdhePublicKey) +
           enc_meta_len);
  purpose->purpose = htonl(GNUNET_SIGNATURE_PURPOSE_GNUID_TICKET);
  write_ptr = (char*) &purpose[1];
  memcpy (write_ptr, &ticket->ecdh_pubkey, sizeof (struct GNUNET_CRYPTO_EcdhePublicKey));
  write_ptr += sizeof (struct GNUNET_CRYPTO_EcdhePublicKey);
  memcpy (write_ptr, enc_meta, enc_meta_len);

  if (GNUNET_OK != GNUNET_CRYPTO_ecdsa_verify (GNUNET_SIGNATURE_PURPOSE_GNUID_TICKET,
                                               purpose,
                                               &ticket->signature,
                                               &ticket_payload->identity_key))
  {
    GNUNET_IDENTITY_PROVIDER_ticket_destroy (ticket);
    GNUNET_free (ticket_decoded);
    json_decref (root);
    GNUNET_free (purpose);
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Error verifying signature for token code\n");
    return GNUNET_SYSERR;
  }
  *result = ticket;
  GNUNET_free (purpose);

  GNUNET_free (enc_meta);
  GNUNET_free (ticket_decoded);
  json_decref (root);
  return GNUNET_OK;

}



/* end of identity-token.c */
