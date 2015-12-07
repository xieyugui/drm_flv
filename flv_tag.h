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

#define FLV_1_NEED_DES_LENGTH 128
#define FLV_1_DES_LENGTH 136
#define FLV_1_DES_SECTION_COUNT 1

#define FLV_3_NEED_DES_LENGTH 8176
#define FLV_3_DES_LENGTH 8184
#define FLV_3_DES_SECTION_COUNT 5

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

typedef struct _drm_header {
	byte signature[3]; //标志信息
	uint32_be version; //版本
	uint32_be videoid_size; //videoid 长度
} drm_header;

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
	FlvTag() : tag_buffer(NULL), tag_reader(NULL),des_buffer(NULL), des_reader(NULL),head_buffer(NULL), head_reader(NULL),
		flv_buffer(NULL),flv_reader(NULL),new_flv_buffer(NULL),new_flv_reader(NULL),
		tag_pos(0),  cl(0),content_length(0), drm_head_length(0),version(0),videoid_size(0),videoid(NULL), userid_size(0), userid(NULL),reserved_size(0), reserved(NULL),
		 flv_need_des_length(0),flv_des_length(0),flv_des_section_count(0),on_meta_data_size(0),
		 duration_file_size(0) ,duration_time(0),duration_video_size(0),duration_audio_size(0),video_body_size(0),start(0), video_type(0), tdes_key(NULL)
	{

		tag_buffer = TSIOBufferCreate();
		tag_reader = TSIOBufferReaderAlloc(tag_buffer);

		des_buffer = TSIOBufferCreate();
		des_reader = TSIOBufferReaderAlloc(des_buffer);

		head_buffer = TSIOBufferCreate();
		head_reader = TSIOBufferReaderAlloc(head_buffer);

		flv_buffer = TSIOBufferCreate();
		flv_reader = TSIOBufferReaderAlloc(flv_buffer);

		new_flv_buffer = TSIOBufferCreate();
		new_flv_reader = TSIOBufferReaderAlloc(new_flv_buffer);

		current_handler = &FlvTag::process_drm_header;
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

        if (des_reader) {
            TSIOBufferReaderFree(des_reader);
            des_reader = NULL;
        }
		if (des_buffer) {
			TSIOBufferDestroy(des_buffer);
			des_buffer = NULL;
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


        tdes_key = NULL;
	}

	int process_tag(TSIOBufferReader reader, bool complete);
	int64_t write_out(TSIOBuffer buffer);
	size_t get_drm_header_size();
	int flv_read_drm_header(TSIOBufferReader readerp, drm_header * header);

	size_t get_flv_header_size();
	int flv_read_flv_header(TSIOBufferReader readerp, flv_header * header);

	size_t get_flv_tag_size();
	int flv_read_flv_tag(TSIOBufferReader readerp, flv_tag * tag);

	int process_drm_header();
	int process_drm_header_videoid();
	int process_drm_header_userid();
	int process_drm_header_reserved();
	int process_decrypt_flv_body();//解密
	int process_initial_flv_header();
	int process_initial_body();
	int process_medial_body();
	int process_check_des_body();

	int update_flv_meta_data(); //更新FLV on_meta_data信息
	int flv_read_metadata(byte *stream,amf_data ** name, amf_data ** data, size_t maxbytes);



public:
	TSIOBuffer tag_buffer;
	TSIOBufferReader tag_reader;

	TSIOBuffer des_buffer;
	TSIOBufferReader    des_reader;

	TSIOBuffer head_buffer;
	TSIOBufferReader head_reader;

	TSIOBuffer flv_buffer;
	TSIOBufferReader flv_reader;

	TSIOBuffer new_flv_buffer;
	TSIOBufferReader new_flv_reader;

	FTHandler current_handler;
	int64_t tag_pos; //已经消费了多少字节
	int64_t cl; //文件总长度
	int64_t content_length;  //从start 处开始的文件总长度
	int64_t drm_head_length;  //drm head 的长度

	//----DRM header start
	//char signature[3]; 标志位
	uint32_t version;  //4

	uint32_t videoid_size;  //4 tail_length
	u_char *videoid; //视频 id 标签

	uint32_t userid_size; //4
	u_char *userid;  //用户 id 标签

	uint32_t reserved_size; //4
	u_char *reserved;
	//----DRM header  end

	size_t flv_need_des_length; //需要des加密的字符长度
	size_t flv_des_length; //加密之后的字符长度
	size_t flv_des_section_count; // 需要加密的一共有几段

	uint64_t on_meta_data_size;

	uint64_t duration_file_size; //丢弃的字节大小
	double duration_time;  //丢弃的时间
	uint64_t duration_video_size; //丢弃的video 大小
	uint64_t duration_audio_size; //丢弃的audio 大小

	uint64_t video_body_size; //FLV header + script tag + 丢弃的(音频和视频)tag
	int64_t start;   //请求flv 播放的起始字节数
	int16_t video_type;
    u_char *tdes_key; //des key
};

#endif /* __FLV_TAG_H__ */
