#pragma once
#include <thread>
#include <iostream>
#include <string>
#include <sstream>
#include <tuple>
#define __STDC_CONSTANT_MACROS

#ifdef _WIN32
// Windows
extern "C" {
#include "SDL.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
};
#else
// Linux...
#ifdef __cplusplus
extern "C" {
#endif
#include <SDL/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
};
#endif
#endif

namespace AudioUtil {

    using std::cout;
using std::endl;
using std::string;

struct AudioInfo {
  int64_t layout;
  int sampleRate;
  int channels;
  AVSampleFormat format;

  AudioInfo() {
    layout = -1;
    sampleRate = -1;
    channels = -1;
    format = AV_SAMPLE_FMT_S16;
  }

  AudioInfo(int64_t l, int rate, int c, AVSampleFormat f)
      : layout(l), sampleRate(rate), channels(c), format(f) {}
};

static void get_codec_ctx(AVFormatContext* fmt_ctx, int* stream_index,
                          AVCodecContext** codec_ctx,
                          AVMediaType type) {
  int ret = -1;
  char errors[1024];
  AVCodec* vcodec = nullptr;
  ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
  if (ret < 0) {
    av_strerror(ret, errors, 1024);
    string err(errors);
    err = "Failed to av_find_best_stream, " + err + "errCode: ";
    err += ret;
    av_log(NULL, AV_LOG_ERROR, err.c_str());
    throw std::runtime_error(err);
  }
  *stream_index = ret;
  auto par = fmt_ctx->streams[*stream_index]->codecpar;
  //auto par = *codec_par;

  // find decoder for the stream
  vcodec = avcodec_find_decoder(par->codec_id);
  if (!vcodec) {
    av_strerror(ret, errors, 1024);
    string err(errors);
    err = "Failed to avcodec_find_decoder, " + err + "errCode: ";
    err += ret;
    av_log(NULL, AV_LOG_ERROR, err.c_str());
    throw std::runtime_error(err);
  }

  (*codec_ctx) = avcodec_alloc_context3(vcodec);
  // auto ctx = *codec_ctx;

  ret = avcodec_parameters_to_context(*codec_ctx, par);
  if (ret < 0) {
    av_strerror(ret, errors, 1024);
    string err(errors);
    err = "Failed to copy parameters codec_ctx, " + err + "errCode: ";
    err += ret;
    av_log(NULL, AV_LOG_ERROR, err.c_str());
    throw std::runtime_error(err);
  }

  ret = avcodec_open2(*codec_ctx, vcodec, NULL);
  if (ret < 0) {
    av_strerror(ret, errors, 1024);
    string err(errors);
    err = "Failed to open codec_ctx, " + err + "errCode: ";
    err += ret;
    av_log(NULL, AV_LOG_ERROR, err.c_str());
    throw std::runtime_error(err);
  }
}

class ReSampler {
  SwrContext* swr;

 public:
  const AudioInfo in;
  const AudioInfo out;

  static AudioInfo getDefaultAudioInfo(int sr) {
    int64_t layout = AV_CH_LAYOUT_STEREO;
    int sampleRate = sr;
    int channels = 2;
    AVSampleFormat format = AV_SAMPLE_FMT_S16;

    return AudioInfo(layout, sampleRate, channels, format);
  }

  ReSampler(AudioInfo input, AudioInfo output) : in(input), out(output) {
    swr = swr_alloc_set_opts(nullptr, out.layout, out.format, out.sampleRate,
                             in.layout, in.format, in.sampleRate, 0, nullptr);

    if (swr_init(swr)) {
      throw std::runtime_error("swr_init error.");
    }
  }

  int allocDataBuf(uint8_t** outData, int inputSamples) {
    int bytePerOutSample = -1;
    switch (out.format) {
      case AV_SAMPLE_FMT_U8:
        bytePerOutSample = 1;
        break;
      case AV_SAMPLE_FMT_S16P:
      case AV_SAMPLE_FMT_S16:
        bytePerOutSample = 2;
        break;
      case AV_SAMPLE_FMT_S32:
      case AV_SAMPLE_FMT_S32P:
      case AV_SAMPLE_FMT_FLT:
      case AV_SAMPLE_FMT_FLTP:
        bytePerOutSample = 4;
        break;
      case AV_SAMPLE_FMT_DBL:
      case AV_SAMPLE_FMT_DBLP:
      case AV_SAMPLE_FMT_S64:
      case AV_SAMPLE_FMT_S64P:
        bytePerOutSample = 8;
        break;
      default:
        bytePerOutSample = 2;
        break;
    }

    int guessOutSamplesPerChannel = av_rescale_rnd(inputSamples, out.sampleRate,
                                                   in.sampleRate, AV_ROUND_UP);
    int guessOutSize =
        guessOutSamplesPerChannel * out.channels * bytePerOutSample;

    std::cout << "GuessOutSamplesPerChannel: " << guessOutSamplesPerChannel
              << std::endl;
    std::cout << "GuessOutSize: " << guessOutSize << std::endl;

    guessOutSize *= 1.2;  // just make sure.

    *outData = (uint8_t*)av_malloc(sizeof(uint8_t) * guessOutSize);
    // av_samples_alloc(&outData, NULL, outChannels,
    // guessOutSamplesPerChannel, AV_SAMPLE_FMT_S16, 0);
    return guessOutSize;
  }

  std::tuple<int, int> reSample(uint8_t* dataBuffer, int dataBufferSize,
                                const AVFrame* frame) {
    int outSamples =
        swr_convert(swr, &dataBuffer, dataBufferSize,
                    (const uint8_t**)&frame->data[0], frame->nb_samples);
    // cout << "reSample: nb_samples=" << frame->nb_samples << ", sample_rate
    // = " << frame->sample_rate <<  ", outSamples=" << outSamples << endl;
    if (outSamples <= 0) {
      throw std::runtime_error("error: outSamples=" + outSamples);
    }

    int outDataSize = av_samples_get_buffer_size(NULL, out.channels, outSamples,
                                                 out.format, 1);

    if (outDataSize <= 0) {
      throw std::runtime_error("error: outDataSize=" + outDataSize);
    }

    return {outSamples, outDataSize};
  }
};

}

