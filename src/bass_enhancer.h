// Calf Bass Enhancer (Markus Schmidt; distortion core by Tom Szilagyi), ported to a standalone,
// N-channel C++ class with two extra modes (phase-aligned, harmonics only).
// Derived from Calf Studio Gear src/modules_dist.cpp, src/audio_fx.cpp and src/calf/biquad.h -
// GNU LGPL 2.1 or later. Behaviour, including numeric types and the resampleN quirks, is
// identical to Calf in Classic mode (verified bit-exact against Calf's own classes).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace calfbass {

constexpr double kPi = 3.14159265358979323846;  // == M_PI

inline void sanitize_denormal(double& v) { if (!std::isnormal(v)) v = 0.0; }
inline void sanitize(double& v) { if (std::abs(v) < (1.0 / 16777216.0)) v = 0.0; }

struct Biquad {  // dsp::biquad_d2
    double a0 = 1, a1 = 0, a2 = 0, b1 = 0, b2 = 0, w1 = 0, w2 = 0;  // Calf default = pass-through (set_null)
    void set_lp_rbj(float fc, float q, float sr, float gain = 1.0f) {
        double omega = (2.0 * kPi * fc / sr), sn = std::sin(omega), cs = std::cos(omega);
        double alpha = (sn / (2 * q)), inv = (1.0 / (1.0 + alpha));
        a2 = a0 = (gain * inv * (1.0 - cs) * 0.5);
        a1 = a0 + a0;
        b1 = (-2.0 * cs * inv);
        b2 = ((1.0 - alpha) * inv);
    }
    void set_hp_rbj(float fc, float q, float esr, float gain = 1.0f) {
        double omega = (double)(2 * kPi * fc / esr), sn = std::sin(omega), cs = std::cos(omega);
        double alpha = (double)(sn / (2 * q)), inv = (double)(1.0 / (1.0 + alpha));
        a0 = (gain * inv * (1 + cs) / 2);
        a1 = -2.f * a0;
        a2 = a0;
        b1 = (-2 * cs * inv);
        b2 = ((1 - alpha) * inv);
    }
    // RBJ all-pass. With q = 0.707 its phase equals that of a pair of RBJ low-passes (LR4) at fc.
    void set_ap_rbj(double fc, double q, double sr) {
        double omega = 2.0 * kPi * fc / sr, sn = std::sin(omega), cs = std::cos(omega);
        double alpha = sn / (2 * q), inv = 1.0 / (1.0 + alpha);
        a0 = (1.0 - alpha) * inv; a1 = -2.0 * cs * inv; a2 = 1.0; b1 = -2.0 * cs * inv; b2 = (1.0 - alpha) * inv;
    }
    void copy_coeffs(const Biquad& s) { a0 = s.a0; a1 = s.a1; a2 = s.a2; b1 = s.b1; b2 = s.b2; }
    void reset() { w1 = w2 = 0; }
    double process(double in) {
        double n = in;
        sanitize_denormal(n); sanitize(n); sanitize(w1); sanitize(w2);
        double tmp = n - w1 * b1 - w2 * b2;
        double out = tmp * a0 + w1 * a1 + w2 * a2;
        w2 = w1; w1 = tmp;
        return out;
    }
};

struct ResampleN {  // dsp::resampleN (quirks preserved: parallel, not cascaded, filters)
    uint32_t srate = 0; int factor = 2, filters = 2;
    double tmp[16] = {};
    Biquad filter[2][4];
    void set_params(uint32_t sr, int fctr, int fltrs) {
        srate = (std::max)(2u, sr);
        factor = (std::min)(16, (std::max)(1, fctr));
        filters = (std::min)(4, (std::max)(1, fltrs));
        filter[0][0].set_lp_rbj(float((std::max)(25000., (double)srate / 2)), 0.8f, (float)srate * factor);
        for (int i = 1; i < filters; i++) { filter[0][i].copy_coeffs(filter[0][0]); filter[1][i].copy_coeffs(filter[0][0]); }
    }
    void reset() { for (auto& r : filter) for (auto& f : r) f.reset(); }
    double* upsample(double sample) {
        tmp[0] = sample;
        if (factor > 1) {
            for (int f = 0; f < filters; f++) tmp[0] = filter[0][f].process(sample);
            for (int i = 1; i < factor; i++) {
                tmp[i] = 0;
                for (int f = 0; f < filters; f++) tmp[i] = filter[0][f].process(sample);
            }
        }
        return tmp;
    }
    double downsample(double* s) {
        if (factor > 1)
            for (int i = 0; i < factor; i++)
                for (int f = 0; f < filters; f++) s[i] = filter[1][f].process(s[i]);
        return s[0];
    }
};

struct TapDistortion {  // dsp::tap_distortion
    float blend_old = -1.f, drive_old = -1.f, meter = 0.f;
    float rdrive = 0, rbdr = 0, kpa = 0, kpb = 0, kna = 0, knb = 0, ap = 0, an = 0, imr = 0, kc = 0, srct = 0, sq = 0, pwrq = 0;
    int over = 1;
    float prev_med = 0, prev_out = 0;
    ResampleN resampler;
    uint32_t srate = 0;
    static float M(float x) { return (std::fabs(x) > 0.00000001f) ? x : 0.0f; }
    static float D(float x) { x = std::fabs(x); return (x > 0.00000001f) ? std::sqrt(x) : 0.0f; }
    void set_params(float blend, float drive) {
        if ((drive_old != drive) || (blend_old != blend)) {
            rdrive = 12.0f / drive;
            rbdr = rdrive / (10.5f - blend) * 780.0f / 33.0f;
            kpa = D(2.0f * (rdrive * rdrive) - 1.0f) + 1.0f;
            kpb = (2.0f - kpa) / 2.0f;
            ap = ((rdrive * rdrive) - kpa + 1.0f) / 2.0f;
            kc = kpa / D(2.0f * D(2.0f * (rdrive * rdrive) - 1.0f) - 2.0f * rdrive * rdrive);
            srct = (0.1f * srate) / (0.1f * srate + 1.0f);
            sq = kc * kc + 1.0f;
            knb = -1.0f * rbdr / D(sq);
            kna = 2.0f * kc * rbdr / D(sq);
            an = rbdr * rbdr / sq;
            imr = 2.0f * knb + D(2.0f * kna + 4.0f * an - 1.0f);
            pwrq = 2.0f / (imr + 1.0f);
            drive_old = drive; blend_old = blend;
        }
    }
    void set_sample_rate(uint32_t sr) {
        srate = sr;
        over = srate * 2 > 96000 ? 1 : 2;
        resampler.set_params(srate, over, 2);
    }
    void reset() { prev_med = prev_out = 0; meter = 0; resampler.reset(); }
    float process(float in) {
        double* samples = resampler.upsample((double)in);
        meter = 0.f;
        for (int o = 0; o < over; o++) {
            float proc = (float)samples[o], med;
            if (proc >= 0.0f) med = (D(ap + proc * (kpa - proc)) + kpb) * pwrq;
            else med = (D(an - proc * (kna + proc)) + knb) * pwrq * -1.0f;
            proc = srct * (med - prev_med + prev_out);
            prev_med = M(med);
            prev_out = M(proc);
            samples[o] = proc;
            meter = (std::max)(meter, proc);
        }
        return (float)resampler.downsample(samples);
    }
};

struct Params {          // Calf defaults (= EasyEffects defaults)
    float level_in = 1.f;    // linear gain
    float level_out = 1.f;   // linear gain
    float amount = 1.f;      // linear gain of the processed (bass + harmonics) signal
    float drive = 8.5f;      // "Harmonics" 0.1..10
    float blend = 0.f;       // "Blend harmonics" -10 (3rd) .. +10 (2nd)
    float freq = 100.f;      // "Scope" Hz, 10..250
    bool listen = false;     // bass solo monitor: 8th-order Butterworth LP at 3x Scope, times listen_gain (Calf: wet only)
    float listen_gain = 1.4125375f;  // linear gain of the bass solo (default +3 dB)
    bool bypass = false;     // output = original input (still low-passed when listen is on)
    bool floor_active = false;
    int mode = 0;            // 0 Calf classic, 1 phase-aligned (no comb notch), 2 harmonics only
    float floor = 20.f;      // Hz, 10..120
};

class BassEnhancer {
public:
    void setup(uint32_t srate, unsigned channels) {
        m_srate = srate;
        m_ch.assign(channels, Chan());
        for (auto& c : m_ch) c.dist.set_sample_rate(srate);
        m_freq_old = m_floor_old = 0.f; m_floor_active_old = false;
        apply(m_p, true);
    }
    void set_params(const Params& p) { apply(p, false); }
    const Params& params() const { return m_p; }
    void reset() {
        for (auto& c : m_ch) {
            for (auto& f : c.lp) f.reset();
            for (auto& f : c.hp) f.reset();
            for (auto& f : c.ap) f.reset();
            for (auto& f : c.post) f.reset();
            for (auto& f : c.mon) f.reset();
            c.env = 0;
            c.dist.reset();
            c.lin_prev_in = c.lin_prev_out = 0; c.w = 0; c.pw = 1e-9;
        }
    }
    // Interleaved, in place. Each channel is processed independently, exactly like Calf's
    // stereo path; mono and >2 channels simply use more independent chains.
    template<typename T> void process(T* buf, size_t frames) {
        const size_t C = m_ch.size();
        const float lin = m_p.level_in, lout = m_p.level_out, amt = m_p.amount;
        const bool listen = m_p.listen, bypass = m_p.bypass, fl = m_p.floor_active;
        const int mode = m_p.mode;
        for (size_t i = 0; i < frames; ++i) {
            T* fr = buf + i * C;
            for (size_t c = 0; c < C; ++c) {
                Chan& ch = m_ch[c];
                const float raw = (float)fr[c];
                float in = raw * lin;
                float proc = in;
                proc = (float)ch.lp[1].process(ch.lp[0].process(proc));
                const float lo = proc;
                if (mode == 2) {
                    // Harmonics only. The bass is level-normalised before the waveshaper (instant
                    // attack, 150 ms release), so the overtone/fundamental ratio stays the same on
                    // quiet bass instead of collapsing (the shaper is nearly linear at low levels).
                    const double a = std::fabs((double)lo);
                    ch.env += (a > ch.env ? 1.0 : m_env_rel) * (a - ch.env);
                    const double g = kNormLevel / (std::max)(ch.env, 1e-3);
                    const float x = float(lo * g);
                    const float d = ch.dist.process(x);
                    // Remove the waveshaper's linear part (same DC blocker) -> overtones only.
                    const float srct = ch.dist.srct, l = m_lin_gain * x;
                    const float lr = srct * (l - ch.lin_prev_in + ch.lin_prev_out);
                    ch.lin_prev_in = l; ch.lin_prev_out = lr;
                    // Normalised LMS tracks the gain of the fundamental. The overtones are
                    // uncorrelated with it, so only the fundamental is removed.
                    const double e = d - ch.w * lr;
                    ch.pw += m_lms_a * ((double)lr * lr - ch.pw);
                    ch.w += m_lms_a * e * lr / (ch.pw + 1e-12);
                    // Undo the normalisation, add make-up gain, and low-pass at 4x Scope (not
                    // at Scope, which would remove most of the 2nd..4th harmonics).
                    proc = (float)(e / g * kHarmMakeup);
                    proc = (float)ch.post[1].process(ch.post[0].process(proc));
                } else {
                    proc = ch.dist.process(proc);
                    proc = (float)ch.lp[2].process(ch.lp[3].process(proc));
                }
                if (fl) proc = (float)ch.hp[0].process(ch.hp[1].process(proc));
                float dry = in;
                if (mode == 1) dry = (float)ch.ap[1].process(ch.ap[0].process(in));  // LP^4 phase == AP^2 phase
                // The engine always runs, so Bypass / Listen toggles are seamless.
                float out = bypass ? raw : (proc * amt + dry) * lout;
                if (listen) out = float(m_p.listen_gain * ch.mon[3].process(ch.mon[2].process(ch.mon[1].process(ch.mon[0].process(out)))));
                fr[c] = (T)out;
            }
        }
    }
private:
    struct Chan { Biquad lp[4], hp[2], ap[2], post[2], mon[4]; double env = 0; TapDistortion dist; float lin_prev_in = 0, lin_prev_out = 0; double w = 0, pw = 1e-9; };
    void apply(const Params& p, bool force) {
        if (p.listen && !m_p.listen) for (auto& c : m_ch) for (auto& f : c.mon) f.reset();
        m_p = p;
        if (m_ch.empty()) return;
        if (force || p.freq != m_freq_old) {
            m_ch[0].lp[0].set_lp_rbj(p.freq, 0.707f, (float)m_srate);
            for (auto& c : m_ch) for (auto& f : c.lp) f.copy_coeffs(m_ch[0].lp[0]);
            Biquad ap; ap.set_ap_rbj(p.freq, 0.707, m_srate);
            for (auto& c : m_ch) for (auto& f : c.ap) f.copy_coeffs(ap);
            Biquad post; post.set_lp_rbj((std::min)(4.f * p.freq, 0.45f * (float)m_srate), 0.707f, (float)m_srate);
            for (auto& c : m_ch) for (auto& f : c.post) f.copy_coeffs(post);
            // 8th-order Butterworth (-48 dB/oct): 1 kHz is ~-80 dB at the default Scope.
            static constexpr float kButterQ[4] = { 0.50980f, 0.60134f, 0.89998f, 2.56292f };
            const float fm = (std::min)(3.f * p.freq, 0.45f * (float)m_srate);
            for (int k = 0; k < 4; ++k) {
                Biquad m; m.set_lp_rbj(fm, kButterQ[k], (float)m_srate);
                for (auto& c : m_ch) c.mon[k].copy_coeffs(m);
            }
            m_freq_old = p.freq;
        }
        if (force || p.floor != m_floor_old || p.floor_active != m_floor_active_old) {
            m_ch[0].hp[0].set_hp_rbj(p.floor, 0.707f, (float)m_srate);
            for (auto& c : m_ch) for (auto& f : c.hp) f.copy_coeffs(m_ch[0].hp[0]);
            m_floor_old = p.floor; m_floor_active_old = p.floor_active;
        }
        for (auto& c : m_ch) c.dist.set_params(p.blend, p.drive);
        // Small-signal slope of the waveshaper at 0 (average of both branches).
        const TapDistortion& d = m_ch[0].dist;
        if (d.ap > 0 && d.an > 0) {
            const double sp = d.pwrq * d.kpa / (2 * std::sqrt(d.ap)), sn = d.pwrq * d.kna / (2 * std::sqrt(d.an));
            m_lin_gain = float(0.5 * (sp + sn));
        } else m_lin_gain = 1.f;
        m_lms_a = 1.0 - std::exp(-1.0 / (0.030 * m_srate));  // ~30 ms tracking
        m_env_rel = 1.0 - std::exp(-1.0 / (0.150 * m_srate));  // 150 ms envelope release
    }
    Params m_p;
    uint32_t m_srate = 44100;
    float m_lin_gain = 1.f;
    double m_lms_a = 0, m_env_rel = 0;
    static constexpr double kNormLevel = 0.5;                  // shaper drive level (-6 dBFS)
    static constexpr double kHarmMakeup = 3.1622776601683795;  // +10 dB
    float m_freq_old = 0, m_floor_old = 0; bool m_floor_active_old = false;
    std::vector<Chan> m_ch;
};

} // namespace calfbass
