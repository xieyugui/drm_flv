/*
 * video_common.h
 *
 *  Created on: 2015年11月17日
 *      Author: xie
 */

#ifndef __VIDEO_COMMON_H__
#define __VIDEO_COMMON_H__

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>

#include <ts/ink_inet.h>
#include <ts/ts.h>
#include <ts/experimental.h>
#include <ts/remap.h>

#include "flv_tag.h"


class IOHandle
{
public:
	IOHandle() : vio(NULL), buffer(NULL), reader(NULL){};

	~IOHandle()
	{
		if(reader) {
			TSIOBufferReaderFree(reader);
			reader = NULL;
		}

		if(buffer) {
			TSIOBufferDestroy(buffer);
			buffer = NULL;
		}
	}

public:
	TSVIO vio;
	TSIOBuffer buffer;
	TSIOBufferReader reader;
};

class FlvTransformContext
{
public:
	FlvTransformContext(int16_t video_type,int64_t st, int64_t n, u_char *des_key) : total(0), parse_over(false)
	{
	    res_buffer = TSIOBufferCreate();
	    res_reader = TSIOBufferReaderAlloc(res_buffer);

	    ftag.start = st;
	    ftag.cl = n;
	    ftag.video_type = video_type;
	    ftag.tdes_key = des_key;
	}

	~FlvTransformContext()
	{
	  if (res_reader) {
	    TSIOBufferReaderFree(res_reader);
	  }


	  if (res_buffer) {
	    TSIOBufferDestroy(res_buffer);
	  }
	}

public:
	IOHandle output;
	TSIOBuffer res_buffer;
	TSIOBufferReader res_reader;
	FlvTag ftag;

	int64_t total;
	bool  parse_over;
};

class FlvContext
{
public:
	FlvContext(int16_t videotype, int64_t s) :start(s), cl(0) , video_type(videotype),  transform_added(false),ftc(NULL){};

	~FlvContext()
	{
		if(ftc) {
			delete ftc;
			ftc = NULL;
		}
	}

public:
	int64_t start;
	int64_t cl;
	int16_t video_type;
	bool transform_added;

	FlvTransformContext *ftc;
};

#endif /* __VIDEO_COMMON_H__ */
