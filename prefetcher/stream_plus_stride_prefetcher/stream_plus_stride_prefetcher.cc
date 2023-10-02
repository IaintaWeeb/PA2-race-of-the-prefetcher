#include <algorithm>
#include <array>
#include <map>

#include "cache.h"

constexpr int PREFETCH_DEGREE = 2;
constexpr int PREFETCH_DISTANCE = 16;

struct stream_tracker {
  uint64_t start_address = 0; // Start address of the Tracked Access Stream
  uint64_t end_address = 0; // End address of the Tracked Access Stream
  int state = 0; // State of the Tracker
  int64_t dir = 0; // Direction of the Tracked Access Stream
  uint64_t last_cl_address = 0;    // the last Cache miss within this stream

  uint64_t last_used_cycle = 0; // use LRU to evict old IP trackers

  int stride = 0; // the stride between the last two cache line addresses 
};

// dir : direction is -1 for descending, 1 for ascending and 0 for not determined
struct lookahead_entry {
  uint64_t address = 0;
  int64_t dir = 0;
  int degree = 0; // degree remaining
  int stride = 0;
};

constexpr std::size_t STREAM_SIZE = 64;
std::map<CACHE*, lookahead_entry> lookahead;
std::map<CACHE*, std::array< stream_tracker, STREAM_SIZE>> trackers;

void CACHE::prefetcher_initialize() {std::cout << NAME << " Stream Plus Stride Prefetcher" << std::endl;}

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in) {   
  uint64_t cl_addr = addr >> LOG2_BLOCK_SIZE;
  int64_t dir = 0;
  int stride = 0;

  // get boundaries of tracking sets
  auto set_begin = std::begin(trackers[this]);
  auto set_end = std::prev(std::end(trackers[this]));

  // find the current data access stream within the trackers
  auto found = std::find_if(set_begin, set_end, [=](stream_tracker x) { return abs((int64_t)cl_addr - (int64_t)x.start_address) < PREFETCH_DISTANCE && x.state;});

  // if we found a matching entry
  if ((found != set_end)  && (abs((int64_t)cl_addr - (int64_t)found->last_cl_address) < 16)) {
    // calculate the stride and direction between the current address and the last address
    dir = ((int64_t)cl_addr > (int64_t)found->last_cl_address)? 1 : -1;
    stride = abs((int64_t)cl_addr - (int64_t)found->last_cl_address);

    // Initialize prefetch state unless we somehow saw the same address twice in
    // a row or if this is the first time we've seen this stride
    if (dir == found->dir and stride == found->stride){ // Direction and stride are trained and we start prefetching
      found->end_address = found->start_address + dir * PREFETCH_DISTANCE;
      lookahead[this] = {found->end_address, dir, PREFETCH_DEGREE, stride};
      *found = {found->start_address + dir * PREFETCH_DEGREE * stride, found->end_address + dir * (PREFETCH_DEGREE*stride), 3, dir, cl_addr, current_cycle, stride};
    }
    else if(dir == found->dir and stride != found->stride){ // direction trained but stride is at 1st occurrence
      *found = {found->start_address, 0, 2, dir, cl_addr, current_cycle, stride}; 
    }
    else if(dir != found->dir and stride == found->stride){ // stride trained but direction is at 1st occurrence
      *found = {cl_addr, 0, 2, dir, cl_addr, current_cycle, stride};
    }
    else if(dir != found->dir and stride != found->stride){ // direction and stride are at 1st occurrence
      if(found->dir == 0) *found = {found->start_address, 0, 2, dir, cl_addr, current_cycle, stride};
      else *found = {cl_addr, 0, 2, dir, cl_addr, current_cycle, stride};
    }
  }
  else {
    // replace by LRU
    found = std::min_element(set_begin, set_end, [](stream_tracker x, stream_tracker y) { return x.last_used_cycle < y.last_used_cycle; });
    *found = {cl_addr, 0, 1, 0, cl_addr, current_cycle, stride};
  }

  return metadata_in; 
}

uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
{ 
  return metadata_in;
}

void CACHE::prefetcher_cycle_operate() {
    // If a lookahead is active
  if (auto [old_pf_address, dir, degree, stride] = lookahead[this]; degree > 0 and stride != 0) {
    auto pf_address = old_pf_address + dir * (stride << LOG2_BLOCK_SIZE); 

    // If the next step would exceed the degree or run off the page, stop
    if (virtual_prefetch || (pf_address >> LOG2_PAGE_SIZE) == (old_pf_address >> LOG2_PAGE_SIZE)) {
      // check the MSHR occupancy to decide if we're going to prefetch to this
      // level or not
      bool success = prefetch_line(0, 0, pf_address, (get_occupancy(0, pf_address) < get_size(0, pf_address) / 2), 0);
      if (success)
        lookahead[this] = {pf_address, dir, degree - 1};
      // If we fail, try again next cycle
    } else {
      lookahead[this] = {};
    }
  }
}

void CACHE::prefetcher_final_stats() {}