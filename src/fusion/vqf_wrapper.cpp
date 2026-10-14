// SPDX-License-Identifier: MIT
// C ABI wrapper around Daniel Laidig's official Full VQF implementation.

#include "vqf_full.hpp"
#include "vqf.h"
#include "app_config.h"
#include "fusion_profile.h"
#include "fusion_algorithm.h"
#include "ekf.hpp"

#include <cmath>
#include <cstring>
#include <new>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
alignas(VQF) unsigned char g_storage[sizeof(VQF)];
VQF* g_vqf = nullptr;
vqf_tuning_t g_tuning;
bool g_tuning_enabled=false;
float g_acc_dt = 0.0005f;
float g_tau_acc = APP_VQF_TAU_ACC;
float g_tau_mag = APP_VQF_TAU_MAG;
float g_rest_th_gyr = APP_VQF_REST_GYR_DPS;
float g_rest_th_acc = APP_VQF_REST_ACC_MS2;
float g_rest_time = 0.0f;
bool g_mag_ready = false;
bool g_mag_rejection_enabled = true;
bool g_rest_bias_enabled = APP_VQF_REST_BIAS_ENABLE != 0U;
uint8_t g_algorithm = FUSION_ALGORITHM_VQF;
float g_calibrated_bias[3] = {0.0f, 0.0f, 0.0f};

VQF& filter()
{
    return *g_vqf;
}

void copy4(const vqf_real_t in[4], float out[4])
{
    for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<float>(in[i]);
}
} // namespace

extern "C" void fusion_select_algorithm(uint8_t algorithm)
{ if(algorithm < FUSION_ALGORITHM_COUNT) g_algorithm=algorithm; }
extern "C" uint8_t fusion_get_algorithm(void) { return g_algorithm; }

extern "C" void vqf_configure_tuning(const vqf_tuning_t *p)
{
    g_tuning_enabled=p && vqf_tuning_valid(p);
    if(g_tuning_enabled) g_tuning=*p;
}
extern "C" void vqf_get_tuning(vqf_tuning_t *out)
{
    if(!out) return;
    vqf_tuning_defaults(out);
    if(!g_vqf) return;
    const VQFParams& p=filter().getParams();
    const float values[VQF_TUNING_COUNT]={p.tauAcc,p.tauMag,p.biasSigmaInit,p.biasSigmaRest,
      p.biasSigmaMotion,p.biasClip,p.biasForgettingTime,p.restFilterTau,p.restMinT,
      p.restThGyr,p.restThAcc,p.magCurrentTau,p.magNormTh,p.magDipTh};
    std::memcpy(out->value,values,sizeof(values));
}
extern "C" int vqf_tuning_is_custom(void) { return g_tuning_enabled?1:0; }
extern "C" void vqf_init(float gyr_dt, float acc_dt)
{
    if (g_vqf) {
        g_vqf->~VQF();
        g_vqf = nullptr;
    }
    g_acc_dt = acc_dt;

    VQFParams params;
    params.tauAcc = g_tau_acc;
    params.tauMag = g_tau_mag;
    params.motionBiasEstEnabled = (APP_VQF_MOTION_BIAS_ENABLE != 0U);
    params.restBiasEstEnabled = g_rest_bias_enabled;
    params.magDistRejectionEnabled = g_mag_rejection_enabled;
    params.biasSigmaInit = APP_VQF_BIAS_SIGMA_INIT_DPS;
    params.biasSigmaRest = APP_VQF_BIAS_SIGMA_REST_DPS;
    params.biasSigmaMotion = APP_VQF_BIAS_SIGMA_MOTION_DPS;
    params.biasClip = APP_VQF_BIAS_CLIP_DPS;
    params.biasForgettingTime = APP_VQF_BIAS_FORGETTING_TIME_S;
    params.restFilterTau = APP_VQF_REST_FILTER_TAU_S;
    // A fixed installation can boot without being rotated. Allow the first
    // stable norm/dip candidate to become the reference after the official
    // 5 s magNewFirstTime; later disturbances are still rejected normally.
    params.magNewMinGyr = 0.0f;
    params.restThGyr = g_rest_th_gyr;
    params.restThAcc = g_rest_th_acc;
    params.restMinT = APP_VQF_REST_MIN_SECONDS;

    if(g_tuning_enabled) {
        const float *v=g_tuning.value;
        params.tauAcc=v[VQF_TAU_ACC]; params.tauMag=v[VQF_TAU_MAG];
        params.biasSigmaInit=v[VQF_SIGMA_INIT]; params.biasSigmaRest=v[VQF_SIGMA_REST];
        params.biasSigmaMotion=v[VQF_SIGMA_MOTION]; params.biasClip=v[VQF_BIAS_CLIP];
        params.biasForgettingTime=v[VQF_FORGET_TIME]; params.restFilterTau=v[VQF_REST_TAU];
        params.restMinT=v[VQF_REST_MIN_T]; params.restThGyr=v[VQF_REST_GYR];
        params.restThAcc=v[VQF_REST_ACC]; params.magCurrentTau=v[VQF_MAG_CURRENT_TAU];
        params.magNormTh=v[VQF_MAG_NORM_TH]; params.magDipTh=v[VQF_MAG_DIP_TH];
    }
    // IST8310 is read at 50 Hz; main.c feeds the compute-heavy Full VQF
    // magnetic update at 10 Hz to preserve every 2 kHz gyro sample.
    g_vqf = new (g_storage) VQF(params, gyr_dt, acc_dt, 0.1f);
    g_rest_time = 0.0f;
    g_mag_ready = false;
    std::memset(g_calibrated_bias, 0, sizeof(g_calibrated_bias));
    ahrs_ekf::set_profile(g_tau_acc,g_tau_mag,g_rest_th_gyr,g_rest_th_acc);
    ahrs_ekf::set_bias_sigmas(APP_VQF_BIAS_SIGMA_INIT_DPS,APP_VQF_BIAS_SIGMA_REST_DPS);
    ahrs_ekf::set_mag_rejection(g_mag_rejection_enabled);
    ahrs_ekf::set_rest_bias_enabled(g_rest_bias_enabled);
    ahrs_ekf::init(gyr_dt,acc_dt);
}

extern "C" void vqf_apply_profile(unsigned profile)
{
    const fusion_profile_t p = fusion_profile_get(profile);
    g_tau_acc = p.tau_acc_s;
    g_tau_mag = p.tau_mag_s;
    g_rest_th_gyr = p.rest_th_gyr_dps;
    g_rest_th_acc = p.rest_th_acc_ms2;
    ahrs_ekf::set_profile(g_tau_acc,g_tau_mag,g_rest_th_gyr,g_rest_th_acc);
    if (!g_vqf) return;
    if(g_tuning_enabled) return; // Custom VQF tuning takes priority over profile presets.
    filter().setTauAcc(g_tau_acc);
    filter().setTauMag(g_tau_mag);
    filter().setRestDetectionThresholds(g_rest_th_gyr, g_rest_th_acc);
}

extern "C" void vqf_set_rest_thresholds(float th_gyr_dps, float th_acc_ms2)
{
    if (!std::isfinite(th_gyr_dps) || th_gyr_dps <= 0) return;
    if (!std::isfinite(th_acc_ms2) || th_acc_ms2 <= 0) return;
    g_rest_th_gyr = th_gyr_dps;
    g_rest_th_acc = th_acc_ms2;
    ahrs_ekf::set_profile(g_tau_acc,g_tau_mag,g_rest_th_gyr,g_rest_th_acc);
    if (g_vqf && !g_tuning_enabled) filter().setRestDetectionThresholds(g_rest_th_gyr, g_rest_th_acc);
}

extern "C" void vqf_set_bias_sigmas(float sigma_init_dps, float sigma_rest_dps)
{
    if (!std::isfinite(sigma_init_dps) || sigma_init_dps <= 0) return;
    if (!std::isfinite(sigma_rest_dps) || sigma_rest_dps <= 0) return;
    ahrs_ekf::set_bias_sigmas(sigma_init_dps,sigma_rest_dps);
    if (g_vqf && !g_tuning_enabled) filter().setBiasSigmas(sigma_init_dps, sigma_rest_dps);
}

extern "C" void vqf_set_tau_acc(float tau)
{
    if(!std::isfinite(tau)) return;
    if (tau < 0.1f) tau = 0.1f;
    g_tau_acc = tau;
    ahrs_ekf::set_profile(g_tau_acc,g_tau_mag,g_rest_th_gyr,g_rest_th_acc);
    if (g_vqf && !g_tuning_enabled) filter().setTauAcc(tau);
}

extern "C" void vqf_set_tau_mag(float tau)
{
    if(!std::isfinite(tau)) return;
    if (tau < 0.2f) tau = 0.2f;
    g_tau_mag = tau;
    ahrs_ekf::set_profile(g_tau_acc,g_tau_mag,g_rest_th_gyr,g_rest_th_acc);
    if (g_vqf && !g_tuning_enabled) filter().setTauMag(tau);
}

extern "C" void vqf_set_gyr_bias(const float gyr_bias[3])
{
    vqf_seed_gyr_bias(gyr_bias, APP_VQF_BIAS_SIGMA_REST_DPS);
}

extern "C" void vqf_set_rest_bias_enabled(int enabled)
{
    const bool value=enabled!=0;
    if(g_rest_bias_enabled==value)return;
    g_rest_bias_enabled=value;
    g_rest_time=0;
    ahrs_ekf::set_rest_bias_enabled(value);
    if(g_vqf)filter().setRestBiasEstEnabled(value);
}
extern "C" int vqf_get_rest_bias_enabled(void) { return g_rest_bias_enabled?1:0; }

extern "C" void vqf_seed_gyr_bias(const float gyr_bias[3], float sigma_dps)
{
    vqf_real_t b[3] = {gyr_bias[0], gyr_bias[1], gyr_bias[2]};
    if (!std::isfinite(sigma_dps) || sigma_dps <= 0 || sigma_dps > 10) return;
    for (unsigned i=0; i<3; ++i) if (!std::isfinite(b[i])) return;
    if (g_vqf) {
        // Keep measured startup bias outside the motion estimator. Its
        // artificial vertical-zero measurement must act on the correction,
        // rather than erase a known nonzero calibration.
        std::memcpy(g_calibrated_bias, gyr_bias, sizeof(g_calibrated_bias));
        ahrs_ekf::seed_bias(sigma_dps);
        vqf_real_t correction[3] = {0, 0, 0};
        filter().setBiasEstimate(correction, sigma_dps * static_cast<float>(M_PI/180.0));
    }
}

extern "C" float vqf_get_bias_sigma_dps(void)
{
    if (!g_vqf) return 0;
    if(g_algorithm==FUSION_ALGORITHM_EKF) return ahrs_ekf::bias_sigma_dps();
    vqf_real_t b[3];
    return filter().getBiasEstimate(b) * static_cast<float>(180.0/M_PI);
}

extern "C" void vqf_prime_rest(const float acc_ms2[3], const float gyr_bias[3])
{
    if (!g_vqf) return;
    vqf_set_gyr_bias(gyr_bias);
    if(g_algorithm==FUSION_ALGORITHM_EKF) {ahrs_ekf::prime(acc_ms2);return;}

    // Initialize Full VQF's second-order accelerometer filter and inclination
    // from the configured stationary average already collected by main.c.
    const unsigned requested = static_cast<unsigned>(std::ceil(g_tau_acc / g_acc_dt)) + 1U;
    const unsigned n = requested > APP_VQF_PRIME_MAX_SAMPLES ? APP_VQF_PRIME_MAX_SAMPLES : requested;
    const vqf_real_t a[3] = {acc_ms2[0], acc_ms2[1], acc_ms2[2]};
    for (unsigned i = 0; i < n; ++i) filter().updateAcc(a);
}

extern "C" void vqf_update_gyr(const float gyr[3])
{
    const vqf_real_t v[3] = {gyr[0] - g_calibrated_bias[0],
                           gyr[1] - g_calibrated_bias[1],
                           gyr[2] - g_calibrated_bias[2]};
    if(g_algorithm==FUSION_ALGORITHM_EKF) {
        const float v[3]={gyr[0]-g_calibrated_bias[0],gyr[1]-g_calibrated_bias[1],gyr[2]-g_calibrated_bias[2]};
        ahrs_ekf::gyro(v);return;
    }
    filter().updateGyr(v);
}

extern "C" void vqf_update_acc(const float acc[3])
{
    if(g_algorithm==FUSION_ALGORITHM_EKF) {ahrs_ekf::accel(acc);return;}
    const vqf_real_t v[3] = {acc[0], acc[1], acc[2]};
    filter().updateAcc(v);
    if (filter().getRestDetected()) g_rest_time += g_acc_dt;
    else g_rest_time = 0.0f;
}

extern "C" void vqf_update(const float gyr[3], const float acc[3])
{
    vqf_update_gyr(gyr);
    vqf_update_acc(acc);
}

extern "C" int vqf_update_mag(const float mag[3])
{
    const float n2 = mag[0]*mag[0] + mag[1]*mag[1] + mag[2]*mag[2];
    if (!g_vqf || n2 < 1.0e-12f || n2 > 4000000.0f) return -1;
    if(g_algorithm==FUSION_ALGORITHM_EKF) return ahrs_ekf::mag(mag);
    const vqf_real_t v[3] = {mag[0], mag[1], mag[2]};
    filter().updateMag(v);
    g_mag_ready = true;
    return 0;
}

extern "C" void vqf_set_mag_rejection_enabled(int enabled)
{
    g_mag_rejection_enabled = enabled != 0;
    ahrs_ekf::set_mag_rejection(g_mag_rejection_enabled);
    if(g_vqf) filter().setMagDistRejectionEnabled(g_mag_rejection_enabled);
}
extern "C" int vqf_get_mag_rejection_enabled(void) { return g_mag_rejection_enabled ? 1 : 0; }

extern "C" void vqf_reset_mag_reference(void)
{
    if(!g_vqf) return;
    if(g_algorithm==FUSION_ALGORITHM_EKF) {ahrs_ekf::reset_mag_reference();return;}
    // Preserve inclination, gyro bias and current heading; relearn the magnetic
    // field reference after calibration without resetting the whole estimator.
    filter().setMagDistRejectionEnabled(false);
    filter().setMagDistRejectionEnabled(g_mag_rejection_enabled);
}

extern "C" void vqf_get_quat6d(float q[4])
{
    if(g_algorithm==FUSION_ALGORITHM_EKF) {ahrs_ekf::quat6(q);return;}
    vqf_real_t out[4];
    filter().getQuat6D(out);
    copy4(out, q);
}

extern "C" void vqf_get_quat9d(float q[4])
{
    if(g_algorithm==FUSION_ALGORITHM_EKF) {ahrs_ekf::quat9(q);return;}
    vqf_real_t out[4];
    if (g_mag_ready) filter().getQuat9D(out);
    else filter().getQuat6D(out);
    copy4(out, q);
}

extern "C" void vqf_get_euler_deg(float *roll_deg, float *pitch_deg, float *yaw_deg)
{
    float q[4];
    vqf_get_quat9d(q);
    const float sinp0 = 2.0f * (q[0]*q[2] - q[3]*q[1]);
    const float sinp = sinp0 > 1.0f ? 1.0f : (sinp0 < -1.0f ? -1.0f : sinp0);
    const float k = 180.0f / static_cast<float>(M_PI);
    *roll_deg = std::atan2(2.0f*(q[0]*q[1] + q[2]*q[3]),
                           1.0f - 2.0f*(q[1]*q[1] + q[2]*q[2])) * k;
    *pitch_deg = std::asin(sinp) * k;
    *yaw_deg = std::atan2(2.0f*(q[0]*q[3] + q[1]*q[2]),
                          1.0f - 2.0f*(q[2]*q[2] + q[3]*q[3])) * k;
}

extern "C" void vqf_get_gyr_bias(float gyr_bias[3])
{
    if(g_algorithm==FUSION_ALGORITHM_EKF) {
        float b[3];ahrs_ekf::bias9(b);
        for(unsigned i=0;i<3;++i)gyr_bias[i]=g_calibrated_bias[i]+b[i];
        return;
    }
    vqf_real_t b[3];
    filter().getBiasEstimate(b);
    for (unsigned i = 0; i < 3; ++i)
        gyr_bias[i] = g_calibrated_bias[i] + static_cast<float>(b[i]);
}

extern "C" void vqf_get_gyr_bias6d(float gyr_bias[3])
{
    if(g_algorithm!=FUSION_ALGORITHM_EKF) {vqf_get_gyr_bias(gyr_bias);return;}
    float b[3];ahrs_ekf::bias(b);
    for(unsigned i=0;i<3;++i)gyr_bias[i]=g_calibrated_bias[i]+b[i];
}

extern "C" void vqf_get_bias_estimator_config(vqf_bias_estimator_config_t *out)
{
    if (!out) return;
    std::memset(out, 0, sizeof(*out));
    if (!g_vqf) return;
    if(g_algorithm==FUSION_ALGORITHM_EKF) {
        out->motion_bias_enabled=APP_VQF_MOTION_BIAS_ENABLE?1U:0U;
        out->rest_bias_enabled=g_rest_bias_enabled?1U:0U;
        ahrs_ekf::bias_sigmas(out->bias_sigma_init_dps,out->bias_sigma_rest_dps);
        out->bias_clip_dps=APP_VQF_BIAS_CLIP_DPS;
        out->tau_acc_s=g_tau_acc;
        // VQF-specific forgetting/vertical-zero pseudo-measurements are unused.
        return;
    }
    const VQFParams& p = filter().getParams();
    out->motion_bias_enabled = p.motionBiasEstEnabled ? 1U : 0U;
    out->rest_bias_enabled = p.restBiasEstEnabled ? 1U : 0U;
    out->bias_sigma_motion_dps = static_cast<float>(p.biasSigmaMotion);
    out->bias_vertical_forgetting = static_cast<float>(p.biasVerticalForgettingFactor);
    out->bias_forgetting_time_s = static_cast<float>(p.biasForgettingTime);
    out->bias_clip_dps = static_cast<float>(p.biasClip);
    out->bias_sigma_rest_dps = static_cast<float>(p.biasSigmaRest);
    out->bias_sigma_init_dps = static_cast<float>(p.biasSigmaInit);
    out->tau_acc_s = static_cast<float>(p.tauAcc);
}

extern "C" float vqf_get_rest_time(void) { return g_algorithm==FUSION_ALGORITHM_EKF?ahrs_ekf::rest_time():g_rest_time; }
extern "C" int vqf_query_update_diagnostic(unsigned index,fusion_update_diagnostic_t* out)
{
    if(!out||index>1)return 0;
    std::memset(out,0,sizeof(*out));
    return g_algorithm==FUSION_ALGORITHM_EKF&&ahrs_ekf::query_update_diagnostic(index,*out);
}
extern "C" unsigned vqf_get_ekf_health_flags(unsigned index)
{
    return g_algorithm==FUSION_ALGORITHM_EKF?ahrs_ekf::health_flags(index):0U;
}
extern "C" int vqf_get_rest_detected(void)
{
    if(g_algorithm==FUSION_ALGORITHM_EKF) return ahrs_ekf::rest()?1:0;
    return g_vqf && filter().getRestDetected() ? 1 : 0;
}
extern "C" float vqf_get_tau_acc(void) { return g_algorithm==FUSION_ALGORITHM_VQF && g_vqf ? filter().getParams().tauAcc : g_tau_acc; }
extern "C" float vqf_get_tau_mag(void) { return g_algorithm==FUSION_ALGORITHM_VQF && g_vqf ? filter().getParams().tauMag : g_tau_mag; }
extern "C" int vqf_get_mag_ready(void) { return g_algorithm==FUSION_ALGORITHM_EKF?(ahrs_ekf::mag_ready()?1:0):(g_mag_ready?1:0); }
extern "C" float vqf_get_mag_delta_deg(void)
{
    if(g_algorithm==FUSION_ALGORITHM_EKF) return ahrs_ekf::mag_delta_deg();
    return g_vqf && g_mag_ready ? static_cast<float>(filter().getDelta() * (180.0 / M_PI)) : 0.0f;
}
extern "C" int vqf_get_mag_dist_detected(void)
{
    if(g_algorithm==FUSION_ALGORITHM_EKF) return ahrs_ekf::mag_disturbed()?1:0;
    return g_vqf && filter().getMagDistDetected() ? 1 : 0;
}






/* Read-only diagnostics at 20 Hz; no estimator parameter/state changes. */
extern "C" void vqf_get_nine_diagnostic(float out[8])
{
    if(g_algorithm==FUSION_ALGORITHM_EKF) {ahrs_ekf::diagnostic(out);return;}
    vqf_real_t q[4];
    filter().getQuat6D(q);
    const float k = 180.0f / static_cast<float>(M_PI);
    const VQFState& state = filter().getState();
    out[0] = std::atan2(2.0f*(q[0]*q[3] + q[1]*q[2]),
                        1.0f - 2.0f*(q[2]*q[2] + q[3]*q[3])) * k;
    out[1] = filter().getMagRefNorm();
    out[2] = filter().getMagRefDip() * k;
    out[3] = state.magRejectT;
    out[4] = state.magCandidateT;
    out[5] = state.lastMagCorrAngularRate * k;
    out[6] = state.lastMagDisAngle * k;
    vqf_real_t bias[3];
    out[7] = filter().getBiasEstimate(bias) * k;
}
