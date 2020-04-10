#include "CMakeTry.h"

using std::cout;
using std::endl;
using std::string;

namespace audio {
int play_audio(string path) {
  int ret = 0;
  int audio_index = 0;
  AVPacket pkt;
  AVFormatContext* fmt_ctx = NULL;

  av_log_set_level(AV_LOG_INFO);

  av_register_all();

  //print info
  ret = avformat_open_input(&fmt_ctx, path.c_str(), NULL, NULL);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "can't open file: %s\n", path);
    throw "can't open file:" + path;
    return -1;
  }

  av_dump_format(fmt_ctx, 0, path.c_str(), 0);

  //2.get stream
  ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Can't find the best stream!\n");
    avformat_close_input(&fmt_ctx);
    return -1;
  }
  audio_index = ret;
  av_init_packet(&pkt);
  cout << pkt.stream_index << " km sss" << endl;
  while (av_read_frame(fmt_ctx, &pkt) >= 0) {
    if (pkt.stream_index == audio_index) {
      
    } 
    av_packet_unref(&pkt);
  }
  avformat_close_input(&fmt_ctx);

  return 0;
}
}  // namespace audio