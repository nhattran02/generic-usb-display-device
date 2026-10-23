/* Simulated patient for the Monitor page: same model as apps/vitalview/vitals.cpp. Demo data only. */
#include <math.h>
#include "ui_internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float gauss(double x, double mu, double sigma)
{
    const double d = (x - mu) / sigma;
    return (float) exp(-0.5 * d * d);
}

static float rnd(ui_vitals_t *v)       /* 0..1 */
{
    v->rng ^= v->rng << 13;
    v->rng ^= v->rng >> 17;
    v->rng ^= v->rng << 5;
    return (float) (v->rng & 0xFFFFFF) / (float) 0x1000000;
}

static float walk(ui_vitals_t *v, float x, float target, float step, float lo, float hi)
{
    x += (target - x) * 0.05f + (rnd(v) - 0.5f) * step;
    return x < lo ? lo : (x > hi ? hi : x);
}

void ui_vitals_init(ui_vitals_t *v)
{
    *v = (ui_vitals_t) { .hr = 72, .spo2 = 98, .rr = 16, .temp = 36.8f, .sys = 118, .dia = 76, .rng = 20261023 };
}

void ui_vitals_advance(ui_vitals_t *v, double dt)
{
    v->numbers_t += dt;
    if (v->numbers_t >= 1.0) {
        v->numbers_t = 0;
        v->hr = walk(v, v->hr, 72, 1.6f, 64, 84);
        v->spo2 = walk(v, v->spo2, 98, 0.5f, 96, 100);
        v->rr = walk(v, v->rr, 16, 0.6f, 13, 19);
        v->temp = walk(v, v->temp, 36.8f, 0.02f, 36.6f, 37.1f);
    }

    /* cardiac cycle: P, Q, R, S, T waves as gaussians over one beat */
    const double prev = v->beat_phase;
    v->beat_phase += dt * v->hr / 60.0;
    if (v->beat_phase >= 1.0)
        v->beat_phase -= 1.0;
    const double p = v->beat_phase;
    if (prev < 0.40 && p >= 0.40)
        v->beat = true;
    v->ecg = 0.12f * gauss(p, 0.20, 0.025) - 0.14f * gauss(p, 0.37, 0.010) + 1.00f * gauss(p, 0.40, 0.011) -
             0.28f * gauss(p, 0.43, 0.011) + 0.30f * gauss(p, 0.64, 0.040) + (rnd(v) - 0.5f) * 0.02f;

    /* pleth: systolic upstroke a little after the R wave, dicrotic notch on the way down */
    double q = p - 0.48;
    if (q < 0)
        q += 1.0;
    v->pleth = 1.0f * gauss(q, 0.16, 0.07) + 0.35f * gauss(q, 0.42, 0.07) - 0.45f;

    v->breath_phase += dt * v->rr / 60.0;
    if (v->breath_phase >= 1.0)
        v->breath_phase -= 1.0;
    v->resp = (float) sin(2 * M_PI * v->breath_phase) * 0.8f;
}
