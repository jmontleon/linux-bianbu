// SPDX-License-Identifier: GPL-2.0
/*
 * spacemit aes skcipher driver
 *
 * Copyright (C) 2023 Spacemit
 */

#include <crypto/aes.h>
#include <crypto/internal/skcipher.h>
#include <crypto/algapi.h>
#include <linux/module.h>
#include <linux/cpufeature.h>
#include <linux/types.h>
#include "crypto/skcipher.h"
#include "spacemit_engine.h"

int aes_expandkey_nouse(struct crypto_aes_ctx *key, u8 const in[], int size){return 0;}
#define aes_expandkey		aes_expandkey_nouse
#define PRIO			500
#define MODE			"spacemit-ce1"

extern int spacemit_aes_ecb_encrypt(int index, const unsigned char *pt,unsigned char *ct, u8 *key, unsigned int len, unsigned int blocks);
extern int spacemit_aes_ecb_decrypt(int index, const unsigned char *ct,unsigned char *pt, u8 *key, unsigned int len, unsigned int blocks);
extern int spacemit_aes_cbc_encrypt(int index, const unsigned char *pt,unsigned char *ct, u8 *key, unsigned int len, u8 *IV,unsigned int blocks);
extern int spacemit_aes_cbc_decrypt(int index, const unsigned char *ct,unsigned char *pt, u8 *key, unsigned int len, u8 *IV,unsigned int blocks);
extern int spacemit_crypto_aes_set_key(int index, struct crypto_tfm *tfm, const u8 *key,unsigned int keylen);

int aes_setkey(struct crypto_skcipher *tfm, const u8 *key,unsigned int keylen)
{
	return spacemit_crypto_aes_set_key(0, &tfm->base, key, keylen);
}

static int ecb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, first;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, false);

	for (first = 1; (blocks = (walk.nbytes / AES_BLOCK_SIZE)); first = 0) {
		spacemit_aes_ecb_encrypt(0,walk.src.virt.addr, walk.dst.virt.addr,
				(u8 *)(ctx->key_enc),(unsigned int)(ctx->key_length), blocks);
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}

	return err;
}

static int ecb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err,  first;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, false);

	for (first = 1; (blocks = (walk.nbytes / AES_BLOCK_SIZE)); first = 0) {
		spacemit_aes_ecb_decrypt(0,walk.src.virt.addr, walk.dst.virt.addr,
				(u8 *)(ctx->key_dec),(unsigned int)(ctx->key_length), blocks);
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}

	return err;
}

static int cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err,  first;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, false);

	for (first = 1; (blocks = (walk.nbytes / AES_BLOCK_SIZE)); first = 0) {
		spacemit_aes_cbc_encrypt(0,walk.src.virt.addr, walk.dst.virt.addr,(u8 *)(ctx->key_enc),
				(unsigned int)(ctx->key_length), (u8 *)walk.iv,blocks);
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}

	return err;
}

static int cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err,  first;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, false);

	for (first = 1; (blocks = (walk.nbytes / AES_BLOCK_SIZE)); first = 0) {
		spacemit_aes_cbc_decrypt(0,walk.src.virt.addr, walk.dst.virt.addr,(u8 *)(ctx->key_dec),
				(unsigned int)(ctx->key_length), (u8 *)walk.iv,blocks);
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}

	return err;
}

static struct skcipher_alg aes_algs[] = {
{
	.base.cra_name		= "ecb(aes)",
	.base.cra_driver_name	= "__driver-ecb-aes-" MODE,
	.base.cra_priority	= PRIO,
	.base.cra_flags		= CRYPTO_ALG_ASYNC,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct crypto_aes_ctx),
	.base.cra_alignmask	= 0xf,
	.base.cra_module	= THIS_MODULE,
	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.ivsize			= 0,
	.setkey			= aes_setkey,
	.encrypt		= ecb_encrypt,
	.decrypt		= ecb_decrypt,
}, {
	.base.cra_name		= "cbc(aes)",
	.base.cra_driver_name	= "__driver-cbc-aes-" MODE,
	.base.cra_priority	= PRIO,
	.base.cra_flags		= CRYPTO_ALG_ASYNC,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct crypto_aes_ctx),
	.base.cra_alignmask	= 0xf,
	.base.cra_module	= THIS_MODULE,
	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= aes_setkey,
	.encrypt		= cbc_encrypt,
	.decrypt		= cbc_decrypt,
},
};

static int __init aes_init(void)
{
	return crypto_register_skciphers(aes_algs, ARRAY_SIZE(aes_algs));
}

static void __exit aes_exit(void)
{
	crypto_unregister_skciphers(aes_algs, ARRAY_SIZE(aes_algs));
}

module_init(aes_init);
module_exit(aes_exit);

MODULE_DESCRIPTION("AES-ECB/CBC using Spacemit CE Engine");
MODULE_ALIAS_CRYPTO("ecb(aes)");
MODULE_ALIAS_CRYPTO("cbc(aes)");
MODULE_LICENSE("GPL v2");
