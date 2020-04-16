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
  int j = 1;
  int *i = &j;
  cout << "j: " << j << " &j: " << &j << endl;
  change1(j);
  cout << "j: " << j << " &j: " << &j << endl;
  change2(i);
  cout << "j: " << j << " &j: " << &j << endl;
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

   
  int quit = 1;
  SDL_Window *window = NULL;
  SDL_Renderer *render = NULL;
  SDL_Event event;
  SDL_Texture *texture = NULL;
  SDL_Rect rect;
  rect.w = 30;
  rect.h = 30;
  SDL_Init(SDL_INIT_VIDEO);

  window = SDL_CreateWindow("Jason", 200, 200, 640, 480, SDL_WINDOW_SHOWN);

  if (!window) {
    cout << "Error!" << endl;
    goto _Exit;
  }

  render = SDL_CreateRenderer(window, -1, 0);
  if (!render) {
    SDL_Log("render error!");
  }
  SDL_RenderClear(render);
  SDL_RenderPresent(render);
  texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_RGBA8888,
                              SDL_TEXTUREACCESS_TARGET, 640, 480);

  if (!texture) {
    SDL_Log("Failed to Create Texture !");
    goto _RENDER;
  }
  while (quit) {
    SDL_WaitEvent(&event);
    switch (event.type) {
      case SDL_QUIT:
        quit = 0;
        break;
      default:
        quit = 1;
        // SDL_Log("event type is %d", event.type);
    }

    rect.x = rand() % 600;
    rect.y = rand() % 450;

    SDL_SetRenderTarget(render, texture);
    SDL_SetRenderDrawColor(render, 0, 0, 0, 0);
    SDL_RenderClear(render);
    SDL_RenderDrawRect(render, &rect);
    SDL_SetRenderDrawColor(render, 255, 0, 0, 0);
    SDL_RenderFillRect(render, &rect);
    SDL_SetRenderTarget(render, NULL);
    SDL_RenderCopy(render, texture, NULL, NULL);
    SDL_RenderPresent(render);
  }
  // SDL_Delay(3000);

  SDL_DestroyTexture(texture);

_RENDER:
  SDL_DestroyRenderer(render);
  SDL_DestroyWindow(window);

_Exit:
  SDL_Quit();

  return 0;
}
