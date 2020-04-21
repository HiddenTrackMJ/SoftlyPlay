#include "FFmpegUtil.h"



#include <deque>
#include <memory>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <mutex>





using std::cout;
using std::endl;

using std::shared_ptr;
using std::string;
using std::unique_ptr;

class Dealer {
  AVFrame* next_frame = av_frame_alloc();
  AVPacket* current_pkt = nullptr;
  std::deque<unique_ptr<AVPacket>> pkt_queue{};
  std::mutex pkt_mutex{};

  int PKT_WAITING_SIZE = 3;
  bool started = false;
  bool closed = false;
  bool stream_finished = false;
  char errors[1024];


  void frame_decoder() {
   
    while (!stream_finished && started) {
      std::unique_lock<std::mutex> locker{nextDataMutex};
      cv.wait(locker, [this] { return !started || !next_ready; });
      while (!stream_finished) {
        if (current_pkt == nullptr) {
          if (!pkt_empty) {
            auto pkt = get_next_pkt();
            if (pkt != nullptr) {
              current_pkt = pkt.release();
            } else if (pkt_empty) {
              current_pkt = nullptr;
            } else {
              return;
            }
          } else {
            cout << "There are no more pkt index=" << stream_index
                 << " finished=" << stream_finished << endl;
          }
        }

        int ret = -1;
        ret = avcodec_send_packet(codec_ctx, current_pkt);
        if (ret == 0) {
          av_packet_free(&current_pkt);
          current_pkt = nullptr;
        } else if (ret == AVERROR(EAGAIN)) {
          cout << "[WARN]  Buffer full. index="
               << stream_index << endl;
        } else if (ret == AVERROR_EOF) {
          cout << "[WARN]  no new packets can be sent to it. index="
               << stream_index << endl;
        } else {
          av_strerror(ret, errors, 1024);
          string err(errors);
          err = "Failed to avcodec_send_packet, " + err + "errCode: ";
          err += ret;
          av_log(NULL, AV_LOG_ERROR, err.c_str());
          throw std::runtime_error(err);
        }

        ret = avcodec_receive_frame(codec_ctx, next_frame);
        if (ret == 0) {
          generateNextData(next_frame);
          next_ready = true;
        } else if (ret == AVERROR_EOF) {
          cout << "Dealer no more output frames. index=" << stream_index << endl;
          stream_finished = true;
        } else if (ret == AVERROR(EAGAIN)) {
          cout << "[WARN]  Need more pkt. index=" << stream_index << endl;
        } else {
          av_strerror(ret, errors, 1024);
          string err(errors);
          err = "Failed to avcodec_receive_frame, " + err + "errCode: ";
          err += ret;
          av_log(NULL, AV_LOG_ERROR, err.c_str());
          throw std::runtime_error(err);
        }
      }
    }
    cout << "[THREAD] next frame keeper finished, index=" << stream_index
         << endl;
    started = false;
    closed = true;
  }

 protected:
  std::atomic<uint64_t> currentTimestamp{0};
  std::atomic<uint64_t> nextFrameTimestamp{0};
  AVRational stream_time_base{1, 0};


  bool pkt_empty = false;

  int stream_index = -1;
  AVCodecContext* codec_ctx = nullptr;

 
  std::condition_variable cv{};
  std::mutex nextDataMutex{};

  bool next_ready{false};

  virtual void generateNextData(AVFrame* f) = 0;

  unique_ptr<AVPacket> get_next_pkt() {
    if (pkt_empty) {
      return nullptr;
    }
    std::lock_guard<std::mutex> lg(pkt_mutex);
    if (pkt_queue.empty()) {
      return nullptr;
    } else {
      auto pkt = std::move(pkt_queue.front());
      if (pkt == nullptr) {
        pkt_empty = true;
        return pkt;
      } else {
        pkt_queue.pop_front();
        return pkt;
      }
    }
  }

  void prepareNextData() {
    
  }

 public:
  ~Dealer() {
    if (next_frame != nullptr) {
      av_frame_free(&next_frame);
    }

    if (current_pkt != nullptr) {
      av_packet_free(&current_pkt);
    }

    if (codec_ctx != nullptr) {
      avcodec_free_context(&codec_ctx);
    }

    // very important here.
    for (auto& p : pkt_queue) {
      auto pkt = p.release();
      av_packet_free(&pkt);
    }

    cout << "~Dealer() called. index=" << stream_index << endl;
  }

  void start() {
    started = true;
    std::thread keeper{&Dealer::frame_decoder, this};
    keeper.detach();
  }

  bool close() {
    started = false;
    int c = 5;
    while (!closed && c > 0) {
      c--;
      cv.notify_one();
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return closed;
  }

  bool isClosed() { return closed; }

  void pushPkt(unique_ptr<AVPacket> pkt) {
    std::lock_guard<std::mutex> lg(pkt_mutex);
    pkt_queue.push_back(std::move(pkt));
  }
  bool isStreamFinished() { return stream_finished; }

  bool needPacket() {
    bool need;
    std::lock_guard<std::mutex> lg(pkt_mutex);
    need = pkt_queue.size() < PKT_WAITING_SIZE;
    return need;
  }

  AVCodecContext* getCodecCtx() { return codec_ctx; }

  uint64_t get_ts() { return currentTimestamp.load(); }
};

class AudioDealer : public Dealer {
  std::unique_ptr<FFmpegUtil::ReSampler> reSampler{};

  uint8_t* out_buffer = nullptr;
  int out_buffer_size = -1;
  int out_data_size = -1;
  int out_samples = -1;

  FFmpegUtil::AudioInfo in_audio;
  FFmpegUtil::AudioInfo out_audio;
  FFmpegUtil::ffmpeg_util ff_util;

protected:
  void generateNextData(AVFrame* frame) final override {
    if (out_buffer == nullptr) {
      out_buffer_size = reSampler->allocDataBuf(&out_buffer, frame->nb_samples);
    } else {
      memset(out_buffer, 0, out_buffer_size);
    }
    std::tie(out_samples, out_data_size) = reSampler->reSample(out_buffer, out_buffer_size, frame);
    auto t = frame->pts * av_q2d(stream_time_base) * 1000;
    nextFrameTimestamp.store((uint64_t)t);
  }

 public:

  ~AudioDealer() {
    if (out_buffer != nullptr) {
      av_freep(&out_buffer);
    }
    cout << "~AudioDealer() called." << endl;
  }

  AudioDealer(FFmpegUtil::ffmpeg_util f) : ff_util(f) {
    codec_ctx = f.get_acodec_ctx();
    stream_index = f.get_audio_index();
    stream_time_base = f.get_audio_base();

    int64_t inLayout = codec_ctx->channel_layout;
    int inSampleRate = codec_ctx->sample_rate;
    int inChannels = codec_ctx->channels;
    AVSampleFormat inFormat = codec_ctx->sample_fmt;

    in_audio =  FFmpegUtil::AudioInfo(inLayout, inSampleRate, inChannels, inFormat);
    out_audio = FFmpegUtil::ReSampler::getDefaultAudioInfo(inSampleRate);

    reSampler.reset(new FFmpegUtil::ReSampler(in_audio, out_audio));
    start();
  }


  int getSamples() { return out_samples; }

  void write_audio_data(uint8_t* stream, int len) {
    static uint8_t* silence_buff = nullptr;
    if (silence_buff == nullptr) {
      silence_buff = (uint8_t*)av_malloc(sizeof(uint8_t) * len);
      std::memset(silence_buff, 0, len);
    }

    if (next_ready) {
      std::lock_guard<std::mutex> locker(nextDataMutex);
      currentTimestamp.store(nextFrameTimestamp.load());
      if (out_data_size != len) {
        cout << "WARNING: outDataSize[" << out_data_size << "] != len[" << len
             << "]" << endl;
      }
      std::memcpy(stream, out_buffer, out_data_size);
      next_ready = false;
    } else {
      cout << "WARNING: writeAudioData, audio data not ready." << endl;
      std::memcpy(stream, silence_buff, len);
    }
    cv.notify_one();
  }


};

class VideoDealer : public Dealer {
  struct SwsContext* sws_ctx = nullptr;
  AVFrame* out_pic = nullptr;
  FFmpegUtil::ffmpeg_util ff_util;

 protected:
  void generateNextData(AVFrame* frame) override {
    auto t = frame->pts * av_q2d(stream_time_base) * 1000;
    nextFrameTimestamp.store((uint64_t)t);
    sws_scale(sws_ctx, (uint8_t const* const*)frame->data, frame->linesize, 0,
              codec_ctx->height, out_pic->data, out_pic->linesize);

  }

 public:
 
  ~VideoDealer() {
    if (sws_ctx != nullptr) {
      sws_freeContext(sws_ctx);
      sws_ctx = nullptr;
    }

    if (out_pic != nullptr) {
      av_frame_free(&out_pic);
    }

    ff_util.close();
    cout << "~VideoDealer() called." << endl;
  }

  VideoDealer(FFmpegUtil::ffmpeg_util f) : ff_util(f)
  {
   
    codec_ctx = f.get_vcodec_ctx();
    stream_index = f.get_video_index();
    stream_time_base = f.get_video_base();

    int w = codec_ctx->width;
    int h = codec_ctx->height;

    sws_ctx = sws_getContext(w, h, codec_ctx->pix_fmt, w, h, AV_PIX_FMT_YUV420P,
                             SWS_BILINEAR, NULL, NULL, NULL);

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, w, h, 32);
    out_pic = av_frame_alloc();
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(out_pic->data, out_pic->linesize, buffer,
                         AV_PIX_FMT_YUV420P, w, h, 32);
    start();
  }

  AVFrame* getFrame() {
    if (next_ready) {
      currentTimestamp.store(nextFrameTimestamp.load());
      return out_pic;
    } else {
      cout << "WARNING: getFrame, video data not ready." << endl;
      return nullptr;
    }
  }

  bool refreshFrame() {
    if (next_ready) {
      currentTimestamp.store(nextFrameTimestamp.load());
      next_ready = false;
      cv.notify_one();
      return true;
    } else {
      cv.notify_one();
      return false;
    }
  }

  double getFrameRate() const {
    if (codec_ctx != nullptr) {
      auto frameRate = codec_ctx->framerate;
      double fr = frameRate.num && frameRate.den ? av_q2d(frameRate) : 0.0;
      return fr;
    } else {
      throw std::runtime_error("can not getFrameRate.");
    }
  }
};
