// CMakeTry.cpp: 定义应用程序的入口点。
//

#include "CMakeTry.h"

using namespace std;

namespace video {
extern int play_video(string);

}

namespace audio {
extern int play_audio(string);

}

void change1(int& j) { j = j + 1; }
void change2(int* j) { *j = *j + 1; }
void change3(int** j) { **j = **j + 1; }


#undef main
int main(int argc, char *argv[]) {
  using namespace video;
  using namespace audio;
  //int j = 1;
  //int *i = &j;
  //cout << "j: " << j << " &j: " << &j << endl;
  //change1(j);
  //cout << "j: " << j << " &j: " << &j << endl;
  //change2(i);
  //cout << "j: " << j << " &j: " << &j << endl;
  //return 0;

  string path = "http://vfx.mtime.cn/Video/2019/03/21/mp4/190321153853126488.mp4";
  //int ret = play_audio(path);
  int ret = -1; 
  try {
    ret = play_video(path);
  } catch (const std::runtime_error &e) {
    std::cout << e.what();
  }
  return ret;

  
}
