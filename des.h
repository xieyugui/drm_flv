
/*
 * Copyright (C) GS
 */


#ifndef PLUGINS_DRM_VIDEO_DES_H_
#define PLUGINS_DRM_VIDEO_DES_H_



#include <rpc/des_crypt.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "ts/apidefs.h"

//typedef unsigned char u_char;

TSReturnCode des_encrypt(const u_char *key, u_char *data, unsigned len);
TSReturnCode des_decrypt(const u_char *key, u_char *data, unsigned len);


#endif /* PLUGINS_DRM_VIDEO_DES_H_ */
