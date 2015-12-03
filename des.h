
/*
 * Copyright (C) GS
 */


#ifndef __DES_H__
#define __DES_H__



#include <rpc/des_crypt.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "ts/apidefs.h"

//typedef unsigned char u_char;

TSReturnCode des_encrypt(const u_char *key, u_char *data, unsigned len);
TSReturnCode des_decrypt(const u_char *key, u_char *data, unsigned len);


#endif /* __DES_H__ */
