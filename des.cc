
/*
 * Copyright (C) GS
 */

#ifndef __DES_CC__
#define __DES_CC__

#include "des.h"

//#define ngx_memcpy(dst, src, n)   (void) memcpy(dst, src, n)

static u_char des_block_size = 8;


TSReturnCode
des_encrypt(const u_char *key, u_char *data, unsigned len)
{
    int  rc;
    char pkey[8], pad, pads;

    memcpy(pkey, key, 8);

    des_setparity(pkey);

    pad = des_block_size - len % des_block_size;
    pads = pad;

    while (pads) {
        data[len++] = pad;
        pads--;
    }

    rc = ecb_crypt(pkey, (char *)data, len, DES_ENCRYPT);
    if (DES_FAILED(rc)) {
        return TS_ERROR;
    }

    return TS_SUCCESS;
}


TSReturnCode
des_decrypt(const u_char *key, u_char *data, unsigned len)
{
    int  rc;
    char pkey[8];

    memcpy(pkey, key, 8);

    des_setparity(pkey);

    rc = ecb_crypt(pkey, (char *)data, len, DES_DECRYPT);
    if (DES_FAILED(rc)) {
        return TS_ERROR;
    }

    return TS_SUCCESS;
}

#endif /* __DES_CC__ */
