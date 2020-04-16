#include "CMakeTry.h"
#include "audioUtil.h"
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
char errors[1024];
int video_stream_index = -1;
int audio_stream_index = -1;

int refresh_thread(void* opaque) {
  thread_quit = 0;
  while (!thread_quit) {
    SDL_Event event;
    event.type = REFRESH_EVENT;
    SDL_PushEvent(&event);
    SDL_Delay(30);
  }
 /* thread_quit = 0;
  SDL_Event event;
  event.type = BREAK_EVENT;
  SDL_PushEvent(&event);*/
  return 0;
}

struct AudioData {
  AVFormatContext* fmt_ctx;
  AVCodecContext* codec_ctx;
  AudioUtil::ReSampler* reSmapler;
};

void audioCallback(void* userdata, Uint8* stream, int len) {
  AudioProcessor* receiver = (AudioProcessor*)userdata;
  receiver->writeAudioData(stream, len);
}

void audio_callback(void* userdata, Uint8* stream, int len) {
  AudioData* audioData = (AudioData*)userdata;
  AudioUtil::ReSampler* reSampler = audioData->reSmapler;

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



int getPkt(AVFormatContext* fmt_ctx, AVPacket* pkt) {
  while (true) {
    if (av_read_frame(fmt_ctx, pkt) >= 0) {
      return pkt->stream_index;
    } else {
      // file end;
      return -1;
    }
  }
}

void pktReader(AVFormatContext* fmt_ctx, AudioProcessor* aProcessor,
               VideoProcessor* vProcessor) {
  const int CHECK_PERIOD = 10;

  cout << "INFO: pkt Reader thread started." << endl;
  int audioIndex = aProcessor->getAudioIndex();
  int videoIndex = vProcessor->getVideoIndex();

  while ( !aProcessor->isClosed() &&
         !vProcessor->isClosed()) {
    while (aProcessor->needPacket() || vProcessor->needPacket()) {
      AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
      int t = getPkt(fmt_ctx, packet);
      if (t == -1) {
        cout << "INFO: file finish." << endl;
        aProcessor->pushPkt(nullptr);
        vProcessor->pushPkt(nullptr);
        break;
      } else if (t == audioIndex && aProcessor != nullptr) {
        unique_ptr<AVPacket> uPacket(packet);
        aProcessor->pushPkt(std::move(uPacket));
      } else if (t == videoIndex && vProcessor != nullptr) {
        unique_ptr<AVPacket> uPacket(packet);
        vProcessor->pushPkt(std::move(uPacket));
      } else {
        av_packet_free(&packet);
        cout << "WARN: unknown streamIndex: [" << t << "]" << endl;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(CHECK_PERIOD));
  }
  cout << "[THREAD] INFO: pkt Reader thread finished." << endl;
}

void playAudio(AVFormatContext* fmt_ctx, AVCodecContext* acodec_ctx,
               SDL_AudioDeviceID& audioDeviceID, AudioProcessor& aProcessor) {
  // for audio play
  int64_t in_layout = acodec_ctx->channel_layout;
  int in_channels = acodec_ctx->channels;
  int in_sample_rate = acodec_ctx->sample_rate;
  AVSampleFormat in_sample_fmt = AVSampleFormat(acodec_ctx->sample_fmt);

  cout << "in sr: " << in_sample_rate << "in sf: " << in_sample_fmt << endl;
  AudioUtil::AudioInfo inAudio(in_layout, in_sample_rate, in_channels,
                               in_sample_fmt);
  AudioUtil::AudioInfo outAudio =
      // AudioUtil::AudioInfo(AV_CH_LAYOUT_STEREO, in_sample_fmt, 2,
      // AV_SAMPLE_FMT_S16);
      AudioUtil::ReSampler::getDefaultAudioInfo(in_sample_rate);
  outAudio.sampleRate = inAudio.sampleRate;

  AudioUtil::ReSampler reSampler(inAudio, outAudio);

  AudioData audioData{fmt_ctx, acodec_ctx, &reSampler};

  SDL_AudioSpec audio_spec;
  SDL_AudioSpec spec;
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

  cout << "wanted_specs.freq:" << audio_spec.freq << endl;
  // cout << "wanted_specs.format:" << wanted_specs.format << endl;
  std::printf("wanted_specs.format: Ox%X\n", audio_spec.format);
  cout << "wanted_specs.channels:" << (int)audio_spec.channels << endl;
  cout << "wanted_specs.samples:" << (int)audio_spec.samples << endl;

  cout << "------------------------------------------------" << endl;
  cout << "specs.freq:" << spec.freq << endl;
  // cout << "specs.format:" << specs.format << endl;
  std::printf("specs.format: Ox%X\n", spec.format);
  cout << "specs.channels:" << (int)spec.channels << endl;
  cout << "specs.silence:" << (int)spec.silence << endl;
  cout << "specs.samples:" << (int)spec.samples << endl;
  SDL_PauseAudioDevice(audioDeviceID, 0);
  cout << "waiting audio play..." << endl;

}

void playVideo(AVCodecContext* vcodec_ctx,
               VideoProcessor& vProcessor) {
  int ret = 0;
  AVFrame* frame = av_frame_alloc();
  AVPicture* pic = nullptr;
  AVPacket* pkt = av_packet_alloc();
  struct SwsContext* sws_ctx = nullptr;
  // for render
  int quit = 1;
  int w_width = vcodec_ctx->width;
  int w_height = vcodec_ctx->height;

  SDL_Window* window = nullptr;
  SDL_Renderer* render = nullptr;
  SDL_Texture* texture = nullptr;
  SDL_Event event;
  SDL_Rect rect;
  SDL_Thread* video_thread;
  rect.x = 0;
  rect.y = 0;
  rect.w = w_width;
  rect.h = w_height;


  window = SDL_CreateWindow("Softly Spoken", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, w_width, w_height,
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

  // init SWS ctx for software scaling
  sws_ctx = sws_getContext(vcodec_ctx->width, vcodec_ctx->height,
                           vcodec_ctx->pix_fmt, w_width, w_height,
                           AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

  pkt = (AVPacket*)av_malloc(sizeof(AVPacket));
  pic = (AVPicture*)malloc(sizeof(AVPicture));
  avpicture_alloc(pic, AV_PIX_FMT_YUV420P, vcodec_ctx->width,
                  vcodec_ctx->height);

  video_thread = SDL_CreateThread(refresh_thread, NULL, NULL);

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
      AVFrame* frame = vProcessor.getFrame();

      if (frame != nullptr) {
        SDL_UpdateYUVTexture(
            texture,  // the texture to update
            NULL,     // a pointer to the rectangle of pixels to update, or
                      // NULL to update the entire texture
            frame->data[0],      // the raw pixel data for the Y plane
            frame->linesize[0],  // the number of bytes between rows of pixel
                                 // data for the Y plane
            frame->data[1],      // the raw pixel data for the U plane
            frame->linesize[1],  // the number of bytes between rows of pixel
                                 // data for the U plane
            frame->data[2],      // the raw pixel data for the V plane
            frame->linesize[2]   // the number of bytes between rows of pixel
                                 // data for the V plane
        );
        SDL_RenderClear(render);
        SDL_RenderCopy(render, texture, NULL, &rect);
        SDL_RenderPresent(render);
        if (!vProcessor.refreshFrame()) {
          cout << "WARN: vProcessor.refreshFrame false" << endl;
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
  sws_freeContext(sws_ctx);
  av_frame_free(&frame);
  avpicture_free(pic);
  free(pic);
  av_packet_free(&pkt);

  if (!texture) SDL_DestroyTexture(texture);
  if (!render) SDL_DestroyRenderer(render);
  if (!window) SDL_DestroyWindow(window);
}

int play_video(string path) {
  int ret = 0;

  AVFormatContext* fmt_ctx = avformat_alloc_context();
  AVCodecParameters* vcodec_par = nullptr;
  AVCodecContext* vcodec_ctx = nullptr;

  AVCodecParameters* acodec_par = nullptr;
  AVCodecContext* acodec_ctx = nullptr;
  
  // uint8_t* out_buffer = nullptr;
  // Unit32 pix_fmt;

  av_log_set_level(AV_LOG_INFO);

  avformat_network_init();

  // print info
  ret = avformat_open_input(&fmt_ctx, path.c_str(), NULL, NULL);
  if (ret < 0) {
    av_strerror(ret, errors, 1024);
    av_log(NULL, AV_LOG_ERROR, "can't open file: %s, %d(%s)\n", path, ret,
           errors);
    throw "can't open file:" + path;
    return -1;
  }

  av_dump_format(fmt_ctx, 0, path.c_str(), 0);

  // 2.get stream
  ret = avformat_find_stream_info(fmt_ctx, NULL);
  if (ret < 0) {
    av_strerror(ret, errors, 1024);
    av_log(NULL, AV_LOG_ERROR, "Can't find stream info: %s, %d(%s)\n", path,
           ret, errors);
    avformat_close_input(&fmt_ctx);
    return -1;
  }

  AudioUtil::get_codec_ctx(fmt_ctx, &video_stream_index, &vcodec_ctx,
                           AVMEDIA_TYPE_VIDEO);
  AudioUtil::get_codec_ctx(fmt_ctx, &audio_stream_index, &acodec_ctx,
                           AVMEDIA_TYPE_AUDIO);

  VideoProcessor videoProcessor(fmt_ctx);
  videoProcessor.start();

  // create AudioProcessor
  AudioProcessor audioProcessor(fmt_ctx);
  audioProcessor.start();

  SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);

  if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to init SDL - %s\n !",
                 SDL_GetError);
    return -1;
  }

  // Uint32 audio device id
  SDL_AudioDeviceID audioDeviceID;

  //playAudio(fmt_ctx, acodec_ctx, audioDeviceID);

  //playVideo( vcodec_ctx);

  // start pkt reader
  std::thread readerThread{pktReader, fmt_ctx, &audioProcessor,
                           &videoProcessor};

  std::thread audioThread(playAudio, fmt_ctx, acodec_ctx,
                          std::ref(audioDeviceID), std::ref(audioProcessor));
  audioThread.join();

  std::thread videoThread(playVideo, vcodec_ctx, std::ref(videoProcessor));

  videoThread.join();

   

 

  //SDL_Delay(30000);
  SDL_PauseAudioDevice(audioDeviceID, 1);
  SDL_CloseAudio();

   bool r;
  r = audioProcessor.close();
  cout << "audioProcessor closed: " << r << endl;
  r = videoProcessor.close();
  cout << "videoProcessor closed: " << r << endl;

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  readerThread.join();
   

_Quit:
  ret = 0;

  
  avcodec_close(vcodec_ctx);
  avcodec_free_context(&vcodec_ctx);
  avcodec_close(acodec_ctx);
  avcodec_free_context(&acodec_ctx);
  avformat_close_input(&fmt_ctx);
  
  SDL_Quit();

  return 0;
}
}  // namespace video