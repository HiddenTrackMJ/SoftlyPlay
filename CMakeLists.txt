# CMakeList.txt: CMakeTry 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#
cmake_minimum_required (VERSION 3.8)

project ("CMakeTry")



# TODO: 如有需要，请添加测试并安装目标。

# specify the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

#set(FFMPEG_INCLUDE_DIR "D:/Study/Scala/VSWS/ffmpeg/ffmpeg-4.2.2-win64-dev/include")
#set(FFMPEG_LIBRARY "D:/Study/Scala/VSWS/ffmpeg/ffmpeg-4.2.2-win64-dev/lib")

#配置头文件目录 : 如果想要在写代码时调用 FFMPEG 中的函数 , 必须设置头文件的查找位置
include_directories("D:/Study/Scala/VSWS/ffmpeg/ffmpeg-4.2.2-win64-dev/include")


#配置库文件目录 : 设置 lib 库文件查找目录 , 相当于 Java 中的 jar 包
link_directories("D:/Study/Scala/VSWS/ffmpeg/ffmpeg-4.2.2-win64-dev/lib")

######################################
#  SDL2 lib setting
######################################
#make sure SDL2.dll is in your PATH.

set(SDL_INCLUDE_DIR "D:/Study/Scala/VSWS/ffmpeg/SDL2-2.0.12/include")
set(SDL_LIBRARY "D:/Study/Scala/VSWS/ffmpeg/SDL2-2.0.12/lib/x64/SDL2.lib")

#find_file(SDL_INCLUDE_DIR NAME SDL.h HINTS SDL)
#find_library(SDL_LIBRARY NAME SDL)



######################################
#  OpenAL lib setting
######################################
#make sure openAL32.dll is in your PATH.

set(OpenAL_INCLUDE_DIR "D:/Program Files (x86)/OpenAL 1.1 SDK/include")
set(OpenAL_LIBRARY "D:/Program Files (x86)/OpenAL 1.1 SDK/libs/Win64/OpenAL32.lib")

message(CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR})
message(PROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR})



# 将源代码添加到此项目的可执行文件。
add_executable (${PROJECT_NAME} 
"src/CMakeTry.cpp" 
"src/playAudio.cpp" 
"src/playVideo.cpp" 
)

target_include_directories( ${PROJECT_NAME}  
	PRIVATE 
		${PROJECT_SOURCE_DIR}/include
		${SDL_INCLUDE_DIR}
		${OpenAL_INCLUDE_DIR}


)

#设置编译链接的 FFMPEG 库 , 需要把 FFMPEG 的库都配置到下面
#注意 使用 002_FFMPEG 的前提是 , 该项目必须在 002_FFMPEG 配置后面进行配置
target_link_libraries( ${PROJECT_NAME} 	#本项目的解决方案名称
	PRIVATE 
		avcodec		#音视频编码核心库 , 用于音视频或图片编解码
		avdevice	#硬件采集 , 加速 , 显示 库
		avfilter	#音视频滤波器
		avformat	#音视频封装格式生成或解析 
		avutil		#该库中封装了一些公共的工具函数
		postproc	#封装了同步, 时间计算的简单算法
		swresample	#音视频编解码格式预设
		swscale		#原始视频格式转换库
		${SDL_LIBRARY}
		${OpenAL_LIBRARY}
)
