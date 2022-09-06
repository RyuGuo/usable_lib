/**
 * @file dlog.h

 * @brief terminal log output macro

 * @version 0.1
 * @date 2022-05-27
 */

#ifndef __DLOG_H__
#define __DLOG_H__

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <errno.h>
#include <pthread.h>
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
    fprintf(stream, "[%s] [%#lx] %s:%d: " format "\n", tbuf, pthread_self(),   \
            __FILE__, __LINE__, ##__VA_ARGS__);                                \
  } while (0)

#define DLOG_INFO(format, ...)                                                 \
  DLOG_STREAM(stderr, "[INFO] " format, ##__VA_ARGS__)
#define DLOG_ERROR(format, ...)                                                \
  DLOG_STREAM(stderr, "[ERROR] " format ": %s", ##__VA_ARGS__, strerror(errno))
#define DLOG_WARNING(format, ...)                                              \
  DLOG_STREAM(stderr, "[WARNING] " format, ##__VA_ARGS__)
#define DLOG_FATAL(format, ...)                                                \
  do {                                                                         \
    DLOG_STREAM(stderr, "[FATAL] " format ": %s", ##__VA_ARGS__,               \
                strerror(errno));                                              \
    fflush(stdout);                                                            \
    abort();                                                                   \
  } while (0)

#define DLOG(format, ...) DLOG_INFO(format, ##__VA_ARGS__)

#define DLOG_FILE(file, format, ...)                                           \
  do {                                                                         \
    FILE *fp = fopen(file, "w+");                                              \
    assert(fp != NULL);                                                        \
    DLOG_STREAM(fp, format, ##__VA_ARGS__);                                    \
    fclose(fp);                                                                \
  } while (0)

#define DLOG_IF(expr, format, ...)                                             \
  do {                                                                         \
    if (expr)                                                                  \
      DLOG(format, ##__VA_ARGS__);                                             \
  } while (0)

inline constexpr const char *type_fmt(const char) { return "%c"; }
inline constexpr const char *type_fmt(const short) { return "%hd"; }
inline constexpr const char *type_fmt(const int) { return "%d"; }
inline constexpr const char *type_fmt(const long) { return "%ld"; }
inline constexpr const char *type_fmt(const long long) { return "%lld"; }
inline constexpr const char *type_fmt(const unsigned char) { return "%hhu"; }
inline constexpr const char *type_fmt(const unsigned short) { return "%hu"; }
inline constexpr const char *type_fmt(const unsigned int) { return "%u"; }
inline constexpr const char *type_fmt(const unsigned long) { return "%lu"; }
inline constexpr const char *type_fmt(const unsigned long long) {
  return "%llu";
}
inline constexpr const char *type_fmt(const float) { return "%f"; }
inline constexpr const char *type_fmt(const double) { return "%lf"; }
inline constexpr const char *type_fmt(const long double) { return "%llf"; }
inline constexpr const char *type_fmt(const void *) { return "%p"; }

/**
 * Assert the judgment between two values.
 * @example DLOG_EXPR(malloc(1), !=, nullptr)
 *
 * @warning In C++11, `NULL` will throw warning: passing NULL to non-pointer
 * argument... You should use `nullptr` instead of `NULL`.
 */
#define DLOG_EXPR(val_a, op, val_b)                                            \
  do {                                                                         \
    decltype(val_a) a = val_a;                                                 \
    decltype(val_b) b = val_b;                                                 \
    if (__glibc_unlikely(!(a op b))) {                                         \
      char fmt[] = "Because " #val_a " = %???, " #val_b " = %???";             \
      char tmp[sizeof(fmt) + 42];                                              \
      snprintf(fmt, sizeof(fmt), "Because " #val_a " = %s, " #val_b " = %s",   \
               type_fmt(a), type_fmt(b));                                      \
      snprintf(tmp, sizeof(tmp), fmt, a, b);                                   \
      DLOG_FATAL("Assertion `" #val_a " " #op " " #val_b "` failed. %s", tmp); \
    }                                                                          \
  } while (0)

#define DLOG_ASSERT(expr, format...)                                           \
  do {                                                                         \
    if (__glibc_unlikely(!(expr))) {                                           \
      DLOG_FATAL("Assertion `" #expr "` failed. " format);                     \
    }                                                                          \
  } while (0)

#endif // __DLOG_H__