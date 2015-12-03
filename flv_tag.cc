/*
 * flv_tag.cc
 *
 *  Created on: 2015年11月17日
 *      Author: xie
 */

#ifndef __FLV_ENCRYPTION_CC__
#define __FLV_ENCRYPTION_CC__

#include "flv_tag.h"
#include "des.h"

static int64_t IOBufferReaderCopy(TSIOBufferReader readerp, void *buf,
		int64_t length);
static char *get_position_ptr(char *str_src, size_t str_len, const char *str_dest);
static size_t swap_duration(char *p_buf, double value);
static void revert_int(char *s, const char *d, int len);

static double int2double(uint64_t i)
{
    union av_intfloat64 v;
    v.i = i;
    return v.f;
}

static uint64_t double2int(double f)
{
	union av_intfloat64 v;
	v.f = f;
	return v.i;
}

int FlvTag::process_tag(TSIOBufferReader readerp, bool complete) {
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
		content_length = (cl - tag_pos) + head_avail;
		TSDebug(PLUGIN_NAME, " content_length = %d, discard_size=%d,",
				content_length, discard_size);
	}

	return rc;
}

int64_t FlvTag::write_out(TSIOBuffer buffer) {
	int64_t dup_avail, head_avail;

	head_avail = TSIOBufferReaderAvail(head_reader);

	if (head_avail > 0) {
		TSIOBufferCopy(buffer, head_reader, head_avail, 0);
		TSIOBufferReaderConsume(head_reader, head_avail);
	}

	return head_avail;
}

int FlvTag::process_header() //先解析pcf 的头 signature, version, videoid tag, userid tag, reserved tag
{

	int64_t avail;
	drm_head_common vheadcommon;
	size_t drm_header_length = sizeof(drm_head_common);

	avail = TSIOBufferReaderAvail(tag_reader);
	TSDebug(PLUGIN_NAME, " %s  avail = %d", __FUNCTION__, avail);
	if (avail < drm_header_length)
		return 0;

	TSDebug(PLUGIN_NAME, " %s  tag_pos = %d", __FUNCTION__, tag_pos);

	vheadcommon = (drm_head_common )TSmalloc(drm_header_length);

	IOBufferReaderCopy(tag_reader, &vheadcommon, drm_header_length);
	TSIOBufferCopy(head_buffer, tag_reader, drm_header_length, 0);
	TSIOBufferReaderConsume(tag_reader, drm_header_length);

	version = ntohl(vheadcommon.version);
	TSDebug(PLUGIN_NAME, " %s  version = %d", __FUNCTION__, version);
	if (version <= 0)
		return -1;

	videoid_size = ntohl(vheadcommon.videoid_size);
	TSDebug(PLUGIN_NAME, " %s  videoid_size = %d", __FUNCTION__, videoid_size);
	if (videoid_size <= 0)
		return -1;

	tag_pos += drm_header_length;

	TSDebug(PLUGIN_NAME, "process_header videoid_size=%d tag_pos=%d",
			videoid_size, tag_pos);

	this->current_handler = &FlvTag::process_header_videoid;
	return process_header_videoid();
}

int FlvTag::process_header_videoid() {
	int64_t avail;
	size_t videoid_size_length = sizeof(uint32_t);
	int64_t read_size = videoid_size + videoid_size_length;

	avail = TSIOBufferReaderAvail(tag_reader);
	if (avail < read_size)
		return 0;

	videoid = (u_char *) TSmalloc(sizeof(u_char) * (videoid_size));
	IOBufferReaderCopy(tag_reader, videoid, videoid_size);
	TSIOBufferCopy(head_buffer, tag_reader, videoid_size, 0);
	TSIOBufferReaderConsume(tag_reader, videoid_size);

	IOBufferReaderCopy(tag_reader, &userid_size, videoid_size_length);
	userid_size = ntohl(userid_size);
	TSIOBufferCopy(head_buffer, tag_reader, videoid_size_length, 0);
	TSIOBufferReaderConsume(tag_reader, videoid_size_length);

	if (userid_size <= 0)
		return -1;

	tag_pos += read_size;

	TSDebug(PLUGIN_NAME,
			"process_header videoid=%.*s, userid_size=%d, tag_pos=%d",
			videoid_size, videoid, userid_size, tag_pos);

	this->current_handler = &FlvTag::process_header_userid;
	return process_header_userid();
}

int FlvTag::process_header_userid() {
	int64_t avail;
	size_t userid_size_length = sizeof(uint32_t);
	int64_t read_size = userid_size + userid_size_length;

	avail = TSIOBufferReaderAvail(tag_reader);
	if (avail < read_size)
		return 0;

	userid = (u_char *) TSmalloc(sizeof(u_char) * (userid_size));
	IOBufferReaderCopy(tag_reader, userid, userid_size);
	TSIOBufferCopy(head_buffer, tag_reader, userid_size, 0);
	TSIOBufferReaderConsume(tag_reader, userid_size);

	IOBufferReaderCopy(tag_reader, &reserved_size, userid_size_length);
	reserved_size = ntohl(reserved_size);
	TSIOBufferCopy(head_buffer, tag_reader, userid_size_length, 0);
	TSIOBufferReaderConsume(tag_reader, userid_size_length);

	tag_pos += read_size;

	drm_head_length = tag_pos;

	TSDebug(PLUGIN_NAME,
			"process_header userid=%.*s, reserved_size=%d, tag_pos=%d",
			userid_size, userid, reserved_size, tag_pos);

	if (reserved_size <= 0) {
		this->current_handler = &FlvTag::process_decrypt_body;
		return process_decrypt_body();

	} else {
		this->current_handler = &FlvTag::process_header_reserved;
		return process_header_reserved();
	}
}

int FlvTag::process_header_reserved() {
	int64_t avail;
	u_char buf[reserved_size];

	avail = TSIOBufferReaderAvail(tag_reader);
	if (avail < reserved_size)
		return 0;

	reserved = (u_char *) TSmalloc(sizeof(u_char) * (reserved_size));
	IOBufferReaderCopy(tag_reader, reserved, reserved_size);
	TSIOBufferCopy(head_buffer, tag_reader, reserved_size, 0);
	TSIOBufferReaderConsume(tag_reader, reserved_size);

	tag_pos += reserved_size;

	drm_head_length = tag_pos;

	TSDebug(PLUGIN_NAME, "process_header reserved=%.*s", reserved_size,
			reserved);

	this->current_handler = &FlvTag::process_decrypt_body;
	return process_decrypt_body();
}

//解密
int FlvTag::process_decrypt_body() {
	int64_t avail;
//	u_char buf[need_read_length];
	u_char des_buf[FLV_1_DES_LENGTH];

	avail = TSIOBufferReaderAvail(tag_reader);
	TSDebug(PLUGIN_NAME, "process_decrypt_body avail %d", avail);
	if (avail < FLV_1_DES_LENGTH)
		return 0;
	int i = 0;
	IOBufferReaderCopy(tag_reader, des_buf, FLV_1_DES_LENGTH);
	for (i = 0; i < FLV_1_DES_LENGTH; i++) {
		TSDebug(PLUGIN_NAME, "des_buf %d", des_buf[i]);
	}


	des_decrypt(tdes_key, des_buf, FLV_1_DES_LENGTH);

	for (i = 0; i < 128; i++) {
		TSDebug(PLUGIN_NAME, "need decrypt des to de %d", des_buf[i]);
	}

	TSIOBufferReaderConsume(tag_reader, FLV_1_DES_LENGTH);

	avail = TSIOBufferReaderAvail(tag_reader);
	TSDebug(PLUGIN_NAME, "process_decrypt_body FLV_1_DES_LENGTH avail %d",
			avail);
	if (avail > 0) {
		u_char tag_reader_buf[avail];
		IOBufferReaderCopy(tag_reader, tag_reader_buf, avail);
		TSIOBufferReaderConsume(tag_reader, avail);
		TSIOBufferWrite(tag_buffer, des_buf, FLV_1_NEED_DES_LENGTH);
		TSIOBufferWrite(tag_buffer, tag_reader_buf, avail);
	} else {
		TSIOBufferWrite(tag_buffer, des_buf, FLV_1_NEED_DES_LENGTH);
//		TSIOBufferReaderCopy(tag_reader, des_buf, FLV_1_NEED_DES_LENGTH);
	}

	TSDebug(PLUGIN_NAME, "process_decrypt_body FLV_1_DES_LENGTH last avail %d",
			TSIOBufferReaderAvail(tag_reader));

	tag_pos += (FLV_1_DES_LENGTH - FLV_1_NEED_DES_LENGTH); //已经消费了多少

	this->current_handler = &FlvTag::process_initial_video_header;
	return process_initial_video_header();
}

int FlvTag::process_initial_video_header() {
	int64_t avail;
	char buf[13];
	//header长度4bytes 整个文件头的长度，一般是9（3+1+1+4），当然头部字段也有可能包含其它信息这个时间其长度就不是9了。
	//FLV Body
	//FLV body就是由很多tag组成的，一个tag包括下列信息：
	//      previoustagsize 4bytes 前一个tag的长度，第一个tag就是0

	avail = TSIOBufferReaderAvail(tag_reader);
	if (avail < 13)
		return 0;

	IOBufferReaderCopy(tag_reader, buf, 13);
	TSDebug(PLUGIN_NAME, "process_initial_video_header %.*s", 3, buf);
	if ((buf[0] != 'F' || buf[1] != 'L' || buf[2] != 'V')
			&& (buf[0] != 'M' || buf[1] != 'P' || buf[2] != '4'))
		return -1;

	TSDebug(PLUGIN_NAME, "process_initial_video_header %d",
			*(uint32_t*) (buf + 9));
	if (*(uint32_t*) (buf + 9) != 0)
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
int FlvTag::process_initial_body() {
	int64_t avail, sz;
	uint32_t n, ts;
	char buf[12];
//    size_t tag_length = sizeof(FLVTag_t);
//    FLVTag_t flvtag;
//    uint16_t h_need_des ;
//    bzero(&flvtag,sizeof(FLVTag_t));
	TSDebug(PLUGIN_NAME, "process_initial_body");
	avail = TSIOBufferReaderAvail(tag_reader);
	TSDebug(PLUGIN_NAME, "process_initial_body avail=%d", avail);

	do {
		if (avail < 11 + 1)
			return 0;
		IOBufferReaderCopy(tag_reader, buf, 12);
		n = (uint32_t) ((uint8_t) buf[1] << 16)
				+ (uint32_t) ((uint8_t) buf[2] << 8)
				+ (uint32_t) ((uint8_t) buf[3]);
		sz = 11 + n + 4; //tag->(tag header, tag body), tagsize

		if (avail < sz)     // insure the whole tag
			return 0;

		ts = (uint32_t) ((uint8_t) buf[4] << 16)
				+ (uint32_t) ((uint8_t) buf[5] << 8)
				+ (uint32_t) ((uint8_t) buf[6]);

		if (ts != 0)
			goto end;

		if (buf[0] == FLV_VIDEODATA && (((uint8_t) buf[11]) >> 4) == 1) {
			TSDebug(PLUGIN_NAME, "process_initial_body FLV_VIDEODATA 9");
//            if (!key_found) {
//                key_found = true;
//
//            } else {
			goto end;
//            }
		} else if(buf[0] == FLV_SCRIPTDATAOBJECT) {
			on_meta_data_size = n;
		}
		TSDebug(PLUGIN_NAME, "process_initial_body sz=%d", sz);

		TSIOBufferCopy(body_buffer, tag_reader, sz, 0);
		TSIOBufferReaderConsume(tag_reader, sz);

		avail -= sz;
		tag_pos += sz;

//        if (key_found)
//        		goto end;

	} while (avail > 0);
	return 0;

	end:

	TSDebug(PLUGIN_NAME, "end tag_pos %d", tag_pos);
	avail = TSIOBufferReaderAvail(body_reader);
	video_body_size += avail; //url?start=datasize (包括flv head + flv 脚本，即所有内容)

//	key_found = false;
	this->current_handler = &FlvTag::process_medial_body;
	return process_medial_body();
}

int FlvTag::process_medial_body() {
	int64_t avail, sz, pass, b_avail;
	uint32_t n, ts;
	char buf[12];
//    size_t tag_length = sizeof(FLVTag_t);
//    FLVTag_t flvtag;
//    bzero(&flvtag,sizeof(FLVTag_t));
//    uint16_t h_need_des ;
//    u_char need_des_body[FLV_1_DES_LENGTH];

	TSDebug(PLUGIN_NAME, "process_medial_body");
	avail = TSIOBufferReaderAvail(tag_reader);

	do {
		if (avail < 11 + 1)
			return 0;
		IOBufferReaderCopy(tag_reader, buf, 12);

		n = (uint32_t) ((uint8_t) buf[1] << 16)
				+ (uint32_t) ((uint8_t) buf[2] << 8)
				+ (uint32_t) ((uint8_t) buf[3]);

		sz = 11 + n + 4; //tag->(tag header, tag body), tagsize
		if (avail < sz)     // insure the whole tag
			return 0;
		video_body_size += sz;
		if (buf[0] == FLV_VIDEODATA && (((uint8_t) buf[11]) >> 4) == 1) { // key frame
			TSDebug(PLUGIN_NAME, "process_medial_body FLV_VIDEODATA 9");

			ts = (uint32_t) ((uint8_t) buf[4] << 16)
					+ (uint32_t) ((uint8_t) buf[5] << 8)
					+ (uint32_t) ((uint8_t) buf[6]);

			if (video_body_size <= start) {
				duration_time += ts; //毫秒
				TSDebug(PLUGIN_NAME, "process_medial_body duration_time＝%lf, ts= %d",duration_time,ts);
			} else {

				TSDebug(PLUGIN_NAME, "process_medial_body last !!!!!!!!");
				TSDebug(PLUGIN_NAME, " process_encrypt_body last avail = %d!",avail);
				TSIOBufferCopy(body_buffer, tag_reader, avail, 0);
				TSIOBufferReaderConsume(tag_reader, avail);
				tag_pos += avail;
				TSDebug(PLUGIN_NAME, "success !!!!!! tag_pos = %d", tag_pos);
				duration_time = duration_time/1000;
				this->current_handler = &FlvTag::process_check_des_body;
				return process_check_des_body();
			}
		}
//		TSDebug(PLUGIN_NAME, "process_medial_body flvtag.type = %d",flvtag.type);

		TSIOBufferReaderConsume(tag_reader, sz);
		discard_size += sz;
		avail -= sz;

		tag_pos += sz;

	} while (avail > 0);

	return 0;
}

int FlvTag::process_check_des_body() {
	int64_t avail, b_avail, need_read_buf;
	u_char need_des_body[FLV_1_DES_LENGTH];
	int i = 0;
	char *buf;

	b_avail = TSIOBufferReaderAvail(body_reader);
	avail = TSIOBufferReaderAvail(tag_reader);
	if ((avail + b_avail) < FLV_1_NEED_DES_LENGTH)
		return 0;

	need_read_buf = 9+4 + sizeof(FLVTag_t) +on_meta_data_size; // FLV head+ 4+ FLVTAG+onmetadata size
	TSDebug(PLUGIN_NAME, "process_check_des_body need_read_buf = %d", need_read_buf);
	if (need_read_buf < FLV_1_NEED_DES_LENGTH)
		need_read_buf = FLV_1_NEED_DES_LENGTH;
	buf = (char *)TSmalloc(sizeof(char) * need_read_buf);
	bzero(buf, sizeof(buf));

	if (avail) {
		TSIOBufferCopy(body_buffer, tag_reader, avail, 0);
		TSIOBufferReaderConsume(tag_reader, avail);
	}

	//先处理duration_time
//	b_avail = TSIOBufferReaderAvail(body_reader);

	IOBufferReaderCopy(body_reader, buf, need_read_buf);
	TSIOBufferReaderConsume(body_reader, need_read_buf);

	double temp =0;//总时间长度
	size_t sw_d;
	char rbuf[32] = {0};
	unsigned int keyframes_num = 0;

	char *keyframes = get_position_ptr(buf, need_read_buf, "keyframes");
	char *times = get_position_ptr(keyframes,need_read_buf,"times");
	char *p_duration = get_position_ptr(buf,need_read_buf,"duration");// 持续时间（秒）
	TSDebug(PLUGIN_NAME, "process_check_des_body time 1");
	if (times[5] != 10){
		TSDebug(PLUGIN_NAME, "process_check_des_body times goto cc_des");
		goto cc_des;
	}
	TSDebug(PLUGIN_NAME, "process_check_des_body time 2");
	revert_int(rbuf, &times[6],4);
	memcpy(&keyframes_num, &rbuf,4);
	TSDebug(PLUGIN_NAME, "process_check_des_body time 3");

	if (p_duration == NULL){
		TSDebug(PLUGIN_NAME, "process_check_des_body time 4");
		revert_int(rbuf, &times[10+(keyframes_num-1)*9+1],8);
	    memcpy(&temp, &rbuf, 8);
	}
	else{
		TSDebug(PLUGIN_NAME, "process_check_des_body time 5");
		revert_int(rbuf, &p_duration[9],8);
		memcpy(&temp, &rbuf, 8);
	}
	TSDebug(PLUGIN_NAME, "process_check_des_body time 6");
	if(temp <= 0) {
		TSDebug(PLUGIN_NAME, "process_check_des_body temp <= 0 goto cc_des");
		goto cc_des;
	}
	if(duration_time > temp)
		duration_time = temp;
	else {
		duration_time = temp - duration_time;
	}

	TSDebug(PLUGIN_NAME, "video all times＝%lf",temp);
	TSDebug(PLUGIN_NAME, "video duration_time = %lf", duration_time);
	if (p_duration != NULL){
	    p_duration = p_duration+9;
		sw_d = swap_duration(p_duration,duration_time);
		TSDebug(PLUGIN_NAME, "video swap_duration = %u", sw_d);
	}

	TSDebug(PLUGIN_NAME,"do again");
	temp =0;//总时间长度
	sw_d = 0;
	rbuf[32] = {0};
	keyframes_num = 0;
	keyframes = NULL;
	times = NULL;
	p_duration = NULL;
	keyframes = get_position_ptr(buf, need_read_buf, "keyframes");
	times = get_position_ptr(keyframes,need_read_buf,"times");
	p_duration = get_position_ptr(buf,need_read_buf,"duration");// 持续时间（秒）
	TSDebug(PLUGIN_NAME, "process_check_des_body time 1");
	if (times[5] != 10){
		TSDebug(PLUGIN_NAME, "process_check_des_body times goto cc_des");
		goto cc_des;
	}
	TSDebug(PLUGIN_NAME, "process_check_des_body time 2");
	revert_int(rbuf, &times[6],4);
	memcpy(&keyframes_num, &rbuf,4);
	TSDebug(PLUGIN_NAME, "process_check_des_body time 3");

	if (p_duration == NULL){
		TSDebug(PLUGIN_NAME, "process_check_des_body time 4");
		revert_int(rbuf, &times[10+(keyframes_num-1)*9+1],8);
	    memcpy(&temp, &rbuf, 8);
	}
	else{
		TSDebug(PLUGIN_NAME, "process_check_des_body time 5");
		revert_int(rbuf, &p_duration[9],8);
		memcpy(&temp, &rbuf, 8);
	}
	TSDebug(PLUGIN_NAME, "process_check_des_body time 6");
	if(temp <= 0) {
		TSDebug(PLUGIN_NAME, "process_check_des_body temp <= 0 goto cc_des");
		goto cc_des;
	}
	TSDebug(PLUGIN_NAME, "video all times＝%lf",temp);

	//处理好了，进行des 加密
	cc_des:
	TSDebug(PLUGIN_NAME, "process_check_des_body  tdes_key '%.*s'", 8,
			tdes_key);
	//从buf 中拷贝
	memcpy(need_des_body, buf, FLV_1_NEED_DES_LENGTH);
//	IOBufferReaderCopy(body_reader, need_des_body, FLV_1_NEED_DES_LENGTH);
	des_encrypt(tdes_key, need_des_body, FLV_1_NEED_DES_LENGTH);
	for (i = 0; i < 136; i++) {
		TSDebug(PLUGIN_NAME, "des to en %d", need_des_body[i]);
	}
	TSIOBufferWrite(head_buffer, need_des_body, FLV_1_DES_LENGTH);
	TSIOBufferWrite(head_buffer, buf+ FLV_1_NEED_DES_LENGTH, (need_read_buf - FLV_1_NEED_DES_LENGTH));

	TSfree((char *)buf);
	buf = NULL;
	avail = TSIOBufferReaderAvail(body_reader);
	if (avail > 0) {
		TSIOBufferCopy(head_buffer, body_reader, avail, 0);
		TSIOBufferReaderConsume(body_reader, avail);
	}
	return 1;
}

static int64_t IOBufferReaderCopy(TSIOBufferReader readerp, void *buf,
		int64_t length) {
	int64_t avail, need, n;
	const char *start;
	TSIOBufferBlock blk;

	n = 0;
	blk = TSIOBufferReaderStart(readerp);

	while (blk) {
		start = TSIOBufferBlockReadStart(blk, readerp, &avail);
		need = length < avail ? length : avail;

		if (need > 0) {
			memcpy((char *) buf + n, start, need);
			length -= need;
			n += need;
		}

		if (length == 0)
			break;

		blk = TSIOBufferBlockNext(blk);
	}

	return n;
}

static char *
get_position_ptr(char *str_src, size_t str_len, const char *str_dest) {
	if (str_src == NULL || str_dest == NULL) {
		return NULL;
	}

	char *buf;
	size_t i = 0, size = 0;
	int iLen = 0;
	char *p = NULL;
	//size_t filesize_tmp = str_len;
	int i_cpoy_len = 0;
	char *buf_tmp = buf;

	buf = (char *)TSmalloc(sizeof(char) * str_len);
	i = 0;
	size = 0;
	i_cpoy_len = 0;
	bzero(buf, sizeof(buf));
	memcpy(buf, str_src, str_len);
	buf_tmp = buf;

	//处理4K内容里有字符结束符
	while (1) {
		//strstr(str1,str2) 函数用于判断字符串str2是否是str1的子串。
		//如果是，则该函数返回str2在str1中首次出现的地址；否则，返回NULL
		p = strstr(buf_tmp, str_dest);
		if (p == NULL) {
			i = strlen(buf_tmp);
			buf_tmp = buf_tmp + i;
			size += (i + 1);
			iLen += (i + 1);

			//处理 buf =  字符串A + 0x0 + 字符串B
			if (size >= str_len) {
				goto free_buf;
			}

			//有符号 0x0
			buf_tmp = buf_tmp + 1;
			//size++;
			//iLen ++;
		} else {
			//找到偏移值
			printf("find it\n");
			iLen = iLen + (p - buf_tmp) * sizeof(char);

			p = str_src + iLen;
			TSfree((char *)buf);
			buf = NULL;
			return p;
		}

	}

free_buf:
	TSfree((char *)buf);
	buf = NULL;

	return NULL;
}

static void revert_int(char *s, const char *d, int len) {
	int i = 0;
	for (i = len - 1; i >= 0; i--) {
		*(s + i) = *d;
		d++;
	}
}

static size_t swap_duration(char *p_buf, double value) {
	union {
		unsigned char dc[8];
		double dd;
	} d;

	if (p_buf == NULL) {

		return -1;
	}
	unsigned char b[8];
	size_t datasize = 0;

	d.dd = value;

	b[0] = d.dc[7];
	b[1] = d.dc[6];
	b[2] = d.dc[5];
	b[3] = d.dc[4];
	b[4] = d.dc[3];
	b[5] = d.dc[2];
	b[6] = d.dc[1];
	b[7] = d.dc[0];

	//datasize += fwrite(b, 1, 8, fp);

	memcpy(p_buf, b, sizeof(char) * 8);
	datasize += 8;
	return datasize;
}

#endif /* __FLV_ENCRYPTION_CC__ */
