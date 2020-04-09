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

int thread_exit = 0;
// Thread
int sfp_refresh_thread(void *opaque) {
  SDL_Event event;
  while (thread_exit == 0) {
    event.type = SFM_REFRESH_EVENT;
    SDL_PushEvent(&event);
    // Wait 40 ms
    SDL_Delay(40);
  }
  return 0;
}

#undef main
int main(int argc, char *argv[]) {
  using namespace video;
  using namespace audio;

  string path = "D:/Download/Videos/LadyLiu/PhumViphurit-SoftlySpoken.mp4";
  //int ret = play_audio(path);
  int ret = play_video(path);
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
