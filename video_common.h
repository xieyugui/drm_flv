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

//#define PLUGIN_NAME "drm_video"
//des key
static u_char *des_key;

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

class VideoTransformContext
{
public:
	VideoTransformContext(int16_t video_type,int64_t st, int64_t n, u_char *des_key) : total(0), parse_over(false)
	{
	    res_buffer = TSIOBufferCreate();
	    res_reader = TSIOBufferReaderAlloc(res_buffer);

	    ftag.start = st; //请求的range 起始位置
	    ftag.cl = n; //加密之后文件的大小，即pcf,pcm 大小
	    ftag.video_type = video_type;//文件类型
	    ftag.tdes_key = des_key;
	}

	~VideoTransformContext()
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

class VideoContext
{
public:
	VideoContext(int16_t videotype, int64_t s) :start(s), cl(0) , video_type(videotype),  transform_added(false),vtc(NULL){};

	~VideoContext()
	{
		if(vtc) {
			delete vtc;
			vtc = NULL;
		}
	}

public:
	int64_t start; //请求的range 起始位置
	int64_t cl;  //加密之后文件的大小，即pcf 大小
	int16_t video_type;
	bool transform_added;

	VideoTransformContext *vtc;
};

#endif /* __VIDEO_COMMON_H__ */
