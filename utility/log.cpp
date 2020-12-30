#include "utility.h"
#include "log.h"
#include <time.h>
#include <stdlib.h>
#include <mutex>
#include <thread>
using namespace std;

char Log::fname_[100];
ofstream Log::fs_;
int Log::is_init_ = false;
mutex log_mutex;

time_t tv;
clock_t ct;

void Log::init(const char* fname) {
	if(is_init_) return;
	is_init_ = true;

	strcpy(fname_, fname);
	fs_.open(fname_, ios::out);
	log("Log::init(), file name=%s\n", fname_);
}

void Log::close() {
	log("Log::close() called\n");
	fs_.close();
}

#define ENABLE_LOG



void Log::log(const char* text, ...) {
#ifdef ENABLE_LOG
	log_mutex.lock();
	if(!is_init_) init("default.log");
	char buffer[MAX_CHAR];
	char timestr[30];

	tv = time(0);
	tm* ltime = localtime(&tv);
	strftime(timestr, sizeof(timestr), "%H:%M:%S", ltime);

	va_list ap;
	va_start(ap, text);
	vsprintf(buffer, text, ap);
	va_end(ap);

	//extended
	ct = clock();
	thread::id this_id = this_thread::get_id();
	unsigned tid = *(unsigned*)&this_id;

	fs_ << timestr << "(" << ct << ")[" << tid << "]: " << buffer;
	fs_.flush();
	log_mutex.unlock();
#endif
}



void Log::slog(const char* text, ...) {
	log_mutex.lock();
	if(!is_init_) init("sdefault.log");
	char buffer[MAX_CHAR];

	char timestr[30];

	tv = time(0);
	tm* ltime = localtime(&tv);
	strftime(timestr, sizeof(timestr), "%H:%M:%S", ltime);

	va_list ap;
	va_start(ap, text);
	vsprintf(buffer, text, ap);
	va_end(ap);

	fs_ << timestr << ": " << buffer;
	fs_.flush();
	log_mutex.unlock();
}

void Log::log_notime(const char* text, ...)
{
#ifdef ENABLE_LOG
	log_mutex.lock();
	if(!is_init_) init("default_notime.log");
	char buffer[MAX_CHAR];
	/*char timestr[30];

	tv = time(0);
	tm* ltime = localtime(&tv);
	strftime(timestr, sizeof(timestr), "%H:%M:%S", ltime);*/

	va_list ap;
	va_start(ap, text);
	vsprintf(buffer, text, ap);
	va_end(ap);

	fs_ /*<< timestr << ": " */<< buffer;
	fs_.flush();
	log_mutex.unlock();
#endif
}


