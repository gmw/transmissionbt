/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <string.h> /* memcpy (), memmove (), memset (), strcmp () */

#include "transmission.h"
#include "crypto.h"
#include "crypto-utils.h"
#include "utils.h"

/**
***
**/

#define PRIME_LEN 96
#define DH_PRIVKEY_LEN 20

static const uint8_t dh_P[PRIME_LEN] =
{
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2,
  0x21, 0x68, 0xC2, 0x34, 0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
  0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74, 0x02, 0x0B, 0xBE, 0xA6,
  0x3B, 0x13, 0x9B, 0x22, 0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
  0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B, 0x30, 0x2B, 0x0A, 0x6D,
  0xF2, 0x5F, 0x14, 0x37, 0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
  0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6, 0xF4, 0x4C, 0x42, 0xE9,
  0xA6, 0x3A, 0x36, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x05, 0x63,
};

static const uint8_t dh_G[] = { 2 };

/**
***
**/

static void
ensureKeyExists (tr_crypto * crypto)
{
  if (crypto->dh == NULL)
    {
      size_t public_key_length;

      crypto->dh = tr_dh_new (dh_P, sizeof (dh_P), dh_G, sizeof (dh_G));
      tr_dh_make_key (crypto->dh, DH_PRIVKEY_LEN, crypto->myPublicKey, &public_key_length);

      assert (public_key_length == KEY_LEN);
    }
}

void
tr_cryptoConstruct (tr_crypto * crypto, const uint8_t * torrentHash, bool isIncoming)
{
  memset (crypto, 0, sizeof (tr_crypto));

  crypto->isIncoming = isIncoming;
  tr_cryptoSetTorrentHash (crypto, torrentHash);
}

void
tr_cryptoDestruct (tr_crypto * crypto)
{
  tr_dh_secret_free (crypto->mySecret);
  tr_dh_free (crypto->dh);
  tr_rc4_free (crypto->enc_key);
  tr_rc4_free (crypto->dec_key);
}

/**
***
**/

bool
tr_cryptoComputeSecret (tr_crypto *     crypto,
                        const uint8_t * peerPublicKey)
{
  ensureKeyExists (crypto);
  crypto->mySecret = tr_dh_agree (crypto->dh, peerPublicKey, KEY_LEN);
  return crypto->mySecret != NULL;
}

const uint8_t*
tr_cryptoGetMyPublicKey (const tr_crypto * crypto,
                         int             * setme_len)
{
  ensureKeyExists ((tr_crypto *) crypto);
  *setme_len = KEY_LEN;
  return crypto->myPublicKey;
}

/**
***
**/

static void
initRC4 (tr_crypto    * crypto,
         tr_rc4_ctx_t * setme,
         const char   * key)
{
  uint8_t buf[SHA_DIGEST_LENGTH];

  assert (crypto->torrentHashIsSet);

  if (*setme == NULL)
    *setme = tr_rc4_new ();

  if (tr_cryptoSecretKeySha1 (crypto,
                              key, 4,
                              crypto->torrentHash, SHA_DIGEST_LENGTH,
                              buf))
    tr_rc4_set_key (*setme, buf, SHA_DIGEST_LENGTH);
}

void
tr_cryptoDecryptInit (tr_crypto * crypto)
{
  unsigned char discard[1024];
  const char * txt = crypto->isIncoming ? "keyA" : "keyB";

  initRC4 (crypto, &crypto->dec_key, txt);
  tr_rc4_process (crypto->dec_key, discard, discard, sizeof (discard));
}

void
tr_cryptoDecrypt (tr_crypto  * crypto,
                  size_t       buf_len,
                  const void * buf_in,
                  void       * buf_out)
{
  /* FIXME: someone calls this function with uninitialized key */
  if (crypto->dec_key == NULL)
    {
      if (buf_in != buf_out)
        memmove (buf_out, buf_in, buf_len);
      return;
    }

  tr_rc4_process (crypto->dec_key, buf_in, buf_out, buf_len);
}

void
tr_cryptoEncryptInit (tr_crypto * crypto)
{
  unsigned char discard[1024];
  const char * txt = crypto->isIncoming ? "keyB" : "keyA";

  initRC4 (crypto, &crypto->enc_key, txt);
  tr_rc4_process (crypto->enc_key, discard, discard, sizeof (discard));
}

void
tr_cryptoEncrypt (tr_crypto  * crypto,
                  size_t       buf_len,
                  const void * buf_in,
                  void       * buf_out)
{
  /* FIXME: someone calls this function with uninitialized key */
  if (crypto->enc_key == NULL)
    {
      if (buf_in != buf_out)
        memmove (buf_out, buf_in, buf_len);
      return;
    }

  tr_rc4_process (crypto->enc_key, buf_in, buf_out, buf_len);
}

bool
tr_cryptoSecretKeySha1 (const tr_crypto * crypto,
                        const void      * prepend_data,
                        size_t            prepend_data_size,
                        const void      * append_data,
                        size_t            append_data_size,
                        uint8_t         * hash)
{
  assert (crypto != NULL);
  assert (crypto->mySecret != NULL);

  return tr_dh_secret_derive (crypto->mySecret,
                              prepend_data, prepend_data_size,
                              append_data, append_data_size,
                              hash);
}

/**
***
**/

void
tr_cryptoSetTorrentHash (tr_crypto     * crypto,
                         const uint8_t * hash)
{
  crypto->torrentHashIsSet = hash != NULL;

  if (hash)
    memcpy (crypto->torrentHash, hash, SHA_DIGEST_LENGTH);
  else
    memset (crypto->torrentHash, 0, SHA_DIGEST_LENGTH);
}

const uint8_t*
tr_cryptoGetTorrentHash (const tr_crypto * crypto)
{
  assert (crypto);

  return crypto->torrentHashIsSet ? crypto->torrentHash : NULL;
}

bool
tr_cryptoHasTorrentHash (const tr_crypto * crypto)
{
  assert (crypto);

  return crypto->torrentHashIsSet;
}

/***
****
***/

char*
tr_ssha1 (const void * plaintext)
{
  enum { saltval_len = 8,
         salter_len  = 64 };
  static const char * salter = "0123456789"
                               "abcdefghijklmnopqrstuvwxyz"
                               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                               "./";

  size_t i;
  unsigned char salt[saltval_len];
  uint8_t sha[SHA_DIGEST_LENGTH];
  char buf[2*SHA_DIGEST_LENGTH + saltval_len + 2];

  tr_rand_buffer (salt, saltval_len);
  for (i=0; i<saltval_len; ++i)
    salt[i] = salter[ salt[i] % salter_len ];

  tr_sha1 (sha, plaintext, strlen (plaintext), salt, saltval_len, NULL);
  tr_sha1_to_hex (&buf[1], sha);
  memcpy (&buf[1+2*SHA_DIGEST_LENGTH], &salt, saltval_len);
  buf[1+2*SHA_DIGEST_LENGTH + saltval_len] = '\0';
  buf[0] = '{'; /* signal that this is a hash. this makes saving/restoring easier */

  return tr_strdup (&buf);
}

bool
tr_ssha1_matches (const char * source, const char * pass)
{
  char * salt;
  size_t saltlen;
  char * hashed;
  uint8_t buf[SHA_DIGEST_LENGTH];
  bool result;
  const size_t sourcelen = strlen (source);

  /* extract the salt */
  if (sourcelen < 2*SHA_DIGEST_LENGTH-1)
    return false;
  saltlen = sourcelen - 2*SHA_DIGEST_LENGTH-1;
  salt = tr_malloc (saltlen);
  memcpy (salt, source + 2*SHA_DIGEST_LENGTH+1, saltlen);

  /* hash pass + salt */
  hashed = tr_malloc (2*SHA_DIGEST_LENGTH + saltlen + 2);
  tr_sha1 (buf, pass, strlen (pass), salt, saltlen, NULL);
  tr_sha1_to_hex (&hashed[1], buf);
  memcpy (hashed + 1+2*SHA_DIGEST_LENGTH, salt, saltlen);
  hashed[1+2*SHA_DIGEST_LENGTH + saltlen] = '\0';
  hashed[0] = '{';

  result = strcmp (source, hashed) == 0 ? true : false;

  tr_free (hashed);
  tr_free (salt);

  return result;
}
