#ifndef PLATFORMUTIL_H
#define PLATFORMUTIL_H
#include <stdio.h>

#ifdef NDEBUG
#define IS_DEBUG 0
#else
#define IS_DEBUG 1
#endif

// platform macros are used a lot - less typing and more flexible to use true/false instead of def/notdef
#define PLATFORM_WIN 0
#define PLATFORM_LINUX 0
#define PLATFORM_OSX 0
#define PLATFORM_IOS 0
#define PLATFORM_ANDROID 0
#define PLATFORM_EMSCRIPTEN 0

// NOTE: PLATFORM_NAME and PLATFORM_TYPE are expected to be lowercase
#ifdef __ANDROID__
#define PLATFORM_NAME "android"
#undef PLATFORM_ANDROID
#define PLATFORM_ANDROID 1
#elif defined(_WIN32)
#define PLATFORM_NAME "windows"
#undef PLATFORM_WIN
#define PLATFORM_WIN 1
#elif defined(__linux__)
#define PLATFORM_NAME "linux"
#undef PLATFORM_LINUX
#define PLATFORM_LINUX 1
#elif defined(__APPLE__)
  #include "TargetConditionals.h"
  #if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
    #define PLATFORM_NAME "ios"
    #undef PLATFORM_IOS
    #define PLATFORM_IOS 1
  #else
    #define PLATFORM_NAME "mac"
    #undef PLATFORM_OSX
    #define PLATFORM_OSX 1
  #endif
#elif defined(__EMSCRIPTEN__)
#define PLATFORM_NAME "emscripten"
#undef PLATFORM_EMSCRIPTEN
#define PLATFORM_EMSCRIPTEN 1
#endif

#if PLATFORM_ANDROID || PLATFORM_IOS || PLATFORM_EMSCRIPTEN
#define PLATFORM_TYPE "mobile"
#define PLATFORM_DESKTOP 0
#define PLATFORM_MOBILE 1
#else
#define PLATFORM_TYPE "desktop"
#define PLATFORM_DESKTOP 1
#define PLATFORM_MOBILE 0
#endif

#if PLATFORM_ANDROID
#include <android/log.h>
#define PLATFORM_LOG(...) __android_log_print(ANDROID_LOG_VERBOSE, "StylusLabs_NDK",  __VA_ARGS__)
//typedef float Dim;
#elif PLATFORM_WIN
extern bool winLogToConsole;
bool attachParentConsole();
void winOutputDebugString(const char* str);
#define PLATFORM_LOG(...) winOutputDebugString(fstring(__VA_ARGS__).c_str())
#elif PLATFORM_IOS && !IS_DEBUG
#include <os/log.h>
#include "stringutil.h"
#define PLATFORM_LOG(...) os_log(OS_LOG_DEFAULT, "%{public}s", fstring(__VA_ARGS__).c_str())
#else
#define PLATFORM_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

#if PLATFORM_WIN && IS_DEBUG
#include <assert.h>
#define ASSERT assert
#else
// Not using standard assert because can't get debugger to break on Linux!  Also for control over NDEBUG case!
#define ASSERT(x) platform_assert(x, #x, __func__, __FILE__, __LINE__)
void platform_assert(bool cond, const char* msg, const char* fnname, const char* file, unsigned int line);
#endif

#include <cstdint>

typedef int64_t Timestamp;
#define MAX_TIMESTAMP LLONG_MAX
#define SECONDS Timestamp(1000)

Timestamp mSecSinceEpoch();

#endif

#ifdef PLATFORMUTIL_IMPLEMENTATION
#undef PLATFORMUTIL_IMPLEMENTATION

#include <chrono>

Timestamp mSecSinceEpoch()
{
  auto timeSinceEpoch = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(timeSinceEpoch).count();
}

void platform_assert(bool cond, const char* msg, const char* fnname, const char* file, unsigned int line)
{
  if(!cond) {
    PLATFORM_LOG("%s:%d: %s: Assertion failed: %s\n", file, line, fnname, msg);
#if IS_DEBUG
    *(volatile int*)0 = 0;  //abort();
#endif
  }
}

#if PLATFORM_WIN
bool winLogToConsole = false;

bool attachParentConsole()
{
  if(AttachConsole(ATTACH_PARENT_PROCESS)) {
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    return true;
  }
  return false;
}

void winOutputDebugString(const char* str)
{
  if(!str || !str[0])
    return;
  if(winLogToConsole)
    fprintf(stderr, str);
  else {
    OutputDebugStringA(str);
    if(str[strlen(str)-1] != '\n')
      OutputDebugStringA("\n");
  }
}
#endif // Windows

#endif
