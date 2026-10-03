// foo_dsp_bassenhancer: Calf's Bass Enhancer (as used by EasyEffects) for foobar2000 v2,
// with two improved modes. The DSP engine lives in bass_enhancer.h (no foobar2000 dependency).
//
// Settings changes made in the dialog are applied live (dsp_v3::apply_preset), so the filter
// state is kept and there are no clicks while adjusting.

#include <helpers/foobar2000+atl.h>
#include <helpers/DarkMode.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>

#include "resource.h"
#include "bass_enhancer.h"

#define COMPONENT_VERSION "1.4.0"

DECLARE_COMPONENT_VERSION(
    "Bass Enhancer", COMPONENT_VERSION,
    "Psychoacoustic bass enhancer: Calf Bass Enhancer by Markus Schmidt (distortion core by\n"
    "Tom Szilagyi), the same plugin EasyEffects uses, plus phase-aligned and harmonics-only modes.\n\n"
    "Derived from Calf Studio Gear, GNU LGPL 2.1 or later.");

VALIDATE_COMPONENT_FILENAME("foo_dsp_bassenhancer.dll");

namespace {

// {9DCC2A60-6D07-4608-8FB8-11FA0FD06ED5}
constexpr GUID guid_dsp = { 0x9dcc2a60, 0x6d07, 0x4608, { 0x8f, 0xb8, 0x11, 0xfa, 0x0f, 0xd0, 0x6e, 0xd5 } };

// Engine modes (calfbass::Params::mode)
enum { kModeClassic = 0, kModeAligned = 1, kModeHarmonics = 2 };

struct Settings {
    int32_t mode = kModeAligned;
    bool listen = false;
    bool floor_on = false;
    bool bypass = false;
    float in_db = 0.f, out_db = 0.f, amount_db = 0.f;
    float drive = 8.5f, blend = 0.f, scope = 100.f, floor = 20.f;
    float listen_db = 0.f;  // 0 dB = Calf's listen level

    calfbass::Params to_params() const {
        auto lin = [](float db) { return db <= -36.f ? 0.f : std::pow(10.f, db / 20.f); };
        calfbass::Params p;
        p.mode = mode;
        p.listen = listen;
        p.bypass = bypass;
        p.listen_gain = std::pow(10.f, listen_db / 20.f);
        p.floor_active = floor_on;
        p.level_in = lin(in_db);
        p.level_out = lin(out_db);
        p.amount = lin(amount_db);
        p.drive = drive;
        p.blend = blend;
        p.freq = scope;
        p.floor = floor;
        return p;
    }
};

constexpr int32_t kPresetVersion = 3;  // v2 adds bypass, v3 listen gain; older presets still load

void make_preset(const Settings& s, dsp_preset& out) {
    dsp_preset_builder b;
    b << kPresetVersion << s.mode << int32_t(s.listen) << int32_t(s.floor_on)
      << s.in_db << s.out_db << s.amount_db << s.drive << s.blend << s.scope << s.floor << int32_t(s.bypass) << s.listen_db;
    b.finish(guid_dsp, out);
}

Settings parse_preset(const dsp_preset& in) {
    Settings s;
    try {
        dsp_preset_parser p(in);
        int32_t ver = 0, listen = 0, floor_on = 0, bypass = 0;
        p >> ver;
        if (ver < 1 || ver > kPresetVersion) return Settings();
        p >> s.mode >> listen >> floor_on >> s.in_db >> s.out_db >> s.amount_db >> s.drive >> s.blend >> s.scope >> s.floor;
        if (ver >= 2) p >> bypass;
        if (ver >= 3) p >> s.listen_db;
        s.bypass = bypass != 0;
        s.listen = listen != 0;
        s.floor_on = floor_on != 0;
    } catch (exception_io_data const&) {
        return Settings();
    }
    s.mode = std::clamp(s.mode, 0, 2);
    s.in_db = std::clamp(s.in_db, -36.f, 36.f);
    s.out_db = std::clamp(s.out_db, -36.f, 36.f);
    s.amount_db = std::clamp(s.amount_db, -36.f, 36.f);
    s.drive = std::clamp(s.drive, 0.1f, 10.f);
    s.blend = std::clamp(s.blend, -10.f, 10.f);
    s.scope = std::clamp(s.scope, 10.f, 250.f);
    s.floor = std::clamp(s.floor, 10.f, 120.f);
    s.listen_db = std::clamp(s.listen_db, -12.f, 12.f);
    return s;
}

void run_config_popup(const dsp_preset& p, HWND parent, dsp_preset_edit_callback& cb);

class dsp_bassenhancer : public dsp_impl_base_t<dsp_v3> {
public:
    explicit dsp_bassenhancer(const dsp_preset& p) {
        m_be.set_params(parse_preset(p).to_params());
    }

    static GUID g_get_guid() { return guid_dsp; }
    static void g_get_name(pfc::string_base& out) { out = "Bass Enhancer"; }
    static bool g_get_default_preset(dsp_preset& out) { make_preset(Settings(), out); return true; }
    static bool g_have_config_popup() { return true; }
    static void g_show_config_popup(const dsp_preset& p, HWND parent, dsp_preset_edit_callback& cb) { run_config_popup(p, parent, cb); }

    bool on_chunk(audio_chunk* chunk, abort_callback&) override {
        if (m_dirty.exchange(false)) {
            std::lock_guard<std::mutex> lock(m_lock);
            if (m_pending.bypass != m_be.params().bypass || m_pending.listen != m_be.params().listen)
                FB2K_console_formatter() << "Bass Enhancer: bypass " << (m_pending.bypass ? "on" : "off")
                                         << ", listen " << (m_pending.listen ? "on" : "off");
            m_be.set_params(m_pending);
        }
        const unsigned rate = chunk->get_sample_rate(), nch = chunk->get_channels();
        if (rate != m_rate || nch != m_nch) {
            m_rate = rate;
            m_nch = nch;
            m_be.setup(rate, nch);
        }
        m_be.process(chunk->get_data(), chunk->get_sample_count());
        return true;
    }
    void on_endofplayback(abort_callback&) override {}
    void on_endoftrack(abort_callback&) override {}
    void flush() override { m_be.reset(); }
    double get_latency() override { return 0; }
    bool need_track_change_mark() override { return false; }

    // Live settings change from the dialog: keep the filter state, no re-creation.
    bool apply_preset(const dsp_preset& p) override {
        if (p.get_owner() != guid_dsp) return false;
        std::lock_guard<std::mutex> lock(m_lock);
        m_pending = parse_preset(p).to_params();
        m_dirty = true;
        return true;
    }

private:
    calfbass::BassEnhancer m_be;
    unsigned m_rate = 0, m_nch = 0;
    std::mutex m_lock;
    calfbass::Params m_pending;
    std::atomic<bool> m_dirty{ false };
};

static dsp_factory_t<dsp_bassenhancer> g_factory;

// ---------------------------------------------------------------------------------------------
// Configuration popup

struct SliderDef { int id, vid; float Settings::* field; float lo, hi, step; const char* fmt; };
constexpr SliderDef kSliders[] = {
    { IDC_BLEND,  IDC_BLEND_V,  &Settings::blend,     -10.f, 10.f,  0.1f, "%+.1f" },
    { IDC_AMOUNT, IDC_AMOUNT_V, &Settings::amount_db, -36.f, 36.f,  0.1f, "%+.1f dB" },
    { IDC_DRIVE,  IDC_DRIVE_V,  &Settings::drive,      0.1f, 10.f,  0.1f, "%.1f" },
    { IDC_SCOPE,  IDC_SCOPE_V,  &Settings::scope,      10.f, 250.f, 1.f,  "%.0f Hz" },
    { IDC_FLOOR,  IDC_FLOOR_V,  &Settings::floor,      10.f, 120.f, 1.f,  "%.0f Hz" },
    { IDC_IN,     IDC_IN_V,     &Settings::in_db,     -36.f, 36.f,  0.1f, "%+.1f dB" },
    { IDC_OUT,    IDC_OUT_V,    &Settings::out_db,    -36.f, 36.f,  0.1f, "%+.1f dB" },
    { IDC_LGAIN,  IDC_LGAIN_V,  &Settings::listen_db, -12.f, 12.f,  0.5f, "%+.1f dB" },
};

// Combo order -> engine mode
constexpr int kComboModes[] = { kModeAligned, kModeHarmonics, kModeClassic };
const wchar_t* const kComboNames[] = {
    L"Phase-aligned (no bass dip)",
    L"Harmonics only (psychoacoustic)",
    L"Classic (identical to Calf / EasyEffects)",
};
const char* const kModeInfo[] = {
    /* classic */   "Exactly Calf's sound. Its processed bass partly cancels the original around half the Scope frequency (about -8 dB at 50 Hz with defaults).",
    /* aligned */   "The original is phase-matched to the processed bass, so they add up: a smooth bass boost plus harmonics, without the dip. Watch for clipping.",
    /* harmonics */ "Adds only overtones (2nd-5th harmonics) at a level that follows the bass; the bass itself stays at its original level. Fuller-sounding bass without extra low-end energy.",
};

class CConfigDialog : public CDialogImpl<CConfigDialog> {
public:
    enum { IDD = IDD_CONFIG };
    CConfigDialog(const dsp_preset& init, dsp_preset_edit_callback& cb) : m_init(init), m_cb(cb), m_s(parse_preset(init)) {}

    BEGIN_MSG_MAP_EX(CConfigDialog)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_HSCROLL(OnHScroll)
        COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnClose)
        COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnClose)
        COMMAND_HANDLER_EX(IDC_RESET, BN_CLICKED, OnReset)
        COMMAND_HANDLER_EX(IDC_MODE, CBN_SELCHANGE, OnMode)
        COMMAND_HANDLER_EX(IDC_LISTEN, BN_CLICKED, OnCheck)
        COMMAND_HANDLER_EX(IDC_FLOOR_ON, BN_CLICKED, OnCheck)
        COMMAND_HANDLER_EX(IDC_BYPASS, BN_CLICKED, OnCheck)
    END_MSG_MAP()

private:
    BOOL OnInitDialog(CWindow, LPARAM) {
        m_dark.AddDialogWithControls(m_hWnd);
        CComboBox mode(GetDlgItem(IDC_MODE));
        for (auto n : kComboNames) mode.AddString(n);
        for (const auto& d : kSliders) {
            CTrackBarCtrl tb(GetDlgItem(d.id));
            tb.SetRange(0, int(std::lround((d.hi - d.lo) / d.step)));
        }
        refresh();
        return TRUE;
    }

    void refresh() {
        m_updating = true;
        CComboBox mode(GetDlgItem(IDC_MODE));
        for (int i = 0; i < 3; ++i) if (kComboModes[i] == m_s.mode) mode.SetCurSel(i);
        CheckDlgButton(IDC_LISTEN, m_s.listen ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_FLOOR_ON, m_s.floor_on ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_BYPASS, m_s.bypass ? BST_CHECKED : BST_UNCHECKED);
        for (const auto& d : kSliders) {
            CTrackBarCtrl tb(GetDlgItem(d.id));
            tb.SetPos(int(std::lround((m_s.*d.field - d.lo) / d.step)));
            update_label(d);
        }
        update_state();
        m_updating = false;
    }

    void update_label(const SliderDef& d) {
        char buf[32];
        const float v = m_s.*d.field;
        if (d.id == IDC_AMOUNT && v <= -36.f) std::snprintf(buf, sizeof(buf), "off");
        else std::snprintf(buf, sizeof(buf), d.fmt, v);
        uSetDlgItemText(m_hWnd, d.vid, buf);
    }

    void update_state() {
        GetDlgItem(IDC_FLOOR).EnableWindow(m_s.floor_on);
        GetDlgItem(IDC_FLOOR_V).EnableWindow(m_s.floor_on);
        GetDlgItem(IDC_LGAIN).EnableWindow(m_s.listen);
        GetDlgItem(IDC_LGAIN_V).EnableWindow(m_s.listen);
        const char* info = kModeInfo[m_s.mode];
        if (m_s.bypass) info = "BYPASSED: the output is the original, unprocessed audio (like Calf / EasyEffects bypass). Untick to hear the effect again.";
        else if (m_s.listen) info = "LISTEN: only the bass the enhancer adds, without the original audio (Calf's listen). Level: Amount, Output and Listen gain.";
        uSetDlgItemText(m_hWnd, IDC_INFO, info);
    }

    void push() {
        dsp_preset_impl p;
        make_preset(m_s, p);
        m_cb.on_preset_changed(p);
    }

    void OnHScroll(UINT, UINT, CScrollBar bar) {
        if (m_updating) return;
        const int id = bar.GetDlgCtrlID();
        for (const auto& d : kSliders) {
            if (d.id != id) continue;
            CTrackBarCtrl tb = bar.m_hWnd;
            m_s.*d.field = std::clamp(d.lo + tb.GetPos() * d.step, d.lo, d.hi);
            update_label(d);
            push();
        }
    }

    void OnMode(UINT, int, CWindow) {
        if (m_updating) return;
        const int sel = CComboBox(GetDlgItem(IDC_MODE)).GetCurSel();
        if (sel >= 0 && sel < 3) m_s.mode = kComboModes[sel];
        update_state();
        push();
    }

    void OnCheck(UINT, int, CWindow) {
        if (m_updating) return;
        m_s.listen = IsDlgButtonChecked(IDC_LISTEN) == BST_CHECKED;
        m_s.floor_on = IsDlgButtonChecked(IDC_FLOOR_ON) == BST_CHECKED;
        m_s.bypass = IsDlgButtonChecked(IDC_BYPASS) == BST_CHECKED;
        update_state();
        push();
    }

    void OnReset(UINT, int, CWindow) {
        m_s = Settings();
        refresh();
        push();
    }

    void OnClose(UINT, int id, CWindow) { EndDialog(id); }

    const dsp_preset& m_init;
    dsp_preset_edit_callback& m_cb;
    Settings m_s;
    bool m_updating = false;
    fb2k::CDarkModeHooks m_dark;
};

void run_config_popup(const dsp_preset& p, HWND parent, dsp_preset_edit_callback& cb) {
    CConfigDialog dlg(p, cb);
    // Cancel restores the settings the dialog was opened with.
    if (dlg.DoModal(parent) != IDOK) cb.on_preset_changed(p);
}

} // namespace
