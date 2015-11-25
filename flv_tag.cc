/*
 * flv_tag.cc
 *
 *  Created on: 2015年11月17日
 *      Author: xie
 */

#ifndef _DRM_VIDEO_FLV_ENCRYPTION_CC_
#define _DRM_VIDEO_FLV_ENCRYPTION_CC_

#include "flv_tag.h"
#include "des.h"

static int64_t IOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length);

int FlvTag::process_tag(TSIOBufferReader readerp, bool complete)
{
	int64_t avail, head_avail;
	int rc;

	avail = TSIOBufferReaderAvail(readerp);
	TSIOBufferCopy(tag_buffer, readerp, avail, 0);

	TSIOBufferReaderConsume(readerp, avail);

	rc = (this->*current_handler)();

	if (rc == 0 && complete) {
		rc = -1;
	}

	if (rc) {
		head_avail = TSIOBufferReaderAvail(head_reader);
		content_length = (cl - tag_pos)+ head_avail;
		TSDebug(PLUGIN_NAME," content_length = %d, discard_size=%d,", content_length,discard_size);
	}

	return rc;
}

int64_t
FlvTag::write_out(TSIOBuffer buffer)
{
	int64_t dup_avail, head_avail;

	head_avail = TSIOBufferReaderAvail(head_reader);

	if (head_avail > 0){
		TSIOBufferCopy(buffer, head_reader, head_avail, 0);
		TSIOBufferReaderConsume(head_reader, head_avail);
	}

    return head_avail;
}

int
FlvTag::process_header() //先解析pcf 的头 signature, version, videoid tag, userid tag, reserved tag
{

	int64_t avail;
	u_char buf[3];

	avail = TSIOBufferReaderAvail(tag_reader);
	TSDebug(PLUGIN_NAME," %s  avail = %d", __FUNCTION__, avail);
	if (avail < 11)
		return 0;

	TSDebug(PLUGIN_NAME," %s  tag_pos = %d", __FUNCTION__, tag_pos);

	IOBufferReaderCopy(tag_reader, buf,3);
	TSIOBufferCopy(head_buffer, tag_reader, 3, 0);
	TSIOBufferReaderConsume(tag_reader, 3);
	TSDebug(PLUGIN_NAME," %s  buf = %.*s", __FUNCTION__, 3 ,buf);

	IOBufferReaderCopy(tag_reader, &version,4);
	version = ntohl(version);
	TSIOBufferCopy(head_buffer, tag_reader, 4, 0);
	TSIOBufferReaderConsume(tag_reader, 4);
	TSDebug(PLUGIN_NAME," %s  version = %d", __FUNCTION__, version);
    if (version <= 0)
        return -1;

	IOBufferReaderCopy(tag_reader, &videoid_size,4);
	videoid_size = ntohl(videoid_size);
	TSIOBufferCopy(head_buffer, tag_reader, 4, 0);
	TSIOBufferReaderConsume(tag_reader, 4);
	TSDebug(PLUGIN_NAME," %s  videoid_size = %d", __FUNCTION__, videoid_size);
    if (videoid_size <= 0)
        return -1;

	tag_pos += 11;

	TSDebug(PLUGIN_NAME,"process_header videoid_size=%d tag_pos=%d", videoid_size, tag_pos);

//	int64_t avail;
//	size_t vhead_length;
//	vhead_length = sizeof(video_head_common);
//
//	avail = TSIOBufferReaderAvail(tag_reader);
//	TSDebug(PLUGIN_NAME," %s  avail = %d", __FUNCTION__, avail);
//	if (avail < vhead_length)
//		return 0;
//
//	TSDebug(PLUGIN_NAME," %s  tag_pos = %d", __FUNCTION__, tag_pos);
//
//	IOBufferReaderCopy(tag_reader, &vheadcommon, vhead_length);
//	TSIOBufferCopy(head_buffer, tag_reader, vhead_length, 0);
//	TSIOBufferReaderConsume(tag_reader, vhead_length);
//	vheadcommon.version = ntohl(vheadcommon.version);
//	vheadcommon.videoid_size = ntohl(vheadcommon.videoid_size);
//
//	tag_pos += vhead_length;
//
//	TSDebug(PLUGIN_NAME,"process_header version = %d videoid_size=%d tag_pos=%d",vheadcommon.version, vheadcommon.videoid_size , tag_pos);

	this->current_handler = &FlvTag::process_header_videoid;
	return process_header_videoid();
}

int
FlvTag::process_header_videoid()
{
	int64_t avail;
	size_t videoid_size_length = sizeof(uint32_t);
	int64_t read_size = videoid_size+ videoid_size_length;

	avail = TSIOBufferReaderAvail(tag_reader);
	if (avail < read_size)
		return 0;

	videoid = (u_char *)TSmalloc(sizeof(u_char)*(videoid_size));
	IOBufferReaderCopy(tag_reader, videoid, videoid_size);
	TSIOBufferCopy(head_buffer, tag_reader, videoid_size, 0);
	TSIOBufferReaderConsume(tag_reader, videoid_size);


	IOBufferReaderCopy(tag_reader, &userid_size,videoid_size_length);
	userid_size = ntohl(userid_size);
	TSIOBufferCopy(head_buffer, tag_reader, videoid_size_length, 0);
	TSIOBufferReaderConsume(tag_reader, videoid_size_length);

    if (userid_size <= 0)
        return -1;

	tag_pos += read_size;

	TSDebug(PLUGIN_NAME,"process_header videoid=%.*s, userid_size=%d, tag_pos=%d",videoid_size, videoid,userid_size,tag_pos);

	this->current_handler = &FlvTag::process_header_userid;
	return process_header_userid();
}

int
FlvTag::process_header_userid()
{
	int64_t avail;
	size_t userid_size_length = sizeof(uint32_t);
	int64_t read_size = userid_size+userid_size_length;

	avail = TSIOBufferReaderAvail(tag_reader);
	if (avail < read_size)
		return 0;

	userid = (u_char *)TSmalloc(sizeof(u_char)*(userid_size));
	IOBufferReaderCopy(tag_reader, userid, userid_size);
	TSIOBufferCopy(head_buffer, tag_reader, userid_size, 0);
	TSIOBufferReaderConsume(tag_reader, userid_size);


	IOBufferReaderCopy(tag_reader, &reserved_size,userid_size_length);
	reserved_size = ntohl(reserved_size);
	TSIOBufferCopy(head_buffer, tag_reader, userid_size_length, 0);
	TSIOBufferReaderConsume(tag_reader, userid_size_length);

	tag_pos += read_size;

	drm_head_length = tag_pos;

	TSDebug(PLUGIN_NAME,"process_header userid=%.*s, reserved_size=%d, tag_pos=%d",userid_size,userid,reserved_size,tag_pos);

    if (reserved_size <= 0) {
		this->current_handler = &FlvTag::process_decrypt_body;
		return process_decrypt_body();

    } else {
		this->current_handler = &FlvTag::process_header_reserved;
		return process_header_reserved();
    }
}

int
FlvTag::process_header_reserved()
{
	int64_t avail;
	u_char buf[reserved_size];

	avail = TSIOBufferReaderAvail(tag_reader);
	if (avail < reserved_size)
		return 0;

	reserved = (u_char *)TSmalloc(sizeof(u_char)*(reserved_size));
	IOBufferReaderCopy(tag_reader, reserved, reserved_size);
	TSIOBufferCopy(head_buffer, tag_reader, reserved_size, 0);
	TSIOBufferReaderConsume(tag_reader, reserved_size);

	tag_pos += reserved_size;

	drm_head_length = tag_pos;

	TSDebug(PLUGIN_NAME,"process_header reserved=%.*s", reserved_size,reserved);

	this->current_handler = &FlvTag::process_decrypt_body;
	return process_decrypt_body();
}

//解密
int
FlvTag::process_decrypt_body()
{
	int64_t avail;
//	u_char buf[need_read_length];
	u_char des_buf[FLV_1_DES_LENGTH];

	avail = TSIOBufferReaderAvail(tag_reader);
	TSDebug(PLUGIN_NAME, "process_decrypt_body avail %d", avail);
	if (avail < FLV_1_DES_LENGTH)
		return 0;
	int i = 0;
	IOBufferReaderCopy(tag_reader, des_buf, FLV_1_DES_LENGTH);
	for(i = 0; i < FLV_1_DES_LENGTH; i++) {
		TSDebug(PLUGIN_NAME, "des_buf %d", des_buf[i]);
	}
//	strncpy((char *)des_buf, (char *)buf , des_length);
//	memcpy(des_buf, buf, des_length);
//	for(i = 0; i < 136; i++) {
//		TSDebug(PLUGIN_NAME, "need decrypt des to en %d", des_buf[i]);
//	}

	des_decrypt(tdes_key, des_buf, FLV_1_DES_LENGTH);

	for(i = 0; i < 128; i++) {
		TSDebug(PLUGIN_NAME, "need decrypt des to de %d", des_buf[i]);
	}

//	need_des_body = (u_char *)TSmalloc(sizeof(u_char)*(FLV_1_NEED_DES_LENGTH));
//
//	memcpy(need_des_body, &des_buf, FLV_1_NEED_DES_LENGTH);

//	dup_pos = start; //视频文件已经丢弃了多少数据

//	for(i = 0; i < 128; i++) {
//		TSDebug(PLUGIN_NAME, "need decrypt need_des_body %d", need_des_body[i]);
//	}
//	TSIOBufferCopy(head_buffer, tag_reader, read_size, 0);
	TSIOBufferReaderConsume(tag_reader, FLV_1_DES_LENGTH);



	avail = TSIOBufferReaderAvail(tag_reader);
	TSDebug(PLUGIN_NAME, "process_decrypt_body FLV_1_DES_LENGTH avail %d", avail);
	if (avail > 0) {
		u_char tag_reader_buf[avail];
		IOBufferReaderCopy(tag_reader,tag_reader_buf, avail);
		TSIOBufferReaderConsume(tag_reader, avail);
		TSIOBufferWrite(tag_buffer, des_buf, FLV_1_NEED_DES_LENGTH);
		TSIOBufferWrite(tag_buffer, tag_reader_buf, avail);
	} else {
		TSIOBufferWrite(tag_buffer, des_buf, FLV_1_NEED_DES_LENGTH);
//		TSIOBufferReaderCopy(tag_reader, des_buf, FLV_1_NEED_DES_LENGTH);
	}

	TSDebug(PLUGIN_NAME, "process_decrypt_body FLV_1_DES_LENGTH last avail %d", TSIOBufferReaderAvail(tag_reader));

	tag_pos += (FLV_1_DES_LENGTH - FLV_1_NEED_DES_LENGTH); //已经消费了多少

	this->current_handler = &FlvTag::process_initial_video_header;
	return process_initial_video_header();
}

int
FlvTag::process_initial_video_header()
{
    int64_t     avail;
    char        buf[13];
    //header长度4bytes 整个文件头的长度，一般是9（3+1+1+4），当然头部字段也有可能包含其它信息这个时间其长度就不是9了。
    //FLV Body
    //FLV body就是由很多tag组成的，一个tag包括下列信息：
    //      previoustagsize 4bytes 前一个tag的长度，第一个tag就是0

    avail = TSIOBufferReaderAvail(tag_reader);
    if (avail < 13)
        return 0;


    IOBufferReaderCopy(tag_reader, buf, 13);
    TSDebug(PLUGIN_NAME, "process_initial_video_header %.*s", 3, buf);
    if ((buf[0] != 'F'  ||  buf[1] != 'L' ||  buf[2] != 'V') && ( buf[0] != 'M' || buf[1] != 'P' ||  buf[2] != '4'))
        return -1;


    TSDebug(PLUGIN_NAME, "process_initial_video_header %d", *(uint32_t*)(buf + 9));
    if (*(uint32_t*)(buf + 9) != 0)
        return -1;



    TSIOBufferCopy(body_buffer, tag_reader, 13, 0);
    TSIOBufferReaderConsume(tag_reader, 13);

    tag_pos += 13;

    TSDebug(PLUGIN_NAME, "process_initial_video_header  tag_pos = %d", tag_pos);

    this->current_handler = &FlvTag::process_initial_body;
    return process_initial_body();
}

//将没用的body 数据丢弃
//解析metadataTag
int
FlvTag::process_initial_body()
{
    int64_t     avail, sz;
    uint32_t    n;
    size_t tag_length = sizeof(FLVTag_t);
    FLVTag_t flvtag;
//    uint16_t h_need_des ;

    TSDebug(PLUGIN_NAME, "process_initial_body");
    avail = TSIOBufferReaderAvail(tag_reader);
    TSDebug(PLUGIN_NAME, "process_initial_body avail=%d",avail);

    do {
		if(avail < tag_length)
				return 0;
		IOBufferReaderCopy(tag_reader, &flvtag, tag_length);
		n = FLV_UI24(flvtag.datasize);
        sz = tag_length + n + 4; //tag->(tag header, tag body), tagsize

        if (avail < sz)     // insure the whole tag
            return 0;

		if(flvtag.type == FLV_SCRIPTDATAOBJECT) {
			TSDebug(PLUGIN_NAME, "process_initial_body FLV_SCRIPTDATAOBJECT 18");
		    key_found = true;
		}
		TSDebug(PLUGIN_NAME, "process_initial_body sz=%d",sz);
//		h_need_des = FLV_1_NEED_DES_LENGTH - need_des_index;
//		if(sz >= h_need_des) {
//			IOBufferReaderCopy(tag_reader, need_des_body+need_des_index, h_need_des);
//			need_des_index = FLV_1_NEED_DES_LENGTH;
//			encrypt_body();
//			TSIOBufferReaderConsume(tag_reader, h_need_des);
//	        TSIOBufferCopy(head_buffer, tag_reader, (sz - h_need_des), 0);
//	        TSIOBufferReaderConsume(tag_reader, (sz - h_need_des));
//		} else {
//			IOBufferReaderCopy(tag_reader, need_des_body+need_des_index, sz);
//			need_des_index += sz;
////			encrypt_body();
//			TSIOBufferReaderConsume(tag_reader, sz);
////	        TSIOBufferCopy(head_buffer, tag_reader, (sz - h_need_des), 0);
////	        TSIOBufferReaderConsume(tag_reader, (sz - h_need_des));
//		}

		TSIOBufferCopy(body_buffer, tag_reader, sz, 0);
		TSIOBufferReaderConsume(tag_reader, sz);


        avail -= sz;
        tag_pos += sz;

        if (key_found)
        		goto end;

    } while (avail > 0);
    return 0;

end:

	TSDebug(PLUGIN_NAME, "end tag_pos %d", tag_pos);
	avail = TSIOBufferReaderAvail(body_reader);
	video_body_size += avail; //url?start=datasize (包括flv head + flv 脚本，即所有内容)
    this->current_handler = &FlvTag::process_medial_body;
    return process_medial_body();
}

int
FlvTag::process_medial_body()
{
    int64_t     avail, sz, pass,b_avail;
    uint32_t    n, ts;
    char        buf[12];
    size_t tag_length = sizeof(FLVTag_t);
    FLVTag_t flvtag;
//    uint16_t h_need_des ;
//    u_char need_des_body[FLV_1_DES_LENGTH];

    TSDebug(PLUGIN_NAME, "process_medial_body");
    avail = TSIOBufferReaderAvail(tag_reader);

    do {
		if(avail < tag_length)
				return 0;
		IOBufferReaderCopy(tag_reader, &flvtag, tag_length);
		n = FLV_UI24(flvtag.datasize);
        sz = tag_length + n + 4; //tag->(tag header, tag body), tagsize
        if (avail < sz)     // insure the whole tag
            return 0;
        video_body_size += sz;
		if(flvtag.type == FLV_VIDEODATA) {
			TSDebug(PLUGIN_NAME, "process_medial_body FLV_VIDEODATA 9");
			if (video_body_size <= start) {

			} else {
//				h_need_des = FLV_1_NEED_DES_LENGTH - need_des_index;
//				if (h_need_des) {
//					if(sz >= h_need_des) {
//						IOBufferReaderCopy(tag_reader, need_des_body+need_des_index, h_need_des);
//						need_des_index = FLV_1_NEED_DES_LENGTH;
//						encrypt_body();
//						TSIOBufferReaderConsume(tag_reader, h_need_des);
//						TSIOBufferCopy(head_buffer, tag_reader, (sz - h_need_des), 0);
//						TSIOBufferReaderConsume(tag_reader, (sz - h_need_des));
//					} else {
//						IOBufferReaderCopy(tag_reader, need_des_body+need_des_index, sz);
//						need_des_index += sz;
//			//			encrypt_body();
//						TSIOBufferReaderConsume(tag_reader, sz);
//					}
//				}

            		TSDebug(PLUGIN_NAME, "process_medial_body last !!!!!!!!");
//            		IOBufferReaderCopy(tag_reader, need_des_body, FLV_1_NEED_DES_LENGTH);
//            		des_encrypt(tdes_key, need_des_body, FLV_1_NEED_DES_LENGTH);
//            		TSIOBufferWrite(head_buffer,need_des_body,FLV_1_DES_LENGTH);

//            		TSIOBufferReaderConsume(tag_reader, FLV_1_NEED_DES_LENGTH);
//            		tag_pos += FLV_1_NEED_DES_LENGTH;
            		//要把tag_reader消费干净
//            		avail = TSIOBufferReaderAvail(tag_reader);
            		TSDebug(PLUGIN_NAME," process_encrypt_body last avail = %d!",avail);
//            		if(avail > 0) {
            		TSIOBufferCopy(body_buffer, tag_reader, avail, 0);
            		TSIOBufferReaderConsume(tag_reader, avail);
//            		}
            		tag_pos +=avail;
            		TSDebug(PLUGIN_NAME, "success !!!!!! tag_pos = %d",tag_pos);

            	    this->current_handler = &FlvTag::process_check_des_body;
            	    return process_check_des_body();
//
//            		return 1;
			}
		}
		TSDebug(PLUGIN_NAME, "process_medial_body flvtag.type = %d",flvtag.type);

        TSIOBufferReaderConsume(tag_reader, sz);
        discard_size +=sz;
        avail -= sz;

        tag_pos += sz;

    } while (avail > 0);

    return 0;
}

int FlvTag::process_check_des_body(){
	int64_t     avail, b_avail;
	u_char need_des_body[FLV_1_DES_LENGTH];
	int i = 0;
	b_avail = TSIOBufferReaderAvail(body_reader);
	avail = TSIOBufferReaderAvail(tag_reader);
	if ((avail +b_avail) < FLV_1_NEED_DES_LENGTH)
		return 0;

	if(avail) {
		TSIOBufferCopy(body_buffer,tag_reader,avail,0);
		TSIOBufferReaderConsume(tag_reader, avail);
	}

	TSDebug(PLUGIN_NAME,"process_check_des_body  tdes_key '%.*s'", 8, tdes_key);
	IOBufferReaderCopy(body_reader, need_des_body, FLV_1_NEED_DES_LENGTH);
	des_encrypt(tdes_key,need_des_body,FLV_1_NEED_DES_LENGTH);
	for(i = 0; i < 136; i++) {
		TSDebug(PLUGIN_NAME, "des to en %d", need_des_body[i]);
	}
	TSIOBufferWrite(head_buffer,need_des_body,FLV_1_DES_LENGTH);
	TSIOBufferReaderConsume(body_reader, FLV_1_NEED_DES_LENGTH);

	avail = TSIOBufferReaderAvail(body_reader);
	if (avail > 0) {
		TSIOBufferCopy(head_buffer, body_reader, avail,0);
		TSIOBufferReaderConsume(body_reader, avail);
	}
	return 1;
}


static int64_t
IOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length)
{
	int64_t avail, need, n;
	const char *start;
	TSIOBufferBlock blk;

	n = 0;
	blk = TSIOBufferReaderStart(readerp);

	while (blk) {
		start = TSIOBufferBlockReadStart(blk, readerp, &avail);
		need = length < avail ? length : avail;

		if (need > 0) {
			memcpy((char *)buf +n, start, need);
			length -= need;
			n += need;
		}

		if (length == 0)
			break;

		blk = TSIOBufferBlockNext(blk);
	}

	return n;
}



#endif /* _DRM_VIDEO_FLV_ENCRYPTION_CC_ */
