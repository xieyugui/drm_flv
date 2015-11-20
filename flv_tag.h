/*
 * flv_tag.h
 *
 *  Created on: 2015年11月17日
 *      Author: xie
 */

#ifndef _DRM_VIDEO_FLV_TAG_H_
#define _DRM_VIDEO_FLV_TAG_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ts/ts.h>
#include <arpa/inet.h>

#define PLUGIN_NAME "drm_video"

class FlvTag;
typedef int (FlvTag::*FTHandler) ();

class FlvTag
{
public:
	FlvTag() : tag_buffer(NULL), tag_reader(NULL), head_length(0),
		head_buffer(NULL), head_reader(NULL), tag_pos(0), dup_pos(0), cl(0), tdes_key(NULL),
		content_length(0), start(0), key_found(false), video_type(0), version(0), need_des_body(NULL),
		videoid_size(0), videoid(NULL), userid_size(0), userid(NULL), reserved_size(0), reserved(NULL)
	{
		tag_buffer = TSIOBufferCreate();
		tag_reader = TSIOBufferReaderAlloc(tag_buffer);

		head_buffer = TSIOBufferCreate();
		head_reader = TSIOBufferReaderAlloc(head_buffer);

		current_handler = &FlvTag::process_header;
	}

	~FlvTag()
	{
		if (tag_reader) {
			TSIOBufferReaderFree(tag_reader);
			tag_reader = NULL;
		}

		if (tag_buffer) {
			TSIOBufferDestroy(tag_buffer);
			tag_buffer = NULL;
		}

        if (head_reader) {
            TSIOBufferReaderFree(head_reader);
            head_reader = NULL;
        }

        if (head_buffer) {
            TSIOBufferDestroy(head_buffer);
            head_buffer = NULL;
        }

        if (videoid) {
        		TSfree(videoid);
        		videoid = NULL;
        }

        if (userid) {
        		TSfree(userid);
        		userid = NULL;
        }

        if (reserved) {
        		TSfree(reserved);
        		reserved = NULL;
        }

        if (need_des_body) {
        		TSfree(need_des_body);
        		need_des_body = NULL;
        }

        tdes_key = NULL;
	}

	int process_tag(TSIOBufferReader reader, bool complete);
	int64_t write_out(TSIOBuffer buffer);

	int process_header();
	int process_header_videoid();
	int process_header_userid();
	int process_header_reserved();
	int process_initial_body();//丢弃的数据
	int process_decrypt_body();//解密
	int process_encrypt_body();//加密

public:
	TSIOBuffer tag_buffer;
	TSIOBufferReader tag_reader;

	TSIOBuffer head_buffer;
	TSIOBufferReader head_reader;

	FTHandler current_handler;
	int64_t tag_pos;
	int64_t dup_pos;
	int64_t cl;
	int64_t content_length;
	int64_t head_length;

	//----DRM header start
	//char signature[3]; 标志位
	uint32_t version;  //4

	uint32_t videoid_size;  //4 tail_length
	u_char *videoid; //视频 id 标签

	uint32_t userid_size; //4
	u_char *userid;  //用户 id 标签

//	uint32_t range_size;    //4
//	uint64_t range_start;//同访问 mp4 时 http response header 的 Content-Range 字段的 start  8
//	uint64_t range_end;//同访问 mp4 时 http response header 的 Content-Range 字段的 end  8
//	uint64_t original_size; //mp4 文件字节数  8
//
//	uint32_t section_size; //4
//	uint32_t section_count; //4  count>0 && count<=5 加密片段个数
//	uint64_t section_length; //8

	uint32_t reserved_size; //4
	u_char *reserved;
	//----DRM header  end

	u_char *need_des_body;//需要进行加密的body字节

	int64_t start;
	int16_t video_type;
    u_char *tdes_key; //des key
	bool key_found;
};

#endif /* _DRM_VIDEO_FLV_TAG_H_ */
