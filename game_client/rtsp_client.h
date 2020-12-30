#pragma once
#include "game_client.h"
#include "utility.h"
#include "CurFrame.h"

#include "SDL.h"

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>

extern "C" {
	#include "libavformat/avformat.h"
	#include "libswscale/swscale.h"
}
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
using namespace std;

#define MAX_RECEIVE_BUFFER 8
#define MAX_DECODE_POOL 20
#define DECODE_POOL_SIZE 2000000
#define RECEIVE_BUFFER_SIZE 8000000 //need to devide by 4 for the Buffer class
#define RTP_RECEIVE_BUFFER_SIZE 2097152 //2 * 1024 * 1024

class VDecoder_H264;

//extern SDL_Window* pSDLWindow;
extern SDL_Window* pSDLWindow;
extern VDecoder_H264* g_decoder;
extern int game_width;
extern int game_height;
extern int decoder_width, decoder_height;

//uint8_t* get_pool_empty_buffer();
//uint8_t* get_pool_used_buffer();

//应当在fakedpresent中来调用，替换原本的present
HRESULT displayPresented(IDirect3DDevice9* pDevice);

int client_rtsp_thread_main();

class VDecoder_H264 {
public:
	static VDecoder_H264* VDecoder_H264::createNew();
	void SetDecodeExtraData(uint8_t* extradata, int extradatasize);
protected:
	VDecoder_H264();
	~VDecoder_H264();
public:
	bool decodeVideo(uint8_t* /*in*/ srcBuffer, int inbuffer_size, uint8_t* outBuffer);
		//因为填进buffer的时候没有调用buffer的write而是直接传了指针
private:
	AVCodecContext* codecCtx;
	AVCodec* codec;
	AVPacket avpkt;
	SwsContext* swsCtx;

};

class StreamClientState {
public:
	StreamClientState();
	virtual ~StreamClientState();

public:
	MediaSubsessionIterator* iter;
	MediaSession* session;
	MediaSubsession* subsession;
	TaskToken streamTimerTask;
	double duration;
};

class DummySink : public MediaSink {
public:
	static DummySink* createNew(UsageEnvironment& env,
		MediaSubsession& subsession, // identifies the kind of data that's being received
		char const* streamId = NULL); // identifies the stream itself (optional)
	~DummySink();
private:
	DummySink(UsageEnvironment& env, MediaSubsession& subsession, char const* streamId);
	// called only by "createNew()"
	//virtual ~DummySink(); seem no need

	static void afterGettingFrame(void* clientData, unsigned frameSize,
		unsigned numTruncatedBytes,
		struct timeval presentationTime,
		unsigned durationInMicroseconds);
	void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
		struct timeval presentationTime, unsigned durationInMicroseconds);

private:
	// redefined virtual functions:
	virtual Boolean continuePlaying();
	uint8_t* get_first_used_buffer();
	uint8_t* get_first_empty_buffer();

private:
	VDecoder_H264* decoder;
	uint8_t* fReceiveBuffer[MAX_RECEIVE_BUFFER];
	int first_empty = 0;
	int first_used = -1;
	mutex empty_mutex;
	condition_variable empty_cv;
	mutex used_mutex;
	condition_variable used_cv;
	//TODO: decode packets in another thread
	//AVPacket* 

	string fStreamId;
	MediaSubsession& fSubsession;
};

class LRRTSPClient : public RTSPClient {
public:
	static LRRTSPClient* createNew(UsageEnvironment& env,
		char const* rtspURL, int verbosityLevel  = 0 ,
		char const* applicationName  = NULL ,
		portNumBits tunnelOverHTTPPortNum  = 0 , int socketNumToServer  = -1 );
	StreamClientState& getScs();
	//Fixme: 这里的生命周期不知道会不会出问题
protected:
	LRRTSPClient(UsageEnvironment& env,
		char const* rtspURL, int verbosityLevel  = 0 ,
		char const* applicationName  = NULL ,
		portNumBits tunnelOverHTTPPortNum  = 0 , int socketNumToServer  = -1 );
	~LRRTSPClient();
	bool init_decoder();
private:
	StreamClientState scs;
};
void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString);
void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString);
void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString);
void setupNextSubsession(RTSPClient* rtspClient);
void subsessionAfterPlaying(void* clientData);
void subsessionByeHandler(void* clientData);
void shutdownStream(RTSPClient* rtspClient, int exitCode = 1);