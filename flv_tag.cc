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
//		head_avail = TSIOBufferReaderAvail(head_reader);
		content_length = cl - start;
		TSDebug(PLUGIN_NAME," content_length = %d", content_length);
	}

	return rc;
}

int64_t
FlvTag::write_out(TSIOBuffer buffer)
{
	int64_t dup_avail, head_avail;

	head_avail = TSIOBufferReaderAvail(head_reader);
//	dup_avail = TSIOBufferReaderAvail(dup_reader);

	if (head_avail > 0){
		TSIOBufferCopy(buffer, head_reader, head_avail, 0);
		TSIOBufferReaderConsume(head_reader, head_avail);
	}

//    if (dup_avail > 0) {
//        TSIOBufferCopy(buffer, dup_reader, dup_avail, 0);
//        TSIOBufferReaderConsume(dup_reader, dup_avail);
//    }

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

	this->current_handler = &FlvTag::process_header_videoid;
	return process_header_videoid();
}

int
FlvTag::process_header_videoid()
{
	int64_t avail;
	int64_t read_size = videoid_size+4;

	avail = TSIOBufferReaderAvail(tag_reader);
	if (avail < read_size)
		return 0;

	videoid = (u_char *)TSmalloc(sizeof(u_char)*(videoid_size));
	IOBufferReaderCopy(tag_reader, videoid, videoid_size);
	TSIOBufferCopy(head_buffer, tag_reader, videoid_size, 0);
	TSIOBufferReaderConsume(tag_reader, videoid_size);


	IOBufferReaderCopy(tag_reader, &userid_size,4);
	userid_size = ntohl(userid_size);
	TSIOBufferCopy(head_buffer, tag_reader, 4, 0);
	TSIOBufferReaderConsume(tag_reader, 4);

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
	int64_t read_size = userid_size+4;

	avail = TSIOBufferReaderAvail(tag_reader);
	if (avail < read_size)
		return 0;

	userid = (u_char *)TSmalloc(sizeof(u_char)*(userid_size));
	IOBufferReaderCopy(tag_reader, userid, userid_size);
	TSIOBufferCopy(head_buffer, tag_reader, userid_size, 0);
	TSIOBufferReaderConsume(tag_reader, userid_size);


	IOBufferReaderCopy(tag_reader, &reserved_size,4);
	reserved_size = ntohl(reserved_size);
	TSIOBufferCopy(head_buffer, tag_reader, 4, 0);
	TSIOBufferReaderConsume(tag_reader, 4);

	tag_pos += read_size;

	head_length = tag_pos;

	TSDebug(PLUGIN_NAME,"process_header userid=%.*s, reserved_size=%d, tag_pos=%d",userid_size,userid,reserved_size,tag_pos);

    if (reserved_size <= 0) {
    		if((head_length+videoid_size) >= cl)
    			return -1;
		this->current_handler = &FlvTag::process_initial_body;
		return process_initial_body();

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

	head_length = tag_pos;

	if((head_length+videoid_size) >= cl)
		return -1;

	TSDebug(PLUGIN_NAME,"process_header reserved=%.*s", reserved_size,reserved);

	this->current_handler = &FlvTag::process_initial_body;
	return process_initial_body();
}

//将没用的body 数据丢弃
int
FlvTag::process_initial_body()
{
	int32_t des_length = 136;
	int32_t decrypt_length = 128;

	if(version == 1) {
		des_length = 136;//对 flv 文件前 128 字节用 DES 加密,加密后长度为 136 字节,然后加上 flv 文件剩余部分。
		decrypt_length = 128;
		TSDebug(PLUGIN_NAME,"%s, version=%d", __FUNCTION__,version);
	} else {
		return -1;
	}
	//计算视频文件总长度
	if (start > (cl - (head_length + videoid_size + (des_length - decrypt_length))))
		return -1;
	TSDebug(PLUGIN_NAME,"start=%d, decrypt_length=%d",start, decrypt_length);
	if (start > decrypt_length) { //不需要解密
		TSDebug(PLUGIN_NAME,"not need decrypt");
		this->current_handler = &FlvTag::process_encrypt_body;
		return process_encrypt_body();

	} else {//需要解密
		TSDebug(PLUGIN_NAME,"need decrypt");
		this->current_handler = &FlvTag::process_decrypt_body;
		return process_decrypt_body();
	}
}

//解密
int
FlvTag::process_decrypt_body()
{
	int64_t avail;
	int32_t des_length = 136;
	int32_t decrypt_length = 128;
	int32_t need_read_length = des_length+start;
	u_char buf[need_read_length];
	u_char des_buf[des_length];

	avail = TSIOBufferReaderAvail(tag_reader);
	if (avail < need_read_length)
		return 0;
	int i = 0;
	IOBufferReaderCopy(tag_reader, buf, need_read_length);
	for(i = 0; i < need_read_length; i++) {
		TSDebug(PLUGIN_NAME, "buf %d", buf[i]);
	}
//	strncpy((char *)des_buf, (char *)buf , des_length);
	memcpy(des_buf, buf, des_length);
	for(i = 0; i < 136; i++) {
		TSDebug(PLUGIN_NAME, "need decrypt des to en %d", des_buf[i]);
	}

	des_decrypt(tdes_key, des_buf, des_length);

	for(i = 0; i < 128; i++) {
		TSDebug(PLUGIN_NAME, "need decrypt des to de %d", des_buf[i]);
	}


	need_des_body = (u_char *)TSmalloc(sizeof(u_char)*(decrypt_length));

	int32_t need_des_length = decrypt_length - start;
	memcpy(need_des_body, des_buf+start, need_des_length);
	memcpy(need_des_body+need_des_length, buf + des_length, start);
//	strncpy((char *)need_des_body, (char *)(des_buf+start) , need_des_length);
//	strncpy((char *)(need_des_body +need_des_length), (char *)(buf + des_length), decrypt_length - need_des_length);

	dup_pos = start; //视频文件已经丢弃了多少数据

	for(i = 0; i < 128; i++) {
		TSDebug(PLUGIN_NAME, "need decrypt need_des_body %d", need_des_body[i]);
	}
//	TSIOBufferCopy(head_buffer, tag_reader, read_size, 0);
	TSIOBufferReaderConsume(tag_reader, need_read_length);

	this->current_handler = &FlvTag::process_encrypt_body;
	return process_encrypt_body();
}

//加密
int
FlvTag::process_encrypt_body()
{
	int64_t avail, discard_length;
	int64_t start_des_length;
	//是否需要丢弃不需要的数据
	int32_t des_length = 136;
	int32_t decrypt_length = 128;
	avail = TSIOBufferReaderAvail(tag_reader);
	if(NULL == need_des_body) {
		start_des_length = start +(des_length - decrypt_length);
		TSDebug(PLUGIN_NAME,"avail=%d, dup_pos=%d", avail,dup_pos);
		if( start_des_length > (dup_pos + avail)){
			dup_pos +=avail;
			TSDebug(PLUGIN_NAME,"TSIOBufferReaderConsume dup_pos=%d", dup_pos);
			TSIOBufferReaderConsume(tag_reader, avail);
			return 0;
		}
		if(dup_pos < start_des_length) {
			discard_length = start_des_length - dup_pos;
			TSIOBufferReaderConsume(tag_reader, discard_length);
			TSDebug(PLUGIN_NAME,"TSIOBufferReaderConsume discard_length=%d", discard_length);
			dup_pos += discard_length;
		}

		avail = TSIOBufferReaderAvail(tag_reader);
		if(avail < decrypt_length)
			return 0;

		TSDebug(PLUGIN_NAME,"%s, dup_pos=%d", __FUNCTION__, dup_pos);
		need_des_body = (u_char *)TSmalloc(sizeof(u_char)*(des_length));
		IOBufferReaderCopy(tag_reader, need_des_body, decrypt_length);

		TSIOBufferReaderConsume(tag_reader, decrypt_length);
		dup_pos +=decrypt_length;
	}
	int i = 0;
	for(i = 0; i < 128; i++) {
		TSDebug(PLUGIN_NAME, "no need decrypt des to de %d", need_des_body[i]);
	}
	TSDebug(PLUGIN_NAME," tdes_key '%.*s'", 8, tdes_key);
	des_encrypt(tdes_key,need_des_body,decrypt_length);
	for(i = 0; i < 136; i++) {
		TSDebug(PLUGIN_NAME, "no need decrypt des to en %d", need_des_body[i]);
	}
	TSIOBufferWrite(head_buffer,need_des_body,des_length);

	//要把tag_reader消费干净
	avail = TSIOBufferReaderAvail(tag_reader);
	TSDebug(PLUGIN_NAME," process_encrypt_body last avail = %d!",avail);
	if(avail > 0) {
		TSIOBufferCopy(head_buffer, tag_reader, avail, 0);
		TSIOBufferReaderConsume(tag_reader, avail);
	}

	TSDebug(PLUGIN_NAME," success!!!!!!!!!!!!!!!!!!");
	return 1;
//	this->current_handler = &FlvTag::process_medial_body;
//	return process_medial_body;
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
