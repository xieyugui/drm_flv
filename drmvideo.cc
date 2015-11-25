/*
 * drmvoideo.cc
 *
 *  Created on: 2015年11月17日
 *      Author: xie
 */

#include "video_common.h"


static char *ts_arg(const char *param, size_t param_len, const char *key, size_t key_len, size_t *val_len);
static int drmvideo_handler(TSCont contp, TSEvent event, void *edata);
static void video_cache_lookup_complete(VideoContext *mc, TSHttpTxn txnp);
static void video_add_transform(VideoContext *videoc, TSHttpTxn txnp);
static int video_transform_entry(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */);
static int video_transform_handler(TSCont contp, VideoContext *videoc);
static void video_read_response(VideoContext *videoc, TSHttpTxn txnp);


TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    snprintf(errbuf, errbuf_size, "[TSRemapInit] - Invalid TSRemapInterface argument");
    return TS_ERROR;
  }

  if (api_info->size < sizeof(TSRemapInterface)) {
    snprintf(errbuf, errbuf_size, "[TSRemapInit] - Incorrect size of TSRemapInterface structure");
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **instance, char *errbuf, int errbuf_size) {
  if (argc < 2) {
	  TSError("[%s] Plugin not initialized, must have des key",PLUGIN_NAME);
	  return TS_ERROR;
  }
  des_key = (u_char *)(TSstrdup(argv[2]));
  TSDebug(PLUGIN_NAME,"TSRemapNewInstance drm video des key is %s",des_key);
  TSDebug(PLUGIN_NAME,"TSRemapNewInstance drm video des key is %s",TSstrdup(argv[2]));

  return TS_SUCCESS;
}

void TSRemapDeleteInstance(void *instance) {
	TSfree((u_char *) des_key);
	TSDebug(PLUGIN_NAME,"free des key success");
}

//只处理pcf (querty带 ?start=字节数) 和 pcm （带range 请求的）
TSRemapStatus
TSRemapDoRemap(void * /* ih ATS_UNUSED */, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
	const char *method, *query, *path, *range;
	int method_len, query_len, path_len, range_len;
	size_t val_len;
	const char *val;
	int ret;
	int64_t start;
	VideoContext *videoc;
	VideoType video_type;
	TSMLoc ae_field, range_field;
	TSCont contp;

	method = TSHttpHdrMethodGet(rri->requestBufp, rri->requestHdrp, &method_len);
	if (method != TS_HTTP_METHOD_GET) {
	  return TSREMAP_NO_REMAP;
	}

	// check suffix
	path = TSUrlPathGet(rri->requestBufp, rri->requestUrl, &path_len);
	if (path == NULL || path_len <= 4) {
	  return TSREMAP_NO_REMAP;
	} else if (strncasecmp(path + path_len - 4, ".pcf", 4) == 0) {
		video_type = VIDEO_PCF;
	} else if(strncasecmp(path + path_len - 4, ".pcm", 4) == 0) {
		video_type = VIDEO_PCM;
	} else {
		return TSREMAP_NO_REMAP;
	}
	start = 0;
	if (video_type == VIDEO_PCF) {
		query = TSUrlHttpQueryGet(rri->requestBufp, rri->requestUrl, &query_len);

		val = ts_arg(query, query_len, "start", sizeof("start") - 1, &val_len);
		if (val != NULL) {
		  ret = sscanf(val, "%u", &start);
		  if (ret != 1)
			start = 0;
		}

		if (start < 0 ) {
			TSHttpTxnSetHttpRetStatus(rh, TS_HTTP_STATUS_BAD_REQUEST);
			TSHttpTxnErrorBodySet(rh, TSstrdup("Invalid request."), sizeof("Invalid request.") - 1, NULL);
			return TSREMAP_NO_REMAP;//?需不需要
		}
		//删除query string
		if (TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, "", -1) == TS_ERROR) {
		    return TSREMAP_NO_REMAP;
		}
		// just for debug
		char *s;
		int len;
		s = TSUrlStringGet(rri->requestBufp, rri->requestUrl, &len);
		TSDebug(PLUGIN_NAME, "new request string is [%.*s]", len, s);
		TSfree(s);
	} else if(video_type == VIDEO_PCM) {  //TODO VIDEO_PCM
		// remove Range  request Range: bytes=500-999, response Content-Range: bytes 21010-47021/47022
		range_field = TSMimeHdrFieldFind(rri->requestBufp, rri->requestHdrp, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE);
		if (range_field) {
			range = TSMimeHdrFieldValueStringGet(rri->requestBufp, rri->requestHdrp, range_field, -1, &range_len);
			size_t b_len = sizeof("bytes=") -1;
			if (range && (strncasecmp(range, "bytes=", b_len) == 0)) {
				//获取range value
				start = (int64_t)strtol(range+b_len, NULL, 10);
			 }
			TSMimeHdrFieldDestroy(rri->requestBufp, rri->requestHdrp, range_field);
			TSHandleMLocRelease(rri->requestBufp, rri->requestHdrp, range_field);
		}
	}

	if (start == 0)
	  return TSREMAP_NO_REMAP;

	// remove Accept-Encoding
	ae_field = TSMimeHdrFieldFind(rri->requestBufp, rri->requestHdrp, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
	if (ae_field) {
	  TSMimeHdrFieldDestroy(rri->requestBufp, rri->requestHdrp, ae_field);
	  TSHandleMLocRelease(rri->requestBufp, rri->requestHdrp, ae_field);
	}

	videoc = new VideoContext(video_type,start);
	TSDebug(PLUGIN_NAME, "TSRemapDoRemap start=%d, type=%d", start, video_type);

	contp = TSContCreate((TSEventFunc) drmvideo_handler, NULL);
	TSContDataSet(contp, videoc);
	TSHttpTxnHookAdd(rh, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
	TSHttpTxnHookAdd(rh, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
	TSHttpTxnHookAdd(rh, TS_HTTP_TXN_CLOSE_HOOK, contp);

	return TSREMAP_NO_REMAP;
}

static int
drmvideo_handler(TSCont contp, TSEvent event, void *edata) {

	TSHttpTxn txnp;
	VideoContext *videoc;

	txnp = (TSHttpTxn)edata;
	videoc = (VideoContext *)TSContDataGet(contp);

	switch (event) {
	case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
	  video_cache_lookup_complete(videoc, txnp);
	  break;

	case TS_EVENT_HTTP_READ_RESPONSE_HDR:
	  video_read_response(videoc, txnp);
	  break;

	case TS_EVENT_HTTP_TXN_CLOSE:
		TSDebug(PLUGIN_NAME, "TS_EVENT_HTTP_TXN_CLOSE");
		delete videoc;
		TSContDestroy(contp);
		break;

	  default:
	    break;
	  }

	  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
	  return 0;
}

static void
video_read_response(VideoContext *videoc, TSHttpTxn txnp)
{
	TSMBuffer bufp;
	TSMLoc hdrp;
	TSMLoc cl_field;
	TSHttpStatus status;
	int64_t n;

    if (TSHttpTxnServerRespGet(txnp, &bufp, &hdrp) != TS_SUCCESS) {
        TSError("[%s] could not get request os data", __FUNCTION__);
        return;
    }

    status = TSHttpHdrStatusGet(bufp, hdrp);
    TSDebug(PLUGIN_NAME, " %s response code %d", __FUNCTION__, status);
    if (status != TS_HTTP_STATUS_OK)
    		goto release;

    n = 0;
    cl_field = TSMimeHdrFieldFind(bufp, hdrp, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH);
    if (cl_field) {
        n = TSMimeHdrFieldValueInt64Get(bufp, hdrp, cl_field, -1);
        TSHandleMLocRelease(bufp, hdrp, cl_field);
    }

    if (n <= 0)
        goto release;

    TSDebug(PLUGIN_NAME, " %s response content length %d", __FUNCTION__, n);
    videoc->cl = n;
    video_add_transform(videoc, txnp);

release:

    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdrp);

}

static void
video_cache_lookup_complete(VideoContext *videoc, TSHttpTxn txnp)
{
	TSMBuffer bufp;
	TSMLoc hdrp;
	TSMLoc cl_field;
	TSHttpStatus code;
	int obj_status;
	int64_t n;

	if (TSHttpTxnCacheLookupStatusGet(txnp, &obj_status) == TS_ERROR) {
		TSError("[%s] %s Couldn't get cache status of object", PLUGIN_NAME, __FUNCTION__);
		return;
	}
	TSDebug(PLUGIN_NAME, " %s object status %d", __FUNCTION__, obj_status);
	if (obj_status != TS_CACHE_LOOKUP_HIT_STALE && obj_status != TS_CACHE_LOOKUP_HIT_FRESH)
		return;

	if (TSHttpTxnCachedRespGet(txnp, &bufp, &hdrp) != TS_SUCCESS) {
	  TSError("[%s] %s Couldn't get cache resp", PLUGIN_NAME, __FUNCTION__);
	  return;
	}

	code = TSHttpHdrStatusGet(bufp, hdrp);
	if (code != TS_HTTP_STATUS_OK) {
		goto release;
	}

	n = 0;

	cl_field = TSMimeHdrFieldFind(bufp, hdrp, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH);
	if (cl_field) {
	  n = TSMimeHdrFieldValueInt64Get(bufp, hdrp, cl_field, -1);
	  TSHandleMLocRelease(bufp, hdrp, cl_field);
	}

	if (n <= 0)
	  goto release;

	TSDebug(PLUGIN_NAME, " %s response content length %d", __FUNCTION__, n);
	videoc->cl = n;
	video_add_transform(videoc, txnp);

release:

	TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdrp);
}

static void
video_add_transform(VideoContext *videoc, TSHttpTxn txnp)
{
	TSVConn connp;
	VideoTransformContext *vtc;

	if (videoc->transform_added)
		return;

	vtc = new VideoTransformContext(videoc->video_type,videoc->start, videoc->cl, des_key);

	TSHttpTxnUntransformedRespCache(txnp, 1);
	TSHttpTxnTransformedRespCache(txnp, 0);

	connp = TSTransformCreate(video_transform_entry, txnp);
	TSContDataSet(connp, videoc);
	TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
	videoc->transform_added = true;
	videoc->vtc = vtc;

}

static int
video_transform_entry(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
	TSDebug(PLUGIN_NAME, "start TS_HTTP_RESPONSE_TRANSFORM_HOOK");
	TSVIO input_vio;
	VideoContext *videoc = (VideoContext *)TSContDataGet(contp);

	if (TSVConnClosedGet(contp)) {
		TSContDestroy(contp);
		return 0;
	}

	switch (event) {
	case TS_EVENT_ERROR:
		TSDebug(PLUGIN_NAME, "TS_EVENT_ERROR");
		input_vio = TSVConnWriteVIOGet(contp);
		TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
		break;

	case TS_EVENT_VCONN_WRITE_COMPLETE:
		TSDebug(PLUGIN_NAME, "TS_EVENT_VCONN_WRITE_COMPLETE");
		TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
		break;

	case TS_EVENT_VCONN_WRITE_READY:
	default:
		TSDebug(PLUGIN_NAME, "TS_EVENT_VCONN_WRITE_READY");
		video_transform_handler(contp, videoc);
		break;
	}

	return 0;
}

static int
video_transform_handler(TSCont contp, VideoContext *videoc)
{
	TSVConn output_conn;
	TSVIO input_vio;
	TSIOBufferReader input_reader;
	int64_t avail, toread, upstream_done, tag_avail;
	int ret;
	bool write_down;
	FlvTag *ftag;
	VideoTransformContext *vtc;
	vtc = videoc->vtc;
	ftag = &(vtc->ftag);

	output_conn = TSTransformOutputVConnGet(contp);
	input_vio = TSVConnWriteVIOGet(contp);
	input_reader = TSVIOReaderGet(input_vio);

	if (!TSVIOBufferGet(input_vio)) {
		TSDebug(PLUGIN_NAME, "TSVIOBufferGet input_vio no data");
		if (vtc->output.vio) {
			TSVIONBytesSet(vtc->output.vio, vtc->total);
			TSVIOReenable(vtc->output.vio);
		}
		return 1;
	}

	avail = TSIOBufferReaderAvail(input_reader);
	TSDebug(PLUGIN_NAME, "TSIOBufferReaderAvail  avail=%d",avail);
	upstream_done = TSVIONDoneGet(input_vio);
	TSDebug(PLUGIN_NAME, "TSVIONDoneGet  upstream_done=%d",upstream_done);

	TSIOBufferCopy(vtc->res_buffer, input_reader, avail, 0);
	TSIOBufferReaderConsume(input_reader, avail);
	TSVIONDoneSet(input_vio, upstream_done + avail);
	TSDebug(PLUGIN_NAME, "TSVIONDoneSet  upstream_done=%d",upstream_done + avail);

	toread = TSVIONTodoGet(input_vio);
	TSDebug(PLUGIN_NAME, "TSVIONTodoGet  toread=%d", toread);
	write_down = false;

	if (!vtc->parse_over) {//有没有开始解析
		TSDebug(PLUGIN_NAME, "first parse over");
		ret = ftag->process_tag(vtc->res_reader, toread <= 0);
		if (ret == 0) { //为0 说明还没解析好
			TSDebug(PLUGIN_NAME, "ret = 0 goto trans");
			goto trans;
		}
		TSDebug(PLUGIN_NAME, "ret = %d",ret);
		vtc->parse_over = true;

		vtc->output.buffer = TSIOBufferCreate();
		vtc->output.reader = TSIOBufferReaderAlloc(vtc->output.buffer);
		vtc->output.vio = TSVConnWrite(output_conn, contp, vtc->output.reader, ftag->content_length);

		tag_avail = ftag->write_out(vtc->output.buffer);
		if (tag_avail > 0) {//专门来处理头的数据
			vtc->total += tag_avail;
			TSDebug(PLUGIN_NAME, "head length = %d total = %d",tag_avail,vtc->total);
			write_down = true;
		}
	}

	avail = TSIOBufferReaderAvail(vtc->res_reader);
	if (avail > 0) {//将res_reader的数据copy 到output
		TSIOBufferCopy(vtc->output.buffer, vtc->res_reader, avail, 0);
		TSIOBufferReaderConsume(vtc->res_reader, avail);
		vtc->total += avail;
		TSDebug(PLUGIN_NAME, "avail > 0  = %d total = %d",avail,vtc->total);
		write_down = true;
	}
	TSDebug(PLUGIN_NAME, "TSVIONTodoGet  toread=%d", toread);

trans:
	if (write_down)
		TSVIOReenable(vtc->output.vio);

	if (toread > 0) {//如果还有需要读的话，继续write_ready
		TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
	} else {//如果没有的话，就通知write_complete
		TSVIONBytesSet(vtc->output.vio, vtc->total);
		TSDebug(PLUGIN_NAME, "xxxxxxxxxxx total = %d",vtc->total);
		TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
	}

	return 1;
}

static char *
ts_arg(const char *param, size_t param_len, const char *key, size_t key_len, size_t *val_len)
{
  const char *p, *last;
  const char *val;

  *val_len = 0;

  if (!param || !param_len)
    return NULL;

  p = param;
  last = p + param_len;

  for (; p < last; p++) {
    p = (char *)memmem(p, last - p, key, key_len);

    if (p == NULL)
      return NULL;

    if ((p == param || *(p - 1) == '&') && *(p + key_len) == '=') {
      val = p + key_len + 1;

      p = (char *)memchr(p, '&', last - p);

      if (p == NULL)
        p = param + param_len;

      *val_len = p - val;

      return (char *)val;
    }
  }

  return NULL;
}
