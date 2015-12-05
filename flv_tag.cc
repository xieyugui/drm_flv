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

static int64_t IOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length);
static const char * get_amf_type_string(byte type);

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
				content_length, duration_file_size);
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

//	vheadcommon = (drm_head_common )TSmalloc(sizeof(drm_head_common));

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
	flv_header header;
	size_t need_length  = sizeof(flv_header) +sizeof(uint32_t);  // flv_header + first previoustagsize 第一个默认为0
	//header长度4bytes 整个文件头的长度，一般是9（3+1+1+4），当然头部字段也有可能包含其它信息这个时间其长度就不是9了。
	//FLV Body
	//FLV body就是由很多tag组成的，一个tag包括下列信息：
	//      previoustagsize 4bytes 前一个tag的长度，第一个tag就是0

	avail = TSIOBufferReaderAvail(tag_reader);
	if (avail < need_length)
		return 0;

	IOBufferReaderCopy(tag_reader, &header, sizeof(flv_header));
	TSDebug(PLUGIN_NAME, "process_initial_video_header %.*s", 3, header.signature);
	if (header.signature[0] != 'F' || header.signature[1] != 'L' || header.signature[2] != 'V')
		return -1;

	TSDebug(PLUGIN_NAME, "process_initial_video_header header.offset %d",
			header.offset);
//	if (*(uint32_t*) (buf + 9) != 0)
//		return -1;

	TSIOBufferCopy(flv_buffer, tag_reader, need_length, 0);
	TSIOBufferReaderConsume(tag_reader, need_length);

	tag_pos += need_length;

	TSDebug(PLUGIN_NAME, "process_initial_video_header  tag_pos = %d", tag_pos);

	this->current_handler = &FlvTag::process_initial_body;
	return process_initial_body();
}

//将没用的body 数据丢弃
//解析metadataTag
int FlvTag::process_initial_body() {
	int64_t avail, sz;
	uint32 body_length, timestamp;
	size_t flv_tag_length = sizeof(flv_tag);
	flv_tag tag;
	TSDebug(PLUGIN_NAME, "process_initial_body");
	avail = TSIOBufferReaderAvail(tag_reader);
	TSDebug(PLUGIN_NAME, "process_initial_body avail=%d", avail);

	do {
		if (avail < flv_tag_length)
			return 0;
		IOBufferReaderCopy(tag_reader, &tag, flv_tag_length);

		body_length = flv_tag_get_body_length(tag);
		sz = flv_tag_length + body_length + sizeof(uint32_be); //tag->(tag header, tag body), tagsize

		if (avail < sz)     // insure the whole tag
			return 0;

//		timestamp = flv_tag_get_timestamp(tag);
//
//		if (timestamp != 0)
//			goto end;

		if (tag.type == FLV_TAG_TYPE_VIDEO ) {
			TSDebug(PLUGIN_NAME, "process_initial_body FLV_VIDEODATA 9");
			goto end;
		} else if(tag.type  == FLV_TAG_TYPE_META) {
			on_meta_data_size = body_length;
		}
		TSDebug(PLUGIN_NAME, "process_initial_body sz=%d", sz);

		TSIOBufferCopy(flv_buffer, tag_reader, sz, 0);
		TSIOBufferReaderConsume(tag_reader, sz);

		avail -= sz;
		tag_pos += sz;

	} while (avail > 0);
	return 0;

end:

	TSDebug(PLUGIN_NAME, "end tag_pos %d", tag_pos);
	avail = TSIOBufferReaderAvail(flv_reader);
	video_body_size += avail; //url?start=datasize (包括flv head + flv 脚本，即所有内容)

	this->current_handler = &FlvTag::process_medial_body;
	return process_medial_body();
}

int FlvTag::process_medial_body() {
	int64_t avail, sz, pass, b_avail;
	uint32 body_length, timestamp;
	size_t flv_tag_length = sizeof(flv_tag);
	flv_tag tag;

	TSDebug(PLUGIN_NAME, "process_medial_body");
	avail = TSIOBufferReaderAvail(tag_reader);

	do {
		if (avail < flv_tag_length)
			return 0;
		IOBufferReaderCopy(tag_reader, &tag, flv_tag_length);

		body_length = flv_tag_get_body_length(tag);
		sz = flv_tag_length + body_length + sizeof(uint32_be); //tag->(tag header, tag body), tagsize

		if (avail < sz)     // insure the whole tag
			return 0;
		video_body_size += sz;
		if (tag.type == FLV_TAG_TYPE_VIDEO ) {
			TSDebug(PLUGIN_NAME, "process_medial_body FLV_VIDEODATA 9");

			timestamp = flv_tag_get_timestamp(tag);

			if (video_body_size <= start) {
				duration_time += timestamp; //毫秒
				duration_video_size += flv_tag_length + body_length;  //丢弃的video
				TSDebug(PLUGIN_NAME, "process_medial_body duration_time＝%lf, ts= %d",duration_time,timestamp);

			} else {
				TSDebug(PLUGIN_NAME, "process_medial_body last !!!!!!!!");
				TSDebug(PLUGIN_NAME, " process_encrypt_body last avail = %d!",avail);
				TSIOBufferCopy(flv_buffer, tag_reader, avail, 0);
				TSIOBufferReaderConsume(tag_reader, avail);
				tag_pos += avail;
				TSDebug(PLUGIN_NAME, "success !!!!!! tag_pos = %d", tag_pos);
				duration_time = duration_time/1000;
				this->current_handler = &FlvTag::process_check_des_body;
				return process_check_des_body();
			}
		} else if(tag.type  == FLV_TAG_TYPE_AUDIO) { //音频
			duration_audio_size += flv_tag_length + body_length; //丢弃的audio
		}

		TSIOBufferReaderConsume(tag_reader, sz);
		duration_file_size += sz; //丢弃的多少数据
		avail -= sz;

		tag_pos += sz;

	} while (avail > 0);

	return 0;
}

int FlvTag::update_flv_meta_data() {
	//copy FLV HEADER ＋ first TAG SIZE
	size_t flv_header_length = sizeof(flv_header) + sizeof(uint32_be);
	TSIOBufferCopy(new_flv_buffer, flv_reader, flv_header_length, 0);
	TSIOBufferReaderConsume(flv_reader, flv_header_length);

	//解析flv tag
	flv_tag tag;
	uint32 body_length;
	amf_data * name;
	amf_data * data;
	amf_data * on_metadata, *on_metadata_name;
	name = NULL;
	data = NULL;
	on_metadata = NULL;
	on_metadata_name = NULL;
	IOBufferReaderCopy(flv_reader, &tag, sizeof(tag));
	TSIOBufferReaderConsume(flv_reader, sizeof(tag));
	body_length = flv_tag_get_body_length(tag);
	byte *buf;
	uint32 prev_tag_size;

	buf = (byte *)TSmalloc(sizeof(byte) * body_length);
	bzero(buf, sizeof(buf));
	IOBufferReaderCopy(flv_reader, &buf, body_length);
	TSIOBufferReaderConsume(flv_reader, body_length);

	flv_read_metadata(buf, &name, &data);

	//	flv_read_prev_tag_size(pFile, &prev_tag_size);
	IOBufferReaderCopy(flv_reader, &prev_tag_size, sizeof(uint32_be));
	TSIOBufferReaderConsume(flv_reader, sizeof(uint32_be));
	prev_tag_size = swap_uint32(prev_tag_size);
	TSDebug(PLUGIN_NAME,"unexpected end of file in previous tag size%d\n",prev_tag_size);

	/* onMetaData checking */
	if (!strcmp((char*) amf_string_get_bytes(name),"onMetaData")) {
		TSDebug(PLUGIN_NAME,"onMetaName= %d\n",amf_data_size(name));
		TSDebug(PLUGIN_NAME,"onMetaData = %d\n",amf_data_size(data));
		on_metadata = amf_data_clone(data);
		on_metadata_name = amf_data_clone(name);
		/* check onMetadata type */
		if (amf_data_get_type(on_metadata) != AMF_TYPE_ASSOCIATIVE_ARRAY) {
			TSDebug(PLUGIN_NAME,"invalid onMetaData data type: %u, should be an associative array (8)\n",amf_data_get_type(on_metadata));
			amf_data_free(name);
			amf_data_free(data);
			goto end;
		}
	}
	amf_data_free(name);
	amf_data_free(data);

	TSDebug(PLUGIN_NAME,"copy onMetaName= %d\n",amf_data_size(on_metadata_name));
	TSDebug(PLUGIN_NAME,"copy onMetaData = %d\n",amf_data_size(on_metadata));

    //解析 metadata
    amf_node * n;
	/* more metadata checks */
	for (n = amf_associative_array_first(on_metadata); n != NULL; n =
			amf_associative_array_next(n)) {
		byte * name;
		amf_data * data;
		byte type;

		name = amf_string_get_bytes(amf_associative_array_get_name(n));
		data = amf_associative_array_get_data(n);
		type = amf_data_get_type(data);

		/* TODO: check UTF-8 strings, in key, and value if string type */
		//找出需要更改的value
		/* duration (number) */
		if (!strcmp((char*) name, "duration")) {
			if (type == AMF_TYPE_NUMBER) {
				number64 file_duration;
				file_duration = amf_number_get_value(data);
				double xie = int2double(file_duration) - duration_time;
				TSDebug(PLUGIN_NAME,"duration should got %lf %lf\n", xie, int2double(file_duration));
				amf_number_set_value(data,double2int(xie)); //修改总时间
			} else {
				TSDebug(PLUGIN_NAME,"invalid type for duration: expected %s, got %s\n",
						get_amf_type_string(AMF_TYPE_NUMBER),
						get_amf_type_string(type));
			}
		}

		/* lasttimestamp: (number) */
		if (!strcmp((char*) name, "lasttimestamp")) {
			if (type == AMF_TYPE_NUMBER) {
				number64 file_lasttimestamp;
				file_lasttimestamp = amf_number_get_value(data);
				double xie = int2double(file_lasttimestamp) - duration_time;
				TSDebug(PLUGIN_NAME,"lasttimestamp should be %lf %lf\n", xie,int2double(file_lasttimestamp));
				amf_number_set_value(data,double2int(xie)); //修改总时间
			} else {
				TSDebug(PLUGIN_NAME,"invalid type for lasttimestamp: expected %s, got %s\n",
						get_amf_type_string(AMF_TYPE_NUMBER),
						get_amf_type_string(type));
			}
		}

		/* lastkeyframetimestamp: (number) */
		if (!strcmp((char*) name, "lastkeyframetimestamp")) {
			if (type == AMF_TYPE_NUMBER) {
				number64 file_lastkeyframetimestamp;
				file_lastkeyframetimestamp = amf_number_get_value(data);
				double xie = int2double(file_lastkeyframetimestamp) - duration_time;;
				TSDebug(PLUGIN_NAME,"lastkeyframetimestamp should be %lf %lf\n", xie,int2double(file_lastkeyframetimestamp));
			} else {
				TSDebug(PLUGIN_NAME,"invalid type for lastkeyframetimestamp: expected %s, got %s\n",
						get_amf_type_string(AMF_TYPE_NUMBER),
						get_amf_type_string(type));
			}
		}

		/* filesize: (number) */
		if (!strcmp((char*) name, "filesize")) {
			if (type == AMF_TYPE_NUMBER) {
				number64 file_filesize;

				file_filesize = amf_number_get_value(data);
				double xie = int2double(file_filesize) - duration_file_size ;
				TSDebug(PLUGIN_NAME,"filesize should be got %lf %lf\n", xie,int2double(file_filesize));
			} else {
				TSDebug(PLUGIN_NAME,"invalid type for filesize: expected %s, got %s\n",
						get_amf_type_string(AMF_TYPE_NUMBER),
						get_amf_type_string(type));
			}
		}

		/* videosize: (number) */
		if (!strcmp((char*) name, "videosize")) {
			if (type == AMF_TYPE_NUMBER) {
				number64 file_videosize;
				file_videosize = amf_number_get_value(data);
				double xie = int2double(file_videosize) - duration_video_size;
				TSDebug(PLUGIN_NAME,"videosize should be got %lf %lf\n", xie,int2double(file_videosize));
			} else {
				TSDebug(PLUGIN_NAME,"invalid type for videosize: expected %s, got %s\n",
						get_amf_type_string(AMF_TYPE_NUMBER),
						get_amf_type_string(type));
			}
		}

		/* audiosize: (number) */
		if (!strcmp((char*) name, "audiosize")) {
			if (type == AMF_TYPE_NUMBER) {
				number64 file_audiosize;
				file_audiosize = amf_number_get_value(data);
				double xie = int2double(file_audiosize) - duration_audio_size;
				TSDebug(PLUGIN_NAME,"audiosize should be got %lf %lf\n", xie,int2double(file_audiosize));
			} else {
				TSDebug(PLUGIN_NAME,"invalid type for audiosize: expected %s, got %s\n",
						get_amf_type_string(AMF_TYPE_NUMBER),
						get_amf_type_string(type));
			}
		}

		/* datasize: (number) */
		if (!strcmp((char*) name, "datasize")) {
			if (type == AMF_TYPE_NUMBER) {
				number64 file_datasize;
				file_datasize = amf_number_get_value(data);
				double xie = int2double(file_datasize) - duration_video_size - duration_audio_size;
				TSDebug(PLUGIN_NAME,"datasize should be got %lf %lf\n", xie,int2double(file_datasize));
			} else {
				TSDebug(PLUGIN_NAME,"invalid type for datasize: expected %s, got %s\n",
						get_amf_type_string(AMF_TYPE_NUMBER),
						get_amf_type_string(type));
			}
		}

		/* keyframes: (object) */
		if (!strcmp((char*) name, "keyframes")) {
			if (type == AMF_TYPE_OBJECT) {
				amf_data * file_times, *file_filepositions;

				file_times = amf_object_get(data, "times");
				file_filepositions = amf_object_get(data, "filepositions");

				/* check sub-arrays' presence */
				if (file_times == NULL) {
					TSDebug(PLUGIN_NAME,"Missing times metadata\n");
				}
				if (file_filepositions == NULL) {
					TSDebug(PLUGIN_NAME,"Missing filepositions metadata\n");
				}

				if (file_times != NULL && file_filepositions != NULL) {
					/* check types */
					uint8 times_type, fp_type;

					times_type = amf_data_get_type(file_times);
					if (times_type != AMF_TYPE_ARRAY) {
						TSDebug(PLUGIN_NAME,"times_type != AMF_TYPE_ARRAY －－ invalid type for times: expected %s, got %s\n",
								get_amf_type_string(AMF_TYPE_ARRAY),
								get_amf_type_string(times_type));
					}

					fp_type = amf_data_get_type(file_filepositions);
					if (fp_type != AMF_TYPE_ARRAY) {
						TSDebug(PLUGIN_NAME,"fp_type != AMF_TYPE_ARRAY －－ invalid type for filepositions: expected %s, got %s\n",
								get_amf_type_string(AMF_TYPE_ARRAY),
								get_amf_type_string(fp_type));
					}

					if (times_type == AMF_TYPE_ARRAY
							&& fp_type == AMF_TYPE_ARRAY) {
						number64 last_file_time;
						int have_last_time;
						amf_node * ff_node, *ft_node;

						/* iterate in parallel, report diffs */
						last_file_time = 0;
						have_last_time = 0;

						ft_node = amf_array_first(file_times);
						ff_node = amf_array_first(file_filepositions);
						TSDebug(PLUGIN_NAME,"file_times size = %d\n",amf_array_size(file_times));
						TSDebug(PLUGIN_NAME,"file_times byte = %d\n",amf_data_size(file_times));

						amf_node * first_t_node, *first_p_node;
						first_t_node = ft_node;
						first_p_node = ff_node;

						while (ft_node != NULL && ff_node != NULL) {
							number64 f_time, f_position;
							double df_time ,df_position;
							/* time */
							if (amf_data_get_type(
									amf_array_get(
											ft_node)) != AMF_TYPE_NUMBER) {
								TSDebug(PLUGIN_NAME,"!= AMF_TYPE_NUMBER  －－ invalid type for time: expected %s, got %s\n",
										get_amf_type_string(AMF_TYPE_NUMBER),
										get_amf_type_string(type));
							} else {
								f_time = amf_number_get_value(
										amf_array_get(ft_node));
								TSDebug(PLUGIN_NAME,"f_time = %.12llu\n",f_time);
								df_time = int2double(f_time);
								TSDebug(PLUGIN_NAME,"invalid keyframe time: expected got %lf\n",
										df_time);

								/* check for duplicate time, can happen in H.264 files */
								if (have_last_time
										&& last_file_time == f_time) {
									double xie = int2double(f_time);
									TSDebug(PLUGIN_NAME,"Duplicate keyframe time: %lf\n",
											xie);
								}
								have_last_time = 1;
								last_file_time = f_time;
							}

							/* position */
							if (amf_data_get_type(
									amf_array_get(
											ff_node)) != AMF_TYPE_NUMBER) {
								TSDebug(PLUGIN_NAME,
										"!= AMF_TYPE_NUMBER invalid type for file position: expected %s, got %s\n",
										get_amf_type_string(AMF_TYPE_NUMBER),
										get_amf_type_string(type));
							} else {
								f_position = amf_number_get_value(
										amf_array_get(ff_node));
								double df_position = int2double(f_position);
								TSDebug(PLUGIN_NAME,
										"invalid keyframe file position: expected  got %lf\n",
										df_position);
							}
							if(df_time!= 0 && df_time <= duration_time) {
								first_t_node->next = amf_array_next(ft_node);
								first_p_node->next = amf_array_next(ff_node);
								first_t_node->next->prev = first_t_node;
								first_p_node->next->prev = first_p_node;

								amf_array_delete(file_times,ft_node);
								amf_array_delete(file_filepositions,ff_node);
								ft_node = first_t_node->next;
								ff_node = first_p_node->next;

								continue;
							} else if(df_time!= 0){ //修改关键帧 和关键帧位置
								amf_number_set_value(amf_array_get(ft_node), double2int(df_time - duration_time));
								amf_number_set_value(amf_array_get(ff_node), double2int(df_position - duration_file_size));
							}

							/* next entry */
							ft_node = amf_array_next(ft_node);
							ff_node = amf_array_next(ff_node);
						}

						TSDebug(PLUGIN_NAME,"------file_times size = %d\n",amf_array_size(file_times));
						TSDebug(PLUGIN_NAME,"------file_times byte = %d\n",amf_data_size(file_times));
					}
				}
			} else {
				TSDebug(PLUGIN_NAME,"invalid type for keyframes: expected %s, got %s\n",
						get_amf_type_string(AMF_TYPE_BOOLEAN),
						get_amf_type_string(type));
			}
		}//end keyframes
	}// end for

end:
	uint32 meta_data_length = amf_data_size(on_metadata_name) + amf_data_size(on_metadata);
	prev_tag_size = sizeof(flv_tag) + meta_data_length;
    /* first "previous tag size" */
//    size = swap_uint32(0);
//    if (fwrite(&size, sizeof(uint32_be), 1, flv_out) != 1) {
//        return ERROR_WRITE;
//    }
	//修改flv_tag 里的长度
//    omft.type = FLV_TAG_TYPE_META;
//    omft.body_length = uint32_to_uint24_be(on_metadata_name_size + on_metadata_size);
//    flv_tag_set_timestamp(&omft, 0);
//    omft.stream_id = uint32_to_uint24_be(0);
//	TSIOBufferCopy(new_flv_buffer, flv_reader, sizeof(uint8), 0);
	tag.body_length = uint32_to_uint24_be(meta_data_length);
	IOBufferReaderCopy(new_flv_reader, &tag, sizeof(flv_tag));

	byte metadata_name_b[amf_data_size(on_metadata_name)];
	amf_data_buffer_write(on_metadata_name,metadata_name_b, amf_data_size(on_metadata_name));
	IOBufferReaderCopy(new_flv_reader, metadata_name_b, amf_data_size(on_metadata_name));

	byte *on_medata_data_b = (byte *)TSmalloc(sizeof(byte) * amf_data_size(on_metadata));
	amf_data_buffer_write(on_metadata,on_medata_data_b, amf_data_size(on_metadata));
	IOBufferReaderCopy(new_flv_reader, on_medata_data_b , amf_data_size(on_metadata));
	uint32_be size = swap_uint32(prev_tag_size);
	IOBufferReaderCopy(new_flv_reader, &size, sizeof(uint32_be));

	amf_data_free(on_metadata);
	amf_data_free(on_metadata_name);

	return 0;
}

int FlvTag::process_check_des_body() {
	int64_t avail, b_avail, need_read_buf;
	int i = 0;
	u_char *buf;
	size_t need_read_length = sizeof(flv_header)+sizeof(uint32_be) *2 +sizeof(flv_tag)+on_meta_data_size;

	b_avail = TSIOBufferReaderAvail(flv_reader);
	avail = TSIOBufferReaderAvail(tag_reader);

	if ((avail + b_avail) < FLV_1_NEED_DES_LENGTH or (avail + b_avail) < need_read_length)
		return 0;

	if (avail) {
		TSIOBufferCopy(flv_buffer, tag_reader, avail, 0);
		TSIOBufferReaderConsume(tag_reader, avail);
	}


	//更新 onmetadata
	update_flv_meta_data();

	b_avail = TSIOBufferReaderAvail(flv_reader);
	if(b_avail > 0) {
		TSIOBufferCopy(new_flv_buffer, flv_reader, b_avail, 0);
		TSIOBufferReaderConsume(flv_reader, b_avail);
	}

	buf = (u_char *)TSmalloc(sizeof(u_char) * FLV_1_DES_LENGTH);
	bzero(buf, sizeof(buf));

	IOBufferReaderCopy(new_flv_reader, buf, FLV_1_NEED_DES_LENGTH);
	TSIOBufferReaderConsume(new_flv_reader, FLV_1_NEED_DES_LENGTH);

	//处理好了，进行des 加密
	TSDebug(PLUGIN_NAME, "process_check_des_body  tdes_key '%.*s'", 8,
			tdes_key);
	//从buf 中拷贝
	des_encrypt(tdes_key, buf, FLV_1_NEED_DES_LENGTH);
	for (i = 0; i < 136; i++) {
		TSDebug(PLUGIN_NAME, "des to en %d", buf[i]);
	}
	TSIOBufferWrite(head_buffer, buf, FLV_1_DES_LENGTH);

	TSfree((char *)buf);
	buf = NULL;

	avail = TSIOBufferReaderAvail(new_flv_reader);
	if (avail > 0) {
		TSIOBufferCopy(head_buffer, new_flv_reader, avail, 0);
		TSIOBufferReaderConsume(new_flv_reader, avail);
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


int FlvTag::flv_read_metadata(byte *stream,amf_data ** name, amf_data ** data) {
    amf_data * d;
    byte error_code;

    /* read metadata name */
    d = amf_data_file_read(stream);
    *name = d;
    error_code = amf_data_get_error_code(d);

    /* if only name can be read, metadata are invalid */
//    data_size = amf_data_size(d);

    /* read metadata contents */
    d = amf_data_file_read(stream);
    *data = d;
    error_code = amf_data_get_error_code(d);

//    data_size = amf_data_size(d);

    return 0;
}

/* get string representing given AMF type */
static const char * get_amf_type_string(byte type) {
	switch (type) {
	case AMF_TYPE_NUMBER:
		return "Number";
	case AMF_TYPE_BOOLEAN:
		return "Boolean";
	case AMF_TYPE_STRING:
		return "String";
	case AMF_TYPE_NULL:
		return "Null";
	case AMF_TYPE_UNDEFINED:
		return "Undefined";
		/*case AMF_TYPE_REFERENCE:*/
	case AMF_TYPE_OBJECT:
		return "Object";
	case AMF_TYPE_ASSOCIATIVE_ARRAY:
		return "Associative array";
	case AMF_TYPE_ARRAY:
		return "Array";
	case AMF_TYPE_DATE:
		return "Date";
		/*case AMF_TYPE_SIMPLEOBJECT:*/
	case AMF_TYPE_XML:
		return "XML";
	case AMF_TYPE_CLASS:
		return "Class";
	default:
		return "Unknown type";
	}
}

#endif /* __FLV_ENCRYPTION_CC__ */
