#include "hlc.h"
#include <sys/time.h>

using uint64_t = unsigned long;

static uint64_t __get_pt() {
  timeval t;
  gettimeofday(&t, nullptr);
  return t.tv_sec * 1000000 + t.tv_usec;
}

uint64_t (*get_pt)() = __get_pt;

struct __hlc_t {
  uint64_t l : 48;
  uint64_t c : 16;

  operator uint64_t() { return (l << 16) | c; }
};

static __hlc_t gt = {0, 0};

static uint64_t max(uint64_t a, uint64_t b) { return a > b ? a : b; }
static uint64_t max(uint64_t a, uint64_t b, uint64_t c) {
  return max(max(a, b), c);
}

hlc_t get_hlc_ts() {
  __hlc_t _t;
  _t.l = gt.l;
  gt.l = max(_t.l, get_pt());
  if (gt.l == _t.l)
    ++gt.c;
  else
    gt.c = 0;
  return gt;
}

hlc_t sync_hlc_ts(__hlc_t m) {
  __hlc_t _t;
  _t.l = gt.l;
  gt.l = max(_t.l, m.l, get_pt());
  if (gt.l == _t.l && _t.l == m.l)
    gt.c = max(gt.c, m.c) + 1;
  else if (gt.l == _t.l)
    ++gt.c;
  else if (gt.l == m.l)
    gt.c = m.c + 1;
  else
    gt.c = 0;
  return gt;
}