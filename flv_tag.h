/*
 * flv_tag.h
 *
 *  Created on: 2015年11月17日
 *      Author: xie
 */

#ifndef __FLV_TAG_H__
#define __FLV_TAG_H__

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ts/ts.h>
#include "types.h"
#include "amf.h"

#define PLUGIN_NAME "drm_video"

#define VIDEO_SIGNATURE_LENGTH 3
#define FLV_1_NEED_DES_LENGTH 128
#define FLV_1_DES_LENGTH 136

#define FLV_UI32(x) (int)(((x[0]) << 24) + ((x[1]) << 16) + ((x[2]) << 8) + (x[3])) //将字符串转为整数
#define FLV_UI24(x) (int)(((x[0]) << 16) + ((x[1]) << 8) + (x[2]))
#define FLV_UI16(x) (int)(((x[0]) << 8) + (x[1]))
#define FLV_UI8(x) (int)((x))

//TAG 类型 8:音频 9:视频 18:脚本 其他:保留
/* FLV tag */
#define FLV_TAG_TYPE_AUDIO  ((uint8)0x08)
#define FLV_TAG_TYPE_VIDEO  ((uint8)0x09)
#define FLV_TAG_TYPE_META   ((uint8)0x12)

typedef enum { VIDEO_VERSION_1 = 1, VIDEO_VERSION_3 = 3, VIDEO_VERSION_4  = 4 } video_version;
typedef enum { VIDEO_PCF , VIDEO_PCM  } VideoType;

//typedef struct _drm_header_common {
//	byte signature[VIDEO_SIGNATURE_LENGTH]; //标志信息
//	uint32_be version; //版本
//	uint32_be videoid_size; //videoid 长度
//} drm_header_common;

typedef struct __flv_header {
    byte            signature[3]; /* always "FLV" */
    uint8           version; /* should be 1 */
    uint8_bitmask   flags;
    uint32_be       offset; /* always 9 */
} flv_header;


typedef struct __flv_tag {
    uint8       type; //1 bytes TAG 类型 8:音频 9:视频 18:脚本 其他:保留
    uint24_be   body_length; /* in bytes, total tag size minus 11 */
    uint24_be   timestamp; /* milli-seconds */
    uint8       timestamp_extended; /* timestamp extension */
    uint24_be   stream_id; /* reserved, must be "\0\0\0" */
    /* body comes next */
} flv_tag;

union av_intfloat64 {
    uint64_t i;
    double f;
};

#define flv_tag_get_body_length(tag)    (uint24_be_to_uint32((tag).body_length))
#define flv_tag_get_timestamp(tag) \
    (uint24_be_to_uint32((tag).timestamp) + ((tag).timestamp_extended << 24))


class FlvTag;
typedef int (FlvTag::*FTHandler) ();

class FlvTag
{
public:
	FlvTag() : tag_buffer(NULL), tag_reader(NULL), drm_head_length(0),video_body_size(0),version(0),videoid_size(0),
		head_buffer(NULL), head_reader(NULL), tag_pos(0), dup_pos(0), cl(0), tdes_key(NULL),duration_file_size(0),on_meta_data_size(0),
		content_length(0), start(0), key_found(false), video_type(0) ,video_head_length(0),duration_time(0),duration_video_size(0),duration_audio_size(0),
		videoid(NULL), userid_size(0), userid(NULL), reserved_size(0), reserved(NULL),dup_reader(NULL),flv_buffer(NULL),flv_reader(NULL),
		new_flv_buffer(NULL),new_flv_reader(NULL)
	{
		tag_buffer = TSIOBufferCreate();
		tag_reader = TSIOBufferReaderAlloc(tag_buffer);
		dup_reader = TSIOBufferReaderAlloc(tag_buffer);

		head_buffer = TSIOBufferCreate();
		head_reader = TSIOBufferReaderAlloc(head_buffer);

		flv_buffer = TSIOBufferCreate();
		flv_reader = TSIOBufferReaderAlloc(flv_buffer);

		new_flv_buffer = TSIOBufferCreate();
		new_flv_reader = TSIOBufferReaderAlloc(new_flv_buffer);

		current_handler = &FlvTag::process_header;
	}

	~FlvTag()
	{
		if (tag_reader) {
			TSIOBufferReaderFree(tag_reader);
			tag_reader = NULL;
		}

        if (dup_reader) {
            TSIOBufferReaderFree(dup_reader);
            dup_reader = NULL;
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

        if (flv_reader) {
            TSIOBufferReaderFree(flv_reader);
            flv_reader = NULL;
        }

        if (flv_buffer) {
            TSIOBufferDestroy(flv_buffer);
            flv_buffer = NULL;
        }

        if (new_flv_reader) {
            TSIOBufferReaderFree(new_flv_reader);
            new_flv_reader = NULL;
        }

        if (new_flv_buffer) {
            TSIOBufferDestroy(new_flv_buffer);
            new_flv_buffer = NULL;
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

//        if (need_des_body) {
//        		TSfree(need_des_body);
//        		need_des_body = NULL;
//        }

        tdes_key = NULL;
	}

	int process_tag(TSIOBufferReader reader, bool complete);
	int64_t write_out(TSIOBuffer buffer);

	int process_header();
	int process_header_videoid();
	int process_header_userid();
	int process_header_reserved();
	int process_decrypt_body();//解密
	int process_initial_video_header();
	int process_initial_body();
	int process_medial_body();
	int process_check_des_body();

	int update_flv_meta_data(); //更新FLV on_meta_data信息
	int flv_read_metadata(byte *stream,amf_data ** name, amf_data ** data, size_t maxbytes);



public:
	TSIOBuffer tag_buffer;
	TSIOBufferReader tag_reader;
	TSIOBufferReader    dup_reader;

	TSIOBuffer head_buffer;
	TSIOBufferReader head_reader;

	TSIOBuffer flv_buffer;
	TSIOBufferReader flv_reader;

	TSIOBuffer new_flv_buffer;
	TSIOBufferReader new_flv_reader;

	FTHandler current_handler;
	int64_t tag_pos;
	int64_t dup_pos;
	int64_t cl;
	int64_t content_length;
	int64_t drm_head_length;
	int64_t video_head_length;

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
	uint64_t on_meta_data_size;


//	uint32_t discard_size;
//	uint32_t duration_time;

	uint64_t duration_file_size;
	double duration_time;  //丢弃的时间
	uint64_t duration_video_size; //丢弃的video 大小
	uint64_t duration_audio_size; //丢弃的audio 大小

	uint32_t reserved_size; //4
	u_char *reserved;
	//----DRM header  end

	uint64_t video_body_size;
	int64_t start;
	int16_t video_type;
    u_char *tdes_key; //des key
	bool key_found;
};

#endif /* __FLV_TAG_H__ */
