#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <cstdint>

template<class T,uint16_t BufferSize>
class BKE_ThreadSafeQueue;

typedef unsigned long ulong;

struct AVStream;
struct AVFrame;
struct SwsContext;
class CompleteVideoFrame{
	CompleteVideoFrame(const CompleteVideoFrame &){}
	const CompleteVideoFrame &operator=(const CompleteVideoFrame &){ return *this; }
public:
	struct MyPicture
	{
		uint8_t *data[8];
		int linesize[8];
	};
	uint64_t repeat;
	double pts;
	MyPicture picture;
	CompleteVideoFrame(AVStream *videoStream, AVFrame *videoFrame, SwsContext *img_convert_ctx, double pts);
	~CompleteVideoFrame()
	{
		free(picture.data[0]);
	}
};

struct cmp_pCompleteVideoFrame{
	bool operator()(CompleteVideoFrame * const &A, CompleteVideoFrame * const &B){
		return A->pts > B->pts;
	}
};

typedef struct{
	bool(*write)(const void *src, ulong length, ulong channels, ulong frequency, void *);
	double(*get_time_offset)(void *);
	void(*wait)(void *, bool);
} audio_f;

typedef struct{
	void (*begin)(int width, int height, float fps);
	void(*begindraw)(BKE_ThreadSafeQueue<CompleteVideoFrame *, 5> *frameQueue, volatile ulong *global_time, BKE_ThreadSafeQueue<CompleteVideoFrame *,100> *deleteQueue);
	void(*enddraw)();
} video_f;

typedef void(*log_f)(const wchar_t *);

typedef void(*play_video_f)(
	const char *file,
	volatile bool *stop,
	volatile bool *pause,
	audio_f audio_output,
	video_f video_output,
	log_f error_output,
	void *user_data);


#endif