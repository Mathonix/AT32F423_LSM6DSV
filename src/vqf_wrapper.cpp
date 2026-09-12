// SPDX-License-Identifier: MIT
// C ABI wrapper around Daniel Laidig's official Full VQF implementation.

#include "vqf_full.hpp"
#include "vqf.h"

#include <cmath>
#include <cstdint>
#include <new>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
alignas(VQF) unsigned char g_storage[sizeof(VQF)];
VQF* g_vqf = nullptr;
float g_gyr_dt = 0.0005f;
float g_acc_dt = 0.0005f;
float g_tau_acc = 3.0f;
float g_tau_mag = 6.0f;
float g_rest_time = 0.0f;
bool g_mag_ready = false;

VQF& filter()
{
    return *g_vqf;
}

void copy4(const vqf_real_t in[4], float out[4])
{
    for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<float>(in[i]);
}
} // namespace

extern "C" void vqf_init(float gyr_dt, float acc_dt)
{
    if (g_vqf) {
        g_vqf->~VQF();
        g_vqf = nullptr;
    }
    g_gyr_dt = gyr_dt;
    g_acc_dt = acc_dt;

    VQFParams params;
    params.tauAcc = g_tau_acc;
    params.tauMag = g_tau_mag;
    params.motionBiasEstEnabled = true;
    params.restBiasEstEnabled = true;
    params.magDistRejectionEnabled = true;
    // A fixed installation can boot without being rotated. Allow the first
    // stable norm/dip candidate to become the reference after the official
    // 5 s magNewFirstTime; later disturbances are still rejected normally.
    params.magNewMinGyr = 0.0f;
    // Keep the official 2 deg/s gyro threshold: the LSM6DSV occasionally
    // produces sub-2 deg/s noise excursions at 2 kHz while mechanically still.
    // biasClip (also 2 deg/s by default) still prevents learning real motion.
    params.restThGyr = 2.0f;
    params.restThAcc = 0.5f;

    // IST8310 is read at 50 Hz; main.c feeds the compute-heavy Full VQF
    // magnetic update at 10 Hz to preserve every 2 kHz gyro sample.
    g_vqf = new (g_storage) VQF(params, gyr_dt, acc_dt, 0.1f);
    g_rest_time = 0.0f;
    g_mag_ready = false;
}

extern "C" void vqf_set_tau_acc(float tau)
{
    if (tau < 0.1f) tau = 0.1f;
    g_tau_acc = tau;
    if (g_vqf) filter().setTauAcc(tau);
}

extern "C" void vqf_set_tau_mag(float tau)
{
    if (tau < 0.2f) tau = 0.2f;
    g_tau_mag = tau;
    if (g_vqf) filter().setTauMag(tau);
}

extern "C" void vqf_set_gyr_bias(const float gyr_bias[3])
{
    vqf_real_t b[3] = {gyr_bias[0], gyr_bias[1], gyr_bias[2]};
    if (g_vqf) filter().setBiasEstimate(b, 0.05f);
}

extern "C" void vqf_prime_rest(const float acc_ms2[3], const float gyr_bias[3])
{
    if (!g_vqf) return;
    vqf_set_gyr_bias(gyr_bias);

    // Initialize Full VQF's second-order accelerometer filter and inclination
    // from the one-second stationary average already collected by main.c.
    const unsigned n = static_cast<unsigned>(std::ceil(g_tau_acc / g_acc_dt)) + 1U;
    const vqf_real_t a[3] = {acc_ms2[0], acc_ms2[1], acc_ms2[2]};
    for (unsigned i = 0; i < n; ++i) filter().updateAcc(a);
}

extern "C" void vqf_update_gyr(const float gyr[3])
{
    const vqf_real_t v[3] = {gyr[0], gyr[1], gyr[2]};
    filter().updateGyr(v);
}

extern "C" void vqf_update_acc(const float acc[3])
{
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
    const vqf_real_t v[3] = {mag[0], mag[1], mag[2]};
    filter().updateMag(v);
    g_mag_ready = true;
    return 0;
}

extern "C" void vqf_get_quat6d(float q[4])
{
    vqf_real_t out[4];
    filter().getQuat6D(out);
    copy4(out, q);
}

extern "C" void vqf_get_quat9d(float q[4])
{
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
    vqf_real_t b[3];
    filter().getBiasEstimate(b);
    for (unsigned i = 0; i < 3; ++i) gyr_bias[i] = static_cast<float>(b[i]);
}

extern "C" float vqf_get_rest_time(void) { return g_rest_time; }
extern "C" int vqf_get_rest_detected(void)
{
    return g_vqf && filter().getRestDetected() ? 1 : 0;
}
extern "C" float vqf_get_tau_acc(void) { return g_tau_acc; }
extern "C" float vqf_get_tau_mag(void) { return g_tau_mag; }
extern "C" int vqf_get_mag_ready(void) { return g_mag_ready ? 1 : 0; }
extern "C" int vqf_get_mag_dist_detected(void)
{
    return g_vqf && filter().getMagDistDetected() ? 1 : 0;
}



