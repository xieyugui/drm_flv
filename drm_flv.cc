/*
 * drmvoideo.cc
 *
 *  Created on: 2015年11月17日
 *      Author: xie
 */

#include "flv_common.h"


static int drm_flv_handler(TSCont contp, TSEvent event, void *edata);
static void flv_cache_lookup_complete(FlvContext *fc, TSHttpTxn txnp);
static void flv_add_transform(FlvContext *fc, TSHttpTxn txnp);
static int flv_transform_entry(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */);
static int flv_transform_handler(TSCont contp, FlvContext *fc);
static void flv_read_response(FlvContext *fc, TSHttpTxn txnp);

//des key
static u_char *des_key = NULL;

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

  if(des_key == NULL) {
	  TSError("[%s] Plugin not initialized, must have des key", PLUGIN_NAME);
	  return TS_ERROR;
  }

  return TS_SUCCESS;
}

void TSRemapDeleteInstance(void *instance) {

}

TSRemapStatus
TSRemapDoRemap(void * /* ih ATS_UNUSED */, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
	const char *method, *path, *range;
	int method_len, path_len, range_len;
	int64_t start;
	FlvContext *fc;
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
	} else if(strncasecmp(path + path_len - 4, ".flv", 4) == 0) {
		video_type = FLV_VIDEO;
	} else {
		return TSREMAP_NO_REMAP;
	}
	start = 0;

	if (TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, "", -1) == TS_ERROR) {
		return TSREMAP_NO_REMAP;
	}

	// remove Range  request Range: bytes=500-999, response Content-Range: bytes 21010-47021/47022
	range_field = TSMimeHdrFieldFind(rri->requestBufp, rri->requestHdrp, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE);
	if (range_field) {
		range = TSMimeHdrFieldValueStringGet(rri->requestBufp, rri->requestHdrp, range_field, -1, &range_len);
		size_t b_len = sizeof("bytes=") -1;
		if (range && (strncasecmp(range, "bytes=", b_len) == 0)) {
			//get range value
			start = (int64_t)strtol(range+b_len, NULL, 10);
			TSDebug(PLUGIN_NAME, "TSRemapDoRemap start=%ld ", start);
		 }
		TSMimeHdrFieldDestroy(rri->requestBufp, rri->requestHdrp, range_field);
		TSHandleMLocRelease(rri->requestBufp, rri->requestHdrp, range_field);
	}

	if (start < 0 ) {
		TSHttpTxnSetHttpRetStatus(rh, TS_HTTP_STATUS_BAD_REQUEST);
		TSHttpTxnErrorBodySet(rh, TSstrdup("Invalid request."), sizeof("Invalid request.") - 1, NULL);
	}

	if (start == 0)
	  return TSREMAP_NO_REMAP;

	// remove Accept-Encoding
	ae_field = TSMimeHdrFieldFind(rri->requestBufp, rri->requestHdrp, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
	if (ae_field) {
	  TSMimeHdrFieldDestroy(rri->requestBufp, rri->requestHdrp, ae_field);
	  TSHandleMLocRelease(rri->requestBufp, rri->requestHdrp, ae_field);
	}

	fc = new FlvContext(video_type,start);
	TSDebug(PLUGIN_NAME, "TSRemapDoRemap start=%ld, type=%d", start, video_type);

	contp = TSContCreate((TSEventFunc) drm_flv_handler, NULL);
	TSContDataSet(contp, fc);
	TSHttpTxnHookAdd(rh, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
	TSHttpTxnHookAdd(rh, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
	TSHttpTxnHookAdd(rh, TS_HTTP_TXN_CLOSE_HOOK, contp);

	return TSREMAP_NO_REMAP;
}

static int
drm_flv_handler(TSCont contp, TSEvent event, void *edata) {

	TSHttpTxn txnp;
	FlvContext *fc;

	txnp = (TSHttpTxn)edata;
	fc = (FlvContext *)TSContDataGet(contp);

	switch (event) {
	case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
	  flv_cache_lookup_complete(fc, txnp);
	  break;

	case TS_EVENT_HTTP_READ_RESPONSE_HDR:
	  flv_read_response(fc, txnp);
	  break;

	case TS_EVENT_HTTP_TXN_CLOSE:
		TSDebug(PLUGIN_NAME, "TS_EVENT_HTTP_TXN_CLOSE");
		delete fc;
		TSContDestroy(contp);
		break;

	  default:
	    break;
	  }

	  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
	  return 0;
}

static void
flv_read_response(FlvContext *fc, TSHttpTxn txnp)
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

    fc->cl = n;
    flv_add_transform(fc, txnp);

release:

    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdrp);

}

static void
flv_cache_lookup_complete(FlvContext *fc, TSHttpTxn txnp)
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

	fc->cl = n;
	flv_add_transform(fc, txnp);

release:

	TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdrp);
}

static void
flv_add_transform(FlvContext *fc, TSHttpTxn txnp)
{
	TSVConn connp;
	FlvTransformContext *ftc;

	if (fc->transform_added)
		return;

	ftc = new FlvTransformContext(fc->video_type,fc->start, fc->cl, des_key);

	TSHttpTxnUntransformedRespCache(txnp, 1);
	TSHttpTxnTransformedRespCache(txnp, 0);

	connp = TSTransformCreate(flv_transform_entry, txnp);
	TSContDataSet(connp, fc);
	TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
	fc->transform_added = true;
	fc->ftc = ftc;

}

static int
flv_transform_entry(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
	TSDebug(PLUGIN_NAME, "start TS_HTTP_RESPONSE_TRANSFORM_HOOK");
	TSVIO input_vio;
	FlvContext *fc = (FlvContext *)TSContDataGet(contp);

	if (TSVConnClosedGet(contp)) {
		TSContDestroy(contp);
		return 0;
	}

	switch (event) {
	case TS_EVENT_ERROR:
		input_vio = TSVConnWriteVIOGet(contp);
		TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
		break;

	case TS_EVENT_VCONN_WRITE_COMPLETE:
		TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
		break;

	case TS_EVENT_VCONN_WRITE_READY:
	default:
		flv_transform_handler(contp, fc);
		break;
	}

	return 0;
}

static int
flv_transform_handler(TSCont contp, FlvContext *fc)
{
	TSVConn output_conn;
	TSVIO input_vio;
	TSIOBufferReader input_reader;
	int64_t avail, toread, upstream_done, tag_avail, dup_avail;
	int ret;
	bool write_down;
	FlvTag *ftag;
	FlvTransformContext *ftc;
	ftc = fc->ftc;
	ftag = &(ftc->ftag);

	output_conn = TSTransformOutputVConnGet(contp);
	input_vio = TSVConnWriteVIOGet(contp);
	input_reader = TSVIOReaderGet(input_vio);

	if (!TSVIOBufferGet(input_vio)) {
		if (ftc->output.vio) {
			TSVIONBytesSet(ftc->output.vio, ftc->total);
			TSVIOReenable(ftc->output.vio);
		}
		return 1;
	}

	avail = TSIOBufferReaderAvail(input_reader);
	upstream_done = TSVIONDoneGet(input_vio);

	TSIOBufferCopy(ftc->res_buffer, input_reader, avail, 0);
	TSIOBufferReaderConsume(input_reader, avail);
	TSVIONDoneSet(input_vio, upstream_done + avail);

	toread = TSVIONTodoGet(input_vio);
	write_down = false;

	if (!ftc->parse_over) {
		ret = ftag->process_tag(ftc->res_reader, toread <= 0);
		if (ret == 0) {
			goto trans;
		}
		ftc->parse_over = true;

		ftc->output.buffer = TSIOBufferCreate();
		ftc->output.reader = TSIOBufferReaderAlloc(ftc->output.buffer);
		ftc->output.vio = TSVConnWrite(output_conn, contp, ftc->output.reader, ftag->content_length);

		if(ret < 0) {
			dup_avail = TSIOBufferReaderAvail(ftag->dup_reader);
			if (dup_avail > 0) {
				TSIOBufferCopy(ftc->output.buffer, ftag->dup_reader, dup_avail, 0);
				TSIOBufferReaderConsume(ftag->dup_reader, dup_avail);
				ftc->total += dup_avail;
				write_down = true;
			}
		} else {
			tag_avail = ftag->write_out(ftc->output.buffer);
			if (tag_avail > 0) {
				ftc->total += tag_avail;
				write_down = true;
			}
		}
	}

	avail = TSIOBufferReaderAvail(ftc->res_reader);
	if (avail > 0) {
		TSIOBufferCopy(ftc->output.buffer, ftc->res_reader, avail, 0);
		TSIOBufferReaderConsume(ftc->res_reader, avail);
		ftc->total += avail;
		write_down = true;
	}

trans:
	if (write_down)
		TSVIOReenable(ftc->output.vio);

	if (toread > 0) {
		TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
	} else {
		TSVIONBytesSet(ftc->output.vio, ftc->total);
		TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
	}

	return 1;
}

