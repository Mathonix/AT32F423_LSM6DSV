// Independently implemented from the algorithm specification in
// docs/REFERENCE_MECHANISMS_SPEC.md. No CH32 source implementation is included.
#ifndef REFERENCE_REST_HPP
#define REFERENCE_REST_HPP
#include <cmath>
#include <cstdint>
#include <cstring>
namespace reference_rest {
constexpr float rad=.017453292519943295f;
constexpr unsigned window_size=122, rollback_slots=24;
#ifndef APP_REFERENCE_HISTORY_DEPTH
#define APP_REFERENCE_HISTORY_DEPTH 64
#endif
#ifndef APP_REFERENCE_FAST
#define APP_REFERENCE_FAST 0
#endif
static_assert(APP_REFERENCE_HISTORY_DEPTH>rollback_slots,"history must retain the current and 24 prior snapshots");
template<unsigned Depth, bool Fast=false> struct TrackerT {
 static_assert(Depth>rollback_slots,"insufficient rollback history");
 float window[window_size][3],sum[3],square[3],previous[3];
 float bias[3],history[Depth][3];
 uint64_t time_ticks,next_snapshot,learn_ticks;
 float level,ac_energy,total_energy;
 uint32_t rollback_events,rolled_snapshots;
 uint16_t head,fill,high_run,low_run,ac_run,evidence,static_snaps,history_head;
 bool stationary,previous_stationary,boot_done,enabled;
 void reset() {
  std::memset(this,0,sizeof(*this));stationary=previous_stationary=enabled=true;
 }
 float threshold() const {
  // Numeric threshold specification (dps), not source implementation text.
  static const float values[20]={.3246f,.3246f,.3246f,.3246f,.3246f,.3054f,.2549f,.2156f,.1854f,.1628f,
    .1462f,.1342f,.1258f,.1200f,.1160f,.1134f,.1116f,.1104f,.1096f,.1091f};
  const unsigned slot=evidence>>7;return values[slot<20?slot:19];
 }
 void step(const float input_rad[3],float dt,float corrected_rad[3]) {
  // All entry validation precedes changes to this foreground-owned object.
  if(!(dt>0.f&&dt<=.005f)||!std::isfinite(dt))return;
  for(unsigned i=0;i<3;i++)if(!std::isfinite(input_rad[i]))return;
  // dt<=5ms => rounded tick value <=500001, exactly convertible through
  // uint32_t. The fast path avoids software float<->uint64 helper calls.
  const uint64_t ticks=Fast?(uint32_t)(dt*1.e8f+.5f):(uint64_t)(dt*1.e8f+.5f);
  time_ticks+=ticks;
  if(!next_snapshot)next_snapshot=time_ticks+2000000ULL;
  for(unsigned axis=0;axis<3;axis++) {
   const float old=window[head][axis],value=previous[axis];
   sum[axis]+=value-old;square[axis]+=value*value-old*old;
   window[head][axis]=value;
  }
  if(++head==window_size)head=0;
  if(fill<window_size)++fill;
  const float inv=1.f/(float)fill;
  level=ac_energy=total_energy=0;
  for(unsigned axis=0;axis<3;axis++) {
   const float mean=sum[axis]*inv;
   if(Fast) {
    const float absolute=std::fabs(mean);
#if defined(__arm__)
    // Match the frozen Q ARM object's contraction exactly: rounded -mean^2,
    // then VFMA(square,inv,negative_square), and fused total accumulation.
    const float negative_square=-mean*mean;
    const float variance=__builtin_fmaf(square[axis],inv,negative_square);
    total_energy=__builtin_fmaf(square[axis],inv,total_energy);
#else
    const float ms=square[axis]*inv,variance=ms-mean*mean;
    total_energy+=ms;
#endif
    // Valid bounded gyro input and convex EMA keep these operands finite.
    // abs(mean) is nonnegative; variance<=0 returns positive zero.
    level=absolute>level?absolute:level;
    ac_energy+=variance>0.f?variance:0.f;
   } else {
    const float ms=square[axis]*inv;
    level=std::fmax(level,std::fabs(mean));
    ac_energy+=std::fmax(0.f,ms-mean*mean);total_energy+=ms;
   }
  }
  // The reference's bandwidth normalization is clamped to >=1; at this
  // target's nominal 125us it is also 1. No decimation/block approximation.
  ac_run=(ac_energy<.20f&&total_energy<4.f)?(ac_run<8?ac_run+1:8):0;
  if(fill<window_size || (!boot_done&&ac_run==8)) {
   stationary=true;high_run=low_run=0;
  } else if(stationary) {
   high_run=level>threshold()?high_run+1:0;
   if(high_run>=8){stationary=false;high_run=0;}
  } else {
   low_run=level<threshold()*.6f?low_run+1:0;
   if(low_run>=800){stationary=true;low_run=0;}
  }
  if(enabled&&previous_stationary&&!stationary&&learn_ticks) {
   const unsigned back=static_snaps<rollback_slots?static_snaps:rollback_slots;
   if(back) {
    const unsigned slot=(history_head+Depth-back)%Depth;
    for(unsigned axis=0;axis<3;axis++)bias[axis]=history[history_head][axis]=history[slot][axis];
    const uint64_t loss=(uint64_t)back*2000000ULL;
    learn_ticks=learn_ticks>loss?learn_ticks-loss:0;
    evidence=evidence>back?evidence-back:0;
    ++rollback_events;rolled_snapshots+=back;
   }
  }
  if(enabled&&stationary) {
   const float alpha=(Fast?(float)(uint32_t)ticks:(float)ticks)*(boot_done?1.f/1200000000.f:1.f/100000000.f);
   for(unsigned axis=0;axis<3;axis++)bias[axis]+=alpha*(input_rad[axis]-bias[axis]);
   learn_ticks+=ticks;if(learn_ticks>=300000000ULL)boot_done=true;
  }
  if(time_ticks>=next_snapshot) {
   if(++history_head==Depth)history_head=0;
   for(unsigned axis=0;axis<3;axis++)history[history_head][axis]=bias[axis];
   if(stationary&&enabled){if(static_snaps<64)++static_snaps;if(evidence<2560)++evidence;}
   else static_snaps=0;
   // A long input interval cannot fabricate multiple observed snapshots.
   next_snapshot+=((time_ticks-next_snapshot)/2000000ULL+1)*2000000ULL;
  }
  for(unsigned axis=0;axis<3;axis++) {
   corrected_rad[axis]=input_rad[axis]-bias[axis];previous[axis]=corrected_rad[axis]/rad;
  }
  previous_stationary=stationary;
 }
};
using Tracker=TrackerT<APP_REFERENCE_HISTORY_DEPTH,(APP_REFERENCE_FAST!=0)>;
}
#endif
