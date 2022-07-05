#include "mm_allocator.h"
#include "./../conqueue.h"
#include "./../extend_mutex.h"
#include "./../fatomic.h"
#include "./../lfbitset.h"
#include "./../seg_allocator.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string.h>

#define SLAB_UNIT_SIZE 64
#define SIZE_CLASS_NUM 32
#define SLICE_UNIT_SIZE 64
#define BLOCK_SIZE 4096
#define CHUNK_SIZE 262144
#define BLOCK_NUM_PER_CHUNK (CHUNK_SIZE / BLOCK_SIZE)
#define SLICE_UNIT_NUM_PER_CHUNK (CHUNK_SIZE / SLICE_UNIT_SIZE)

using chunk_id_t = uint64_t;
using block_id_t = uint64_t; // start at 1

static void (*MM_FLUSH)(void *ptr, size_t size);
static void (*MM_DRAIN)();

template <typename T> static void mm_obj_sync(T *e) {
  MM_FLUSH((void *)e, sizeof(T));
  MM_DRAIN();
}
template <typename T> static void mm_obj_sync_nodrain(T *e) {
  MM_FLUSH((void *)e, sizeof(T));
}
template <typename T> static void obj_clr(T &e) { memset(&e, 0, sizeof(T)); }
template <typename A, typename B> constexpr static A ceil(A a, B b) {
  return (a + b - 1) / b;
}
template <typename A, typename B> constexpr static A align_up(A a, B b) {
  return ceil(a, b) * b;
}
static void get_block_idx(block_id_t block_id, chunk_id_t *chunk_id,
                          int *block_idx) {
  --block_id;
  if (chunk_id) {
    *chunk_id = block_id / BLOCK_NUM_PER_CHUNK;
  }
  if (block_idx) {
    *block_idx = block_id % BLOCK_NUM_PER_CHUNK;
  }
}
static block_id_t get_block_id(chunk_id_t chunk_id, int block_idx) {
  return chunk_id * BLOCK_NUM_PER_CHUNK + block_idx + 1;
}

static bool STATUS = false;
static char *ADDR = nullptr;
static size_t MAX_SIZE = 0;

struct theap_s;

/**
 * 存储布局
 *          4nKB         256KB  256KB
 * |---------^---------||--^--||--^--|
 * [superblock][headers][chunk][chunk]...
 *                      |-------v--------|
 *                         N ≈ C / 256KB
 *
 * superblock: global_meta, free_chunk_bitset
 * headers: [chunk_hdr][block_hdr][block_hdr]...[block_hdr]     |
 *          [chunk_hdr][block_hdr][block_hdr]...[block_hdr]...  } N
 *                     |-----------------v----------------|     |
 *                                      64
 *          4KB
 *        |--^--|
 * chunk: [block][block]...
 *        |-------v-------|
 *                64
 *
 *            64B
 *        |----^----|
 * block: [slab unit][slab unit]...
 *        |-----------v-----------|
 *                   64
 */

struct mm_block_header {
  union {
    /**
     * @brief size class
     * 0 - 64B
     * 1 - 128B
     * 3 - 192B
     * ...
     * 31 - 2048B
     * 32 - huge obj
     */
    int size_cls;
  };
  /**
   * @brief block 中空闲的slab单元
   */
  lfbitset<64> bitset;
};

enum chunk_type { tSMALL, tBIG, tHUGE };

struct mm_chunk_header {
  struct free_meta {
    lfbitset<BLOCK_NUM_PER_CHUNK> free_block_bitset;
  };

  chunk_type type;
  union {
    free_meta block_free_meta;
    size_t huge_size;
  };
};

struct mm_global_header {
  uint64_t end_offset;
  uint64_t chunk_total_num;
  uint64_t actual_use;
  uint64_t gh_offset;
  uint64_t free_chunk_bitset_offset;
  uint64_t free_chunk_bitword_offset;
  uint64_t hl_offset;
  uint64_t data_offset;
};

struct mm_hdr_layout {
  struct {
    mm_chunk_header ch;
    union {
      mm_block_header bh[BLOCK_NUM_PER_CHUNK];
      lfbitset<SLICE_UNIT_NUM_PER_CHUNK> slice_bitset;
    };
  } hdr_zone[0];
};

struct layout_t {
  uint64_t chunk_num;

  mm_global_header *gh;
  lfbitset_base *free_chunk_bitset;
  mm_hdr_layout *hl;
  char *data;

  uint64_t data_offset;
};

struct dram_block_meta {
  enum lock_bit_type {
    recycle_block = 1,
  };
  std::atomic<uint32_t> lock;
  volatile int ver;
  std::atomic<uint32_t> slab_used_num;
};

struct dram_chunk_meta {
  chunk_id_t id;
  theap_s *owner;
  mm_chunk_header *hdr;
  chunk_type type;

  enum lock_bit_type {
    recycle_block = 1,
    has_been_recycled = 2,
  };

  std::atomic<uint8_t> lock;

  seg_allocator_t huge_allocator;

  union {
    ConQueue<block_id_t> *free_block;
    size_t huge_size;
    std::atomic<uint32_t> block_used_num;
    std::atomic<uint32_t> slice_used_num;
  };
};

struct dram_global_pool_meta {
  size_t meta_use_memory;
  size_t actual_use_memory;

  layout_t layout;
  std::atomic<uint64_t> chunk_used_num;

  ConQueue<dram_chunk_meta *> *almost_free_chunk;
  ConQueue<dram_chunk_meta *> *almost_slice_free_chunk;
  seg_allocator_greater_t huge_chunk_allocator;

  mm_block_header &get_mm_block_header(block_id_t block_id) {
    chunk_id_t chunk_id;
    int block_idx;
    get_block_idx(block_id, &chunk_id, &block_idx);
    return layout.hl->hdr_zone[chunk_id].bh[block_idx];
  }

  chunk_id_t get_chunk_id_from_nbit(uint64_t nbit) {
    const uint64_t tail = layout.chunk_num % 512;
    if (nbit % 512 < tail) {
      return nbit / 512 + (nbit % 512) * ceil(layout.chunk_num, 512);
    } else {
      return (nbit - tail) / 512 +
             ((nbit - tail) % 512) * (layout.chunk_num / 512) +
             ceil(layout.chunk_num, 512) * tail;
    }
  }

  uint64_t get_nbit_from_chunk_id(chunk_id_t chunk_id) {
    const uint64_t tail = layout.chunk_num % 512;
    const uint64_t fi = ceil(layout.chunk_num, 512) * tail;
    if (chunk_id < fi) {
      return chunk_id / ceil(layout.chunk_num, 512) +
             (chunk_id % ceil(layout.chunk_num, 512)) * 512;
    } else {
      return (chunk_id - fi) / (layout.chunk_num / 512) +
             ((chunk_id - fi) % (layout.chunk_num / 512)) * 512 + tail;
    }
  }
};

struct theap_s {
  /**
   * @brief 线程自持的slab类
   * 每个元素是不同slab大小的block_id，从1开始，0表示没有
   */
  block_id_t slabs[SIZE_CLASS_NUM];
  /**
   * @brief chunk 被线程单独持有
   */
  dram_chunk_meta *chunk;
  dram_chunk_meta *huge_chunk;

  theap_s() : chunk(nullptr), huge_chunk(nullptr) { obj_clr(slabs); }
};

const char *chunk_type_to_str(chunk_type type) {
  switch (type) {
  case tSMALL:
    return "tSMALL";
    break;
  case tBIG:
    return "tBIG";
    break;
  case tHUGE:
    return "tHUGE";
    break;
  }
  return "Error Type";
}

static dram_global_pool_meta *global_pool;
static dram_chunk_meta *chunk_metas;
static dram_block_meta *block_metas;
static thread_local theap_s theap;

void mm_allocator::env_init(void *addr, size_t max_size,
                            void (*mm_flush)(void *ptr, size_t size),
                            void (*mm_drain)()) {
  if (STATUS)
    return;
  ADDR = static_cast<char *>(addr);
  MAX_SIZE = max_size;

  global_pool = new dram_global_pool_meta();
  if (global_pool == nullptr)
    throw "Out of memory";
  global_pool->meta_use_memory = sizeof(*global_pool);
  layout_t &layout = global_pool->layout;

  uint64_t chunk_num;
  uint64_t gh_offset, free_chunk_bitset_offset, free_chunk_bitword_offset,
      hl_offset, data_offset;
  uint64_t actual_use = max_size + 1;

  gh_offset = 0;
  free_chunk_bitset_offset = gh_offset + sizeof(*layout.gh);
  free_chunk_bitword_offset =
      align_up(free_chunk_bitset_offset + sizeof(lfbitset_base), 64);
  for (chunk_num = max_size / CHUNK_SIZE; actual_use > max_size; --chunk_num) {
    hl_offset = align_up(free_chunk_bitword_offset + ceil(chunk_num, 8), 64);
    data_offset =
        align_up(hl_offset + sizeof(*layout.hl->hdr_zone) * chunk_num, 4096);
    actual_use = data_offset + chunk_num * CHUNK_SIZE;
  }
  if (chunk_num == 0)
    throw "Out of memory";
  global_pool->actual_use_memory = actual_use;

  layout.gh = reinterpret_cast<mm_global_header *>(ADDR + gh_offset);
  layout.gh->chunk_total_num = chunk_num;
  layout.gh->end_offset = max_size;
  layout.gh->free_chunk_bitset_offset = free_chunk_bitset_offset;
  layout.gh->free_chunk_bitword_offset = free_chunk_bitword_offset;
  layout.gh->data_offset = data_offset;
  layout.gh->hl_offset = hl_offset;
  layout.gh->actual_use = actual_use;
  layout.gh->gh_offset = gh_offset;

  layout.free_chunk_bitset =
      reinterpret_cast<lfbitset_base *>(ADDR + free_chunk_bitset_offset);
  *layout.free_chunk_bitset = lfbitset_base(
      reinterpret_cast<uint64_t *>(ADDR + free_chunk_bitword_offset),
      chunk_num);
  layout.free_chunk_bitset->reset();

  layout.chunk_num = chunk_num;
  layout.hl = reinterpret_cast<mm_hdr_layout *>(ADDR + hl_offset);
  for (uint64_t i = 0; i < chunk_num; ++i) {
    auto &hz = layout.hl->hdr_zone[i];
    hz.ch.block_free_meta.free_block_bitset.reset();
  }
  layout.data_offset = data_offset;
  layout.data = ADDR + data_offset;

  chunk_metas = new dram_chunk_meta[chunk_num];
  if (chunk_metas == nullptr)
    throw "Out of memory";
  global_pool->meta_use_memory += sizeof(*chunk_metas) * chunk_num;
  block_metas = new dram_block_meta[chunk_num * BLOCK_NUM_PER_CHUNK];
  if (block_metas == nullptr)
    throw "Out of memory";
  global_pool->meta_use_memory +=
      sizeof(*block_metas) * chunk_num * BLOCK_NUM_PER_CHUNK;
  for (uint64_t i = 0; i < chunk_num; ++i) {
    auto &cm = chunk_metas[i];
    cm.id = i;
    cm.hdr = &layout.hl->hdr_zone[i].ch;
    cm.owner = nullptr;
    cm.free_block = nullptr;
    cm.lock = 0;
  }

  global_pool->chunk_used_num = 0;
  global_pool->huge_chunk_allocator.init(0, chunk_num);
  global_pool->almost_free_chunk = new ConQueue<dram_chunk_meta *>(chunk_num);
  if (global_pool->almost_free_chunk == nullptr)
    throw "Out of memory";
  global_pool->meta_use_memory +=
      sizeof(*global_pool->almost_free_chunk) +
      global_pool->almost_free_chunk->capacity() * 8;
  global_pool->almost_slice_free_chunk =
      new ConQueue<dram_chunk_meta *>(chunk_num);
  if (global_pool->almost_slice_free_chunk == nullptr)
    throw "Out of memory";
  global_pool->meta_use_memory +=
      sizeof(*global_pool->almost_slice_free_chunk) +
      global_pool->almost_slice_free_chunk->capacity() * 8;

  MM_FLUSH = mm_flush;
  MM_DRAIN = mm_drain;
  mm_flush(ADDR, data_offset);
  mm_drain();
  STATUS = true;
}

void env_recovery(void *addr, size_t max_size,
                  void (*mm_flush)(void *ptr, size_t size),
                  void (*mm_drain)()) {
  if (STATUS)
    return;
  ADDR = static_cast<char *>(addr);
  MAX_SIZE = max_size;

  global_pool = new dram_global_pool_meta();
  if (global_pool == nullptr)
    throw "Out of memory";
  global_pool->meta_use_memory = sizeof(*global_pool);
  layout_t &layout = global_pool->layout;

  uint64_t chunk_num;
  uint64_t gh_offset, free_chunk_bitset_offset, free_chunk_bitword_offset,
      hl_offset, data_offset, actual_use;

  gh_offset = 0;

  layout.gh = reinterpret_cast<mm_global_header *>(ADDR + gh_offset);

  chunk_num = layout.gh->chunk_total_num;
  free_chunk_bitset_offset = layout.gh->free_chunk_bitset_offset;
  free_chunk_bitword_offset = layout.gh->free_chunk_bitword_offset;
  hl_offset = layout.gh->hl_offset;
  data_offset = layout.gh->data_offset;
  actual_use = layout.gh->actual_use;
  if (actual_use > max_size)
    throw "Out of memory";

  layout.chunk_num = chunk_num;
  layout.hl = reinterpret_cast<mm_hdr_layout *>(ADDR + hl_offset);
  layout.free_chunk_bitset =
      reinterpret_cast<lfbitset_base *>(ADDR + free_chunk_bitset_offset);
  *layout.free_chunk_bitset = lfbitset_base(
      reinterpret_cast<uint64_t *>(ADDR + free_chunk_bitword_offset),
      chunk_num);
  layout.data_offset = data_offset;
  layout.data = ADDR + data_offset;

  chunk_metas = new dram_chunk_meta[chunk_num];
  if (chunk_metas == nullptr)
    throw "Out of memory";
  global_pool->meta_use_memory += sizeof(*chunk_metas) * chunk_num;
  block_metas = new dram_block_meta[chunk_num * BLOCK_NUM_PER_CHUNK];
  if (block_metas == nullptr)
    throw "Out of memory";
  global_pool->meta_use_memory +=
      sizeof(*block_metas) * chunk_num * BLOCK_NUM_PER_CHUNK;

  global_pool->huge_chunk_allocator.init(0, chunk_num);
  global_pool->almost_free_chunk = new ConQueue<dram_chunk_meta *>(chunk_num);
  if (global_pool->almost_free_chunk == nullptr)
    throw "Out of memory";
  global_pool->meta_use_memory +=
      sizeof(*global_pool->almost_free_chunk) +
      global_pool->almost_free_chunk->capacity() * 8;
  global_pool->almost_slice_free_chunk =
      new ConQueue<dram_chunk_meta *>(chunk_num);
  if (global_pool->almost_slice_free_chunk == nullptr)
    throw "Out of memory";
  global_pool->meta_use_memory +=
      sizeof(*global_pool->almost_slice_free_chunk) +
      global_pool->almost_slice_free_chunk->capacity() * 8;

  global_pool->chunk_used_num = 0;

  for (uint64_t i = 0; i < chunk_num; ++i) {
    auto &cm = chunk_metas[i];
    cm.id = i;
    cm.hdr = &layout.hl->hdr_zone[i].ch;
    cm.owner = nullptr;
    cm.lock = 0;

    if (!layout.free_chunk_bitset->test(
            global_pool->get_nbit_from_chunk_id(i))) {
      cm.free_block = nullptr;
    } else {
      ++global_pool->chunk_used_num;
      cm.type = cm.hdr->type;
      switch (cm.type) {
      case tSMALL:
        for (uint64_t j = 0; j < BLOCK_NUM_PER_CHUNK; ++j) {
          if (!cm.hdr->block_free_meta.free_block_bitset.test(j))
            continue;
          ++cm.block_used_num;
          auto &bm = block_metas[i * BLOCK_NUM_PER_CHUNK + j];
          bm.lock = 0;
          bm.slab_used_num = __builtin_popcount(
              *layout.hl->hdr_zone[i].bh[j].bitset.to_ulong());
          if (bm.slab_used_num == 0) {
            if (cm.free_block == nullptr) {
              cm.free_block = new ConQueue<block_id_t>(BLOCK_NUM_PER_CHUNK);
            }
            cm.free_block->push(j + 1);
          }
        }
        if (cm.block_used_num < BLOCK_NUM_PER_CHUNK) {
          global_pool->almost_free_chunk->push(&cm);
        }
        break;
      case tBIG:
        cm.huge_allocator.init(0, SLICE_UNIT_NUM_PER_CHUNK);
        for (uint64_t j = 0; j < SLICE_UNIT_NUM_PER_CHUNK; ++j) {
          if (!layout.hl->hdr_zone[i].slice_bitset.test(j)) {
            continue;
          }
          ++cm.slice_used_num;
          uint64_t size_off = i * CHUNK_SIZE + j * SLICE_UNIT_SIZE;
          size_t size = *(size_t *)(layout.data + size_off);
          uint64_t alloc_slice_num =
              ceil(size + sizeof(size_t), SLICE_UNIT_SIZE);
          cm.huge_allocator.alloc_placement(j, alloc_slice_num);
          j += alloc_slice_num - 1;
        }
        if (cm.huge_allocator.has_free_seg(SLAB_UNIT_SIZE * SIZE_CLASS_NUM +
                                           1)) {
          global_pool->almost_slice_free_chunk->push(&cm);
        }
        break;
      case tHUGE: {
        uint64_t alloc_chunk_num = ceil(cm.hdr->huge_size, CHUNK_SIZE);
        global_pool->huge_chunk_allocator.alloc_placement(i, alloc_chunk_num);
        i += alloc_chunk_num - 1;
      } break;
      }
    }
  }

  // TODO : huge_chunk_allocator, almost_free_chunk, almost_slice_free_chunk

  MM_FLUSH = mm_flush;
  MM_DRAIN = mm_drain;
  mm_flush(ADDR, data_offset);
  mm_drain();
  STATUS = true;
}

void mm_allocator::env_release() {
  STATUS = false;
  delete global_pool->almost_free_chunk;
  delete global_pool->almost_slice_free_chunk;
  for (uint64_t i = 0; i < global_pool->layout.chunk_num; ++i) {
    if (chunk_metas[i].free_block)
      delete chunk_metas[i].free_block;
  }
  delete chunk_metas;
  delete global_pool;
}

/**
 * @brief
 * @example
 *   slab   size=4*64B=256B  size_cls=3
 * |---^--|
 * [][][][][]...[] 64个slab unit，每个64B，共4KB
 * bitset: 1111000...
 *
 * @param block_offset
 * @return uint64_t slab在block中的位置
 */
static int alloc_slab_from_block(block_id_t block_id) {
  auto &mbh = global_pool->get_mm_block_header(block_id);
  int size_cls = mbh.size_cls;
  int bit_idx = mbh.bitset.ffs_and_set_bulk(size_cls + 1);
  if (bit_idx == -1)
    return -1;
  block_metas[block_id - 1].slab_used_num += size_cls + 1;
  mm_obj_sync(mbh.bitset.to_ulong());
  return bit_idx / (size_cls + 1);
}

static block_id_t alloc_block_from_chunk(int size_cls) {
  block_id_t block_id = 0;
  int block_idx;
  dram_chunk_meta *chunk_meta = theap.chunk;
  auto &bitset = chunk_meta->hdr->block_free_meta.free_block_bitset;
  chunk_meta->free_block->pop_out(&block_id);
  if (block_id == 0) {
    block_idx = bitset.ffs_and_set();
    if (block_idx == -1) {
      return 0;
    }
    ++chunk_meta->block_used_num;
    block_id = get_block_id(chunk_meta->id, block_idx);
  } else {
    get_block_idx(block_id, nullptr, &block_idx);
    bitset.set(block_idx);
    ++chunk_meta->block_used_num;
  }
  auto &mbh = global_pool->get_mm_block_header(block_id);
  mbh.size_cls = size_cls;
  mbh.bitset.reset();
  auto &bm = block_metas[block_id - 1];
  bm.slab_used_num = 0;
  bm.lock = 0;
  mm_obj_sync_nodrain(&mbh);
  mm_obj_sync_nodrain(bitset.to_ulong() + block_idx / 64);
  MM_DRAIN();
  return block_id;
}

static void theap_unload_chunk() {
  if (theap.chunk) {
    theap.chunk->owner = nullptr;
  }
  theap.chunk = nullptr;
  obj_clr(theap.slabs);
}

static dram_chunk_meta *alloc_chunk() {
  mm_global_header &mgh = *global_pool->layout.gh;
  if (global_pool->layout.chunk_num == global_pool->chunk_used_num) {
    return nullptr;
  }
  uint64_t raw_id = global_pool->layout.free_chunk_bitset->ffs_and_set();
  chunk_id_t chunk_id = global_pool->get_chunk_id_from_nbit(raw_id);
  if (chunk_id == -1)
    return nullptr;
  ++global_pool->chunk_used_num;
  return chunk_metas + chunk_id;
}

static int get_raw_chunk(chunk_id_t *chunk_id, chunk_type type) {
  dram_chunk_meta *chunk_meta = alloc_chunk();
  if (chunk_meta == nullptr)
    return -1;
  mm_chunk_header *ch = chunk_meta->hdr;
  *chunk_id = chunk_meta->id;
  ch->type = type;
  chunk_meta->type = type;
  chunk_meta->block_used_num = 0;
  if (type == tSMALL) {
    auto &bitset = ch->block_free_meta.free_block_bitset;
    ch->block_free_meta.free_block_bitset.reset();
    MM_FLUSH(bitset.to_ulong(), ceil(bitset.capacity(), sizeof(uint64_t)));
    chunk_meta->free_block = new ConQueue<block_id_t>(BLOCK_NUM_PER_CHUNK);
    global_pool->meta_use_memory +=
        sizeof(*chunk_meta->free_block) +
        chunk_meta->free_block->capacity() * sizeof(block_id_t);
  } else if (type == tBIG) {
    auto &bitset = global_pool->layout.hl->hdr_zone[*chunk_id].slice_bitset;
    new (&bitset) std::remove_reference<decltype(bitset)>::type();
    MM_FLUSH(bitset.to_ulong(), ceil(bitset.capacity(), sizeof(uint64_t)));
  }
  mm_obj_sync_nodrain(ch);
  mm_obj_sync_nodrain(global_pool->layout.free_chunk_bitset->to_ulong() +
                      global_pool->get_nbit_from_chunk_id(chunk_meta->id) / 64);
  MM_DRAIN();
  return 0;
}

/**
 * @brief
 *
 * @param mbh
 * @param off 基于数据区域的偏移
 */
void free_slab_to_block(mm_block_header &mbh, uint64_t off) {
  int size_cls = mbh.size_cls;
  int slab_unit_idx = (off % BLOCK_SIZE) / SLAB_UNIT_SIZE;
  mbh.bitset.reset_bulk(slab_unit_idx, size_cls + 1);
  mm_obj_sync(mbh.bitset.to_ulong());
}

static uint64_t small_alloc(int size_cls) {
  chunk_id_t chunk_id;
  block_id_t block_id = 0;
  int rc = 0;
  int slab_idx;
  dram_chunk_meta *new_chunk_meta = nullptr;
retry:
  // 检查线程slab[]中是否记录了block的索引
  if (theap.slabs[size_cls] != 0) {
    block_id = theap.slabs[size_cls];
    slab_idx = alloc_slab_from_block(block_id);
    if (slab_idx != -1)
      goto finish2;
  }

  theap.slabs[size_cls] = 0;
  barrier();
  if (theap.chunk != nullptr) {
    block_id = alloc_block_from_chunk(size_cls);
    if (block_id != 0) {
      // 将满的block从slabs[]中踢出，加入空的block
      theap.slabs[size_cls] = block_id;
      goto finish;
    }
  }

  do {
    global_pool->almost_free_chunk->pop_out(&new_chunk_meta);
  } while (new_chunk_meta != nullptr && new_chunk_meta->block_used_num == 0);

  if (new_chunk_meta != nullptr) {
    new_chunk_meta->lock ^= dram_chunk_meta::has_been_recycled;
    theap_unload_chunk();
    theap.chunk = new_chunk_meta;
    new_chunk_meta->owner = &theap;
    goto retry;
  }

  rc = get_raw_chunk(&chunk_id, tSMALL);
  if (rc != 0)
    return 0;
  theap_unload_chunk();
  theap.chunk = &chunk_metas[chunk_id];
  theap.chunk->free_block->clear();
  theap.chunk->owner = &theap;
  goto retry;

finish:
  slab_idx = alloc_slab_from_block(block_id);
finish2:
  return global_pool->layout.data_offset + (block_id - 1) * BLOCK_SIZE +
         slab_idx * (size_cls + 1) * SLAB_UNIT_SIZE;
}

static uint64_t big_alloc_in_chunk(size_t size) {
  chunk_id_t chunk_id;
  int rc = 0;
  uint64_t ret_off;
  uint64_t alloc_slice_num = ceil(size + sizeof(size_t), SLICE_UNIT_SIZE);
  size_t alloc_size = alloc_slice_num * SLAB_UNIT_SIZE;
retry:
  dram_chunk_meta *new_chunk_meta = nullptr;
  if (theap.huge_chunk != nullptr &&
      theap.huge_chunk->huge_allocator.has_free_seg(alloc_slice_num)) {
    chunk_id = theap.huge_chunk->id;
    dram_chunk_meta &chunk_meta = chunk_metas[chunk_id];
    uint64_t raw_slice_idx_pre =
        theap.huge_chunk->huge_allocator.alloc(alloc_slice_num);
    auto &bitset = global_pool->layout.hl->hdr_zone[chunk_id].slice_bitset;
    for (uint64_t i = 0; i < alloc_slice_num; ++i) {
      bitset.set(raw_slice_idx_pre + i);
    }
    ++theap.huge_chunk->slice_used_num;
    ret_off = chunk_id * CHUNK_SIZE + raw_slice_idx_pre * SLAB_UNIT_SIZE;
    *(size_t *)(global_pool->layout.data + ret_off) = size;
    MM_FLUSH(global_pool->layout.data + ret_off, sizeof(size_t));
    ret_off += global_pool->layout.data_offset + sizeof(size_t);
    MM_FLUSH(bitset.to_ulong() + raw_slice_idx_pre / 64,
             ceil(alloc_slice_num, sizeof(uint64_t)));
    MM_DRAIN();
    goto finish;
  }

  do {
    global_pool->almost_slice_free_chunk->pop_out(&new_chunk_meta);
  } while (new_chunk_meta != nullptr && new_chunk_meta->slice_used_num == 0);

  if (new_chunk_meta != nullptr) {
    new_chunk_meta->lock ^= dram_chunk_meta::has_been_recycled;
    if (theap.huge_chunk) {
      theap.huge_chunk->owner = nullptr;
    }
    theap.huge_chunk = new_chunk_meta;
    new_chunk_meta->owner = &theap;
    goto retry;
  }

  rc = get_raw_chunk(&chunk_id, tBIG);
  if (rc != 0)
    return 0;
  if (theap.huge_chunk) {
    theap.huge_chunk->owner = nullptr;
  }
  theap.huge_chunk = &chunk_metas[chunk_id];
  theap.huge_chunk->owner = &theap;
  theap.huge_chunk->slice_used_num = 0;
  theap.huge_chunk->huge_allocator.init(0, SLICE_UNIT_NUM_PER_CHUNK);
  goto retry;
finish:
  return ret_off;
}

static uint64_t huge_alloc_multi_chunk(size_t size) {
  std::vector<chunk_id_t> junk;
  uint64_t alloc_chunk_num = ceil(size, CHUNK_SIZE);
retry:
  chunk_id_t chunk_id_pre =
      global_pool->huge_chunk_allocator.alloc(alloc_chunk_num);
  if (chunk_id_pre == -1)
    return 0;
  for (uint64_t i = 0; i < alloc_chunk_num; ++i) {
    if (global_pool->layout.free_chunk_bitset->set(chunk_id_pre + i)) {
      for (uint64_t j = 0; j < i; ++j) {
        global_pool->layout.free_chunk_bitset->reset(chunk_id_pre + j);
      }
      junk.push_back(chunk_id_pre);
      goto retry;
    }
  }
  global_pool->chunk_used_num += alloc_chunk_num;

  dram_chunk_meta &chunk_meta = chunk_metas[chunk_id_pre];
  chunk_meta.type = tHUGE;

  uint64_t ret_off =
      global_pool->layout.data_offset + chunk_id_pre * CHUNK_SIZE;
  for (chunk_id_t c : junk) {
    global_pool->huge_chunk_allocator.free(c, alloc_chunk_num);
  }
  chunk_meta.hdr->type = tHUGE;
  chunk_meta.hdr->huge_size = size;
  mm_obj_sync_nodrain(chunk_meta.hdr);
  MM_FLUSH(global_pool->layout.free_chunk_bitset->to_ulong() +
               chunk_id_pre / 64,
           ceil(alloc_chunk_num, sizeof(uint64_t)));
  MM_DRAIN();
  return ret_off;
}

static uint64_t huge_alloc(size_t size) {
  uint64_t ret_off;
  if (size <= CHUNK_SIZE) {
    ret_off = big_alloc_in_chunk(size);
  } else {
    ret_off = huge_alloc_multi_chunk(size);
  }
  return ret_off;
}

mm_allocator::mm_ptr<void> mm_allocator::mm_alloc(size_t __size) {
  if (!STATUS)
    throw "mm env not init";
  uint64_t off;
  mm_allocator::mm_ptr<char> ptr = mm_allocator::mm_nullptr;
  if (__size == 0)
    return mm_allocator::mm_cast<void>(ptr);
  int size_cls = (__size - 1) / SLAB_UNIT_SIZE;
  if (size_cls < SIZE_CLASS_NUM) {
    off = small_alloc(size_cls);
    ptr += off;
    return mm_allocator::mm_cast<void>(ptr);
  } else {
    off = huge_alloc(__size);
    ptr += off;
    return mm_allocator::mm_cast<void>(ptr);
  }
}

void mm_allocator::mm_free(mm_allocator::mm_ptr<void> __ptr) {
  uint64_t off = __ptr;
  if (off < global_pool->layout.data_offset)
    throw "free failure";
  off -= global_pool->layout.data_offset;
  chunk_id_t chunk_id = off / CHUNK_SIZE;
  dram_chunk_meta &chunk_meta = chunk_metas[chunk_id];
  if (chunk_meta.type == tSMALL) {
    block_id_t block_id = off / BLOCK_SIZE + 1;
    int block_idx;
    get_block_idx(block_id, nullptr, &block_idx);
    mm_block_header &mbh = global_pool->get_mm_block_header(block_id);
    auto &bm = block_metas[block_id - 1];
    int ver = bm.ver;
    free_slab_to_block(mbh, off);
    bm.slab_used_num -= mbh.size_cls + 1;
    /**
     * 其他线程释放slab以致block为空时，持有block权的线程毫无感知
     * 如果block已经从slabs[]中踢出，则意味着这个block不能重用
     * 需要释放线程主动将block加入到free_block队列中
     */
    /**
     * 多线程竞争问题：
     * 1. 同时判空，同时加入队列
     * 2. 如果block还在其他线程theap的slabs[]中
     *    不能加入到队列
     */
    if (bm.slab_used_num == 0) {
      if ((dram_block_meta::recycle_block &
           bm.lock.fetch_or(dram_block_meta::recycle_block)) ||
          ver != bm.ver) {
        return;
      }
      int size_cls = mbh.size_cls;
      if (theap.slabs[size_cls] == block_id) {
        theap.slabs[size_cls] = 0;
      }
      auto &free_block_bitset =
          chunk_meta.hdr->block_free_meta.free_block_bitset;
      /**
       * 如果重新分配空间导致slabs[]踢出，判断通过，需要再次判断block是否为空
       */
      barrier();
      if (bm.slab_used_num == 0 &&
          (chunk_meta.owner == nullptr ||
           chunk_meta.owner->slabs[size_cls] != block_id)) {
        chunk_meta.free_block->push(block_id);
        free_block_bitset.reset(block_idx);
        --chunk_meta.block_used_num;
        mm_obj_sync(free_block_bitset.to_ulong() + block_idx / 64);
      }
      ++bm.ver;
      bm.lock ^= dram_block_meta::recycle_block;
      /**
       * 其他线程释放block以致chunk为空时，持有chunk权的线程毫无感知
       * 如果chunk已经从theap中踢出，则意味着这个chunk不能重用
       * 需要释放线程主动将chunk加入到free_chunk队列中
       */
      /**
       * 完全释放的chunk，允许不用reset其bit，只将它加入到free list中即可
       * 在恢复的时候重建free list
       *
       * 在判断chunk是否为空的情况，需要结合上文的bitset.reset()，情况复杂
       */
      if (dram_chunk_meta::recycle_block &
          chunk_meta.lock.fetch_or(dram_chunk_meta::recycle_block)) {
        return;
      }
      /**
       * 此处可能出现判错
       */
      if (chunk_meta.block_used_num == 0) {
        if (chunk_meta.owner == nullptr || chunk_meta.owner == &theap) {
          global_pool->meta_use_memory -=
              sizeof(*chunk_meta.free_block) +
              chunk_meta.free_block->capacity() * sizeof(block_id_t);
          delete chunk_meta.free_block;
          chunk_meta.free_block = nullptr;
          uint64_t raw_id_bit = global_pool->get_nbit_from_chunk_id(chunk_id);
          global_pool->layout.free_chunk_bitset->reset(raw_id_bit);
          --global_pool->chunk_used_num;
          mm_obj_sync(global_pool->layout.free_chunk_bitset->to_ulong() +
                      raw_id_bit / 64);
        }
        if (chunk_meta.owner == &theap) {
          theap.chunk = nullptr;
          chunk_meta.owner = nullptr;
        }
      } else if (chunk_meta.owner == nullptr &&
                 !(dram_chunk_meta::has_been_recycled &
                   chunk_meta.lock.fetch_or(
                       dram_chunk_meta::has_been_recycled))) {
        global_pool->almost_free_chunk->push(&chunk_meta);
      }
      chunk_meta.lock ^= dram_chunk_meta::recycle_block;
    }
  } else if (chunk_meta.type == tBIG) {
    // HUGE
    off -= sizeof(size_t);
    size_t size = *(size_t *)(global_pool->layout.data + off);
    uint64_t raw_slice_idx_pre = off / SLICE_UNIT_SIZE;
    uint64_t alloc_slice_num = ceil(size + sizeof(size_t), SLICE_UNIT_SIZE);
    auto &bitset = global_pool->layout.hl->hdr_zone[chunk_id].slice_bitset;
    bitset.reset_bulk(raw_slice_idx_pre, alloc_slice_num);
    MM_FLUSH(bitset.to_ulong() + raw_slice_idx_pre / 64,
             ceil(alloc_slice_num, sizeof(uint64_t)));
    chunk_meta.huge_allocator.free(raw_slice_idx_pre, alloc_slice_num);
    --chunk_meta.slice_used_num;
    if (dram_chunk_meta::recycle_block &
        chunk_meta.lock.fetch_or(dram_chunk_meta::recycle_block)) {
      return;
    }
    if (chunk_meta.slice_used_num == 0) {
      if (chunk_meta.owner == nullptr || chunk_meta.owner == &theap) {
        uint64_t raw_id_bit = global_pool->get_nbit_from_chunk_id(chunk_id);
        global_pool->layout.free_chunk_bitset->reset(raw_id_bit);
        --global_pool->chunk_used_num;
        mm_obj_sync_nodrain(global_pool->layout.free_chunk_bitset->to_ulong() +
                            raw_id_bit / 64);
      }
      if (chunk_meta.owner == &theap) {
        theap.huge_chunk = nullptr;
        chunk_meta.owner = nullptr;
      }
    } else if (chunk_meta.owner == nullptr &&
               !(dram_chunk_meta::has_been_recycled &
                 chunk_meta.lock.fetch_or(
                     dram_chunk_meta::has_been_recycled))) {
      global_pool->almost_slice_free_chunk->push(&chunk_meta);
    }
    chunk_meta.lock ^= dram_chunk_meta::recycle_block;
    MM_DRAIN();
  } else {
    uint64_t alloc_chunk_num = ceil(chunk_meta.huge_size, CHUNK_SIZE);
    global_pool->layout.free_chunk_bitset->reset_bulk(chunk_id,
                                                      alloc_chunk_num);
    MM_FLUSH(global_pool->layout.free_chunk_bitset->to_ulong() + chunk_id / 64,
             ceil(alloc_chunk_num, sizeof(uint64_t)));
    MM_DRAIN();
  }
}

void *mm_allocator::mm_ptr_at(uint64_t off) {
  if (!STATUS)
    throw "mm env not init";
  if (off < global_pool->layout.data_offset || off >= MAX_SIZE)
    throw "Memory overflow";
  return ADDR + off;
}

void mm_allocator::mm_print_stat(FILE *__restrict stream) {

#define PRINT(format, ...) fprintf(stream, format "\n", ##__VA_ARGS__)
#define PRINTt1(format, ...) fprintf(stream, "\t" format "\n", ##__VA_ARGS__)
#define PRINTt2(format, ...) fprintf(stream, "\t\t" format "\n", ##__VA_ARGS__)
#define PRINTt3(format, ...)                                                   \
  fprintf(stream, "\t\t\t" format "\n", ##__VA_ARGS__)
#define PRINTtn(n, format, ...) PRINTt##n(format, ##__VA_ARGS__);
#define PRINTV(val, format) PRINT(#val ": " format, val)
#define PRINTVtn(n, val, format) PRINTtn(n, #val ": " format, val)
#define PRINTatoV(val, format) PRINT(#val ": " format, val.load())
#define PRINTatoVtn(n, val, format) PRINTtn(n, #val ": " format, val.load())

  if (!STATUS) {
    PRINT("Not Ready");
    return;
  }

  PRINT("================ Global ================");
  PRINT();

  PRINTV(SLAB_UNIT_SIZE, "%d");
  PRINTV(SIZE_CLASS_NUM, "%d");
  PRINTV(BLOCK_SIZE, "%d");
  PRINTV(CHUNK_SIZE, "%d");
  PRINTV(BLOCK_NUM_PER_CHUNK, "%d");

  PRINT();

  PRINTV(ADDR, "%p");
  PRINTV(MAX_SIZE, "%lu");

  PRINT();

  PRINTV(global_pool, "%p");
  PRINTV(chunk_metas, "%p");
  PRINTV(block_metas, "%p");

  PRINT();

  PRINTV(global_pool->layout.gh, "%p");
  PRINTV(global_pool->layout.free_chunk_bitset, "%p");
  PRINTV(global_pool->layout.hl, "%p");
  PRINTV(global_pool->layout.data, "%p");

  PRINT();

  PRINTV(global_pool->layout.chunk_num, "%lu");
  PRINTV(global_pool->layout.data_offset, "%lu");

  PRINT();

  PRINTV(global_pool->actual_use_memory, "%lu");
  PRINTV(global_pool->meta_use_memory, "%lu");

  PRINT();

  PRINT("============= Global Pool ==============");
  PRINT();

  PRINTatoV(global_pool->chunk_used_num, "%lu");

  PRINT();

  PRINT("Chunk bitset:");
  fprintf(stream, "\t");
  for (int j = 0; j < ceil(global_pool->layout.chunk_num, 64); ++j) {
    auto w = *(global_pool->layout.free_chunk_bitset->to_ulong() + j);
    fprintf(stream, "%016lx ", w);
  }
  PRINT();
  PRINT();

  for (uint64_t i = 0; i < global_pool->layout.chunk_num; ++i) {
    if (!global_pool->layout.free_chunk_bitset->test(
            global_pool->get_nbit_from_chunk_id(i)))
      continue;
    dram_chunk_meta &cm = chunk_metas[i];
    chunk_id_t chunk_id = cm.id;
    PRINT("Chunk %lu: ", chunk_id);
#define PRINTVtn_ref(n, cm, val, format)                                       \
  ({                                                                           \
    auto &val = cm.val;                                                        \
    PRINTVtn(n, val, format);                                                  \
  })
#define PRINTatoVtn_ref(n, cm, val, format)                                    \
  ({                                                                           \
    auto &val = cm.val;                                                        \
    PRINTatoVtn(n, val, format);                                               \
  })
    PRINTVtn_ref(1, cm, id, "%lu");
    PRINTVtn_ref(1, cm, owner, "%p");
    PRINTVtn_ref(1, cm, hdr, "%p");
    PRINTtn(1, "type: %s", chunk_type_to_str(cm.type));
    switch (cm.type) {
    case tSMALL:
      PRINTatoVtn_ref(2, cm, block_used_num, "%u");
      PRINTtn(2, "Block bitset: ");
      fprintf(stream, "\t\t\t");
      for (int j = 0; j < BLOCK_NUM_PER_CHUNK / 64; ++j) {
        auto w = *(cm.hdr->block_free_meta.free_block_bitset.to_ulong() + j);
        fprintf(stream, "%016lx ", w);
      }
      PRINT();
      PRINT();
      for (int j = 0; j < BLOCK_NUM_PER_CHUNK; ++j) {
        if (!cm.hdr->block_free_meta.free_block_bitset.test(j))
          continue;
        block_id_t block_id = get_block_id(i, j);
        dram_block_meta &bm = block_metas[block_id - 1];
        auto mbh = global_pool->get_mm_block_header(block_id);
        PRINTtn(2, "Block %lu: ", block_id);

        PRINTatoVtn_ref(3, bm, slab_used_num, "%u");
        PRINTVtn_ref(3, mbh, size_cls, "%d");
        auto bitset = *mbh.bitset.to_ulong();
        PRINTVtn(3, bitset, "%#016lx");
      }
      break;
    case tBIG:
      PRINTatoVtn_ref(2, cm, slice_used_num, "%u");
      PRINTtn(2, "Slice bitset: ");
      fprintf(stream, "\t\t\t");
      for (int j = 0; j < SLICE_UNIT_NUM_PER_CHUNK / 64; ++j) {
        auto w =
            *(global_pool->layout.hl->hdr_zone[i].slice_bitset.to_ulong() + j);
        fprintf(stream, "%016lx ", w);
      }
      PRINT();
      PRINT();
      for (int j = 0; j < SLICE_UNIT_NUM_PER_CHUNK; ++j) {
        if (!global_pool->layout.hl->hdr_zone[i].slice_bitset.test(j))
          continue;
        uint64_t size_off = i * CHUNK_SIZE + j * SLICE_UNIT_SIZE;
        size_t size = *(size_t *)(global_pool->layout.data + size_off);
        PRINTtn(2, "Slice pre: %d, Size: %lu", j, size);
        j += ceil(size + sizeof(size_t), SLICE_UNIT_SIZE) - 1;
      }
      break;
    case tHUGE:
      PRINTVtn_ref(2, cm, huge_size, "%lu");
      fprintf(stream, "\tcross chunk: ");
      for (uint64_t j = 0; j < ceil(cm.huge_size, CHUNK_SIZE); ++j) {
        fprintf(stream, "%lu ", cm.id + j);
      }
      PRINT();
      i += ceil(cm.huge_size, CHUNK_SIZE) - 1;
      break;
    }

#undef PRINTatoVtn_ref
#undef PRINTVtn_ref
  }

  PRINT();

  PRINT("================= END =================");

#undef PRINT
#undef PRINTt1
#undef PRINTt2
#undef PRINTt3
#undef PRINTtn
#undef PRINTV
#undef PRINTVtn
#undef PRINTatoV
#undef PRINTatoVtn
}