#ifndef ESKF_BIAS_LEARNING_HPP
#define ESKF_BIAS_LEARNING_HPP
#include <cmath>
namespace ahrs_eskf8k {
/* Explicitly selected policy: gravity cannot identify heading-axis bias.
 * Remove that component from each EMA increment, preserving the startup
 * calibration. This is not a new observation and does not change q/P math.
 * Gravity direction may change when the board tilts; the invariant is local
 * to the current gravity vector, not a fixed body-Z-axis assumption. */
inline bool update_observable_bias(float bias[3],const float gyro[3],
                                   const float gravity[3],float alpha) {
  float innovation[3],normal2=0.f,vertical=0.f;
  for(int i=0;i<3;++i) {
    innovation[i]=gyro[i]-bias[i];
    normal2+=gravity[i]*gravity[i];
    vertical+=gravity[i]*innovation[i];
  }
  if(!(normal2>1.e-12f) || !std::isfinite(normal2) ||
     !(alpha>0.f&&alpha<=1.f))return false;
  vertical/=normal2;
  float next[3];
  constexpr float limit=2.f*0.017453292519943295f;
  for(int i=0;i<3;++i) {
    next[i]=bias[i]+alpha*(innovation[i]-gravity[i]*vertical);
    /* Reject the entire step at the existing safety limit. Per-axis resets
     * would inject an unintended component along tilted gravity. */
    if(!std::isfinite(next[i]) || std::fabs(next[i])>limit)return false;
  }
  for(int i=0;i<3;++i)bias[i]=next[i];
  return true;
}
#ifndef APP_ESKF_REST_BIAS_LEARN_YAW
#define APP_ESKF_REST_BIAS_LEARN_YAW 0
#endif
#ifndef APP_ESKF_REST_YAW_TAU_S
#define APP_ESKF_REST_YAW_TAU_S 12.0f
#endif
#ifndef APP_ESKF_REST_YAW_CLAMP_DPS
#define APP_ESKF_REST_YAW_CLAMP_DPS 0.2f
#endif
/* Optional gravity-axis (heading) rest learning. x/y step is identical to
 * update_observable_bias; the gravity component uses its own alpha and the
 * learned gravity-axis residual (relative to the loaded/startup seed) is
 * clamped to +/-APP_ESKF_REST_YAW_CLAMP_DPS. Absorbs real rotation slower
 * than the rest threshold. */
inline bool update_rest_bias_with_yaw(float bias[3],const float gyro[3],
                                      const float gravity[3],float alpha,
                                      float alpha_yaw) {
  float innovation[3],normal2=0.f,vertical=0.f;
  for(int i=0;i<3;++i) {
    innovation[i]=gyro[i]-bias[i];
    normal2+=gravity[i]*gravity[i];
    vertical+=gravity[i]*innovation[i];
  }
  if(!(normal2>1.e-12f) || !std::isfinite(normal2) ||
     !(alpha>0.f&&alpha<=1.f) || !(alpha_yaw>0.f&&alpha_yaw<=1.f))return false;
  vertical/=normal2;
  float next[3],along=0.f;
  constexpr float limit=2.f*0.017453292519943295f;
  constexpr float yaw_limit=APP_ESKF_REST_YAW_CLAMP_DPS*0.017453292519943295f;
  for(int i=0;i<3;++i) {
    next[i]=bias[i]+alpha*(innovation[i]-gravity[i]*vertical)+alpha_yaw*gravity[i]*vertical;
    along+=gravity[i]*next[i];
  }
  along/=normal2;const float norm_g=std::sqrt(normal2);
  const float along_rad=along*norm_g;
  if(std::fabs(along_rad)>yaw_limit) {
    const float target=(along_rad>0.f?yaw_limit:-yaw_limit)/norm_g;
    for(int i=0;i<3;++i)next[i]+=gravity[i]*(target-along);
  }
  for(int i=0;i<3;++i)
    if(!std::isfinite(next[i]) || std::fabs(next[i])>limit)return false;
  for(int i=0;i<3;++i)bias[i]=next[i];
  return true;
}
}
#endif
