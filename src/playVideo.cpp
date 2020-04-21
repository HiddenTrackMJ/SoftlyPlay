#include "CMakeTry.h"
#include "FFmpegUtil.h"
#include "packetStorer.h"

extern "C" {

#include "libavutil/log.h"
};

using std::cout;
using std::endl;
using std::string;

#define REFRESH_EVENT (SDL_USEREVENT + 1)
#define BREAK_EVENT (SDL_USEREVENT + 2)

namespace video {
int thread_quit = 0;
std::atomic<bool> faster{false};
char errors[1024];
int video_stream_index = -1;
int audio_stream_index = -1;

int refresh_thread(void* opaque) {
  double* frame_rate = (double*)opaque;
  thread_quit = 0;
  while (!thread_quit) {
    SDL_Event event;
    event.type = REFRESH_EVENT;
    SDL_PushEvent(&event);
    if (faster.load()) {
      //cout << "play faster" << endl;
      SDL_Delay((int)(1000 / *frame_rate) / 2);
    }
     
    else {
      //cout << "play normaly" << endl;
      SDL_Delay((int)(1000 / *frame_rate));
    }
      
  }
  thread_quit = 0;
  SDL_Event event;
  event.type = BREAK_EVENT;
  SDL_PushEvent(&event);
  return 0;
}

struct AudioData {
  AVFormatContext* fmt_ctx;
  AVCodecContext* codec_ctx;
  FFmpegUtil::ReSampler* reSmapler;
};

void audioCallback(void* userdata, Uint8* stream, int len) {
  AudioDealer* receiver = (AudioDealer*)userdata;
  receiver->write_audio_data(stream, len);
}

void audio_callback(void* userdata, Uint8* stream, int len) {
  AudioData* audioData = (AudioData*)userdata;
  FFmpegUtil::ReSampler* reSampler = audioData->reSmapler;

  static uint8_t* outBuffer = nullptr;
  static int outBufferSize = 0;
  static AVFrame* aFrame = av_frame_alloc();
  static AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
  int ret = 0;
  av_init_packet(packet);
  AVFormatContext* fmt_ctx = audioData->fmt_ctx;
  AVCodecContext* codec_ctx = audioData->codec_ctx;

  while (true) {
    while (1) {
      ret = av_read_frame(fmt_ctx, packet);
      if (ret < 0) {
        av_strerror(ret, errors, 1024);
        string err(errors);
        err = "Failed to av_read_frame, " + err + "errCode: ";
        err += ret;
        av_log(NULL, AV_LOG_ERROR, err.c_str());
        throw std::runtime_error(err);
      }
      if (packet->stream_index == audio_stream_index) break;
    }
    {
      ret = avcodec_send_packet(codec_ctx, packet);
      if (ret >= 0) {
        /*if (packet->stream_index != audio_stream_index) {
          av_packet_unref(packet);
          continue;
        } else {
          av_packet_unref(packet);
        }*/

        av_packet_unref(packet);
        // cout << "[VIDEO] avcodec_send_packet success." << endl;;

        int ret = avcodec_receive_frame(codec_ctx, aFrame);
        if (ret >= 0) {
          ret = 2;
          break;
        } else if (ret == AVERROR(EAGAIN)) {
          continue;
        } else {
          av_strerror(ret, errors, 1024);
          string err(errors);
          err = "Failed to avcodec_receive_frame, " + err + "errCode: ";
          err += ret;
          av_log(NULL, AV_LOG_ERROR, err.data());
          throw std::runtime_error(err);
        }
      } else if (ret == AVERROR(EAGAIN)) {
        // buff full, can not decode anymore, do nothing.
      } else {
        av_strerror(ret, errors, 1024);
        string err(errors);
        err = "Failed to avcodec_send_packet, " + err + ", errCode: ";
        err += ret;
        err += "  \n";
        cout << "ret: " << ret << endl;
        av_log(NULL, AV_LOG_ERROR, err.c_str());
        throw std::runtime_error(err);
      }
    }
  }

  int outDataSize = -1;
  int outSamples = -1;

  // if (ret == 2) {
  // cout << "play with ReSampler!" << endl;
  if (outBuffer == nullptr) {
    outBufferSize = reSampler->allocDataBuf(&outBuffer, aFrame->nb_samples);
    cout << " --------- audio samples: " << aFrame->nb_samples << endl;
  } else {
    memset(outBuffer, 0, outBufferSize);
  }

  std::tie(outSamples, outDataSize) =
      reSampler->reSample(outBuffer, outBufferSize, aFrame);

  if (outDataSize != len) {
    cout << "WARNING: outDataSize[" << outDataSize << "] != len[" << len << "]"
         << endl;
  }

  std::memcpy(stream, outBuffer, outDataSize);
}
//}



int get_packet(AVFormatContext* fmt_ctx, AVPacket* pkt) {
  while (true) {
    if (av_read_frame(fmt_ctx, pkt) >= 0) {
      return pkt->stream_index;
    } else {
      // file end;
      return -1;
    }
  }
}

void packet_grabber(FFmpegUtil::ffmpeg_util f, AudioDealer* ad,
               VideoDealer* vd) {
  const int CHECK_PERIOD = 10;

  auto fmt_ctx = f.get_fmt_ctx();
  int audio_index = f.get_audio_index();
  int video_index = f.get_video_index();

  while ( !ad->isClosed() && !vd->isClosed() && !thread_quit) {
    while (ad->needPacket() || vd->needPacket()) {
      AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
      int t = get_packet(fmt_ctx, packet);
      if (t == -1) {
        cout << "INFO: file finish." << endl;
        ad->pushPkt(nullptr);
        vd->pushPkt(nullptr);
        break;
      } else if (t == audio_index && ad != nullptr) {
        std::unique_ptr<AVPacket> uPacket(packet);
        ad->pushPkt(std::move(uPacket));
      } else if (t == video_index && vd != nullptr) {
        std::unique_ptr<AVPacket> uPacket(packet);
        vd->pushPkt(std::move(uPacket));
      } else {
        av_packet_free(&packet);
        //cout << "WARN: unknown streamIndex: [" << t << "]" << endl;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(CHECK_PERIOD));
  }
  cout << "[THREAD] INFO: pkt Reader thread finished." << endl;
}

void playAudio(FFmpegUtil::ffmpeg_util f, SDL_AudioDeviceID& audioDeviceID, AudioDealer& aProcessor) {
  // for audio play
  auto fmt_ctx = f.get_fmt_ctx();
  auto acodec_ctx = f.get_acodec_ctx();
  int64_t in_layout = acodec_ctx->channel_layout;
  int in_channels = acodec_ctx->channels;
  int in_sample_rate = acodec_ctx->sample_rate;
  AVSampleFormat in_sample_fmt = AVSampleFormat(acodec_ctx->sample_fmt);

  cout << "in sr: " << in_sample_rate << " in sf: " << in_sample_fmt << endl;
  FFmpegUtil::AudioInfo inAudio(in_layout, in_sample_rate, in_channels,
                               in_sample_fmt);

  FFmpegUtil::AudioInfo outAudio = FFmpegUtil::ReSampler::getDefaultAudioInfo(in_sample_rate);
  outAudio.sampleRate = inAudio.sampleRate;

  FFmpegUtil::ReSampler reSampler(inAudio, outAudio);

  AudioData audioData{fmt_ctx, acodec_ctx, &reSampler};

  SDL_AudioSpec audio_spec;
  SDL_AudioSpec spec;

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // set audio settings from codec info
  audio_spec.freq = acodec_ctx->sample_rate;
  audio_spec.format = AUDIO_S16SYS;
  audio_spec.channels = acodec_ctx->channels;
  audio_spec.samples = 1024;
  audio_spec.callback = audioCallback;
  audio_spec.userdata = &aProcessor;

  // open audio device
  audioDeviceID = SDL_OpenAudioDevice(nullptr, 0, &audio_spec, &spec, 0);

  // SDL_OpenAudioDevice returns a valid device ID that is > 0 on success or 0
  // on failure
  if (audioDeviceID == 0) {
    string errMsg = "Failed to open audio device:";
    errMsg += SDL_GetError();
    cout << errMsg << endl;
    throw std::runtime_error(errMsg);
  }

  SDL_PauseAudioDevice(audioDeviceID, 0);
  cout << "waiting audio play..." << endl;

}

void playVideo(AVCodecContext* vcodec_ctx, VideoDealer& vd,
               AudioDealer* ad = nullptr) {
  int ret = 0;
  AVFrame* frame = av_frame_alloc();
  AVPacket* pkt = av_packet_alloc();

  // for render
  int quit = 1;
  int w_width = vcodec_ctx->width;
  int w_height = vcodec_ctx->height;

  SDL_Window* window = nullptr;
  SDL_Renderer* render = nullptr;
  SDL_Texture* texture = nullptr;
  SDL_Event event;
  SDL_Thread* video_thread;



  window = SDL_CreateWindow("Softly Spoken", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, w_width / 2, w_height / 2,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

  if (!window) {
    cout << "Failed to create a window!" << endl;
    throw "Failed to create a window!";
  }

  render = SDL_CreateRenderer(window, -1, 0);
  if (!render) {
    cout << "Failed to create a render!" << endl;
    throw "Failed to create a render!";
  }

  texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_IYUV,
                              SDL_TEXTUREACCESS_STREAMING, w_width, w_height);
  if (!texture) {
    cout << "Failed to create a texture!" << endl;
    throw "Failed to create a texture!";
  }

  auto frame_rate = vd.get_frame_rate();
  
  cout << "fr1: " << frame_rate << "fr2: " << (int)(1000 / frame_rate) << endl;
  video_thread = SDL_CreateThread(refresh_thread, "Refresh", &frame_rate);

  // read frames and save first five frames to disk
  int frameFinished = 0;
  int failCount = 0;
  int fastCount = 0;
  int slowCount = 0;
  while (1) {
    SDL_WaitEvent(&event);

    if (event.type == REFRESH_EVENT) {
      //if (av_read_frame(fmt_ctx, pkt) >= 0) {
      //  if (pkt->stream_index == video_stream_index) {
      //    ret = avcodec_decode_video2(vcodec_ctx, frame, &frameFinished, pkt);
      //    if (ret < 0) {
      //      av_strerror(ret, errors, 1024);
      //      av_log(NULL, AV_LOG_ERROR, "decode error %s, %d(%s)\n",
      //             av_get_media_type_string(AVMEDIA_TYPE_VIDEO), ret, errors);
      //      // return -1;
      //    }
      //    if (frameFinished) {
      //      sws_scale(sws_ctx, (const unsigned char* const*)frame->data,
      //                frame->linesize, 0, vcodec_ctx->height, pic->data,
      //                pic->linesize);

      //      SDL_UpdateYUVTexture(texture, &rect, pic->data[0], pic->linesize[0],
      //                           pic->data[1], pic->linesize[1], pic->data[2],
      //                           pic->linesize[2]);
      //      /* rect.x = 0;
      //       rect.y = 0;*/
      //      /*rect.w = w_width;
      //      rect.h = w_height;*/

      //      SDL_RenderClear(render);
      //      SDL_RenderCopy(render, texture, NULL, &rect);
      //      SDL_RenderPresent(render);
      //    }
      //    av_packet_unref(pkt);
      //  }
      //}

       if (vd.isStreamFinished()) {
        thread_quit = true;
        continue;  // skip REFRESH event.
      }

      if (ad != nullptr) {
        auto vTs = vd.get_ts();
        auto aTs = ad->get_ts();
        //cout << "vTs: " << vTs << "aTs: " << aTs << endl;
        if (vTs > aTs && vTs - aTs > 30) {
          //cout << "VIDEO IS FASTER ================= " << (vTs - aTs)
          //     << "ms, SKIP A EVENT" << endl;
          // skip a REFRESH_EVENT
          faster.store(false);
          slowCount++;
          continue;
        } else if (vTs < aTs && aTs - vTs > 30) {
          //cout << "VIDEO IS SLOWER ================= " << (aTs - vTs)
          //    << "ms, Faster" << endl;
          faster.store(true);
          fastCount++;
        } else {
          faster.store(false);
        }
      } else
        cout << "no audio processor" << endl;

      AVFrame* frame = vd.get_frame();

      if (frame != nullptr) {
        SDL_UpdateYUVTexture(texture, NULL,
                             frame->data[0], frame->linesize[0],
                             frame->data[1], frame->linesize[1],
                             frame->data[2], frame->linesize[2]
        );
        SDL_RenderClear(render);
        SDL_RenderCopy(render, texture, NULL, NULL);
        SDL_RenderPresent(render);
        if (!vd.refresh()) {
          cout << "WARN: video dealer failed to refresh frame " << endl;
        }
      } else {
        failCount++;
        cout << "WARN: getFrame fail. failCount = " << failCount << endl;
      }
    } else if (event.type == SDL_QUIT) {
      thread_quit = 1;
      goto _Quit;
    } else if (event.type == SDL_WINDOWEVENT) {
      SDL_GetWindowSize(window, &w_width, &w_height);

    } else if (event.type == BREAK_EVENT) {
      break;
    }
  }

_Quit:
  av_frame_free(&frame);
  av_packet_free(&pkt);

  if (!texture) SDL_DestroyTexture(texture);
  if (!render) SDL_DestroyRenderer(render);
  if (!window) SDL_DestroyWindow(window);
}

int play_video(string path) {

  FFmpegUtil::ffmpeg_util ffmpeg_ctx(path);

  VideoDealer vd(ffmpeg_ctx);

  AudioDealer ad(ffmpeg_ctx);


  SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);

  if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to init SDL - %s\n !",
                 SDL_GetError);
    return -1;
  }

  SDL_AudioDeviceID audioDeviceID;

  std::thread grab_thread{packet_grabber, ffmpeg_ctx, &ad, &vd};

  grab_thread.detach();

  std::thread audio_thread(playAudio, ffmpeg_ctx, std::ref(audioDeviceID), std::ref(ad));
  std::thread video_thread(playVideo, ffmpeg_ctx.get_vcodec_ctx(), std::ref(vd), &ad);

  audio_thread.join();
  video_thread.join();

  ad.close();
  vd.close();


  SDL_PauseAudioDevice(audioDeviceID, 1);
  SDL_CloseAudio();
  
  SDL_Quit();
  //std::this_thread::sleep_for(std::chrono::milliseconds(1000));


  return 0;
}
}  // namespace video