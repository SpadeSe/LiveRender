#include "game_server.h"
#include "utility.h"
#include "CurFrame.h"

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>

extern "C" {
	//#include "x264.h"
	#include "libavcodec/avcodec.h"
	#include "libavformat/avformat.h"
	#include "libavformat/avio.h"
	//#include "libavutil/avutil.h"
	#include "libavutil/imgutils.h"
	#include "libswscale/swscale.h"
	#include "libavutil/opt.h"
}
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
using namespace std;

//settings
#define MAX_RGBBUFFER 4
#define MAX_ENCODEDBUFFER 8
#define BUFFER_SIZE 625000
//in wrap_direct3d9, inited in createDevice
extern int game_width, game_height;
extern int encoder_width, encoder_height;
//图方便所以只用一个video channel，所以就不要cid了。
//线程主函数
void liveserver_main();

class DebugTaskScheduler : public BasicTaskScheduler {
public:
	static DebugTaskScheduler* createNew(unsigned maxSchedulerGranularity  = 10000 );
	~DebugTaskScheduler();
	virtual void doEventLoop(char volatile* watchVariable);
	virtual void SingleStep(unsigned maxDelayTime);
	virtual void setBackgroundHandling(int socketNum, int conditionSet, BackgroundHandlerProc* handlerProc, void* clientData);
	virtual void InternalError();
protected:
	DebugTaskScheduler(unsigned maxSchedulerGranularity);
#if defined(__WIN32__) || defined(_WIN32)
	// Hack to work around a bug in Windows' "select()" implementation:
	int fDummySocketNum;
#endif
};

//class DebugRTSPServer : public RTSPServer {
//public:
//	RTSPServer* createNew(UsageEnvironment& env, Port ourPort  = 554 ,
//		UserAuthenticationDatabase* authDatabase  = NULL ,
//		unsigned reclamationSeconds  = 65 );
//	class DebugRTSPClientSession : RTSPServer::RTSPClientSession {
//	public:
//		virtual void handleCmd_PLAY(RTSPClientConnection* ourClientConnection, ServerMediaSubsession* subsession, char const* fullRequestStr);
//	};
//};

class VEncoder_h264{
public:
	struct spsppsdata {
		unsigned char* sps;
		int spslen;
		unsigned char* pps;
		int ppslen;
	};
	static VEncoder_h264* createNew();
	~VEncoder_h264();
	Buffer* get_first_empty_buffer();
	Buffer* get_first_used_buffer();
	int get_encoded(uint8_t* dst, unsigned* size);
	bool CopyD3D9RenderingFrame(IDirect3DDevice9* pDevice);
	//void StartEncoding();
	void getExtraData(uint8_t** outData, int* outDataSize);
	void getExtraData(uint8_t** outSps, int* outSpslen, uint8_t** outPps, int* outPpslen);
	spsppsdata* getSpsppsdata();
protected:
	VEncoder_h264();
	//void encoding_loop();
	uint8_t* findStartCode(uint8_t* data, int datalen);
private:
	Buffer* buffers[MAX_RGBBUFFER];
	//Buffer* encoded_buffers[MAX_ENCODEDBUFFER];
	//AVPacket encoded_packet;// s[MAX_RGBBUFFER];
	int first_empty = 0;
	int first_used = -1;
	mutex empty_mutex;
	condition_variable empty_cv;
	mutex used_mutex;
	condition_variable used_cv;

	AVCodec* codec;
	AVBufferRef* hw_device_ctx;
	AVCodecContext* codecCtx;
	SwsContext* swsCtx;
	spsppsdata* spspps;
};

class FSource: public FramedSource{
public: 
	static FSource* createNew(UsageEnvironment& env);
protected:
	FSource(UsageEnvironment& env);
	~FSource();
private:
	VEncoder_h264* encoder;
	void doGetNextFrame();
};

class H264Sink : public H264VideoRTPSink {
public:
	static H264Sink* createNew(UsageEnvironment& env, Groupsock* RTPgs, unsigned char rtpPayloadFormat,
		u_int8_t const* sps, unsigned spsSize, u_int8_t const* pps, unsigned ppsSize);
protected:
	H264Sink(UsageEnvironment& env, Groupsock* RTPgs, unsigned char rtpPayloadFormat,
		u_int8_t const* sps = NULL, unsigned spsSize = 0,
		u_int8_t const* pps = NULL, unsigned ppsSize = 0);
	~H264Sink();
};

class H264FileSink : public H264VideoFileSink {

};

class MSubsession: public OnDemandServerMediaSubsession{
public:
	static MSubsession* createNew(UsageEnvironment& env, 
		portNumBits initialPortNum = 6970,
		Boolean multiplexRTCPWithRTP = False);
protected:
	MSubsession(UsageEnvironment& env,
		portNumBits initialPortNum = 6970,
		Boolean multiplexRTCPWithRTP = False);
protected:
	virtual FramedSource* createNewStreamSource(unsigned clientSessionId,
		unsigned& estBitrate);
	// "estBitrate" is the stream's estimated bitrate, in kbps
	virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
		unsigned char rtpPayloadTypeIfDynamic,
		FramedSource* inputSource);
	virtual char const* sdpLines();
	virtual void getStreamParameters(unsigned clientSessionId,
		netAddressBits clientAddress,
		Port const& clientRTPPort,
		Port const& clientRTCPPort,
		int tcpSocketNum,
		unsigned char rtpChannelId,
		unsigned char rtcpChannelId,
		netAddressBits& destinationAddress,
		u_int8_t& destinationTTL,
		Boolean& isMulticast,
		Port& serverRTPPort,
		Port& serverRTCPPort,
		void*& streamToken);
	virtual void startStream(unsigned clientSessionId, void* streamToken, 
		TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData, unsigned short& rtpSeqNum, 
		unsigned& rtpTimestamp, ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler, 
		void* serverRequestAlternativeByteHandlerClientData);
};



