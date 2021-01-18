#include "rtsp_server.h"
#include "UsageEnvironment.hh"
#include "HandlerSet.hh"
#include "libavutil/hwcontext_dxva2.h"
#include <cstdio>
using namespace std;
//outer variables
extern condition_variable rtsp_thread_cv;
extern uint8_t* encode_extradata;
extern int encode_extradatasize;
extern uint8_t *sps, *pps;
extern int spslen, ppslen;
//global variables(to use outside rtsp_server.cpp)
bool rtsp_server_inited = false;
VEncoder_h264* g_encoder;
//settings
const int video_bitrate = 12 * 1024 * 1024; //bit. used for estBitrate
const int max_out_packet_size = 2000000; //8000000;//used when create new stream source
const int rgb_buffer_size = 8000000; //bit. the size of rgb_buffer. remember to divide by 4. Too small will lead to exception in copy
AVPixelFormat sws_infmt = AVPixelFormat::AV_PIX_FMT_BGR0;
AVHWDeviceType hwtype = AVHWDeviceType::AV_HWDEVICE_TYPE_DXVA2;
AVDXVA2FramesContext dxva2hwCtx;
AVPixelFormat hwfmt = AVPixelFormat::AV_PIX_FMT_DXVA2_VLD;

CurFrame curfame;

DebugTaskScheduler* DebugTaskScheduler::createNew(unsigned maxSchedulerGranularity /* = 10000 */)
{
	Log::log("DebugTaskScheduler::createNew()\n");
	return new DebugTaskScheduler(maxSchedulerGranularity);
}

DebugTaskScheduler::~DebugTaskScheduler()
{
#if defined(__WIN32__) || defined(_WIN32)
	if (fDummySocketNum >= 0) closeSocket(fDummySocketNum);
#endif
}

void DebugTaskScheduler::doEventLoop(char volatile* watchVariable)
{
	int count = 0;
	int step = 10;
	while (1) {
		if (watchVariable != NULL && *watchVariable != 0) break;
		double cur_time = (float)timeGetTime();
		Log::log("DebugTaskScheduler::doEventLoop() begin SingleStep. cur_time: %f\n", cur_time);
		SingleStep(0);
		count++;
		if (count/* % step == 0*/) {
			cur_time = (float)timeGetTime();
			Log::log("DebugTaskScheduler::doEventLoop() SingleStep for %d times.cur_time: %f\n", count, cur_time);
		}
		count %= 1 << 30;
	}
	if (watchVariable != NULL) {
		Log::log("watchVariable: %d", *watchVariable);
	}
}


#ifndef MILLION
#define MILLION 1000000
#endif

void DebugTaskScheduler::SingleStep(unsigned maxDelayTime)
{
	Log::log("DebugTaskScheduler::SingleStep()...\n");
	fd_set readSet = fReadSet; // make a copy for this select() call
	fd_set writeSet = fWriteSet; // ditto
	fd_set exceptionSet = fExceptionSet; // ditto

	DelayInterval const& timeToDelay = fDelayQueue.timeToNextAlarm();
	struct timeval tv_timeToDelay;
	tv_timeToDelay.tv_sec = timeToDelay.seconds();
	tv_timeToDelay.tv_usec = timeToDelay.useconds();
	// Very large "tv_sec" values cause select() to fail.
	// Don't make it any larger than 1 million seconds (11.5 days)
	const long MAX_TV_SEC = MILLION;
	if (tv_timeToDelay.tv_sec > MAX_TV_SEC) {
		tv_timeToDelay.tv_sec = MAX_TV_SEC;
	}
	// Also check our "maxDelayTime" parameter (if it's > 0):
	if (maxDelayTime > 0 &&
		(tv_timeToDelay.tv_sec > (long)maxDelayTime / MILLION ||
			(tv_timeToDelay.tv_sec == (long)maxDelayTime / MILLION &&
				tv_timeToDelay.tv_usec > (long)maxDelayTime % MILLION))) {
		tv_timeToDelay.tv_sec = maxDelayTime / MILLION;
		tv_timeToDelay.tv_usec = maxDelayTime % MILLION;
	}
	Log::log("select() in SingleStep()\n");
	int selectResult = select(fMaxNumSockets, &readSet, &writeSet, &exceptionSet, &tv_timeToDelay);
	if (selectResult < 0) {
		Log::log("selectResult < 0...\n");
#if defined(__WIN32__) || defined(_WIN32)
		int err = WSAGetLastError();
		// For some unknown reason, select() in Windoze sometimes fails with WSAEINVAL if
		// it was called with no entries set in "readSet".  If this happens, ignore it:
		if (err == WSAEINVAL && readSet.fd_count == 0) {
			err = EINTR;
			// To stop this from happening again, create a dummy socket:
			if (fDummySocketNum >= 0) closeSocket(fDummySocketNum);
			fDummySocketNum = socket(AF_INET, SOCK_DGRAM, 0);
			FD_SET((unsigned)fDummySocketNum, &fReadSet);
		}
		if (err != EINTR) {
#else
		if (errno != EINTR && errno != EAGAIN) {
#endif
			// Unexpected error - treat this as fatal:
#if !defined(_WIN32_WCE)
			perror("BasicTaskScheduler::SingleStep(): select() fails");
			// Because this failure is often "Bad file descriptor" - which is caused by an invalid socket number (i.e., a socket number
			// that had already been closed) being used in "select()" - we print out the sockets that were being used in "select()",
			// to assist in debugging:
			fprintf(stderr, "socket numbers used in the select() call:");
			for (int i = 0; i < 10000; ++i) {
				if (FD_ISSET(i, &fReadSet) || FD_ISSET(i, &fWriteSet) || FD_ISSET(i, &fExceptionSet)) {
					fprintf(stderr, " %d(", i);
					if (FD_ISSET(i, &fReadSet)) fprintf(stderr, "r");
					if (FD_ISSET(i, &fWriteSet)) fprintf(stderr, "w");
					if (FD_ISSET(i, &fExceptionSet)) fprintf(stderr, "e");
					fprintf(stderr, ")");
				}
			}
			fprintf(stderr, "\n");
#endif
			internalError();
		}
		}

	Log::log("Call the handler function for one readable socket in SingleStep()...\n");
	// Call the handler function for one readable socket:
	HandlerIterator iter(*fHandlers);
	HandlerDescriptor* handler;
	// To ensure forward progress through the handlers, begin past the last
	// socket number that we handled:
	if (fLastHandledSocketNum >= 0) {
		while ((handler = iter.next()) != NULL) {
			if (handler->socketNum == fLastHandledSocketNum) break;
		}
		if (handler == NULL) {
			fLastHandledSocketNum = -1;
			iter.reset(); // start from the beginning instead
		}
	}
	Log::log("ensure forward progress through the handlers end. in SingleStep()...\n");
	while ((handler = iter.next()) != NULL) {
		int sock = handler->socketNum; // alias
		int resultConditionSet = 0;
		if (FD_ISSET(sock, &readSet) && FD_ISSET(sock, &fReadSet)/*sanity check*/) resultConditionSet |= SOCKET_READABLE;
		if (FD_ISSET(sock, &writeSet) && FD_ISSET(sock, &fWriteSet)/*sanity check*/) resultConditionSet |= SOCKET_WRITABLE;
		if (FD_ISSET(sock, &exceptionSet) && FD_ISSET(sock, &fExceptionSet)/*sanity check*/) resultConditionSet |= SOCKET_EXCEPTION;
		if ((resultConditionSet & handler->conditionSet) != 0 && handler->handlerProc != NULL) {
			fLastHandledSocketNum = sock;
			// Note: we set "fLastHandledSocketNum" before calling the handler,
			// in case the handler calls "doEventLoop()" reentrantly.
			Log::log("call handler->handlerProc(): %p, clientData: %p. handlersock: %d\n", *handler->handlerProc, handler->clientData, sock);
			(*handler->handlerProc)(handler->clientData, resultConditionSet);
			Log::log("call handler->handlerProc() end.\n");
			break;
		}
	}
	Log::log("iter handler end in SingleStep().\n");
	if (handler == NULL && fLastHandledSocketNum >= 0) {
		// We didn't call a handler, but we didn't get to check all of them,
		// so try again from the beginning:
		iter.reset();
		while ((handler = iter.next()) != NULL) {
			int sock = handler->socketNum; // alias
			int resultConditionSet = 0;
			if (FD_ISSET(sock, &readSet) && FD_ISSET(sock, &fReadSet)/*sanity check*/) resultConditionSet |= SOCKET_READABLE;
			if (FD_ISSET(sock, &writeSet) && FD_ISSET(sock, &fWriteSet)/*sanity check*/) resultConditionSet |= SOCKET_WRITABLE;
			if (FD_ISSET(sock, &exceptionSet) && FD_ISSET(sock, &fExceptionSet)/*sanity check*/) resultConditionSet |= SOCKET_EXCEPTION;
			if ((resultConditionSet & handler->conditionSet) != 0 && handler->handlerProc != NULL) {
				fLastHandledSocketNum = sock;
				// Note: we set "fLastHandledSocketNum" before calling the handler,
					// in case the handler calls "doEventLoop()" reentrantly.
				Log::log("call handler->handlerProc(): %p, clientData: %p. handlersock: %d\n", *handler->handlerProc, handler->clientData, sock);
				(*handler->handlerProc)(handler->clientData, resultConditionSet);
				Log::log("call handler->handlerProc() end.\n");
				break;
			}
		}
		if (handler == NULL) fLastHandledSocketNum = -1;//because we didn't call a handler
	}
	Log::log("handle any newly-triggered event in SingleStep()...\n");
	// Also handle any newly-triggered event (Note that we do this *after* calling a socket handler,
	// in case the triggered event handler modifies The set of readable sockets.)
	if (fTriggersAwaitingHandling != 0) {
		if (fTriggersAwaitingHandling == fLastUsedTriggerMask) {
			// Common-case optimization for a single event trigger:
			fTriggersAwaitingHandling &= ~fLastUsedTriggerMask;
			if (fTriggeredEventHandlers[fLastUsedTriggerNum] != NULL) {
				(*fTriggeredEventHandlers[fLastUsedTriggerNum])(fTriggeredEventClientDatas[fLastUsedTriggerNum]);
			}
		}
		else {
			// Look for an event trigger that needs handling (making sure that we make forward progress through all possible triggers):
			unsigned i = fLastUsedTriggerNum;
			EventTriggerId mask = fLastUsedTriggerMask;

			do {
				i = (i + 1) % MAX_NUM_EVENT_TRIGGERS;
				mask >>= 1;
				if (mask == 0) mask = 0x80000000;

				if ((fTriggersAwaitingHandling & mask) != 0) {
					fTriggersAwaitingHandling &= ~mask;
					if (fTriggeredEventHandlers[i] != NULL) {
						(*fTriggeredEventHandlers[i])(fTriggeredEventClientDatas[i]);
					}

					fLastUsedTriggerMask = mask;
					fLastUsedTriggerNum = i;
					break;
				}
			} while (i != fLastUsedTriggerNum);
		}
	}
	Log::log("fDelayQueue.handleAlarm() in SingleStep()...\n");
	// Also handle any delayed event that may have come due.
	fDelayQueue.handleAlarm();
	Log::log("DebugTaskScheduler::SingleStep() end.\n");
}


void DebugTaskScheduler::setBackgroundHandling(int socketNum, int conditionSet, BackgroundHandlerProc* handlerProc, void* clientData)
{
	Log::log("DebugTaskScheduler::setBackgroundHandling(socketNum %d, handlerProc %p, clientData %p)\n", socketNum, handlerProc, clientData);
	if (socketNum < 0) return;
#if !defined(__WIN32__) && !defined(_WIN32) && defined(FD_SETSIZE)
	if (socketNum >= (int)(FD_SETSIZE)) return;
#endif
	FD_CLR((unsigned)socketNum, &fReadSet);
	FD_CLR((unsigned)socketNum, &fWriteSet);
	FD_CLR((unsigned)socketNum, &fExceptionSet);
	if (conditionSet == 0) {
		fHandlers->clearHandler(socketNum);
		if (socketNum + 1 == fMaxNumSockets) {
			--fMaxNumSockets;
		}
	}
	else {
		fHandlers->assignHandler(socketNum, conditionSet, handlerProc, clientData);
		if (socketNum + 1 > fMaxNumSockets) {
			fMaxNumSockets = socketNum + 1;
		}
		if (conditionSet & SOCKET_READABLE) FD_SET((unsigned)socketNum, &fReadSet);
		if (conditionSet & SOCKET_WRITABLE) FD_SET((unsigned)socketNum, &fWriteSet);
		if (conditionSet & SOCKET_EXCEPTION) FD_SET((unsigned)socketNum, &fExceptionSet);
	}
}

void DebugTaskScheduler::InternalError()
{
	Log::log("DebugTaskScheduler::InternalError() called. abort.\n");
	abort();
}

DebugTaskScheduler::DebugTaskScheduler(unsigned maxSchedulerGranularity)
	:BasicTaskScheduler(maxSchedulerGranularity), fDummySocketNum(-1)
{

}

//the main func of the liveserver thread.
void liveserver_main(){

	FILE *se, *so;
	if (freopen_s(&se, "stderr.log", "w", stderr)) {
		Log::log("failed to reopen stderr");
	};
	if (freopen_s(&so, "stdout.log", "w", stdout)) {
		Log::log("failed to reopen stdout");
	};

	//create encoder first
	Log::log("thread live server_main started.\n");
	g_encoder = VEncoder_h264::createNew();
	g_encoder->getExtraData(&encode_extradata, &encode_extradatasize);
	//g_encoder->getExtraData(sps, &spslen, pps, &ppslen);
	Log::log("Copy ExtraData end.\n");

	TaskScheduler* scheduler = DebugTaskScheduler::createNew();
	//UserAuthenticationDatabase* authDB = NULL;//not used
	BasicUsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);

	//TODO：总之先固定使用8554
	int serverport = 8554;
	Log::log("Create New RtspServer over port %d\n", serverport);
	RTSPServer* rtspServer = RTSPServer::createNew(*env, serverport);

	ServerMediaSession* sms = ServerMediaSession::createNew(*env, "StreamName");
	sms->addSubsession(MSubsession::createNew(*env));

	rtspServer->addServerMediaSession(sms);


	if(rtspServer->setUpTunnelingOverHTTP(80)
	|| rtspServer->setUpTunnelingOverHTTP(8000)
	|| rtspServer->setUpTunnelingOverHTTP(8080)) {
		Log::log("rtspServer setup tunneling end.(over 80, 8000, 8080).\n");
		
	} else {
		//*env << "\n(RTSP-over-HTTP tunneling is not available.)\n";
		Log::log("RTSP-over-HTTP tunneling is not available.\n");
	}
	Log::log("rtsp_thread_cv notify_one()...\n");
	rtsp_server_inited = true;
	rtsp_thread_cv.notify_one();

	env->taskScheduler().doEventLoop();
	//TODO: some release operation?
	Log::log("rtsp server: env->taskScheduler().doEventLoop() end.Doing release works...\n");
	delete g_encoder;
}


FSource* FSource::createNew(UsageEnvironment& env)
{
	Log::log("FSource::createNew() callled...\n");
	return new FSource(env);
}

FSource::FSource(UsageEnvironment& env): FramedSource(env)
{
	encoder = g_encoder;
}

FSource::~FSource()
{
	Log::log("FSource::~FSource() callled.\n");
	//TODO: find out what's wrong with this
	//delete encoder;
}

void FSource::doGetNextFrame()
{
	//TODO: split the encode and deliver process
	//		now the get_encoded will blocked the thread.
	//		which means the waiting for copyed buffer should be in the get_encoded() func;
	Log::log("FSource::doGetNextFrame()...\n");

	encoder->get_encoded(fTo, &fFrameSize);
	//if (pkt) {
	//	fFrameSize = pkt->size;
	//	memmove(fTo, pkt->data, fFrameSize);
	//	//av_packet_free(&pkt);
	//}
	//else {
	//	fFrameSize = 0;
	//}
	static int no_encoded_count = 0;
	if (fFrameSize == 0) {
		no_encoded_count++;
		Log::log("no output in doGetNextFrame() count : %d\n", no_encoded_count);
	}
	Log::log("FSource get frame size: %d\n", fFrameSize);
	FramedSource::afterGetting(this);
}

MSubsession* MSubsession::createNew(UsageEnvironment& env, portNumBits initialPortNum /*= 6970*/, Boolean multiplexRTCPWithRTP /*= False*/)
{
	return new MSubsession(env, initialPortNum, multiplexRTCPWithRTP);
}

MSubsession::MSubsession(UsageEnvironment& env, portNumBits initialPortNum /*= 6970*/, Boolean multiplexRTCPWithRTP /*= False*/)
	: OnDemandServerMediaSubsession(env, TRUE)
{
	//nothing to do here
}

FramedSource* MSubsession::createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate)
{
	Log::log("MSubsession::createNewStreamSource(）called...\n");
	FramedSource* result = NULL;
	estBitrate = video_bitrate;
	OutPacketBuffer::increaseMaxSizeTo(max_out_packet_size);
	result = FSource::createNew(envir());
	result = H264VideoStreamDiscreteFramer::createNew(envir(), result);
	Log::log("MSubsession::createNewStreamSource() end.\n");
	return result;
}

RTPSink* MSubsession::createNewRTPSink(Groupsock* rtpGroupsock, 
	unsigned char rtpPayloadTypeIfDynamic, 
	FramedSource* inputSource)
{
	Log::log("MSubsession::createNewRTPSink() called...\n");
	VEncoder_h264::spsppsdata* spspps = g_encoder->getSpsppsdata();
	RTPSink* result = 
		H264Sink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
		spspps->sps, spspps->spslen, spspps->pps, spspps->ppslen);
	Log::log("MSubsession::createNewRTPSink() end.\n");
	return result;
}

char const* MSubsession::sdpLines()
{
	Log::log("MSubsession::sdpLines() called...\n");
	return OnDemandServerMediaSubsession::sdpLines();
}

void MSubsession::getStreamParameters(unsigned clientSessionId, netAddressBits clientAddress, Port const& clientRTPPort, Port const& clientRTCPPort, int tcpSocketNum, unsigned char rtpChannelId, unsigned char rtcpChannelId, netAddressBits& destinationAddress, u_int8_t& destinationTTL, Boolean& isMulticast, Port& serverRTPPort, Port& serverRTCPPort, void*& streamToken)
{
	Log::log("MSubsession::getStreamParameters() called...\n");
	OnDemandServerMediaSubsession::getStreamParameters(clientSessionId, clientAddress, clientRTPPort, clientRTCPPort,
		tcpSocketNum, rtpChannelId, rtcpChannelId, destinationAddress, destinationTTL, 
		isMulticast, serverRTPPort, serverRTCPPort, streamToken);
}

void MSubsession::startStream(unsigned clientSessionId, void* streamToken, TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData, unsigned short& rtpSeqNum, unsigned& rtpTimestamp, ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler, void* serverRequestAlternativeByteHandlerClientData)
{
	Log::log("MSubsession::startStream() called:%p...\n", &MSubsession::startStream);
	Log::log("streamToken %p, rtcpRRHandler %p, rtcpRRHandlerClientData %p \n, \
		serverRequestAlternativeByteHandler %p, serverRequestAlternativeByteHandlerClientData %p\n",
		streamToken, rtcpRRHandler, rtcpRRHandlerClientData, serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);
	//OnDemandServerMediaSubsession::startStream(clientSessionId, streamToken, rtcpRRHandler, serverRequestAlternativeByteHandlerClientData, rtpSeqNum, rtpTimestamp, serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);
	/////////////////////////COPYED FROM LIVE555//////////////////////////////////
	StreamState* streamState = (StreamState*)streamToken;
	Destinations* destinations
		= (Destinations*)(fDestinationsHashTable->Lookup((char const*)clientSessionId));
	if (streamState != NULL) {
		Log::log("\n");
		streamState->startPlaying(destinations, clientSessionId,
			rtcpRRHandler, rtcpRRHandlerClientData,
			serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);
		RTPSink* rtpSink = streamState->rtpSink(); // alias
		if (rtpSink != NULL) {
			rtpSeqNum = rtpSink->currentSeqNo();
			rtpTimestamp = rtpSink->presetNextTimestamp();
		}
	}
}

VEncoder_h264* VEncoder_h264::createNew()
{
	return new VEncoder_h264();
}

void freeHWFramesCtx(AVHWFramesContext* ctx) {
	Log::log("freeHWFramesCtx() called.\n");
}

VEncoder_h264::VEncoder_h264()
{
	Log::log("VEncoder_h264::VEncoder_h264() called...\n");
	int ret = 0;
	char* errbuf = new char[100];
	//init buffers
	for (int i = 0; i < MAX_RGBBUFFER; i++) {
		buffers[i] = new Buffer(rgb_buffer_size / 4);
		//encoded_packets[i] = nullptr;
	}
	av_register_all();//注册组件
	codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (codec == NULL) {
		Log::log("Error: cant find encoder in in init VEncoder_h264");
	}
	Log::log("find encoder end.\n");
	codecCtx = avcodec_alloc_context3(codec);
	if (cs.config_->use_hw_) {
		Log::log("VEncoder_h264 start creating hwctx.\n");
		ret = av_hwdevice_ctx_create(&hw_device_ctx, hwtype, nullptr, nullptr, 0);
		if (ret < 0) {
			Log::log("Error: failed to create hw_device_ctx.\n");
		}
		Log::log("VEncoder_H264::VEncoder_H264() hw_device_ctx create end.\n");
		hw_frames_ctx = av_hwframe_ctx_alloc(hw_device_ctx);
		if (hw_frames_ctx == nullptr) {
			Log::log("Error: failed to alloc hwd_frame_ctx\n");
		}
		/*AVPixelFormat** fmt_list = (AVPixelFormat**)av_malloc(sizeof(AVPixelFormat**));
		ret = av_hwframe_transfer_get_formats(hw_frames_ctx,
			AVHWFrameTransferDirection::AV_HWFRAME_TRANSFER_DIRECTION_TO, fmt_list, 0);
		if (ret) {
			Log::log("Error: failed to av_hwframe_transfer_get_formats(): %d\n", ret);
		}
		Log::log("*fmt_list: %d\n", ** fmt_list);
		for (AVPixelFormat* pix_fmt = *fmt_list; *pix_fmt != AV_PIX_FMT_NONE; pix_fmt++) {
			Log::log("support: %d\n", *pix_fmt);
			if (*pix_fmt == AVPixelFormat::AV_PIX_FMT_NV12) {
				Log::log("support AVPixelFormat::AV_PIX_FMT_NV12\n");
			}
		av_free(fmt_list);
		}*/
		AVHWFramesContext* av_hwFrames_ctx = (AVHWFramesContext*)hw_frames_ctx->data;
		Log::log("av_hwFrames_ctx: %p, hw_frames_ctx->data: %p\n", av_hwFrames_ctx, hw_frames_ctx->data);
		av_hwFrames_ctx->format = hwfmt;
		av_hwFrames_ctx->sw_format = AVPixelFormat::AV_PIX_FMT_YUV420P;
		av_hwFrames_ctx->height = encoder_height;
		av_hwFrames_ctx->width = encoder_width;
		//av_hwFrames_ctx->free = freeHWFramesCtx;
		hwframespool = av_buffer_pool_init((encoder_height * encoder_width) << 2, NULL);
		av_hwFrames_ctx->pool = hwframespool;
		dxva2hwCtx.surface_type = DXVA2_VideoProcessorRenderTarget;
		av_hwFrames_ctx->hwctx = &dxva2hwCtx;
		ret = av_hwframe_ctx_init(hw_frames_ctx);
		if (ret) {
			Log::log("Error: failed to init hwd_frame_ctx: %s\n", av_make_error_string(errbuf, 100, ret));
		}
		//codecCtx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
		//codecCtx->hw_frames_ctx = av_buffer_ref(hw_frames_ctx);
	}
	else {
		hw_device_ctx = nullptr;
	}
	codecCtx->width = encoder_width;
	codecCtx->height = encoder_height;
	codecCtx->time_base.num = 1;
	codecCtx->time_base.den = cs.config_->max_fps_;//TODO: fps setting to be improved
	codecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	codecCtx->pix_fmt = (codecCtx->hw_device_ctx)?hwfmt:AVPixelFormat::AV_PIX_FMT_YUV420P;
	codecCtx->sw_pix_fmt = AVPixelFormat::AV_PIX_FMT_YUV420P;
	codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;//Place global headers in extradata instead of every keyframe.
	codecCtx->rc_buffer_size = BUFFER_SIZE;
	//codecCtx->bit_rate = video_bitrate; //设置这个会越来越糊，大小影响多久变得越来越糊
	codecCtx->bit_rate_tolerance = video_bitrate * 2;
	Log::log("encoder gop size: %d\n", cs.config_->encoder_gop_size_);
	codecCtx->gop_size = cs.config_->encoder_gop_size_;
	
	ret = avcodec_open2(codecCtx, codec, NULL);
	if (ret < 0) {
		Log::log("Error: failed to avcodec_open2 in init VEncoder_h264.ret:%s\n", av_make_error_string(errbuf, 100, ret));
		exit(1);
	}
	Log::log("codecCtx init end.\n");
	swsCtx = nullptr;
	swsCtx = sws_getCachedContext(swsCtx,
		game_width, game_height,
		sws_infmt,//the format here!
		encoder_width, encoder_height,
		(codecCtx->hw_device_ctx)?AV_PIX_FMT_NV12:AVPixelFormat::AV_PIX_FMT_YUV420P,
		SWS_BICUBIC,
		NULL, NULL, NULL);
	//get sps & pps 
	uint8_t * extradata = codecCtx->extradata;
	int extradatalen = codecCtx->extradata_size;
	Log::log("codecCtx->extradata_size: %d\n", extradatalen);
	spspps = new spsppsdata{};
	for (int i = 2; i < extradatalen; i++) {
		Log::log_notime("%02x ", extradata[i]);
		if (extradata[i] == 0x67 && extradata[i - 2] == 0) {
			spspps->sps = &extradata[i];
			spspps->spslen = extradata[i - 1];
			//Log::log("i: %d, spslen: %d\n", i, spspps->spslen);
			//memcpy(spspps->sps, &extradata[i], spspps->spslen);
		}
		if (extradata[i] == 0x68 && extradata[i - 2] == 0) {
			spspps->pps = &extradata[i];
			spspps->ppslen = extradata[i - 1];
			//Log::log("i: %d, ppslen: %d\n", i, spspps->ppslen);
			//memcpy(spspps->pps, &extradata[i], spspps->ppslen);
		}
	}
	Log::log_notime("\n");
	delete errbuf;
	//init packet
	//av_init_packet(&encoded_packet);
	//Log::log("av_init_packet(&encoded_packet) end.\n");
	//finished
	Log::log("VEncoder_h264::VEncoder_h264() end. codec: %p, ctx: %p, swsCtx: %p, \n", codec, codecCtx, swsCtx);
}

//not called now
//void VEncoder_h264::StartEncoding()
//{
//	//TODO: doing this in an extra thread
//}

void VEncoder_h264::getExtraData(uint8_t** outData, int* outDataSize)
{
	Log::log("VEncoder_h264::getExtraData(uint8_t* outData, int* outDataSize) called...\n");
	delete *outData;
	*outData = new uint8_t[codecCtx->extradata_size];
	memcpy(*outData, codecCtx->extradata, codecCtx->extradata_size);
	*outDataSize = codecCtx->extradata_size;
	Log::log("get ExtraData end. *outData: %p\n", *outData);
}

void VEncoder_h264::getExtraData(uint8_t** outSps, int* outSpslen, uint8_t** outPps, int* outPpslen)
{
	Log::log("VEncoder_h264::getExtraData(uint8_t* outSps, int* outSpslen, uint8_t* outPps, int* outPpslen) called...\n");
	delete (*outSps);
	delete (*outPps);
	*outSps = new uint8_t[spspps->spslen];
	*outPps = new uint8_t[spspps->ppslen];
	memcpy(*outSps, spspps->sps, spspps->spslen);
	*outSpslen = spspps->spslen;
	memcpy(*outPps, spspps->pps, spspps->ppslen);
	*outPpslen = spspps->ppslen;
	Log::log("get ExtraData end. *outSps: %p, *outPps: %p\n", *outSps, *outPps);
}

VEncoder_h264::spsppsdata* VEncoder_h264::getSpsppsdata()
{
	return spspps;
}

//not called now
//void VEncoder_h264::encoding_loop()
//{
//	//TODO: doing this in an extra thread
//}

//not called now
uint8_t* VEncoder_h264::findStartCode(uint8_t* data, int datalen)
{
	//Fixme: maybe no need for this
	return NULL;
}

VEncoder_h264::~VEncoder_h264()
{
	Log::log("~VEncoder_h264() called...\n");
	delete[] buffers;
	avcodec_close(codecCtx);
	avcodec_free_context(&codecCtx);
	av_buffer_unref(&hw_frames_ctx);
	av_buffer_pool_uninit(&hwframespool);
	av_buffer_unref(&hw_device_ctx);
	delete spspps;
	Log::log("~VEncoder_h264() end.\n");
}

Buffer* VEncoder_h264::get_first_empty_buffer()
{
	double cur_time = (float)timeGetTime();
	Log::log("Try Get first empty buffer during frame %d. cur first_empty: %d, cur first_used: %d. cur_time:%f\n", 
		curfame.GetCurFrame(), first_empty, first_used, cur_time);
	Buffer* result = nullptr;
	unique_lock<mutex> lock(empty_mutex);
	while (first_empty == first_used) {
		empty_cv.wait(lock);
	}
	result = buffers[first_empty];
	cur_time = (float)timeGetTime();
	Log::log("Get first empty buffer during frame %d. idx: %d, cur_time: %f\n", 
		curfame.GetCurFrame(), first_empty, cur_time);
	return result;
}


Buffer* VEncoder_h264::get_first_used_buffer()
{
	//TODO: deal with no first_used situation;
	double cur_time = (float)timeGetTime();
	Log::log("Try Get first used buffer during frame %d. cur first_empty: %d, cur first_used: %d, cur_time: %f\n", 
		curfame.GetCurFrame(), first_empty, first_used, cur_time);
	Buffer* result = nullptr;
	unique_lock<mutex> lock(used_mutex);
	while (first_used < 0) {
		used_cv.wait(lock);
	}
	result = buffers[first_used];
	cur_time = (float)timeGetTime();
	Log::log("Get first used buffer during frame %d. idx: %d, cur_time: %f\n", 
		curfame.GetCurFrame(), first_used, cur_time);
	return result;
}

int VEncoder_h264::get_encoded(uint8_t* dst, unsigned* size)
{
	Log::log("VEncoder_h264::get_encoded()...\n");
	Log::log("VEncoder_h264: codec extradatalen:%d\n", codecCtx->extradata_size);
	Buffer* srcBuffer = get_first_used_buffer();

	int ret = 0;
	*size = 0;
	AVFrame* yuvFrame = av_frame_alloc();
	AVFrame* rgbFrame = av_frame_alloc();
	AVPacket* encoded_packet = av_packet_alloc();
	yuvFrame->format = AVPixelFormat::AV_PIX_FMT_YUV420P;
	yuvFrame->width = encoder_width;
	yuvFrame->height = encoder_height;
	do 
	{
		if (codecCtx->hw_device_ctx) {
			ret = av_hwframe_get_buffer(hw_frames_ctx, yuvFrame, 0);
		}
		else {
			ret = av_frame_get_buffer(yuvFrame, 0);
		}
		//TODO: error process
		if (ret) {
			Log::log("Error: failed to av_frame_get_buffer in get_encoded()\n");
			break;
		}
		Log::log("av_frame_get_buffer in get_encoded() end.\n");
		ret = av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, (uint8_t*)srcBuffer->get_data_start(),
			sws_infmt, encoder_width, encoder_height, 1);//align for what?
		/*ret = avpicture_fill((AVPicture *)rgbFrame, (uint8_t *)srcBuffer->get_data_start(), AV_PIX_FMT_ARGB,
			game_width, game_height);*/
		if (ret < 0) {
			Log::log("Error: failed to av_image_fill_arrays in get_encoded()\n");
			break;
		}
		else {
			Log::log("the size in bytes required for src: %d\n", ret);
		}
		ret = sws_scale(swsCtx, rgbFrame->data, rgbFrame->linesize, 0, game_height,
			yuvFrame->data, yuvFrame->linesize);
		Log::log("the height of the output slice(sws_scale): %d\n", ret);

		av_init_packet(encoded_packet);
		Log::log("av_init_packet() end. start send_frame. codec: %p, codecCtx->codec: %p, yuvFrame: %p\n",
			codec, codecCtx->codec, yuvFrame);
		/*int got_packet;
		ret = avcodec_encode_video2(codecCtx, &encoded_packet, yuvFrame, &got_packet);
		if (!got_packet) {
			Log::log("not get packet after avcodec_encode_video2()\n");
		}
		if (ret < 0) {
			Log::log("Failed to avcodec_encode_video2(). ret: %d\n", ret);
		}*/
		ret = avcodec_send_frame(codecCtx, yuvFrame);
		if (ret) {
			Log::log("Error: failed to send_frame in get_encoded(): %d\n", ret);
			break;
		}
		else {
			Log::log("avcodec_send_frame() success.\n");
			srcBuffer->clear();
			first_used = (first_used + 1) % MAX_RGBBUFFER;
			if (first_used == first_empty) {
				first_used = -1;//所有待编码的全部编完了。
			}
			empty_cv.notify_one();//send一个buffer之后必然空出来一个buffer
		}
		ret = avcodec_receive_packet(codecCtx, encoded_packet);
		if (ret) {
			Log::log("Error: failed to receive_packet in get_encoded(): %d\n", ret);
			break;
		}
		else {
			Log::log("avcodec_receive_packet success.\n");
			*size = encoded_packet->size;
			memmove(dst, encoded_packet->data, encoded_packet->size);
		}
		Log::log("VEncoder_h264 encode end. cur_frame: %d, cur first_empty: %d, cur first_used: %d\n", curfame.GetCurFrame(), first_empty, first_used);
	} while (false);
	
	av_frame_free(&rgbFrame);
	av_frame_free(&yuvFrame);
	av_packet_free(&encoded_packet);
	Log::log("VEncoder_h264 encode release resources end.\n");
	return *size;
}


bool VEncoder_h264::CopyD3D9RenderingFrame(IDirect3DDevice9* pDevice)
{
	Log::log("Start copying Frame\n");
	IDirect3DSurface9 *renderSurface = nullptr, *oldRenderSurface = nullptr;
	HRESULT hr = pDevice->GetRenderTarget(0, &renderSurface);
	if (FAILED(hr)) {
		Log::log("ERROR: GetRenderTarget Error in CopyD3DFrame\n");
		return false;
	}
	Log::log_notime("\tget render target end\n");
	D3DSURFACE_DESC desc;
	renderSurface->GetDesc(&desc);
	if (desc.Width != game_width || desc.Height != game_height) {
		Log::log("ERROR: surface size not consistent with viewport settings in CopyD3DFrame\n");
		Log::log("\tdesc.width:%d, game_width:%d, desc.height:%d, desc.width:%d\n", desc.Width, game_width, desc.Height, game_height);
		return false;
	}
	Log::log_notime("\tget Desc end, desc.Format: %d\n", desc.Format);
	IDirect3DSurface9 *resolvedSurface = NULL;
	IDirect3DSurface9 *oldSurface = NULL;
	if (desc.MultiSampleType != D3DMULTISAMPLE_TYPE::D3DMULTISAMPLE_NONE) {
		hr = pDevice->CreateRenderTarget(game_width, game_height,
			desc.Format, D3DMULTISAMPLE_TYPE::D3DMULTISAMPLE_NONE, 0, FALSE, &resolvedSurface, NULL);
		if (FAILED(hr)) {
			Log::log("ERROR: failed to create resolvedSurface in CopyD3DFrame\n");
			return false;
		}
		Log::log_notime("\tcreate resolvedsurface end\n");
		hr = pDevice->StretchRect(renderSurface, NULL, resolvedSurface, NULL, D3DTEXTUREFILTERTYPE::D3DTEXF_NONE);
		if (FAILED(hr)) {
			Log::log("ERROR: failed to StretchRect in CopyD3DFrame\n");
			return false;
		}
		Log::log_notime("\tstretchrect end\n");
		oldSurface = renderSurface;
		renderSurface = resolvedSurface;
	}
	IDirect3DSurface9 *offscreenSurface;
	hr = pDevice->CreateOffscreenPlainSurface(game_width, game_height,
		desc.Format, D3DPOOL::D3DPOOL_SYSTEMMEM, &offscreenSurface, NULL);
	if (FAILED(hr)) {
		Log::log("ERROR: failed to create offscreenSurface in CopyD3DFrame\n");
		return false;
	}
	Log::log_notime("\tcreate offscreen plainsurface end\n");
	hr = pDevice->GetRenderTargetData(renderSurface, offscreenSurface);
	if (FAILED(hr)) {
		Log::log("ERROR: failed to get renderTargetData in CopyD3DFrame\n");
		if (oldSurface) {
			oldSurface->Release();
		}
		else {
			renderSurface->Release();
		}
		return false;
	}
	Log::log_notime("\tget rendertarget data end\n");
	if (oldSurface) {
		oldSurface->Release();
	}
	else {
		renderSurface->Release();
	}
	D3DSURFACE_DESC offScreenDesc;
	hr = offscreenSurface->GetDesc(&offScreenDesc);
	Log::log_notime("\toffscreenSurface: width:%d, height:%d\n", offScreenDesc.Width, offScreenDesc.Height);
	D3DLOCKED_RECT lockedRect;
	//RECT rect{ offScreenDesc.Width / 2, 0, offScreenDesc.Width, offScreenDesc.Height / 2};
	//	//这里直接用rect来动copy的范围，而不搞下面copy部分的动作.
	//	// 于是 / 2可以换成参数
	hr = offscreenSurface->LockRect(&lockedRect, NULL, NULL);
	if (FAILED(hr)) {
		Log::log("ERROR: failed to LockRect in CopyD3DFrame\n");
		return false;
	}
	Log::log("\tLock Rect in CopyingFrame.\n");
	char *src = (char*)lockedRect.pBits;
	int stride = encoder_width << 2;//rgba
	Buffer* desBuffer = get_first_empty_buffer();
	Log::log("\tstride(srcRect): %d, pitch: %d\n", stride, lockedRect.Pitch);
	//TODO: this may be implemented multi-threadly
	for (int i = 0; i < game_height; i++) {
		src += stride;
		desBuffer->write_byte_arr(src, stride);
		src += stride;
	}
	Log::log("\tWrite rect to buffer end in CopyingFrame.\n");
	//Do release works
	offscreenSurface->Release();
	if (first_used == -1) {
		first_used = first_empty;
		used_cv.notify_one();
	}
	//TODO: some mutex may need to be locked here.
	first_empty = (first_empty + 1) % MAX_RGBBUFFER;
	Log::log_notime("\tCopy Frame End. Curframe: %d, cur first_empty: %d, cur first_used: %d\n", curfame.GetCurFrame(), first_empty, first_used);
	curfame.IncreaseCurFrame();
	return true;
}

H264Sink* H264Sink::createNew(UsageEnvironment& env, Groupsock* RTPgs, unsigned char rtpPayloadFormat, 
	u_int8_t const* sps, unsigned spsSize, 
	u_int8_t const* pps, unsigned ppsSize)
{
	Log::log("H264Sink::createNew() called...\n");
	return new H264Sink(env, RTPgs, rtpPayloadFormat, sps, spsSize, pps, ppsSize);
}

H264Sink::H264Sink(UsageEnvironment& env, Groupsock* RTPgs, unsigned char rtpPayloadFormat, 
	u_int8_t const* sps /*= NULL*/, unsigned spsSize /*= 0*/, 
	u_int8_t const* pps /*= NULL*/, unsigned ppsSize /*= 0*/):
	H264VideoRTPSink(env, RTPgs, rtpPayloadFormat, sps, spsSize, pps, ppsSize)
{

}

H264Sink::~H264Sink()
{

}



