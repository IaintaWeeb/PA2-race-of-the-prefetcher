#include <algorithm>
#include <array>
#include <map>
#include<bits/stdc++.h>
#include "cache.h"

//This is the initial value which would be changed 3 parameters: Accuracy, Lateness and Pollution
int PREFETCH_DEGREE =4;

//other global variables
long long unsigned int prefetch_total=0;
long long no_instructions=1;

// long long polution_total=0;
// long long misses_total=0;
// vector<bool> Pollution_filter((1<<24),0);

//Dynamic control
  //Prefetch degree boundation
  int PREFETCH_DEGREE_min=1; //Wouldn't go below this
  int PREFETCH_DEGREE_max=7; //Wouldn't go beyond this

  //Parameter counters
  double PF_Accuracy=50;    // (# of useful PF )/( # PF sent to memory)
  double PF_Lateness=50;    // (# of useful PF )/( # PF sent to memory)
//  double PF_Pollution=50;   //  Got by some filter

  //Parameter comparing values
  double PF_Acc_hi=75;  //Accuracy
  double PF_Acc_lo=40;
  double PF_Late_TH=1;  //Lateness threshold
//  double PF_Pol_TH=0.5;   //Pollution threshold

  //lookup table for aggressiveness
  //Stride_LT[Accuracy][Latness][Pollution]
  //0- not late/polluting   ||  1- late / polluting
  //2-high , 1 - mid , 0-low 
  int Stride_LT[3][2]={{-1,1},{0,1},{0,1}};


struct tracker_entry {
  uint64_t ip = 0;              // the IP we're tracking
  uint64_t last_cl_addr = 0;    // the last address accessed by this IP
  int64_t last_stride = 0;      // the stride between the last two addresses accessed by this IP
  uint64_t last_used_cycle = 0; // use LRU to evict old IP trackers
};

struct lookahead_entry {
  uint64_t address = 0;
  int64_t stride = 0;
  int degree = 0; // degree remaining
};



constexpr std::size_t TRACKER_SETS = 256;
constexpr std::size_t TRACKER_WAYS = 4;
std::map<CACHE*, lookahead_entry> lookahead;
std::map<CACHE*, std::array<tracker_entry, TRACKER_SETS * TRACKER_WAYS>> trackers;

double maxm(double a,double b){
  if(a>b)return a;
  return b;
}

void CACHE::prefetcher_initialize() { std::cout << NAME << " IP-based stride prefetcher" << std::endl; }


//called on each cycle
void CACHE::prefetcher_cycle_operate()
{
  // If a lookahead is active
  if (auto [old_pf_address, stride, degree] = lookahead[this]; degree > 0) {
    auto pf_address = old_pf_address + (stride << LOG2_BLOCK_SIZE);

    // If the next step would exceed the degree or run off the page, stop
    if (virtual_prefetch || (pf_address >> LOG2_PAGE_SIZE) == (old_pf_address >> LOG2_PAGE_SIZE)) {
      // check the MSHR occupancy to decide if we're going to prefetch to this
      // level or not
      bool success = prefetch_line(0, 0, pf_address, (get_occupancy(0, pf_address) < get_size(0, pf_address) / 2), 0);
      if (success)
        lookahead[this] = {pf_address, stride, degree - 1};
      // If we fail, try again next cycle
    } 
    else {
      lookahead[this] = {};
    }
  }

  prefetch_total+=PREFETCH_DEGREE;
  no_instructions++;
  //Dynamic control metrics for prefetching
  PF_Accuracy=(100.0*pf_useful)/maxm(pf_useful+pf_useless,0.01);
  PF_Lateness=(100*pf_late)/maxm(pf_useful,0.01);
//  PF_Pollution=100;

  //determining index for lookup table depending on metrics comparison with thresholds;
  int A=1,L=0;
  if(PF_Accuracy<=PF_Acc_lo) A=0;
  else if(PF_Accuracy<=PF_Acc_lo) A=2;
  if(PF_Lateness>=PF_Late_TH) L=1;
//  if(PF_Pollution>=PF_Pol_TH) P=1;

  //Updating prefetch degree
  
  PREFETCH_DEGREE=min(max(PREFETCH_DEGREE+Stride_LT[A][L],PREFETCH_DEGREE_min),PREFETCH_DEGREE_max);

}

//called on a cache access both mit and hit
uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in)
{
  uint64_t cl_addr = addr >> LOG2_BLOCK_SIZE;
  int64_t stride = 0;


  // get boundaries of tracking set
  auto set_begin = std::next(std::begin(trackers[this]), ip % TRACKER_SETS);
  auto set_end = std::next(set_begin, TRACKER_WAYS);

  // find the current ip within the set
  auto found = std::find_if(set_begin, set_end, [ip](tracker_entry x) { return x.ip == ip; });

  // if we found a matching entry
  if (found != set_end) {
    // calculate the stride between the current address and the last address
    // no need to check for overflow since these values are downshifted
    stride = (int64_t)cl_addr - (int64_t)found->last_cl_addr;

    // Initialize prefetch state unless we somehow saw the same address twice in
    // a row or if this is the first time we've seen this stride
    if (stride != 0 && stride == found->last_stride)
      lookahead[this] = {cl_addr, stride, PREFETCH_DEGREE};
  } else {
    // replace by LRU
    found = std::min_element(set_begin, set_end, [](tracker_entry x, tracker_entry y) { return x.last_used_cycle < y.last_used_cycle; });
  }

  // update tracking set
  *found = {ip, cl_addr, stride, current_cycle};

  return metadata_in;
}

//Called whhen cache gets the data from the memory
uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}

void CACHE::prefetcher_final_stats() {

    cout<<"DYNAMIC PREFETCHER STATS   Average value: "<<prefetch_total/no_instructions<<endl;
}
