#include "CMakeTry.h"

extern "C" {

#include "libavutil/log.h"
};

using std::cout;
using std::endl;
using std::string;

namespace video {

int play_video(string path) {
  int ret = 0;
  char errors[1024];
  int video_stream_index = -1;
  int audio_stream_index = -1;

  AVFormatContext* fmt_ctx = nullptr;
  AVCodecParameters* codec_par = nullptr;
  AVCodecContext* codec_ctx = nullptr;
  AVCodec* codec = nullptr;
  AVFrame* frame = av_frame_alloc();
  AVPicture* pic = nullptr;
  AVPacket* pkt;
  struct SwsContext* sws_ctx = nullptr;
  uint8_t* out_buffer = nullptr;
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

  ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (ret < 0) {
    av_strerror(ret, errors, 1024);
    av_log(NULL, AV_LOG_ERROR, "Can't find %s stream: %s, %d(%s)\n",
           av_get_media_type_string(AVMEDIA_TYPE_VIDEO), path, ret, errors);
    avformat_close_input(&fmt_ctx);
    return -1;
  }
  video_stream_index = ret;
  codec_par = fmt_ctx->streams[video_stream_index]->codecpar;

  // find decoder for the stream
  codec = avcodec_find_decoder(codec_par->codec_id);
  if (!codec) {
    av_strerror(ret, errors, 1024);
    av_log(NULL, AV_LOG_ERROR, "Can't find %s codec, %d(%s)\n",
           av_get_media_type_string(AVMEDIA_TYPE_VIDEO), ret, errors);
    return -1;
  }

  codec_ctx = avcodec_alloc_context3(codec);

  ret = avcodec_parameters_to_context(codec_ctx, codec_par);
  if (ret < 0) {
    av_strerror(ret, errors, 1024);
    av_log(NULL, AV_LOG_ERROR, "Can't copy parameters %s codec_ctx, %d(%s)\n",
           av_get_media_type_string(AVMEDIA_TYPE_VIDEO), ret, errors);
    return -1;
  }

  ret = avcodec_open2(codec_ctx, codec, NULL);
  if (ret < 0) {
    av_strerror(ret, errors, 1024);
    av_log(NULL, AV_LOG_ERROR, "Can't open %s codec_ctx, %d(%s)\n",
           av_get_media_type_string(AVMEDIA_TYPE_VIDEO), ret, errors);
    return -1;
  }

  // allocate video frame
  frame = av_frame_alloc();

  // for render
  int quit = 1;
  int w_width = 640;
  int w_height = 480;
  SDL_Window* window = nullptr;
  SDL_Renderer* render = nullptr;
  SDL_Texture* texture = nullptr;
  SDL_Event event;
  SDL_Rect rect;
  rect.x = 0;
  rect.y = 0;
  rect.w = 640;
  rect.h = 480;
  

  if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to init SDL - %s\n !",
                 SDL_GetError);
    return -1;
  }

  w_width = 640;
  w_height = 480;

  window = SDL_CreateWindow("Softly Spoken", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, w_width, w_height,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

  if (!window) {
    cout << "Failed to create a window!" << endl;
    throw "Failed to create a window!";
    return -1;
  }

  render = SDL_CreateRenderer(window, -1, 0);
  if (!render) {
    cout << "Failed to create a render!" << endl;
    throw "Failed to create a render!";
    return -1;
  }

  texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_IYUV,
                              SDL_TEXTUREACCESS_STREAMING, w_width, w_height);
  if (!texture) {
    cout << "Failed to create a texture!" << endl;
    throw "Failed to create a texture!";
    return -1;
  }

  // init SWS ctx for software scaling
  sws_ctx = sws_getContext(codec_ctx->width, codec_ctx->height,
                           codec_ctx->pix_fmt, w_width, w_height,
                           AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

  
  pkt = (AVPacket*)av_malloc(sizeof(AVPacket));
  pic = (AVPicture*)malloc(sizeof(AVPicture));
  avpicture_alloc(pic, AV_PIX_FMT_YUV420P, codec_ctx->width, codec_ctx->height);

  // read frames and save first five frames to disk
  int frameFinished = 0;
  while (av_read_frame(fmt_ctx, pkt) >= 0) {
    if (pkt->stream_index == video_stream_index) {
      ret = avcodec_decode_video2(codec_ctx, frame, &frameFinished, pkt);
      if (ret < 0) {
        av_strerror(ret, errors, 1024);
        av_log(NULL, AV_LOG_ERROR, "decode error %s, %d(%s)\n",
               av_get_media_type_string(AVMEDIA_TYPE_VIDEO), ret, errors);
        //return -1;
      }
      if (frameFinished) {
        sws_scale(sws_ctx, (const unsigned char* const*)frame->data,
                  frame->linesize, 0, codec_ctx->height, pic->data,
                  pic->linesize);

        SDL_UpdateYUVTexture(texture, &rect, pic->data[0], pic->linesize[0],
                             pic->data[1], pic->linesize[1], pic->data[2],
                             pic->linesize[2]);
        
        SDL_RenderClear(render);
        SDL_RenderCopy(render, texture, NULL, &rect);
        SDL_RenderPresent(render);
      }
      av_free_packet(pkt);
      
      SDL_Delay(30);

      SDL_PollEvent(&event);
      switch (event.type) {
        case SDL_QUIT:
          goto _Quit;
          break;
        default:
          break;
      }

    
    }
  }

_Quit:
  ret = 0;
  sws_freeContext(sws_ctx);
  avcodec_close(codec_ctx);
  avcodec_free_context(&codec_ctx);
  av_frame_free(&frame);
  avpicture_free(pic);
  free(pic);
  av_packet_free(&pkt);
  avformat_close_input(&fmt_ctx);
  if (!texture) SDL_DestroyTexture(texture);
  if (!render) SDL_DestroyRenderer(render);
  if (!window) SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
}  // namespace video