// paulstretch~ / mc.paulstretch~ — Paulstretch playback of a named buffer~.

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "ext_buffer.h"

#include <algorithm>
#include <cstddef>
#include <type_traits>

#include "pstretch_shared.h"

static t_class* s_pstretch_buf_class = nullptr; // paulstretch~
static t_class* s_pstretch_mc_class = nullptr; // mc.paulstretch~

struct t_pstretch_buf
{
    maxpaulstretch::PstretchBase base;

    t_buffer_ref* buf_ref;
    t_symbol* buf_name;

    t_atom_long loop;

    long multichannel;
    long num_channels;

    void* done_outlet; // bangs when a one-shot pass finishes (rightmost outlet)
    t_clock* done_clock; // defers that bang off the audio thread

    // Transport
    long playing;
    long pending_reset; // set on the message thread by pstretch_buf_int, consumed in perform64
    long* cursors; // per-channel read position in buffer frames (num_channels long)
};

// `base` must sit at offset 0 so the t_pxobject leads the struct (MSP) and the
// CLASS_ATTR_* offsets computed against PstretchBase stay valid for t_pstretch_buf.
static_assert(std::is_standard_layout_v<t_pstretch_buf>);
static_assert(offsetof(t_pstretch_buf, base) == 0);

void* pstretch_buf_new(t_symbol* s, long argc, t_atom* argv);
void pstretch_buf_free(t_pstretch_buf* x);
void pstretch_buf_assist(t_pstretch_buf* x, void* b, long m, long a, char* dst);
void pstretch_buf_set(t_pstretch_buf* x, t_symbol* s, long argc, t_atom* argv);
void pstretch_buf_int(t_pstretch_buf* x, long n);
void pstretch_buf_done_tick(t_pstretch_buf* x);
long pstretch_buf_multichanneloutputs(t_pstretch_buf* x, long index);
t_max_err pstretch_buf_notify(t_pstretch_buf* x, t_symbol* s, t_symbol* msg, void* sender, void* data);
void pstretch_buf_dsp64(t_pstretch_buf* x, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags);
void pstretch_buf_perform64(t_pstretch_buf* x, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam);

static t_class* pstretch_buf_class_setup(const char* name)
{
    t_class* c = class_new(name, (method)pstretch_buf_new, (method)pstretch_buf_free, sizeof(t_pstretch_buf), nullptr, A_GIMME, 0);

    class_addmethod(c, (method)pstretch_buf_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)pstretch_buf_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)pstretch_buf_set, "set", A_GIMME, 0);
    class_addmethod(c, (method)pstretch_buf_int, "int", A_LONG, 0);
    class_addmethod(c, (method)pstretch_buf_multichanneloutputs, "multichanneloutputs", A_CANT, 0);
    class_addmethod(c, (method)pstretch_buf_notify, "notify", A_CANT, 0);

    maxpaulstretch::register_common_attrs(c);

    CLASS_ATTR_LONG(c, "loop", 0, t_pstretch_buf, loop);
    CLASS_ATTR_STYLE_LABEL(c, "loop", 0, "onoff", "Loop");

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    return c;
}

extern "C" void ext_main(void*)
{
    // One implementation, two names. mc.paulstretch~ is also mapped to this external's
    // file by the objectfile entry in the package init/ folder, so Max can resolve the
    // name; the class registered here handles it once the file is loaded.
    s_pstretch_buf_class = pstretch_buf_class_setup("paulstretch~");
    s_pstretch_mc_class = pstretch_buf_class_setup("mc.paulstretch~");
}

// Grow or shrink the per-channel engine and cursor arrays to `n` (>= 1). engines[0]
// is created in pstretch_buf_new and never freed here, so it keeps the cached
// stretch/arbitrary envelopes; every newly created engine is seeded from it. The
// caller (dsp64) then configures each engine, so no stretcher is built here.
static void pstretch_buf_set_channels(t_pstretch_buf* x, long n)
{
    if (n < 1) {
        n = 1;
    }
    if (n == x->base.num_engines) {
        return;
    }

    maxpaulstretch::Engine** engines = new maxpaulstretch::Engine*[n];
    long* cursors = new long[n];
    const long keep = x->base.num_engines < n ? x->base.num_engines : n;
    for (long i = 0; i < keep; ++i) {
        engines[i] = x->base.engines[i];
        cursors[i] = x->cursors[i];
    }
    for (long i = keep; i < n; ++i) {
        engines[i] = new maxpaulstretch::Engine();
        if (!x->base.engines[0]->stretch_envelope().empty()) {
            engines[i]->set_stretch_envelope(x->base.engines[0]->stretch_envelope());
        }
        if (!x->base.engines[0]->arbitrary_filter().empty()) {
            engines[i]->set_arbitrary_filter(x->base.engines[0]->arbitrary_filter());
        }
        cursors[i] = 0;
    }
    for (long i = n; i < x->base.num_engines; ++i) {
        delete x->base.engines[i];
    }
    delete[] x->base.engines;
    delete[] x->cursors;
    x->base.engines = engines;
    x->cursors = cursors;
    x->base.num_engines = n;
    x->num_channels = n;
}

void* pstretch_buf_new(t_symbol* s, long argc, t_atom* argv)
{
    long multichannel = (s == gensym("mc.paulstretch~"));
    t_pstretch_buf* x = (t_pstretch_buf*)object_alloc(multichannel ? s_pstretch_mc_class : s_pstretch_buf_class);
    if (!x) {
        return nullptr;
    }

    x->multichannel = multichannel;

    dsp_setup((t_pxobject*)x, 1); // inlet 0 carries transport/buffer messages
    // Outlets are created right-to-left: bang (rightmost), then sync, then audio
    // (leftmost, so the stretched signal stays outlet 0).
    x->done_outlet = bangout((t_object*)x);
    outlet_new((t_object*)x, "signal"); // sync: normalized play position 0..1
    outlet_new((t_object*)x, x->multichannel ? "multichannelsignal" : "signal");
    x->done_clock = clock_new(x, (method)pstretch_buf_done_tick);

    // Start with a single engine so the transport handler is safe before dsp64;
    // dsp64 grows this to the buffer's channel count for mc.paulstretch~.
    x->base.engines = new maxpaulstretch::Engine*[1]{ new maxpaulstretch::Engine() };
    x->base.num_engines = 1;
    x->cursors = new long[1]{ 0 };
    x->num_channels = 1;
    x->playing = 0;
    x->pending_reset = 0;
    x->base.sr = sys_getsr();

    x->base.stretch = 8.0;
    x->base.fftsize = 4096;
    x->loop = 0;
    x->base.window = 2; // Hann (library default)
    x->base.onset = 0.0; // onset detection off
    maxpaulstretch::process_attrs_init(&x->base.procattrs);

    // First (non-attribute) argument names the buffer~.
    t_symbol* name = gensym("");
    if (argc && atom_gettype(argv) == A_SYM) {
        name = atom_getsym(argv);
    }
    x->buf_name = name;
    x->buf_ref = buffer_ref_new((t_object*)x, name);

    attr_args_process(x, (short)argc, argv);
    maxpaulstretch::update_attr_disabled((t_object*)x, x->base.procattrs);
    return x;
}

void pstretch_buf_free(t_pstretch_buf* x)
{
    dsp_free((t_pxobject*)x);
    if (x->buf_ref) {
        object_free(x->buf_ref);
    }
    if (x->done_clock) {
        object_free(x->done_clock);
    }
    for (long i = 0; i < x->base.num_engines; ++i) {
        delete x->base.engines[i];
    }
    delete[] x->base.engines;
    delete[] x->cursors;
}

// Deferred from the audio thread (see pstretch_buf_perform64): bang the rightmost
// outlet on the scheduler thread when a one-shot pass finishes.
void pstretch_buf_done_tick(t_pstretch_buf* x)
{
    outlet_bang(x->done_outlet);
}

void pstretch_buf_assist(t_pstretch_buf* x, void*, long m, long a, char* dst)
{
    if (m == ASSIST_INLET) {
        snprintf(dst, 512, "set <name>, 1/0 start/stop");
    }
    else {
        switch (a) {
            case 0:
                snprintf(dst, 512, x->multichannel ? "(multichannelsignal) Stretched output" : "(signal) Stretched output");
                break;
            case 1:
                snprintf(dst, 512, "(signal) Play position 0..1");
                break;
            default:
                snprintf(dst, 512, "(bang) When a one-shot pass finishes");
                break;
        }
    }
}

void pstretch_buf_set(t_pstretch_buf* x, t_symbol*, long argc, t_atom* argv)
{
    if (argc && atom_gettype(argv) == A_SYM) {
        x->buf_name = atom_getsym(argv);
        buffer_ref_set(x->buf_ref, x->buf_name);
    }
}

// Runs on the message thread. Engine::reset() rebuilds the live Stretcher, so
// calling it here would race with a concurrently running perform64. Just flag
// it and let perform64 do the actual reset on the audio thread.
void pstretch_buf_int(t_pstretch_buf* x, long n)
{
    if (n) {
        x->pending_reset = 1;
        x->playing = 1;
    }
    else {
        x->playing = 0;
    }
}

// Report the output channel count per outlet. Outlet 0 (audio) follows the buffer's
// channel count for mc.paulstretch~ (min 1) and is mono for plain paulstretch~; the
// sync outlet (index 1) is always 1 channel. Outlet 0 is read here and in dsp64
// during the same compile pass so the two agree.
long pstretch_buf_multichanneloutputs(t_pstretch_buf* x, long index)
{
    if (index != 0 || !x->multichannel) {
        return 1;
    }
    t_buffer_obj* buf = buffer_ref_getobject(x->buf_ref);
    long n = buf ? (long)buffer_getchannelcount(buf) : 1;
    return n < 1 ? 1 : n;
}

t_max_err pstretch_buf_notify(t_pstretch_buf* x, t_symbol* s, t_symbol* msg, void* sender, void* data)
{
    return buffer_ref_notify(x->buf_ref, s, msg, sender, data);
}

void pstretch_buf_dsp64(t_pstretch_buf* x, t_object* dsp64, short*, double samplerate, long, long)
{
    x->base.sr = samplerate;

    // Latch the channel count from the buffer for this DSP run. It must match
    // what pstretch_buf_multichanneloutputs reported for the chain to line up.
    pstretch_buf_set_channels(x, pstretch_buf_multichanneloutputs(x, 0));

    for (long i = 0; i < x->base.num_engines; ++i) {
        if (!x->base.engines[i]->configure(samplerate, x->base.stretch, x->base.fftsize, x->base.window, x->base.onset)) {
            object_error((t_object*)x, "failed to initialize the stretcher");
        }
        maxpaulstretch::apply_process_options(x->base.engines[i], x->base.procattrs); // re-apply hot state to the fresh stretcher
    }
    object_method(dsp64, gensym("dsp_add64"), x, (method)pstretch_buf_perform64, 0, nullptr);
}

void pstretch_buf_perform64(t_pstretch_buf* x, t_object*, double**, long, double** outs, long numouts, long sampleframes, long, void*)
{
    auto silence = [&]() {
        for (long ch = 0; ch < numouts; ++ch) {
            std::fill_n(outs[ch], sampleframes, 0.0);
        }
    };

    if (!x->playing || x->base.num_engines < 1 || !x->base.engines[0]->ready()) {
        silence();
        return;
    }

    if (x->pending_reset) {
        x->pending_reset = 0;
        for (long i = 0; i < x->base.num_engines; ++i) {
            x->cursors[i] = 0;
            x->base.engines[i]->reset();
        }
    }

    t_buffer_obj* buf = buffer_ref_getobject(x->buf_ref);
    if (!buf) {
        silence();
        return;
    }
    float* tab = buffer_locksamples(buf);
    if (!tab) {
        silence();
        return;
    }

    const long frames = (long)buffer_getframecount(buf);
    const long live = (long)buffer_getchannelcount(buf); // buffer's current channel count
    const long audio = numouts > 0 ? numouts - 1 : 0; // last outlet carries the sync signal
    const long nch = x->num_channels < audio ? x->num_channels : audio;

    // One stretcher per output channel, each reading its own column of the buffer
    // with its own cursor (identical across channels while onset detection is off).
    for (long ch = 0; ch < nch; ++ch) {
        auto fill = [x, tab, frames, live, ch](float* d, int count) -> float {
            long c = x->cursors[ch];
            if (d) {
                for (int k = 0; k < count; ++k) {
                    d[k] = (c >= 0 && c < frames && ch < live) ? tab[c * live + ch] : 0.0f;
                    ++c;
                    if (x->loop && frames > 0 && c >= frames) {
                        c = 0;
                    }
                }
            }
            else if (count > 0) {
                c += count;
                if (x->loop && frames > 0) {
                    c %= frames;
                }
            }
            x->cursors[ch] = c;
            if (frames <= 0) {
                return 0.0f;
            }
            float pct = 100.0f * (float)c / (float)frames;
            return pct < 0.0f ? 0.0f : (pct > 100.0f ? 100.0f : pct);
        };
        x->base.engines[ch]->pull(outs[ch], sampleframes, fill);
    }

    buffer_unlocksamples(buf);

    // Zero any audio channels we didn't fill (buffer has fewer channels than the outlet).
    for (long ch = nch; ch < audio; ++ch) {
        std::fill_n(outs[ch], sampleframes, 0.0);
    }

    // Sync outlet (rightmost signal outlet): normalized play position 0..1, taken from
    // channel 0's cursor (all channels share it while onset detection is off).
    if (numouts > 0) {
        double pos = frames > 0 ? (double)x->cursors[0] / (double)frames : 0.0;
        pos = pos < 0.0 ? 0.0 : (pos > 1.0 ? 1.0 : pos);
        std::fill_n(outs[numouts - 1], sampleframes, pos);
    }

    // End of a one-shot pass: halt once every channel cursor runs past the buffer,
    // and bang the done outlet (deferred off the audio thread by the clock).
    if (nch > 0 && !x->loop && frames > 0) {
        bool done = true;
        for (long ch = 0; ch < nch; ++ch) {
            if (x->cursors[ch] < frames) {
                done = false;
                break;
            }
        }
        if (done) {
            x->playing = 0;
            clock_delay(x->done_clock, 0);
        }
    }
}
