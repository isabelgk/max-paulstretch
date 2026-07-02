// Shared object base, attribute setters, and registration for paulstretch.stream~ and
// paulstretch~.
//
// The two objects differ only in where they source samples (a live record ring
// vs. a named buffer~) and their transport. Everything else around the
// stretcher is identical and lives here.
//
// PstretchBase holds the state the shared code touches and is embedded as the
// FIRST member of each object struct. That keeps the t_pxobject at offset 0 (as
// MSP requires) and makes offsets computed against PstretchBase by the
// CLASS_ATTR_* macros equal to the offsets in the full object struct. Because
// `base` sits at offset 0, the object pointer Max hands to a registered setter
// is also a valid PstretchBase*, so the setters below take PstretchBase* and are
// cast into place by CLASS_ATTR_ACCESSORS / class_addmethod.

#pragma once

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"

#include <cstddef>
#include <type_traits>

#include "paulstretch_engine.h"
#include "process_attrs.h"
#include "envelopes.h"

namespace maxpaulstretch {

// Common object header + the attribute state the shared setters operate on.
// Embed as the first member of each object struct (see file header).
struct PstretchBase
{
    t_pxobject ob;

    // One stretcher per output channel. paulstretch.stream~ always has exactly
    // one; mc.paulstretch~ has one per buffer channel. The array is a raw pointer
    // (not std::vector) because object_alloc does not run C++ constructors — the
    // owning object new/deletes the engines and the array by hand.
    Engine** engines;
    long num_engines;

    double stretch; // stretch factor (>= 1)
    t_atom_long fftsize; // FFT window size in samples (library sanitizes)
    t_atom_long window; // analysis window type (enumindex, applied on DSP restart)
    double onset; // onset-detection sensitivity 0..1 (hot)
    ProcessAttrs procattrs; // spectral processing (all hot)

    double sr;
};

static_assert(std::is_standard_layout_v<PstretchBase>,
              "PstretchBase must be standard-layout for offsetof in the CLASS_ATTR_* macros to be well-defined");

// Apply an operation to every per-channel engine. All shared attribute/message
// handlers fan out through this so a parameter change lands on all channels.
template <class Fn>
inline void engines_for_each(PstretchBase* x, Fn fn)
{
    for (long i = 0; i < x->num_engines; ++i) {
        fn(x->engines[i]);
    }
}

// Base stretch factor which hot-swaps without a DSP restart.
inline t_max_err pstretch_stretch_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->stretch = atom_getfloat(av);
        if (x->stretch < 1.0) {
            x->stretch = 1.0;
        }
        engines_for_each(x, [x](Engine* e) { e->set_stretch(x->stretch); }); // hot-swap, no DSP restart
    }
    return MAX_ERR_NONE;
}

// Onset-detection sensitivity which hot-swaps without a DSP restart.
inline t_max_err pstretch_onset_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->onset = atom_getfloat(av);
        x->onset = x->onset < 0.0 ? 0.0 : (x->onset > 1.0 ? 1.0 : x->onset);
        engines_for_each(x, [x](Engine* e) { e->set_onset(x->onset); }); // hot-swap, no DSP restart
    }
    return MAX_ERR_NONE;
}

// Spectral-processing attribute setters. Each stores the value into its
// procattrs field, then re-pushes the whole ProcessOptions to the live
// stretcher (a plain struct copy with no realloc).

inline t_max_err pstretch_pitchshift_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.pitch_shift_enabled = (t_atom_long)atom_getlong(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
        update_attr_disabled((t_object*)x, x->procattrs);
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_pitchcents_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.pitch_shift_cents = (t_atom_long)atom_getlong(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_octaves_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.octave_enabled = (t_atom_long)atom_getlong(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
        update_attr_disabled((t_object*)x, x->procattrs);
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_freqshift_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.frequency_shift_enabled = (t_atom_long)atom_getlong(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
        update_attr_disabled((t_object*)x, x->procattrs);
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_freqshifthz_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.frequency_shift_hz = (t_atom_long)atom_getlong(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_compressor_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.compressor_enabled = (t_atom_long)atom_getlong(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
        update_attr_disabled((t_object*)x, x->procattrs);
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_comppower_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.compressor_power = atom_getfloat(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_filter_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.filter_enabled = (t_atom_long)atom_getlong(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
        update_attr_disabled((t_object*)x, x->procattrs);
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_filterlow_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.filter_low_hz = atom_getfloat(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_filterhigh_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.filter_high_hz = atom_getfloat(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_filterdamp_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.filter_high_damp = atom_getfloat(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_filterstop_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.filter_stop = (t_atom_long)atom_getlong(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_harmonics_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.harmonics_enabled = (t_atom_long)atom_getlong(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
        update_attr_disabled((t_object*)x, x->procattrs);
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_harmfreq_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.harmonics_frequency_hz = atom_getfloat(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_harmbw_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.harmonics_bandwidth_cents = atom_getfloat(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_harmcount_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.harmonics_count = (t_atom_long)atom_getlong(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_harmgauss_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.harmonics_gauss = (t_atom_long)atom_getlong(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_spread_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.spread_enabled = (t_atom_long)atom_getlong(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
        update_attr_disabled((t_object*)x, x->procattrs);
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_spreadbw_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.spread_bandwidth = atom_getfloat(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_tonalnoise_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.tonal_noise_enabled = (t_atom_long)atom_getlong(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
        update_attr_disabled((t_object*)x, x->procattrs);
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_tonalpreserve_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.tonal_noise_preserve = atom_getfloat(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_tonalbw_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    if (ac && av) {
        x->procattrs.tonal_noise_bandwidth = atom_getfloat(av);
        engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
    }
    return MAX_ERR_NONE;
}

inline t_max_err pstretch_octavemix_set(PstretchBase* x, void*, long ac, t_atom* av)
{
    long n = ac < 6 ? ac : 6;
    for (long i = 0; i < n; ++i) {
        x->procattrs.octave[i] = atom_getfloat(av + i);
    }
    engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
    return MAX_ERR_NONE;
}

// Breakpoint-envelope message handlers. `stretchenv` sets a stretch-multiplier
// curve (empty list clears it); `arbfilter` sets an arbitrary spectral gain curve
// and flips the procattrs enable flag (empty list clears + disables). Positions
// are parsed and clamped by atoms_to_breakpoints.

inline void pstretch_stretchenv(PstretchBase* x, t_symbol*, long argc, t_atom* argv)
{
    if (argc <= 0) {
        engines_for_each(x, [](Engine* e) { e->clear_stretch_envelope(); });
        return;
    }
    auto env = atoms_to_breakpoints(argc, argv);
    engines_for_each(x, [&env](Engine* e) { e->set_stretch_envelope(env); });
}

inline void pstretch_arbfilter(PstretchBase* x, t_symbol*, long argc, t_atom* argv)
{
    if (argc <= 0) {
        engines_for_each(x, [](Engine* e) { e->clear_arbitrary_filter(); });
        x->procattrs.arbitrary_filter_enabled = 0;
    }
    else {
        auto filter = atoms_to_breakpoints(argc, argv);
        engines_for_each(x, [&filter](Engine* e) { e->set_arbitrary_filter(filter); });
        x->procattrs.arbitrary_filter_enabled = 1;
    }
    engines_for_each(x, [x](Engine* e) { apply_process_options(e, x->procattrs); });
}

// Register every attribute and message common to both objects: the base
// stretch/FFT/window/onset attributes, the full spectral-processing suite, and
// the stretchenv/arbfilter messages. Each object registers its own
// source-specific attributes (windowsize / loop), methods, and DSP routines.
inline void register_common_attrs(t_class* c)
{
    CLASS_ATTR_DOUBLE(c, "stretch", 0, PstretchBase, stretch);
    CLASS_ATTR_ACCESSORS(c, "stretch", nullptr, pstretch_stretch_set);
    CLASS_ATTR_FILTER_MIN(c, "stretch", 1.0);
    CLASS_ATTR_LABEL(c, "stretch", 0, "Stretch Factor");

    CLASS_ATTR_LONG(c, "fftsize", 0, PstretchBase, fftsize);
    CLASS_ATTR_FILTER_MIN(c, "fftsize", 16);
    CLASS_ATTR_LABEL(c, "fftsize", 0, "FFT Size");

    CLASS_ATTR_LONG(c, "window", 0, PstretchBase, window);
    CLASS_ATTR_ENUMINDEX5(c, "window", 0, "Rectangular", "Hamming", "Hann", "Blackman", "Blackman-Harris");
    CLASS_ATTR_FILTER_CLIP(c, "window", 0, 4);
    CLASS_ATTR_LABEL(c, "window", 0, "Analysis Window");

    CLASS_ATTR_DOUBLE(c, "onset", 0, PstretchBase, onset);
    CLASS_ATTR_ACCESSORS(c, "onset", nullptr, pstretch_onset_set);
    CLASS_ATTR_FILTER_CLIP(c, "onset", 0.0, 1.0);
    CLASS_ATTR_LABEL(c, "onset", 0, "Onset Detection Sensitivity");

    // Spectral-processing attributes (all live/hot). Toggles use the "onoff"
    // style; everything is grouped under the "Spectral Processing" category.
    CLASS_ATTR_LONG(c, "pitchshift", 0, PstretchBase, procattrs.pitch_shift_enabled);
    CLASS_ATTR_ACCESSORS(c, "pitchshift", nullptr, pstretch_pitchshift_set);
    CLASS_ATTR_STYLE_LABEL(c, "pitchshift", 0, "onoff", "Pitch Shift");
    CLASS_ATTR_CATEGORY(c, "pitchshift", 0, "Spectral Processing");

    CLASS_ATTR_LONG(c, "pitchcents", 0, PstretchBase, procattrs.pitch_shift_cents);
    CLASS_ATTR_ACCESSORS(c, "pitchcents", nullptr, pstretch_pitchcents_set);
    CLASS_ATTR_LABEL(c, "pitchcents", 0, "Pitch Shift (cents)");
    CLASS_ATTR_CATEGORY(c, "pitchcents", 0, "Spectral Processing");

    CLASS_ATTR_LONG(c, "octaves", 0, PstretchBase, procattrs.octave_enabled);
    CLASS_ATTR_ACCESSORS(c, "octaves", nullptr, pstretch_octaves_set);
    CLASS_ATTR_STYLE_LABEL(c, "octaves", 0, "onoff", "Octave Mixer");
    CLASS_ATTR_CATEGORY(c, "octaves", 0, "Spectral Processing");

    CLASS_ATTR_LONG(c, "freqshift", 0, PstretchBase, procattrs.frequency_shift_enabled);
    CLASS_ATTR_ACCESSORS(c, "freqshift", nullptr, pstretch_freqshift_set);
    CLASS_ATTR_STYLE_LABEL(c, "freqshift", 0, "onoff", "Frequency Shift");
    CLASS_ATTR_CATEGORY(c, "freqshift", 0, "Spectral Processing");

    CLASS_ATTR_LONG(c, "freqshifthz", 0, PstretchBase, procattrs.frequency_shift_hz);
    CLASS_ATTR_ACCESSORS(c, "freqshifthz", nullptr, pstretch_freqshifthz_set);
    CLASS_ATTR_LABEL(c, "freqshifthz", 0, "Frequency Shift (Hz)");
    CLASS_ATTR_CATEGORY(c, "freqshifthz", 0, "Spectral Processing");

    CLASS_ATTR_LONG(c, "compressor", 0, PstretchBase, procattrs.compressor_enabled);
    CLASS_ATTR_ACCESSORS(c, "compressor", nullptr, pstretch_compressor_set);
    CLASS_ATTR_STYLE_LABEL(c, "compressor", 0, "onoff", "Spectral Compressor");
    CLASS_ATTR_CATEGORY(c, "compressor", 0, "Spectral Processing");

    CLASS_ATTR_DOUBLE(c, "comppower", 0, PstretchBase, procattrs.compressor_power);
    CLASS_ATTR_ACCESSORS(c, "comppower", nullptr, pstretch_comppower_set);
    CLASS_ATTR_LABEL(c, "comppower", 0, "Compressor Power");
    CLASS_ATTR_CATEGORY(c, "comppower", 0, "Spectral Processing");

    CLASS_ATTR_LONG(c, "filter", 0, PstretchBase, procattrs.filter_enabled);
    CLASS_ATTR_ACCESSORS(c, "filter", nullptr, pstretch_filter_set);
    CLASS_ATTR_STYLE_LABEL(c, "filter", 0, "onoff", "Bandpass Filter");
    CLASS_ATTR_CATEGORY(c, "filter", 0, "Spectral Processing");

    CLASS_ATTR_DOUBLE(c, "filterlow", 0, PstretchBase, procattrs.filter_low_hz);
    CLASS_ATTR_ACCESSORS(c, "filterlow", nullptr, pstretch_filterlow_set);
    CLASS_ATTR_LABEL(c, "filterlow", 0, "Filter Low (Hz)");
    CLASS_ATTR_CATEGORY(c, "filterlow", 0, "Spectral Processing");

    CLASS_ATTR_DOUBLE(c, "filterhigh", 0, PstretchBase, procattrs.filter_high_hz);
    CLASS_ATTR_ACCESSORS(c, "filterhigh", nullptr, pstretch_filterhigh_set);
    CLASS_ATTR_LABEL(c, "filterhigh", 0, "Filter High (Hz)");
    CLASS_ATTR_CATEGORY(c, "filterhigh", 0, "Spectral Processing");

    CLASS_ATTR_DOUBLE(c, "filterdamp", 0, PstretchBase, procattrs.filter_high_damp);
    CLASS_ATTR_ACCESSORS(c, "filterdamp", nullptr, pstretch_filterdamp_set);
    CLASS_ATTR_LABEL(c, "filterdamp", 0, "Filter High Damp");
    CLASS_ATTR_CATEGORY(c, "filterdamp", 0, "Spectral Processing");

    CLASS_ATTR_LONG(c, "filterstop", 0, PstretchBase, procattrs.filter_stop);
    CLASS_ATTR_ACCESSORS(c, "filterstop", nullptr, pstretch_filterstop_set);
    CLASS_ATTR_STYLE_LABEL(c, "filterstop", 0, "onoff", "Filter Notch (stop-band)");
    CLASS_ATTR_CATEGORY(c, "filterstop", 0, "Spectral Processing");

    CLASS_ATTR_LONG(c, "harmonics", 0, PstretchBase, procattrs.harmonics_enabled);
    CLASS_ATTR_ACCESSORS(c, "harmonics", nullptr, pstretch_harmonics_set);
    CLASS_ATTR_STYLE_LABEL(c, "harmonics", 0, "onoff", "Harmonics");
    CLASS_ATTR_CATEGORY(c, "harmonics", 0, "Spectral Processing");

    CLASS_ATTR_DOUBLE(c, "harmfreq", 0, PstretchBase, procattrs.harmonics_frequency_hz);
    CLASS_ATTR_ACCESSORS(c, "harmfreq", nullptr, pstretch_harmfreq_set);
    CLASS_ATTR_LABEL(c, "harmfreq", 0, "Harmonics Frequency (Hz)");
    CLASS_ATTR_CATEGORY(c, "harmfreq", 0, "Spectral Processing");

    CLASS_ATTR_DOUBLE(c, "harmbw", 0, PstretchBase, procattrs.harmonics_bandwidth_cents);
    CLASS_ATTR_ACCESSORS(c, "harmbw", nullptr, pstretch_harmbw_set);
    CLASS_ATTR_LABEL(c, "harmbw", 0, "Harmonics Bandwidth (cents)");
    CLASS_ATTR_CATEGORY(c, "harmbw", 0, "Spectral Processing");

    CLASS_ATTR_LONG(c, "harmcount", 0, PstretchBase, procattrs.harmonics_count);
    CLASS_ATTR_ACCESSORS(c, "harmcount", nullptr, pstretch_harmcount_set);
    CLASS_ATTR_LABEL(c, "harmcount", 0, "Harmonics Count");
    CLASS_ATTR_CATEGORY(c, "harmcount", 0, "Spectral Processing");

    CLASS_ATTR_LONG(c, "harmgauss", 0, PstretchBase, procattrs.harmonics_gauss);
    CLASS_ATTR_ACCESSORS(c, "harmgauss", nullptr, pstretch_harmgauss_set);
    CLASS_ATTR_STYLE_LABEL(c, "harmgauss", 0, "onoff", "Harmonics Gaussian");
    CLASS_ATTR_CATEGORY(c, "harmgauss", 0, "Spectral Processing");

    CLASS_ATTR_LONG(c, "spread", 0, PstretchBase, procattrs.spread_enabled);
    CLASS_ATTR_ACCESSORS(c, "spread", nullptr, pstretch_spread_set);
    CLASS_ATTR_STYLE_LABEL(c, "spread", 0, "onoff", "Spread");
    CLASS_ATTR_CATEGORY(c, "spread", 0, "Spectral Processing");

    CLASS_ATTR_DOUBLE(c, "spreadbw", 0, PstretchBase, procattrs.spread_bandwidth);
    CLASS_ATTR_ACCESSORS(c, "spreadbw", nullptr, pstretch_spreadbw_set);
    CLASS_ATTR_LABEL(c, "spreadbw", 0, "Spread Bandwidth");
    CLASS_ATTR_CATEGORY(c, "spreadbw", 0, "Spectral Processing");

    CLASS_ATTR_LONG(c, "tonalnoise", 0, PstretchBase, procattrs.tonal_noise_enabled);
    CLASS_ATTR_ACCESSORS(c, "tonalnoise", nullptr, pstretch_tonalnoise_set);
    CLASS_ATTR_STYLE_LABEL(c, "tonalnoise", 0, "onoff", "Tonal/Noise Preserve");
    CLASS_ATTR_CATEGORY(c, "tonalnoise", 0, "Spectral Processing");

    CLASS_ATTR_DOUBLE(c, "tonalpreserve", 0, PstretchBase, procattrs.tonal_noise_preserve);
    CLASS_ATTR_ACCESSORS(c, "tonalpreserve", nullptr, pstretch_tonalpreserve_set);
    CLASS_ATTR_LABEL(c, "tonalpreserve", 0, "Tonal/Noise Amount");
    CLASS_ATTR_CATEGORY(c, "tonalpreserve", 0, "Spectral Processing");

    CLASS_ATTR_DOUBLE(c, "tonalbw", 0, PstretchBase, procattrs.tonal_noise_bandwidth);
    CLASS_ATTR_ACCESSORS(c, "tonalbw", nullptr, pstretch_tonalbw_set);
    CLASS_ATTR_LABEL(c, "tonalbw", 0, "Tonal/Noise Bandwidth");
    CLASS_ATTR_CATEGORY(c, "tonalbw", 0, "Spectral Processing");

    CLASS_ATTR_DOUBLE_ARRAY(c, "octavemix", 0, PstretchBase, procattrs.octave, 6);
    CLASS_ATTR_ACCESSORS(c, "octavemix", nullptr, pstretch_octavemix_set);
    CLASS_ATTR_LABEL(c, "octavemix", 0, "Octave Mix (-2 -1 0 +1 +1.5 +2)");
    CLASS_ATTR_CATEGORY(c, "octavemix", 0, "Spectral Processing");

    class_addmethod(c, (method)pstretch_stretchenv, "stretchenv", A_GIMME, 0);
    class_addmethod(c, (method)pstretch_arbfilter, "arbfilter", A_GIMME, 0);
}

} // namespace maxpaulstretch
