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
#include <arpa/inet.h>

#define PLUGIN_NAME "drm_video"

#define VIDEO_SIGNATURE_LENGTH 3
#define FLV_1_NEED_DES_LENGTH 128
#define FLV_1_DES_LENGTH 136

#define FLV_UI32(x) (int)(((x[0]) << 24) + ((x[1]) << 16) + ((x[2]) << 8) + (x[3])) //将字符串转为整数
#define FLV_UI24(x) (int)(((x[0]) << 16) + ((x[1]) << 8) + (x[2]))
#define FLV_UI16(x) (int)(((x[0]) << 8) + (x[1]))
#define FLV_UI8(x) (int)((x))

//TAG 类型 8:音频 9:视频 18:脚本 其他:保留
#define FLV_AUDIODATA   8
#define FLV_VIDEODATA   9
#define FLV_SCRIPTDATAOBJECT    18

typedef enum { VIDEO_VERSION_1 = 1, VIDEO_VERSION_3 = 3, VIDEO_VERSION_4  = 4 } video_version;
typedef enum { VIDEO_PCF , VIDEO_PCM  } VideoType;

typedef struct {
	u_char signature[VIDEO_SIGNATURE_LENGTH]; //标志信息
	uint32_t version; //版本
	uint32_t videoid_size; //videoid 长度
} drm_head_common;


typedef struct {
        unsigned char type; //1 bytes TAG 类型 8:音频 9:视频 18:脚本 其他:保留
        unsigned char datasize[3];// 3 bytes 数据长度   在数据区的长度
        unsigned char timestamp[3];// 3 bytes 时间戳  整数，单位是毫秒。对于脚本型的tag总是0
        unsigned char timestamp_ex;//时间戳扩展	1 bytes	将时间戳扩展为4bytes，代表高8位。很少用到
        unsigned char streamid[3];// StreamsID	3 bytes	总是0
} FLVTag_t;// TAG 头信息

union av_intfloat64 {
    uint64_t i;
    double f;
};

class FlvTag;
typedef int (FlvTag::*FTHandler) ();

class FlvTag
{
public:
	FlvTag() : tag_buffer(NULL), tag_reader(NULL), drm_head_length(0),video_body_size(0),version(0),videoid_size(0),
		head_buffer(NULL), head_reader(NULL), tag_pos(0), dup_pos(0), cl(0), tdes_key(NULL),discard_size(0),on_meta_data_size(0),
		content_length(0), start(0), key_found(false), video_type(0) ,video_head_length(0),duration_time(0),
		videoid(NULL), userid_size(0), userid(NULL), reserved_size(0), reserved(NULL),dup_reader(NULL),body_buffer(NULL),body_reader(NULL)
	{
		tag_buffer = TSIOBufferCreate();
		tag_reader = TSIOBufferReaderAlloc(tag_buffer);
		dup_reader = TSIOBufferReaderAlloc(tag_buffer);

		head_buffer = TSIOBufferCreate();
		head_reader = TSIOBufferReaderAlloc(head_buffer);

		body_buffer = TSIOBufferCreate();
		body_reader = TSIOBufferReaderAlloc(body_buffer);

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

        if (body_reader) {
            TSIOBufferReaderFree(body_reader);
            body_reader = NULL;
        }

        if (body_buffer) {
            TSIOBufferDestroy(body_buffer);
            body_buffer = NULL;
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


public:
	TSIOBuffer tag_buffer;
	TSIOBufferReader tag_reader;
	TSIOBufferReader    dup_reader;

	TSIOBuffer head_buffer;
	TSIOBufferReader head_reader;

	TSIOBuffer body_buffer;
	TSIOBufferReader body_reader;

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

	uint64_t discard_size;
	double duration_time;

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
