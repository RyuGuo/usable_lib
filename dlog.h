/**
 * @file dlog.h

 * @brief terminal log output macro

 * @version 0.1
 * @date 2022-05-27
 */

#ifndef __DLOG_H__
#define __DLOG_H__

#include <assert.h>
#include <cstdlib>
#include <ctime>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

#define DLOG_STREAM(stream, format, ...)                                       \
  do {                                                                         \
    struct timeval tv;                                                         \
    struct tm tm;                                                              \
    char tbuf[28];                                                             \
    gettimeofday(&tv, NULL);                                                   \
    localtime_r(&tv.tv_sec, &tm);                                              \
    size_t l = strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S.", &tm);        \
    sprintf(tbuf + l, "%06d", (int)tv.tv_usec);                                \
    fprintf(stream, "[%s] [%d] %s:%d: " format "\n", tbuf, gettid(), __FILE__, \
            __LINE__, __VA_ARGS__);                                            \
  } while (0)

#define DLOG_INFO(format, ...)                                                 \
  DLOG_STREAM(stderr, "[INFO] " format, __VA_ARGS__)
#define DLOG_ERROR(format, ...)                                                \
  DLOG_STREAM(stderr, "[ERROR] " format, __VA_ARGS__)
#define DLOG_WARNING(format, ...)                                              \
  DLOG_STREAM(stderr, "[WARNING] " format, __VA_ARGS__)

#define DLOG(format, ...) DLOG_INFO(format, __VA_ARGS__)

#define DLOG_FILE(file, format, ...)                                           \
  do {                                                                         \
    FILE *fp = fopen(file, "w+");                                              \
    assert(fp != NULL);                                                        \
    DLOG_STREAM(fp, format, __VA_ARGS__);                                      \
    fclose(fp);                                                                \
  } while (0)

#define DLOG_IF(expr, format, ...)                                             \
  if (expr)                                                                    \
  DLOG(format, __VA_ARGS__)

#define DLOG_ASSERT(expr, format, ...)                                         \
  do {                                                                         \
    if (__glibc_unlikely(expr)) {                                              \
      DLOG_ERROR(format, __VA_ARGS__);                                         \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

#endif // __DLOG_H__