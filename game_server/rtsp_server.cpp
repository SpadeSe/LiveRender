#include "rtsp_server.h"

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
const int video_bitrate = 3000000; //bit. used for estBitrate
const int max_out_packet_size = 2000000; //8000000;//used when create new stream source
const int rgb_buffer_size = 8000000; //bit. the size of rgb_buffer. remember to divide by 4. Too small will lead to exception in copy
AVPixelFormat outFormat = AVPixelFormat::AV_PIX_FMT_BGR0;

CurFrame curfame;

//the main func of the liveserver thread.
void liveserver_main(){
	//create encoder first
	Log::log("thread live server_main started.\n");
	g_encoder = VEncoder_h264::createNew();
	g_encoder->getExtraData(&encode_extradata, &encode_extradatasize);
	//g_encoder->getExtraData(sps, &spslen, pps, &ppslen);
	Log::log("Copy ExtraData end.\n");

	TaskScheduler* scheduler = BasicTaskScheduler::createNew();
	//UserAuthenticationDatabase* authDB = NULL;//not used
	BasicUsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);

	//TODO：总之先固定使用8554
	RTSPServer* rtspServer = RTSPServer::createNew(*env, 8554);
	Log::log("Create New RtspServer over port %d\n", 8554);

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
	AVPacket* pkt = encoder->get_encoded();
	fFrameSize = pkt->size;
	Log::log("FSource get frame size: %d\n", fFrameSize);
	memmove(fTo, pkt->data, fFrameSize);

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
	RTPSink* result = H264Sink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
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
	Log::log("MSubsession::startStream() called...\n");
	OnDemandServerMediaSubsession::startStream(clientSessionId, streamToken, rtcpRRHandler, serverRequestAlternativeByteHandlerClientData, rtpSeqNum, rtpTimestamp, serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);
}

VEncoder_h264* VEncoder_h264::createNew()
{
	return new VEncoder_h264();
}

VEncoder_h264::VEncoder_h264()
{
	Log::log("VEncoder_h264::VEncoder_h264() called...\n");
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
	codecCtx->width = encoder_width;
	codecCtx->height = encoder_height;
	codecCtx->time_base.num = 1;
	codecCtx->time_base.den = cs.config_->max_fps_;//TODO: fps setting to be improved
	codecCtx->pix_fmt = AVPixelFormat::AV_PIX_FMT_YUV420P;
	codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;//Place global headers in extradata instead of every keyframe.
	//codecCtx->rc_buffer_size = BUFFER_SIZE;
	//codecCtx->bit_rate = video_bitrate;
	//codecCtx->bit_rate_tolerance = video_bitrate * 2;
	Log::log("encoder gop size: %d\n", cs.config_->encoder_gop_size_);
	codecCtx->gop_size = cs.config_->encoder_gop_size_;
	
	int ret = avcodec_open2(codecCtx, codec, NULL);
	if (ret < 0) {
		Log::log("Error: failed to avcodec_open2 in init VEncoder_h264.ret=%d\n", &ret);
	}
	Log::log("codecCtx init end.\n");
	swsCtx = nullptr;
	swsCtx = sws_getCachedContext(swsCtx,
		game_width, game_height,
		outFormat,//the format here!
		encoder_width, encoder_height,
		AVPixelFormat::AV_PIX_FMT_YUV420P,
		SWS_BICUBIC,
		NULL, NULL, NULL);
	//get sps & pps 
	uint8_t * extradata = codecCtx->extradata;
	int extradatalen = codecCtx->extradata_size;
	Log::log("codecCtx->extradata_size: %d\n", extradatalen);
	spspps = new spsppsdata{};
	for (int i = 2; i < extradatalen; i++) {//因为不会很长而且只过一次，所以这样问题不大又简单
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
	//init packet
	av_init_packet(&encoded_packet);
	Log::log("av_init_packet(&encoded_packet) end.\n");
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
	delete spspps;
	Log::log("~VEncoder_h264() end.\n");
}

Buffer* VEncoder_h264::get_first_empty_buffer()
{
	Log::log("Try Get first empty buffer during frame %d. cur first_empty: %d, cur first_used: %d\n", curfame.GetCurFrame(), first_empty, first_used);
	Buffer* result = nullptr;
	unique_lock<mutex> lock(empty_mutex);
	while (first_empty == first_used) {
		empty_cv.wait(lock);
	}
	result = buffers[first_empty];
	Log::log("Get first empty buffer during frame %d. idx: %d\n", curfame.GetCurFrame(), first_empty);
	return result;
}


Buffer* VEncoder_h264::get_first_used_buffer()
{
	//TODO: deal with no first_used situation;
	Log::log("Try Get first used buffer during frame %d. cur first_empty: %d, cur first_used: %d\n", curfame.GetCurFrame(), first_empty, first_used);
	Buffer* result = nullptr;
	unique_lock<mutex> lock(used_mutex);
	while (first_used < 0) {
		used_cv.wait(lock);
	}
	result = buffers[first_used];
	Log::log("Get first used buffer during frame %d. idx: %d\n", curfame.GetCurFrame(), first_used);
	return result;
}

AVPacket* VEncoder_h264::get_encoded()
{
	Log::log("VEncoder_h264::get_encoded()...\n");
	Log::log("VEncoder_h264: codec extradatalen:%d\n", codecCtx->extradata_size);
	Buffer* srcBuffer = get_first_used_buffer();

	AVFrame* yuvFrame = av_frame_alloc();
	yuvFrame->format = AVPixelFormat::AV_PIX_FMT_YUV420P;
	yuvFrame->width = encoder_width;
	yuvFrame->height = encoder_height;
	int ret = av_frame_get_buffer(yuvFrame, 0);
	//TODO: error process
	if (ret) {
		Log::log("Error: failed to av_frame_get_buffer in get_encoded()\n");
	}
	Log::log("av_frame_get_buffer in get_encoded() end.\n");
	AVFrame* rgbFrame = av_frame_alloc();
	ret = av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, (uint8_t *)srcBuffer->get_data_start(), 
		outFormat, encoder_width, encoder_height, 1);//align for what?
	/*ret = avpicture_fill((AVPicture *)rgbFrame, (uint8_t *)srcBuffer->get_data_start(), AV_PIX_FMT_ARGB,
		game_width, game_height);*/
	if (ret < 0) {
		Log::log("Error: failed to av_image_fill_arrays in get_encoded()\n");
	}
	else {
		Log::log("the size in bytes required for src: %d\n", ret);
	}
	ret = sws_scale(swsCtx, rgbFrame->data, rgbFrame->linesize, 0, game_height,
		yuvFrame->data, yuvFrame->linesize);
	Log::log("the height of the output slice(sws_scale): %d\n", ret);

	av_init_packet(&encoded_packet);
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
	}
	else {
		Log::log("avcodec_send_frame() success.\n");
	}
	ret = avcodec_receive_packet(codecCtx, &encoded_packet);
	if (ret) {
		Log::log("Error: failed to receive_packet in get_encoded(): %d\n", ret);
	}
	else {
		Log::log("avcodec_receive_packet success.\n");
	}
	//Log::log("encoded_packet size:%d\n", encoded_packet.size);
	srcBuffer->clear();
	first_used = (first_used + 1) % MAX_RGBBUFFER;
	if (first_used == first_empty) {
		first_used = -1;//所有待编码的全部编完了。
	}
	empty_cv.notify_one();//编码完一个buffer之后必然空出来一个buffer
	Log::log("VEncoder_h264 encode end. cur_frame: %d, cur first_empty: %d, cur first_used: %d\n", curfame.GetCurFrame(), first_empty, first_used);
	av_frame_free(&rgbFrame);
	av_frame_free(&yuvFrame);
	return &encoded_packet;
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
	Log::log_notime("\tLock Rect.\n");
	char *src = (char*)lockedRect.pBits;
	int stride = encoder_width << 2;//rgba
	Buffer* desBuffer = get_first_empty_buffer();
	Log::log_notime("\tstride(srcRect): %d, pitch: %d\n", stride, lockedRect.Pitch);
	//TODO: this may be implemented multi-threadly
	for (int i = 0; i < game_height; i++) {
		src += stride;
		desBuffer->write_byte_arr(src, stride);
		src += stride;
	}
	Log::log_notime("\tWrite rect to buffer end.\n");
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
