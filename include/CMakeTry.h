// CMakeTry.h: 标准系统包含文件的包含文件
// 或项目特定的包含文件。

#pragma once

#include <iostream>
#include <cstdlib>

#include <string>
#include <stdio.h>
 
#define __STDC_CONSTANT_MACROS

#ifdef _WIN32
// Windows
extern "C" {
#include "SDL.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
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

// Full Screen
#define SHOW_FULLSCREEN 0
// Output YUV420P
#define OUTPUT_YUV420P 0


#define SFM_REFRESH_EVENT (SDL_USEREVENT + 1)


