#include "rtsp_client.h"
#include "game_client.h"
#include <cstdlib>
#include <sstream>
#include <string.h>
using namespace std;

//outer variables
extern CommandClient cc;
extern uint8_t* decode_extradata;
extern int decode_extradatasize;
extern uint8_t *sps, *pps;
extern int spslen, ppslen;
//settings
int rtp_packet_reordering_threshold = 300000;//300ms
int server_port = 8554;
AVPixelFormat outFormat = AVPixelFormat::AV_PIX_FMT_BGR0;
Uint32 sdlFormat = SDL_PIXELFORMAT_ARGB8888;
AVHWDeviceType hwtype = AVHWDeviceType::AV_HWDEVICE_TYPE_DXVA2;
AVPixelFormat hwfmt = AVPixelFormat::AV_PIX_FMT_DXVA2_VLD;
//global variables
char eventLoopWatchVariable = 1;
VDecoder_H264* g_decoder = nullptr;

uint8_t* decoded_pool[MAX_DECODE_POOL];
mutex pool_in_mutex, pool_out_mutex;
int pool_first_empty = 0, pool_first_used = -1;
condition_variable pool_in_cv, pool_out_cv;

bool sdl_inited = false;
SDL_Window* pSDLWindow = NULL;
SDL_Renderer* sdlRenderer;
SDL_Surface* sdlSurface;
SDL_Texture* sdlTexture;

CurFrame curframe;

	
uint8_t* get_pool_empty_buffer()
{
	Log::log("Try Get first empty decoded pool buffer during frame: %d. cur pool_first_empty: %d, cur pool_first_used: %d\n", 
		curframe.GetCurFrame(), pool_first_empty, pool_first_used);
	uint8_t* result = nullptr;
	unique_lock<mutex> lock(pool_in_mutex);
	while (pool_first_empty == pool_first_used) {
		pool_in_cv.wait(lock);
	}
	Log::log("Get first empty decoded pool buffer during frame: %d. idx: %d\n", curframe.GetCurFrame(), pool_first_empty);
	result = decoded_pool[pool_first_empty];
	return result;
}

uint8_t* get_pool_used_buffer()
{
	Log::log("Try Get first used decoded pool buffer during frame: %d. cur pool_first_empty: %d, cur pool_first_used: %d\n",
		curframe.GetCurFrame(), pool_first_empty, pool_first_used);
	uint8_t* result = nullptr;
	unique_lock<mutex> lock(pool_out_mutex);
	while (pool_first_used < 0) {
		pool_out_cv.wait(lock);
	}
	Log::log("Get first used decoded pool buffer during frame: %d. idx: %d\n", curframe.GetCurFrame(), pool_first_used);
	result = decoded_pool[pool_first_used];
	return result;
}

bool initSDL() {
	if (!sdl_inited) {
		Log::log("Start initing SDL......\n");
		if (!sdlRenderer) {
			sdlRenderer = SDL_CreateRenderer(pSDLWindow, -1, 0);
		}
		if (!sdlRenderer) {
			Log::log("Failed to create sdlRenderer.\n");
			sdl_inited = false;
		}
		else {
			sdl_inited = true;
		}
		Log::log("initSDL() End. success: %d\n", sdl_inited);
	}
	return sdl_inited;
}

LRRTSPClient* openURL(UsageEnvironment& env, char const* progName, char const* rtspURL) {
	Log::log("Start opening URL: %s\n", rtspURL);
	LRRTSPClient* rtspClient = LRRTSPClient::createNew(env, rtspURL, 1, progName);
	stringstream ss;
	if (rtspClient == NULL) {
		ss << "Failed to create a RTSP client for URL \"" << rtspURL << "\": " << env.getResultMsg() << "\n";
		Log::log(ss.str().c_str());
		return NULL;
	}

	rtspClient->sendDescribeCommand(continueAfterDESCRIBE);
	return rtspClient;
}

int client_rtsp_thread_main()
{
	Log::log("client_rtsp_thread_main() start...\n");
	//initSDL();
	if (g_decoder == nullptr) {
		Log::log("g_decoder is nullptr. static CreateNew() called\n");
		g_decoder = VDecoder_H264::createNew();
	}
	BasicTaskScheduler0* scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);

	string server_url;
	stringstream ss;
	ss << "rtsp://" << cc.get_config()->srv_ip_ << ":" << server_port << "/StreamName";
	server_url  = ss.str();

	LRRTSPClient* client = openURL(*env, "", server_url.c_str());
	Log::log("OpenURL end.\n");
	//env->taskScheduler().doEventLoop(&eventLoopWatchVariable);

	int count = 0;
	int count_step = 60;
	while (eventLoopWatchVariable) {
		scheduler->SingleStep(1000000);
		if (count % count_step == 0) {
			Log::log("thread rtsp_client: SingleStep for %d times. \n", count);
		}
		count = (count + 1) % (1 << 30);
	}
	Log::log("Start shutting down.");
	//TODO: some release works

	shutdownStream(client);
	Log::log("rtsp_thread terminated.");
	return 0;
}


HRESULT merge_Present(IDirect3DDevice9* pDevice)
{
	Log::log("Start presenting......\n");
	//Finished：copy out the rendered part
	if (!sdl_inited) {
		Log::log("SDL renderer not inited. start initing.\n");
		if (!initSDL()) {
			Log::log("Failed to initSDL().");
			return -1;
		}
	}
	HRESULT hr;
	IDirect3DSurface9* renderingSurface;
	pDevice->GetRenderTarget(0, &renderingSurface);
	D3DSURFACE_DESC desc;
	renderingSurface->GetDesc(&desc);
	static IDirect3DSurface9* tempSurface = NULL;
	if (tempSurface == NULL) {
		hr = pDevice->CreateOffscreenPlainSurface(desc.Width, desc.Height,
			desc.Format, D3DPOOL::D3DPOOL_SYSTEMMEM, &tempSurface, NULL);
		if (FAILED(hr)) {
			Log::log("Error: Failed to create tempSurface\n");
			return -1;
		}
	}
	hr = pDevice->GetRenderTargetData(renderingSurface, tempSurface);
	if (FAILED(hr)) {
		Log::log("Error: Failed to get render target Data\n");
		return -1;
	}
	D3DLOCKED_RECT d3dRect;
	tempSurface->LockRect(&d3dRect, NULL, NULL);
	uint8_t* client_src = (uint8_t*)d3dRect.pBits;
	Log::log("get rendered part end.rectPitch:%d\n", d3dRect.Pitch);

	uint8_t* server_src = get_pool_used_buffer();//拿到服务端的已解码完的部分

	//TODO：combine with the left part
	Log::log("Start Dealing With render texture......\n");
	sdlTexture = SDL_CreateTexture(sdlRenderer, sdlFormat,//use this format to make sure the color is rightu
		SDL_TEXTUREACCESS_STREAMING, desc.Width/* * 2*/, desc.Height);
	if (!sdlTexture) {
		Log::log("Failed to create sdlTexture\n");
	}
	//SDL_SetTextureBlendMode(sdlTexture, SDL_BlendMode::SDL_BLENDMODE_BLEND);
	uint8_t* pSDLData;
	int sdlPitch;

	SDL_LockTexture(sdlTexture, NULL, (void**)&pSDLData, &sdlPitch);
	Log::log("LockTexture end. sdlPitch:%d, d3dRect.Pitch:%d\n", sdlPitch, d3dRect.Pitch);
	for (int i = 0; i < desc.Height; i++) {
		memcpy(pSDLData, client_src, d3dRect.Pitch / 2);
		pSDLData += d3dRect.Pitch / 2;
		memcpy(pSDLData, server_src, d3dRect.Pitch / 2);
		pSDLData += d3dRect.Pitch / 2;
		server_src += d3dRect.Pitch / 2;//server端拷贝的时候就已经是只有一半了
		client_src += d3dRect.Pitch;
	}
	SDL_UnlockTexture(sdlTexture);
	tempSurface->UnlockRect();

	//Log::log("Deal with render texture end.\n");

	//TODO：display using SDL2
	//hr = pDevice->Present(NULL, NULL, NULL, NULL);
	hr = SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
	if (FAILED(hr)) {
		Log::log("Error: Failed to RenderCopy\n");
		return hr;
	}
	SDL_RenderPresent(sdlRenderer);
	SDL_DestroyTexture(sdlTexture);
	sdlTexture = NULL;
	Log::log("SDL present end.\n");
	//Deal with used pool buffer
	pool_first_used = (pool_first_used + 1) % MAX_DECODE_POOL;
	if (pool_first_used == pool_first_empty) {
		pool_first_used = -1;
	}

	Log::log("displayPresented End. cur pool_first_used: %d. cur_frame: %d\n", pool_first_used, curframe.GetCurFrame());
	curframe.IncreaseCurFrame();
	return hr;
}

StreamClientState::StreamClientState()
	:iter(NULL), session(NULL), subsession(NULL), streamTimerTask(NULL), duration(0.0)
{
	//nothing to do here
}

StreamClientState::~StreamClientState()
{
	delete iter;
	if (session != NULL) {
		UsageEnvironment& env = session->envir();
		env.taskScheduler().unscheduleDelayedTask(streamTimerTask);
		Medium::close(session);
	}
}

DummySink* DummySink::createNew(UsageEnvironment& env, MediaSubsession& subsession, /* identifies the kind of data that's being received */ char const* streamId /*= NULL*/)
{
	return new DummySink(env, subsession, streamId);
}

DummySink::DummySink(UsageEnvironment& env, MediaSubsession& subsession, char const* streamId)
	: MediaSink(env), fSubsession(subsession), fStreamId(streamId)
{
	//FIXME: 同server，虽然感觉有些不对劲
	decoder = g_decoder;
	for (int i = 0; i < MAX_RECEIVE_BUFFER; i++) {
		fReceiveBuffer[i] = new uint8_t[RECEIVE_BUFFER_SIZE / 4];//new Buffer(RECEIVE_BUFFER_SIZE / 4);
	}
	Log::log("DummySink created.\n");
	//fReceiveBuffer = Buffer(RECEIVE_BUFFER_SIZE / 4 * MAX_RECEIVE_BUFFER);
}

DummySink::~DummySink()
{
	delete[] fReceiveBuffer;
}

void DummySink::afterGettingFrame(void* clientData, unsigned frameSize, unsigned numTruncatedBytes, struct timeval presentationTime, unsigned durationInMicroseconds)
{
	Log::log("static afterGettingFrame() called. frameSize: %d, numTruncatedBytes: %d\n", frameSize, numTruncatedBytes);
	DummySink* sink = (DummySink*)clientData;
	sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
}

void DummySink::afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes, struct timeval presentationTime, unsigned durationInMicroseconds)
{
	Log::log("DummySink::afterGettingFrame() called...\n");
	//not used now
	RTPSource *rtpsrc = fSubsession.rtpSource();
	RTPReceptionStatsDB::Iterator iter(rtpsrc->receptionStatsDB());
	RTPReceptionStats* stats = iter.next(True);

	//decode
	//uint8_t* decoded;

	bool ret = decoder->get_decoded(get_first_used_buffer(), frameSize, get_pool_empty_buffer());
	first_used = (first_used + 1) % MAX_RECEIVE_BUFFER;
	if (first_used == first_empty) {
		first_used = -1;//所有待编码的全部编完了。
	}
	empty_cv.notify_one();//send一个buffer之后必然空出来一个buffer

	//继续解码同一个packet中可能有的frame
	//TODO: 这里可以非阻塞式进行
	static int no_output_count = 0;
	int count = 0;
	while (ret) {
		count++;
		Log::log("decode one frame in after getting frame. idx: %d\n", count);
		if (pool_first_used == -1) {
			pool_first_used = pool_first_empty;
			pool_out_cv.notify_one();
		}
		pool_first_empty = (pool_first_empty + 1) % MAX_DECODE_POOL;
		ret = decoder->get_decoded(nullptr, 0, get_pool_empty_buffer());
	}
	if (!count)
	{
		no_output_count++;
		Log::log("no output in fterGettingFrame() count: %d, during frame %d\n",no_output_count, curframe.GetCurFrame());
	}

	//TODO: display works


	continuePlaying();
}

bool DummySink::continuePlaying()
{
	Log::log("DummySink::continuePlaying. \n");
	if (fSource == NULL) return False; // sanity check (should not happen)
	//get frame data from fSource
	uint8_t* buffer = get_first_empty_buffer();
	Log::log("fSource->fIsCurrentlyAwaitingData: %d\n", fSource->isCurrentlyAwaitingData());
	fSource->getNextFrame(buffer,
		RECEIVE_BUFFER_SIZE,
		afterGettingFrame, this, onSourceClosure, this);
	Log::log("fSource->getNextFrame. buffer: %p\n", buffer);
	if (first_used == -1) {
		first_used = first_empty;
		used_cv.notify_one();
	}
	first_empty = (first_empty + 1) % MAX_RECEIVE_BUFFER;
	Log::log("DummySink::continuePlaying end. \n");
	return true;
}

uint8_t* DummySink::get_first_used_buffer()
{
	Log::log("Try Get first used raw buffer during frame %d. cur first_empty:%d, cur first_used: %d\n", curframe.GetCurFrame(), first_empty, first_used);
	uint8_t* result = nullptr;
	unique_lock<mutex> lock(used_mutex);
	while (first_used < 0) {
		used_cv.wait(lock);
	}
	Log::log("Get first used raw buffer during frame %d. idx: %d\n", curframe.GetCurFrame(), first_used);
	result = fReceiveBuffer[first_used];
	return result;
}

uint8_t* DummySink::get_first_empty_buffer()
{
	Log::log("Try Get first empty raw buffer during frame %d. cur first_empty:%d, cur first_used: %d\n", curframe.GetCurFrame(), first_empty, first_used);
	uint8_t* result = nullptr;
	unique_lock<mutex> lock(empty_mutex);
	while (first_empty == first_used) {
		empty_cv.wait(lock);
	}
	Log::log("Get first empty raw buffer during frame %d. idx: %d\n", curframe.GetCurFrame(), first_empty);
	result = fReceiveBuffer[first_empty];
	return result;
}

LRRTSPClient* LRRTSPClient::createNew(UsageEnvironment& env, char const* rtspURL, 
	int verbosityLevel /*= 0 */, char const* applicationName /*= NULL */, 
	portNumBits tunnelOverHTTPPortNum /*= 0 */, int socketNumToServer /*= -1 */)
{
	return new LRRTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum, socketNumToServer);
}


LRRTSPClient::LRRTSPClient(UsageEnvironment& env, char const* rtspURL, 
	int verbosityLevel /*= 0 */, char const* applicationName /*= NULL */, 
	portNumBits tunnelOverHTTPPortNum /*= 0 */, int socketNumToServer /*= -1 */)
	:RTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum, -1)
{
}

bool LRRTSPClient::init_decoder()
{
	//TODO：Not used now
	return true;
}

LRRTSPClient::~LRRTSPClient()
{

}


StreamClientState& LRRTSPClient::getScs()
{
	return scs;
}

void continueAfterDESCRIBE(RTSPClient * rtspClient, int resultCode, char * resultString)
{
	Log::log("continueAfterDESCRIBE() called...\n");
	//TODO: error handling
	UsageEnvironment& env = rtspClient->envir(); // alias
	StreamClientState& scs = ((LRRTSPClient*)rtspClient)->getScs(); // alias
	if (resultCode != 0) {
		//stringstream ss;
		//ss << "Failed to get a SDP description: " << resultString << "\n";
		Log::log("Failed to get a SDP description: %s\n", resultString);
		return;
	}
	char* const sdpDesc = resultString;
	Log::log("Got SDP Description:\n");
	Log::log_notime(sdpDesc);
	scs.session = MediaSession::createNew(env, sdpDesc);
	delete[] sdpDesc;
	Log::log("scs.session: %p, is null: %d\n", scs.session, (scs.session == NULL));
	if (scs.session == NULL) {
		Log::log("Error: Failed to create a MediaSession object from the SDP description: %s\n", env.getResultMsg());
		eventLoopWatchVariable = 0;
		return;
	}
	else if (!scs.session->hasSubsessions()) {
		Log::log("Error: Session has no media subsessions\n");
		eventLoopWatchVariable = 0;
		return;
	}
	scs.iter = new MediaSubsessionIterator(*scs.session);
	Log::log("scs.iter: %p, is null: %d\n", scs.iter, (scs.iter == NULL));
	Log::log("continueAfterDESCRIBE() end. Call setup NextSubsession.\n");
	setupNextSubsession(rtspClient);
}

void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString)
{
	Log::log("continueAfterSETUP() called...\n");
	do {
		UsageEnvironment& env = rtspClient->envir(); // alias
		StreamClientState& scs = ((LRRTSPClient*)rtspClient)->getScs(); // alias
		if (resultCode != 0) {
			//stringstream ss;
			//ss << "Failed to get a SDP description: " << resultString << "\n";
			Log::log("Error: Failed to setup the [%s/%s] subsession on %s\n", scs.subsession->mediumName(), scs.subsession->codecName(), env.getResultMsg());
			break;
		}
		if (scs.subsession == NULL) {
			Log::log("something wrong with scs.subsession. scs.subsession == NULL: %d", (scs.subsession == NULL));
			break;
		}
		Log::log("Setup the subsesscion. codec:[%s/%s]. client port:[%d]\n", scs.subsession->mediumName(), scs.subsession->codecName(), scs.subsession->clientPortNum());

		scs.subsession->sink = DummySink::createNew(env, *scs.subsession, rtspClient->url());
		if (scs.subsession->sink == NULL) {
			Log::log("Error: Failed to create a sink for the subsession:[%s/%s]-%s", scs.subsession->mediumName(), scs.subsession->codecName(), env.getResultMsg());
			break;
		}
		Log::log("Created a sink for the subsession:[%s/%s]-%s\n", scs.subsession->mediumName(), scs.subsession->codecName(), env.getResultMsg());
		scs.subsession->miscPtr = rtspClient; // a hack to let subsession handle functions get the "RTSPClient" from the subsession 

		
		if (scs.subsession->rtcpInstance() != NULL) {
			scs.subsession->rtcpInstance()->setByeHandler(subsessionByeHandler, scs.subsession);
		}
		if (scs.subsession->rtpSource()) {//FIXME: something wrong. new_size is always 0 -> socketNum is -1! solve this
			int socketNum = scs.subsession->rtpSource()->RTPgs()->socketNum(); 
			Log::log("scs.subsession->rtpSource()->RTPgs(): %p, ->RTPgs()->socketNum(): [%d]\n", 
				scs.subsession->rtpSource()->RTPgs(), socketNum);
			unsigned int rcvBuffSize;
			int sizeSize = sizeof rcvBuffSize;
			getsockopt(socketNum, SOL_SOCKET, SO_RCVBUF, (char*)&rcvBuffSize, &sizeSize);
			Log::log("Start increasing ReceiveBuffer.cursize: %d(live555) / %d(socket)\n", getReceiveBufferSize(env, socketNum), rcvBuffSize);
			int new_size = increaseReceiveBufferTo(env, socketNum, RTP_RECEIVE_BUFFER_SIZE);
			Log::log("Receive buffer of socket[%d] increased to %d. request size: %d\n", socketNum, new_size, RTP_RECEIVE_BUFFER_SIZE);
		}
		//TODO: should this line put here?
		Log::log("scs.subsession->sink->startPlaying()\n");
		scs.subsession->sink->startPlaying(*(scs.subsession->readSource()), subsessionAfterPlaying, scs.subsession);

		//TODO：这里是不是也要照ga来一个nat hole punching？
	} while (0);
	Log::log("continueAfterSETUP() end.\n");
	setupNextSubsession(rtspClient);
}

void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString)
{
	Log::log("continueAfterPLAY() called.\n");
	UsageEnvironment& env = rtspClient->envir(); // alias
	StreamClientState& scs = ((LRRTSPClient*)rtspClient)->getScs(); // alias
	if (resultCode != 0) {
		Log::log("Error: Failed to start session", resultString);
		return;
	}
	//no need to deal with duration here?
	//TODO: 果然还是处理一下stream handler好一点
	Log::log("Started playing session for up to %.4f seconds\n", scs.duration);
	return;
}

void setupNextSubsession(RTSPClient* rtspClient)
{
	Log::log("Setting up next subsession.\n");
	UsageEnvironment& env = rtspClient->envir(); // alias
	StreamClientState& scs = ((LRRTSPClient*)rtspClient)->getScs(); // alias

	bool rtpOverTCP = true;
	if (scs.iter == NULL) {
		Log::log("Something wrong with scs.iter: %p, is null: %d.\nreturn from setupNextSubsession().\n", scs.iter, (scs.iter == NULL));
		return;
	}
	scs.subsession = scs.iter->next();
	if (scs.subsession != nullptr) {
		if (!scs.subsession->initiate()) {
			Log::log("Error: failed to initiate subsession. go to next one.");
			setupNextSubsession(rtspClient);//???
		}
		else {
			Log::log("Subsession not null.Now Log subsession info: \n\t codec:[%s/%s]. client port:[%d]. socketNum: [%d], rtpSource->RTPGs: %p\n", 
				scs.subsession->mediumName(), scs.subsession->codecName(), 
				scs.subsession->clientPortNum(), scs.subsession->rtpSource()->RTPgs()->socketNum(), scs.subsession->rtpSource()->RTPgs());

			int session_fmt = scs.subsession->rtpPayloadFormat();
			string codec_name(scs.subsession->codecName());
			//ga中设置了一个这个东西，暂时不明是否必要（rtp_packet_handler是一个自定义func）
			//scs.subsession->rtpSource()->setAuxilliaryReadHandler(rtp_packet_handler, NULL);
			scs.subsession->rtpSource()->setPacketReorderingThresholdTime(rtp_packet_reordering_threshold);
			//FIXME: 这里检查decoder似乎不需要
			/*if (decoder == nullptr) {
				if (!init_decoder()) {
					Log::log("Error: Failed to init_decoder in setupNextSubsession");
				}
			}*/
		}
		Log::log("rtspClient->sendSetupCommand: continueAfterSETUP\n");
		rtspClient->sendSetupCommand(*scs.subsession, continueAfterSETUP,
			False, rtpOverTCP, False, NULL);
		return;
	}
	//Log::log("scs.subsession->rtpSource(): %p, ->RTPgs()->socketNum(): [%d]\n", scs.subsession->rtpSource(), scs.subsession->rtpSource()->RTPgs()->socketNum());
	Log::log("rtspClient->sendPlayCommand: continueAfterPLAY\n");
	rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY);
}

void subsessionAfterPlaying(void* clientData)
{
	Log::log("subsessionAfterPlaying()called...\n");
	MediaSubsession* subsession = (MediaSubsession*)clientData;
	RTSPClient* rtspClient = (RTSPClient*)(subsession->miscPtr);

	// Begin by closing this subsession's stream:
	Medium::close(subsession->sink);
	subsession->sink = NULL;

	// Next, check whether *all* subsessions' streams have now been closed:
	MediaSession& session = subsession->parentSession();
	MediaSubsessionIterator iter(session);
	while ((subsession = iter.next()) != NULL) {
		if (subsession->sink != NULL) return; // this subsession is still active
	}

	// All subsessions' streams have now been closed, so shutdown the client:
	shutdownStream(rtspClient);
}

void subsessionByeHandler(void* clientData)
{
	Log::log("subsessionByeHandler() called...\n");
	MediaSubsession* subsession = (MediaSubsession*)clientData;
	RTSPClient* rtspClient = (RTSPClient*)subsession->miscPtr;
	StreamClientState scs = ((LRRTSPClient*)rtspClient)->getScs();
	UsageEnvironment& env = rtspClient->envir(); // alias

	Log::log("Received RTCP [BYE] on the [%s/%s] subsession on %s\n", scs.subsession->mediumName(), scs.subsession->codecName(), env.getResultMsg());

	// Now act as if the subsession had closed:
	subsessionAfterPlaying(subsession);
}

void shutdownStream(RTSPClient* rtspClient, int exitCode/* =1 */)
{
	//TODO: some release works
	Log::log("shutdownStream() called.\n");
}

VDecoder_H264* VDecoder_H264::createNew()
{
	return new VDecoder_H264();
}

void VDecoder_H264::SetDecodeExtraData(uint8_t* extradata, int extradatasize)
{
	
}

static enum AVPixelFormat get_hw_format(AVCodecContext* ctx,
	const enum AVPixelFormat* pix_fmts)
{
	/*Log::log("get_hw_format() called.\n");
	const enum AVPixelFormat* p;

	for (p = pix_fmts; *p != -1; p++) {
		if (*p == hw_pix_fmt)
			return *p;
	}

	Log::log("Failed to get HW surface format.\n");
	return AV_PIX_FMT_NONE;*/
	return hwfmt;
}
VDecoder_H264::VDecoder_H264()
{
	int ret;
	//init decoder pool buffer
	for (int i = 0; i < MAX_DECODE_POOL; i++) {
		decoded_pool[i] = new uint8_t[DECODE_POOL_SIZE];
	}
	Log::log("VDecoder_H264::VDecoder_H264() called....\n");
	avcodec_register_all();
	codec = avcodec_find_decoder(AVCodecID::AV_CODEC_ID_H264);
	if (!codec) {
		Log::log("Error: failed to find h264 codec");
	}
	Log::log("VDecoder_H264::VDecoder_H264() codec init end.\n");
	
	
	codecCtx = avcodec_alloc_context3(codec);
	if (!codec) {
		Log::log("Error: failed to alloc context3");
	}
	if (cc.get_config()->use_hw_) {
		codecCtx->get_format = get_hw_format;
		Log::log("VDecoder_H264::VDecoder_H264() start creating hwctx.\n");
		ret = av_hwdevice_ctx_create(&hw_device_ctx, hwtype, nullptr, nullptr, 0);
		if (ret < 0) {
			Log::log("Error: failed to create hwdevice_ctx.\n");
		}
		Log::log("VDecoder_H264::VDecoder_H264() hwdevice_ctx create end.\n");
		/*for (int i = 0; ; i++) { // find hw_pix_fmt
			const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
			if (!config) {
				Log::log("Decoder %s does not support device type %s.\n",
					codec->name, av_hwdevice_get_type_name(hwtype));
				ret = -1; break;
			}
			if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
				config->device_type == hwtype) {
				hwfmt = config->pix_fmt;
				Log::log("hw_pix_fmt: %d, AV_PIX_FMT_YUV420P: %d\n", hwfmt, AVPixelFormat::AV_PIX_FMT_DXVA2_VLD);
				ret = 0; break;
			}
		}*/
		codecCtx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
	}
	else {
		hw_device_ctx = nullptr;
	}
	codecCtx->width = decoder_width;
	codecCtx->height = decoder_height;
	codecCtx->time_base.num = 1;
	codecCtx->time_base.den = cc.get_config()->max_fps_;//TODO: fps setting to be improved
	codecCtx->pix_fmt = AVPixelFormat::AV_PIX_FMT_YUV420P;
	codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;//Place global headers in extradata instead of every keyframe.
	//codecCtx->gop_size = 1;
	codecCtx->extradata_size = decode_extradatasize;
	codecCtx->extradata = (uint8_t*)av_malloc(decode_extradatasize + AV_INPUT_BUFFER_PADDING_SIZE);
	memcpy(codecCtx->extradata, decode_extradata, decode_extradatasize);//WARNING: make sure decode_extradata is inited!
	ret = avcodec_open2(codecCtx, codec, NULL);
	Log::log("VDecoder_H264::VDecoder_H264() codecCtx init end.\n");
	//frame = av_frame_alloc();
	av_init_packet(&avpkt);
	Log::log("VDecoder_H264::VDecoder_H264() avpkt init end.\n");

	swsCtx = nullptr;
	swsCtx = sws_getContext(
		decoder_width, decoder_height,
		(cc.get_config()->use_hw_)?
			hwfmt : AVPixelFormat::AV_PIX_FMT_YUV420P,
		game_width, game_height,
		outFormat,//TODO:the format here!
		SWS_BICUBIC,
		NULL, NULL, NULL);
	if (swsCtx == nullptr) {
		Log::log("Error: failed to get swsCtx.\n");
	}
	else {
		Log::log("VDecoder_H264::VDecoder_H264() swsCtx init end.\n");
	}
	Log::log("VDecoder_H264::VDecoder_H264() end.\n");
}

VDecoder_H264::~VDecoder_H264()
{
	avcodec_free_context(&codecCtx);
	sws_freeContext(swsCtx);
	av_buffer_unref(&hw_device_ctx);
	//av_frame_free(&frame);
}

bool VDecoder_H264::get_decoded(uint8_t* srcBuffer, int inbuffer_size,  /* in */
	uint8_t* outBuffer) /* out */
{
	/*AVPacket* avpkt = new AVPacket();
	av_init_packet(avpkt);*/
	//TODO: decode a frame
	Log::log("Start decoding a frame...\n");
	Log::log("avpkt: %p, avpkt.data: %p, srcBuffer: %p \n",&avpkt, avpkt.data, srcBuffer);
	int ret;
	AVFrame* yuvFrame_0 = av_frame_alloc(); //cpu frame is not using hardware, otherwise gpu frame
	AVFrame* yuvFrame_1 = av_frame_alloc(); //if use hardware, this is cpu frame
	AVFrame* rgbFrame = av_frame_alloc();
	AVFrame* yuvFrame;
	do 
	{
		if (srcBuffer != nullptr && inbuffer_size > 0) {
			avpkt.data = srcBuffer;
			avpkt.size = inbuffer_size;

			ret = avcodec_send_packet(codecCtx, &avpkt);
			if (ret) {
				Log::log("Error: failed to send_packet in decodeVideo: %d\n", ret);
				ret = -1; break;
			}
			Log::log("send packet end. ret: %d\n", ret);
		}
		else {
			Log::log("no packet to send... receive frame directly.\n");
		}

		ret = avcodec_receive_frame(codecCtx, yuvFrame_0);
		if (ret) {
			Log::log("Error: failed to receive_frame in decodeVideo: %d\n", ret);
			ret = -1; break;
		}
		Log::log("receive frame end. ret: %d, yuvFrame height: %d, yuvFrame width: %d, yuvFrame data: %p, yuvFrame linesize: %d\n",
			ret, yuvFrame_0->height, yuvFrame_0->width, yuvFrame_0->data, yuvFrame_0->linesize);
		if (hw_device_ctx) {
			ret = av_hwframe_transfer_data(yuvFrame_1, yuvFrame_0, 0);
			if (ret < 0) {
				Log::log("Error: failed to transferring the data to system memory，ret: %d\n", ret);
				break;
			}
			Log::log("hwframe_transfer_data end. ret: %d, yuvFrame height: %d, yuvFrame width: %d, yuvFrame data: %p, yuvFrame linesize: %d\n",
				ret, yuvFrame_1->height, yuvFrame_1->width, yuvFrame_1->data, yuvFrame_1->linesize);
			yuvFrame = yuvFrame_1;//这里之后可以改成加设置开关
		}
		else {
			yuvFrame = yuvFrame_0;
		}

		//此处暂时和server的encoder相同
		rgbFrame->format = outFormat;
		rgbFrame->width = decoder_width;
		rgbFrame->height = decoder_height;
		ret = av_frame_get_buffer(rgbFrame, 0);
		if (ret < 0) {
			Log::log("Error: failed to av_frame_get_buffer(rgbFrame1, 0).\n");
			break;
		}
		Log::log("decoder_height: %d, decoder_width: %d\n", decoder_height, decoder_width);
		ret = sws_scale(swsCtx, yuvFrame->data, yuvFrame->linesize, 0, yuvFrame->height,
			rgbFrame->data, rgbFrame->linesize);
		if (ret <= 0) {
			Log::log("Error: failed to sws_scale in decodeVideo\n");
			ret = -1; break;
		}
		Log::log("sws_scale end. ret(slice height):%d\n", ret);
		/*if (codecCtx->pix_fmt == AVPixelFormat::AV_PIX_FMT_YUV420P) {

		}*/
		memcpy(outBuffer, rgbFrame->data[0], (decoder_width * decoder_height) << 2);
		//outBuffer = rgbFrame->data[0];//因为是rgb所以直接用[0]。。。？
		Log::log("Decoding a frame success. do release work\n");
		ret = 0;
	} while (false);
	
	av_frame_free(&rgbFrame);
	av_frame_free(&yuvFrame_0);
	av_frame_free(&yuvFrame_1);
	Log::log("Decode release work end.\n");
	if (ret == 0)
		return true;
	else
		return false;
}