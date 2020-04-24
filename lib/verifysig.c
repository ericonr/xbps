/*-
 * Licensed under the SPDX BSD-2-Clause identifier.
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/pem.h>

#include "xbps_api_impl.h"

static bool
rsa_verify_hash(struct xbps_repo *repo, xbps_data_t pubkey,
		unsigned char *sig, unsigned int siglen,
		unsigned char *sha256)
{
	BIO *bio;
	RSA *rsa;
	int rv;

	ERR_load_crypto_strings();
	SSL_load_error_strings();

	bio = BIO_new_mem_buf(__UNCONST(xbps_data_data_nocopy(pubkey)),
			xbps_data_size(pubkey));
	assert(bio);

	rsa = PEM_read_bio_RSA_PUBKEY(bio, NULL, NULL, NULL);
	if (rsa == NULL) {
		xbps_dbg_printf(repo->xhp, "`%s' error reading public key: %s\n",
		    repo->uri, ERR_error_string(ERR_get_error(), NULL));
		return false;
	}

	rv = RSA_verify(NID_sha1, sha256, SHA256_DIGEST_LENGTH, sig, siglen, rsa);
	RSA_free(rsa);
	BIO_free(bio);
	ERR_free_strings();

	return rv ? true : false;
}

bool
xbps_verify_signature(struct xbps_repo *repo, const char *sigfile,
		unsigned char *digest)
{
	xbps_dictionary_t repokeyd = NULL;
	xbps_data_t pubkey;
	char *hexfp = NULL;
	unsigned char *sig_buf = NULL;
	size_t sigbuflen, sigfilelen;
	char *rkeyfile = NULL;
	bool val = false;

	if (!xbps_dictionary_count(repo->idxmeta)) {
		xbps_dbg_printf(repo->xhp, "%s: unsigned repository\n", repo->uri);
		return false;
	}
	hexfp = xbps_pubkey2fp(repo->xhp,
	    xbps_dictionary_get(repo->idxmeta, "public-key"));
	if (hexfp == NULL) {
		xbps_dbg_printf(repo->xhp, "%s: incomplete signed repo, missing hexfp obj\n", repo->uri);
		return false;
	}

	/*
	 * Prepare repository RSA public key to verify fname signature.
	 */
	rkeyfile = xbps_xasprintf("%s/keys/%s.plist", repo->xhp->metadir, hexfp);
	repokeyd = xbps_plist_dictionary_from_file(repo->xhp, rkeyfile);
	if (xbps_object_type(repokeyd) != XBPS_TYPE_DICTIONARY) {
		xbps_dbg_printf(repo->xhp, "cannot read rkey data at %s: %s\n",
		    rkeyfile, strerror(errno));
		goto out;
	}

	pubkey = xbps_dictionary_get(repokeyd, "public-key");
	if (xbps_object_type(pubkey) != XBPS_TYPE_DATA)
		goto out;

	if (!xbps_mmap_file(sigfile, (void *)&sig_buf, &sigbuflen, &sigfilelen)) {
		xbps_dbg_printf(repo->xhp, "can't open signature file %s: %s\n",
		    sigfile, strerror(errno));
		goto out;
	}
	/*
	 * Verify fname RSA signature.
	 */
	if (rsa_verify_hash(repo, pubkey, sig_buf, sigfilelen, digest))
		val = true;

out:
	if (hexfp)
		free(hexfp);
	if (rkeyfile)
		free(rkeyfile);
	if (sig_buf)
		(void)munmap(sig_buf, sigbuflen);
	if (repokeyd)
		xbps_object_release(repokeyd);

	return val;
}

bool
xbps_verify_file_signature(struct xbps_repo *repo, const char *fname)
{
	char sig[PATH_MAX];
	unsigned char digest[XBPS_SHA256_DIGEST_SIZE];
	bool val = false;

	if (!xbps_file_sha256_raw(digest, sizeof digest, fname)) {
		xbps_dbg_printf(repo->xhp, "can't open file %s: %s\n", fname, strerror(errno));
		return false;
	}

	snprintf(sig, sizeof sig, "%s.sig", fname);
	val = xbps_verify_signature(repo, sig, digest);

	return val;
}
