#pragma once
#include <mutex>
using namespace std;
class CurFrame {
public:
	CurFrame() = default;
	int GetCurFrame() {
		cur_frame_mutex.lock();
		int re = cur_frame;
		cur_frame_mutex.unlock();
		return re;
	}
	int IncreaseCurFrame() {
		cur_frame_mutex.lock();
		cur_frame = (cur_frame + 1) % (1 << 30);
		int re = cur_frame;
		cur_frame_mutex.unlock();
		return re;
	}
private:
	int cur_frame = 0;
	mutex cur_frame_mutex;
};