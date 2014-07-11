#include "Thread.h"
#include "BTL/ThreadSafeQueue.h"
#include <set>

#include "VideoPlayer.h"

extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#define AUDIO_QUEUE_MAX_SIZE 5
#define FRAME_QUEUE_MAX_SIZE 5
#define AUDIO_QUEUE_REFILL_WAIT 25
#define FRAME_QUEUE_REFILL_WAIT 25

#ifdef AV_NOPTS_VALUE
#undef AV_NOPTS_VALUE
#endif
static const int64_t AV_NOPTS_VALUE = 0x8000000000000000LL;

struct Packet{
	bool free_packet;
	AVPacket packet;
	int64_t dts,
		pts;
	Packet(AVPacket *packet);
	~Packet();
	double compute_time(AVStream *stream, AVFrame *frame);
	double compute_time(AVStream *stream);
private:
	Packet(const Packet &){}
	Packet &operator=(const Packet &){ return *this; }
};

struct audioBuffer{
	int16_t *buffer;
	size_t size;
	ulong time_offset;
	audioBuffer(const uint8_t *src, size_t size, double t){
		this->size = size;
		this->buffer = (int16_t *)src;
		this->time_offset = ulong(t*1000.0);
	}
	audioBuffer(bool q){
		this->buffer = 0;
	}
	audioBuffer(const audioBuffer &b){
		this->buffer = b.buffer;
		this->size = b.size;
		this->time_offset = b.time_offset;
	}
	~audioBuffer(){
		if (this->buffer)
			delete this->buffer;
	}
	//1 if this buffer is done
};

struct video_player;

class AudioOutput{
	BKE_Thread thread;
	ulong frequency,
		channels;
	AVSampleFormat fmt;
	size_t buffers_size;
	BKE_ThreadSafeQueue<audioBuffer *,30> *incoming_queue;
	volatile bool stop_thread;
	void running_thread(video_player *);
public:
	bool good,
		expect_buffers;
	audio_f audio_output;
	AudioOutput(ulong channels, ulong frequency, AVSampleFormat fmt);
	~AudioOutput();
	BKE_ThreadSafeQueue<audioBuffer *,30> *startThread(video_player *vp);
	void stopThread(bool join);
	void wait_until_stop(video_player *vp, bool immediate);
};

struct video_player{
	volatile ulong global_time,
		start_time,
		real_global_time,
		real_start_time;
	//volatile bool stop_playback;
	volatile bool *stop_thread;
	volatile bool stop;
	void *user_data;
	uint64_t global_pts;

	video_player() :global_pts(AV_NOPTS_VALUE){
		stop = 0;
	}
	~video_player(){
	}
	struct get_buffer_struct{
		int64_t pts;
		video_player *vp;
	};
	static int get_buffer(struct AVCodecContext *c, AVFrame *pic);
	static void release_buffer(struct AVCodecContext *c, AVFrame *pic);
	void decode_audio(AVCodecContext *audioCC, AVStream *audioS, BKE_ThreadSafeQueue<Packet *, 200> *, AudioOutput *output, SwrContext *swr_context);
	void decode_video(AVCodecContext *videoCC, AVStream *videoS, BKE_ThreadSafeQueue<Packet *, 25> *packet_queue, void *user_data, SwsContext *img_convert_ctx, video_f *video_func);
	bool play_video(
		const char *file,
		volatile bool *stop,
		volatile bool *pause,
		audio_f audio_output,
		video_f video_output,
		log_f error_output,
		void *user_data
		);
};

AudioOutput::AudioOutput(ulong channels, ulong frequency, AVSampleFormat fmt){
	this->stop_thread = 0;
	this->buffers_size = frequency / 10 * channels;
	this->good = 0;
	this->frequency = frequency;
	this->channels = channels;
	this->fmt = fmt;
	this->good = 1;
}

AudioOutput::~AudioOutput(){
	this->stop_thread = 1;
	this->thread.join();
}

BKE_ThreadSafeQueue<audioBuffer *,30> *AudioOutput::startThread(video_player *vp){
	this->incoming_queue = new BKE_ThreadSafeQueue<audioBuffer *,30>;
	this->thread.call(member_bind(&AudioOutput::running_thread, this, vp), 1);
	return this->incoming_queue;
}

void AudioOutput::stopThread(bool join){
	this->stop_thread = 1;
	if (join)
		this->thread.join();
}

void AudioOutput::running_thread(video_player *vp){
	while (this->incoming_queue->is_empty());
	static BKE_Clock c;
	vp->real_start_time = vp->start_time = c.get();
	while (!this->stop_thread){
		BKE_Thread::sleep(10);
		ulong now = c.get();
		vp->global_time = now - vp->start_time;
		vp->real_global_time = now - vp->real_start_time;
		if (!this->expect_buffers)
			continue;
		this->incoming_queue->lock();
		while (this->incoming_queue->size()){
			audioBuffer *buffer = this->incoming_queue->peek();
			if (this->audio_output.write(buffer->buffer, buffer->size / this->channels, this->channels, this->frequency, vp->user_data)){
				delete buffer;
				this->incoming_queue->pop_without_copy();
				continue;
			}
			break;
		}
		this->incoming_queue->unlock();
		vp->start_time = now - (ulong)this->audio_output.get_time_offset(vp->user_data);
	}
	while (!incoming_queue->is_empty())
	{
		delete incoming_queue->pop();
	}
	delete this->incoming_queue;
	this->incoming_queue = 0;
}

void AudioOutput::wait_until_stop(video_player *vp, bool immediate){
	this->audio_output.wait(vp->user_data, immediate);
}

CompleteVideoFrame::CompleteVideoFrame(AVStream *videoStream, AVFrame *pFrame, SwsContext *img_convert_ctx, double pts)
{
	AVCodecContext *pVideoCC = videoStream->codec;
//	avpicture_alloc((AVPicture *)&picture, PIX_FMT_YUV420P, videoStream->codec->width, videoStream->codec->height);

	picture.data[0] = (uint8_t *)malloc(videoStream->codec->width * videoStream->codec->height * 3 / 2);
	picture.linesize[0] = videoStream->codec->width;
	picture.data[1] = picture.data[0] + videoStream->codec->width * videoStream->codec->height;
	picture.linesize[1] = videoStream->codec->width / 2;
	picture.data[2] = picture.data[1] + videoStream->codec->width * videoStream->codec->height / 4;
	picture.linesize[2] = videoStream->codec->width / 2;
	picture.data[3] = NULL;
	picture.linesize[3] = 0;
	
	sws_scale(img_convert_ctx, pFrame->data, pFrame->linesize,
		0, pVideoCC->height,
		picture.data, picture.linesize);

	this->pts = pts;
	this->repeat = pFrame->repeat_pict;
}

Packet::Packet(AVPacket *packet){
	this->free_packet = !!packet;
	if (this->free_packet){
		this->packet = *packet;
		this->dts = this->packet.dts;
		this->pts = this->packet.pts;
	}
	else
		memset(&this->packet, 0, sizeof(this->packet));
}

Packet::~Packet(){
	if (this->free_packet)
		av_free_packet(&this->packet);

}

double Packet::compute_time(AVStream *stream, AVFrame *frame){
	double ret;
	if (this->dts != AV_NOPTS_VALUE)
		ret = (double)this->dts;
	else{
		ret = 0;
		if (frame->opaque){
			int64_t pts = ((video_player::get_buffer_struct *)frame->opaque)->pts;
			if (pts != AV_NOPTS_VALUE)
				ret = (double)pts;
		}
	}
	return ret*av_q2d(stream->time_base);
}

double Packet::compute_time(AVStream *stream){
	double ret;
	if ((this->dts != AV_NOPTS_VALUE))
		ret = (double)this->dts;
	else
		ret = 0;
	return ret*av_q2d(stream->time_base);
}

int video_player::get_buffer(struct AVCodecContext *c, AVFrame *pic){
	int ret = avcodec_default_get_buffer(c, pic);
	get_buffer_struct *gbs = (get_buffer_struct *)c->opaque;
	gbs->pts = gbs->vp->global_pts;
	return ret;
}

void video_player::release_buffer(struct AVCodecContext *c, AVFrame *pic) {
	if (pic)
		delete (get_buffer_struct *)pic->opaque;
	avcodec_default_release_buffer(c, pic);
}

void video_player::decode_audio(AVCodecContext *audioCC, AVStream *audioS, BKE_ThreadSafeQueue<Packet *, 200> *packet_queue, AudioOutput *output, SwrContext *swr_context){
	BKE_ThreadSafeQueue<audioBuffer *,30> *queue = output->startThread(this);
	AVFrame *audioFrame = avcodec_alloc_frame();
	if (!audioCC)
		return;

	{
		get_buffer_struct *gbs = new get_buffer_struct;
		gbs->vp = this;
		audioCC->opaque = gbs;
	}
	audioCC->get_buffer = get_buffer;
	audioCC->release_buffer = release_buffer;
	while (1){
		Packet *packet = 0;
		while (packet_queue->is_empty() && !*stop_thread && !stop)
			BKE_Thread::sleep(10);
		if (*stop_thread || stop)
			break;
		packet = packet_queue->pop();
		int done = 0;
		avcodec_decode_audio4(audioCC, audioFrame, &done, &packet->packet);
		if (done)
		{
			if (audioCC->sample_fmt != AV_SAMPLE_FMT_S16)
			{
				int in_data_size = av_samples_get_buffer_size(NULL, audioCC->channels, audioFrame->nb_samples, audioCC->sample_fmt, 0);
				uint8_t *buffer = new uint8_t[audioFrame->nb_samples * audioCC->channels * 2];
				int out_data_size = swr_convert(swr_context, &buffer, audioFrame->nb_samples, (const uint8_t **)audioFrame->data, audioFrame->nb_samples);
				queue->push(new audioBuffer(buffer, out_data_size * audioCC->channels, packet->compute_time(audioS)));
			}
			else
			{
				const int size = audioFrame->nb_samples * audioCC->channels * 2;
				uint8_t *buffer = new uint8_t[size];
				memcpy(buffer, audioFrame->data[0], size);
				queue->push(new audioBuffer(buffer, size / 2, packet->compute_time(audioS)));
			}
		}
		delete packet;
	}
	output->stopThread(0);

	while (!packet_queue->is_empty()){
		Packet *packet = packet_queue->pop();
		if (packet)
			delete packet;
	}
}

void video_player::decode_video(AVCodecContext *videoCC, AVStream *videoS, BKE_ThreadSafeQueue<Packet *, 25> *packet_queue, void *user_data, SwsContext *img_convert_ctx, video_f *video_func){
	AVFrame *videoFrame = avcodec_alloc_frame();
	std::set<CompleteVideoFrame *, cmp_pCompleteVideoFrame> preQueue;
	BKE_ThreadSafeQueue<CompleteVideoFrame *,5> frameQueue(stop_thread);
	BKE_ThreadSafeQueue<CompleteVideoFrame *> deleteQueue;
	
	video_func->begindraw(&frameQueue, &this->global_time,&deleteQueue);
	{
		get_buffer_struct *gbs = new get_buffer_struct;
		gbs->vp = this;
		videoCC->opaque = gbs;
	}
	videoCC->get_buffer = get_buffer;
	videoCC->release_buffer = release_buffer;
	double max_pts = -9999;
	bool msg = 0;
	while (1){
		Packet *packet;
		if (*stop_thread || stop)
			break;
		if (packet_queue->is_empty()){
			if (!msg){
				msg = 1;
			}
			BKE_Thread::sleep(10);
			continue;
		}
		while (!deleteQueue.is_empty())
			delete deleteQueue.pop();
		msg = 0;
		packet = packet_queue->pop();
		int frameFinished;
		global_pts = packet->pts;
		//avcodec_decode_video(videoCC,videoFrame,&frameFinished,packet->data,packet->size);
		avcodec_decode_video2(videoCC, videoFrame, &frameFinished, &packet->packet);
		double pts = packet->compute_time(videoS, videoFrame);
		if (frameFinished){
			CompleteVideoFrame *new_frame = new CompleteVideoFrame(videoS, videoFrame,img_convert_ctx, pts);
			if (new_frame->pts>max_pts){
				max_pts = new_frame->pts;
				while (!preQueue.empty() && !*stop_thread){
					std::set<CompleteVideoFrame *, cmp_pCompleteVideoFrame>::iterator first = preQueue.begin();
					frameQueue.push(*first);
					preQueue.erase(first);
				}
			}
			preQueue.insert(new_frame);
		}
		delete packet;
	}
	av_free(videoFrame);
	video_func->enddraw();
	while (!frameQueue.is_empty()){
		CompleteVideoFrame *frame = frameQueue.pop();
		if (frame)
			delete frame;
	}
	while (!preQueue.empty()){
		std::set<CompleteVideoFrame *, cmp_pCompleteVideoFrame>::iterator first = preQueue.begin();
		delete *first;
		preQueue.erase(first);
	}
	while (!packet_queue->is_empty()){
		Packet *packet = packet_queue->pop();
		if (packet)
			delete packet;
	}
	while (!deleteQueue.is_empty())
		delete deleteQueue.pop();
}


struct auto_stream{
	AVFormatContext *&avfc;
	auto_stream(AVFormatContext *&avfc) :avfc(avfc){}
	~auto_stream(){
		avformat_close_input(&this->avfc);
	}
};

struct auto_codec_context{
	AVCodecContext *&cc;
	bool close;
	auto_codec_context(AVCodecContext *&cc, bool close) :cc(cc), close(close){}
	~auto_codec_context(){
		if (close)
			avcodec_close(this->cc);
	}
};

bool video_player::play_video(
	const char *file,
	volatile bool *stop,
	volatile bool *pause,
	audio_f audio_output,
	video_f video_output,
	log_f error_output,
	void *user_data){
	stop_thread = stop;
	this->global_time = 0;
	this->user_data = user_data;
	av_register_all();
	AVFormatContext *avfc;
	stop = 0;
	std::vector<uchar> io_buffer((1 << 12) + FF_INPUT_BUFFER_PADDING_SIZE);
	avfc = NULL;
	if (long a = avformat_open_input(&avfc, file, NULL, NULL) != 0) {
		if (error_output)
		{
			error_output(L"打开视频失败。\n");
		}
		return false;
	}
	auto_stream as(avfc);
	if (av_find_stream_info(avfc)<0){
		if (error_output)
		{
			error_output(L"寻找视频流失败。\n");
		}
		return 0;
	}

	AVCodecContext *videoCC,
		*audioCC = 0;
	long videoStream = -1,
		audioStream = -1;
	for (ulong a = 0; a<avfc->nb_streams && (videoStream<0 || audioStream<0); a++){
		if (avfc->streams[a]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
			videoStream = a;
		else if (avfc->streams[a]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
			audioStream = a;
	}
	bool useAudio = (audioStream != -1);
	if (videoStream == -1){
		if (error_output)
		{
			error_output(L"无视频流。\n");
		}
		return 0;
	}
	videoCC = avfc->streams[videoStream]->codec;
	if (useAudio)
		audioCC = avfc->streams[audioStream]->codec;

	AVCodec *videoCodec = avcodec_find_decoder(videoCC->codec_id),
		*audioCodec = 0;
	if (useAudio)
		audioCodec = avcodec_find_decoder(audioCC->codec_id);
	if (!videoCodec || useAudio && !audioCodec){
		if (!videoCodec)
			if (error_output)
			{
				error_output(L"找不到视频解码器。不支持的视频格式。\n");
			}
		if (!audioCodec)
			if (error_output)
			{
				error_output(L"找不到音频解码器。不支持的音频格式。视频将静默播放。\n");
			}
		return 0;
	}
	int video_codec = avcodec_open2(videoCC, videoCodec, NULL),
		audio_codec = 0;
	if (useAudio)
		audio_codec = avcodec_open2(audioCC, audioCodec, NULL);
	auto_codec_context acc_video(videoCC, video_codec >= 0);
	auto_codec_context acc_audio(audioCC, useAudio && audio_codec >= 0);
	if (video_codec<0 || useAudio && audio_codec<0){
		if (video_codec<0)
			if (error_output)
			{
				error_output(L"视频解码器打开失败。不支持的视频格式。\n");
			}
		if (useAudio && audio_codec<0)
			if (error_output)
			{
				error_output(L"音频解码器打开失败。不支持的音频格式。视频将静默播放。\n");
			}
		return 0;
	}
	ulong channels, sample_rate;
	AVSampleFormat fmt;
	if (useAudio){
		channels = audioCC->channels;
		if (channels !=1 && channels !=2)
		{
			if (error_output)
			{
				error_output(L"音频只支持单声道或者双声道。视频将静默播放。\n");
			}
			useAudio = false;
		}
		sample_rate = audioCC->sample_rate;
		fmt = audioCC->sample_fmt;
		audioCC->request_sample_fmt = AV_SAMPLE_FMT_S16;
	}
	else{
		channels = 1;
		sample_rate = 11025;
		fmt = AV_SAMPLE_FMT_S16;
	}
	{
		AudioOutput output(channels, sample_rate, fmt);
		SwrContext *swr_context = NULL;
		if (fmt != AV_SAMPLE_FMT_S16)
		{
			swr_context = swr_alloc_set_opts(NULL, channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, sample_rate, audioCC->channel_layout, fmt, sample_rate, 0, NULL);
			swr_init(swr_context);
		}
		if (!output.good){
			if (error_output)
			{
				error_output(L"内部错误。音频输出流打开失败。\n");
			}
			return 0;
		}
		output.audio_output = audio_output;
		output.expect_buffers = useAudio;

		AVPacket packet;
		BKE_ThreadSafeQueue<Packet *, 200> audio_packets;
		BKE_ThreadSafeQueue<Packet *, 25> video_packets;
		SwsContext *img_convert_ctx = sws_getContext(videoCC->width,
			videoCC->height,
			videoCC->pix_fmt,
			videoCC->width,
			videoCC->height,
			PIX_FMT_YUV420P,
			SWS_FAST_BILINEAR, NULL, NULL, NULL);
		video_output.begin(videoCC->width, videoCC->height, ((float)videoCC->time_base.num) / videoCC->time_base.den );
		BKE_Thread audio_decoder(member_bind(&video_player::decode_audio, this, audioCC, avfc->streams[audioStream], &audio_packets, &output, swr_context), 1);
		BKE_Thread video_decoder(member_bind(&video_player::decode_video, this, videoCC, avfc->streams[videoStream], &video_packets, user_data, img_convert_ctx, &video_output));
		for (ulong a = 0; av_read_frame(avfc, &packet) >= 0; a++){
			if (*stop_thread){
				av_free_packet(&packet);
				break;
			}
			while (*pause)
			{
				BKE_Thread::sleep(10);
			}
			if (packet.stream_index == videoStream)
				video_packets.push(new Packet(&packet));
			else if (packet.stream_index == audioStream)
				audio_packets.push(new Packet(&packet));
			else
				av_free_packet(&packet);
		}
		this->stop = 1;
		audio_decoder.join();
		video_decoder.join();
		sws_freeContext(img_convert_ctx);
		if (swr_context)
		{
			swr_free(&swr_context);
		}
		if (!useAudio)
			output.stopThread(0);
		output.wait_until_stop(this, !!*stop_thread);
	}
	return 1;
}

extern "C" __declspec(dllexport) void __cdecl PlayVideo(
	const char *file,
	volatile bool *stop,
	volatile bool *pause,
	audio_f audio_output,
	video_f video_output,
	log_f error_output,
	void *user_data)
{
	static video_player vp;
	vp.play_video(file, stop, pause, audio_output, video_output, error_output,user_data);
}