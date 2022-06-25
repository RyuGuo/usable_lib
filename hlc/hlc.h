#ifndef __HLC_H__
#define __HLC_H__

using hlc_t = unsigned long;

hlc_t get_hlc_ts();
hlc_t sync_hlc_ts(hlc_t m);

#endif // __HLC_H__