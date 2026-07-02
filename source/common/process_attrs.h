// Shared spectral-processing attributes for paulstretch.stream~ and paulstretch~.
//
// libpaulstretch's ProcessOptions exposes a suite of optional spectral effects.
// All of them hot-swap via StreamingStretcher::set_process_options (a plain
// struct copy,  no realloc), so each maps to a live Max attribute, same
// thread-safety class as the `stretch` attribute.
//
// This header supplies the shared struct and library<->Max conversion helpers.
// The per-attribute setters and CLASS_ATTR_* registration that drive these
// fields live in pstretch_shared.h, shared by both objects.

#pragma once

#include "ext.h"
#include "ext_obex.h"

#include "paulstretch/paulstretch.h"
#include "paulstretch_engine.h"

namespace maxpaulstretch {

// POD mirror of paulstretch::ProcessOptions using Max-native attribute types
// (t_atom_long for toggles/ints, double for floats). Field names match the
// table below; embedded as `procattrs` in each object struct.
struct ProcessAttrs
{
    t_atom_long pitch_shift_enabled;
    t_atom_long pitch_shift_cents;

    t_atom_long octave_enabled;
    double octave[6]; // gains for -2, -1, 0, +1, +1.5, +2 octaves

    t_atom_long frequency_shift_enabled;
    t_atom_long frequency_shift_hz;

    t_atom_long compressor_enabled;
    double compressor_power;

    t_atom_long filter_enabled;
    double filter_low_hz;
    double filter_high_hz;
    double filter_high_damp;
    t_atom_long filter_stop;

    t_atom_long harmonics_enabled;
    double harmonics_frequency_hz;
    double harmonics_bandwidth_cents;
    t_atom_long harmonics_count;
    t_atom_long harmonics_gauss;

    t_atom_long spread_enabled;
    double spread_bandwidth;

    t_atom_long tonal_noise_enabled;
    double tonal_noise_preserve;
    double tonal_noise_bandwidth;

    t_atom_long arbitrary_filter_enabled; // managed by the `arbfilter` message
};

// Library-matching defaults (NOT all-zero: octave_0 = 1, filter_high = 22000,
// harmonics defaults). Call from each object's *_new before attr_args_process.
inline void process_attrs_init(ProcessAttrs* p)
{
    p->pitch_shift_enabled = 0;
    p->pitch_shift_cents = 0;

    p->octave_enabled = 0;
    p->octave[0] = 0.0; // -2
    p->octave[1] = 0.0; // -1
    p->octave[2] = 1.0; // 0
    p->octave[3] = 0.0; // +1
    p->octave[4] = 0.0; // +1.5
    p->octave[5] = 0.0; // +2

    p->frequency_shift_enabled = 0;
    p->frequency_shift_hz = 0;

    p->compressor_enabled = 0;
    p->compressor_power = 0.0;

    p->filter_enabled = 0;
    p->filter_low_hz = 0.0;
    p->filter_high_hz = 22000.0;
    p->filter_high_damp = 0.0;
    p->filter_stop = 0;

    p->harmonics_enabled = 0;
    p->harmonics_frequency_hz = 440.0;
    p->harmonics_bandwidth_cents = 25.0;
    p->harmonics_count = 10;
    p->harmonics_gauss = 0;

    p->spread_enabled = 0;
    p->spread_bandwidth = 0.3;

    p->tonal_noise_enabled = 0;
    p->tonal_noise_preserve = 0.5;
    p->tonal_noise_bandwidth = 0.9;

    p->arbitrary_filter_enabled = 0;
}

// Build a library ProcessOptions from the Max-facing struct.
inline paulstretch::ProcessOptions to_process_options(const ProcessAttrs& p)
{
    paulstretch::ProcessOptions o;
    o.pitch_shift_enabled = p.pitch_shift_enabled != 0;
    o.pitch_shift_cents = static_cast<int>(p.pitch_shift_cents);

    o.octave_enabled = p.octave_enabled != 0;
    o.octave_minus2 = static_cast<float>(p.octave[0]);
    o.octave_minus1 = static_cast<float>(p.octave[1]);
    o.octave_0 = static_cast<float>(p.octave[2]);
    o.octave_plus1 = static_cast<float>(p.octave[3]);
    o.octave_plus15 = static_cast<float>(p.octave[4]);
    o.octave_plus2 = static_cast<float>(p.octave[5]);

    o.frequency_shift_enabled = p.frequency_shift_enabled != 0;
    o.frequency_shift_hz = static_cast<int>(p.frequency_shift_hz);

    o.compressor_enabled = p.compressor_enabled != 0;
    o.compressor_power = static_cast<float>(p.compressor_power);

    o.filter_enabled = p.filter_enabled != 0;
    o.filter_low_hz = static_cast<float>(p.filter_low_hz);
    o.filter_high_hz = static_cast<float>(p.filter_high_hz);
    o.filter_high_damp = static_cast<float>(p.filter_high_damp);
    o.filter_stop = p.filter_stop != 0;

    o.harmonics_enabled = p.harmonics_enabled != 0;
    o.harmonics_frequency_hz = static_cast<float>(p.harmonics_frequency_hz);
    o.harmonics_bandwidth_cents = static_cast<float>(p.harmonics_bandwidth_cents);
    o.harmonics_count = static_cast<int>(p.harmonics_count);
    o.harmonics_gauss = p.harmonics_gauss != 0;

    o.spread_enabled = p.spread_enabled != 0;
    o.spread_bandwidth = static_cast<float>(p.spread_bandwidth);

    o.tonal_noise_enabled = p.tonal_noise_enabled != 0;
    o.tonal_noise_preserve = static_cast<float>(p.tonal_noise_preserve);
    o.tonal_noise_bandwidth = static_cast<float>(p.tonal_noise_bandwidth);

    o.arbitrary_filter_enabled = p.arbitrary_filter_enabled != 0;
    return o;
}

inline void apply_process_options(Engine* engine, const ProcessAttrs& attrs)
{
    if (engine) {
        engine->set_process_options(to_process_options(attrs));
    }
}

// Grey out (in the inspector) each attribute that has no effect while its
// feature toggle is off. Call at object creation and from every toggle setter.
inline void update_attr_disabled(t_object* x, const ProcessAttrs& p)
{
    auto dep = [x](const char* name, t_atom_long enabled) {
        object_attr_setdisabled(x, gensym(name), enabled ? 0 : 1);
    };
    dep("pitchcents", p.pitch_shift_enabled);
    dep("octavemix", p.octave_enabled);
    dep("freqshifthz", p.frequency_shift_enabled);
    dep("comppower", p.compressor_enabled);
    dep("filterlow", p.filter_enabled);
    dep("filterhigh", p.filter_enabled);
    dep("filterdamp", p.filter_enabled);
    dep("filterstop", p.filter_enabled);
    dep("harmfreq", p.harmonics_enabled);
    dep("harmbw", p.harmonics_enabled);
    dep("harmcount", p.harmonics_enabled);
    dep("harmgauss", p.harmonics_enabled);
    dep("spreadbw", p.spread_enabled);
    dep("tonalpreserve", p.tonal_noise_enabled);
    dep("tonalbw", p.tonal_noise_enabled);
}

} // namespace maxpaulstretch
