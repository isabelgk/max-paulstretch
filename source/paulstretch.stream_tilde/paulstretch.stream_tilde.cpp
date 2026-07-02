// paulstretch.stream~ — live Paulstretch time-stretch.

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <vector>

#include "pstretch_shared.h"

static t_class* s_pstretch_class = nullptr;

struct t_pstretch
{
    maxpaulstretch::PstretchBase base; // common object header + shared attrs

    double windowsize; // recorded window length in ms

    // Live record ring (the input the stretcher scans)
    std::vector<float>* ring;
    long ring_len; // == ring->size(); 0 until dsp64
    long ring_write; // write head (live input)
    long ring_read; // read cursor (stretcher input)
};

// `base` must sit at offset 0 so the t_pxobject leads the struct (MSP) and the
// CLASS_ATTR_* offsets computed against PstretchBase stay valid for t_pstretch.
static_assert(std::is_standard_layout_v<t_pstretch>);
static_assert(offsetof(t_pstretch, base) == 0);

void* pstretch_new(t_symbol* s, long argc, t_atom* argv);
void pstretch_free(t_pstretch* x);
void pstretch_assist(t_pstretch* x, void* b, long m, long a, char* dst);
void pstretch_dsp64(t_pstretch* x, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags);
void pstretch_perform64(t_pstretch* x, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam);

extern "C" void ext_main(void*)
{
    t_class* c = class_new("paulstretch.stream~", (method)pstretch_new, (method)pstretch_free, sizeof(t_pstretch), nullptr, A_GIMME, 0);

    class_addmethod(c, (method)pstretch_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)pstretch_assist, "assist", A_CANT, 0);

    maxpaulstretch::register_common_attrs(c);

    CLASS_ATTR_DOUBLE(c, "windowsize", 0, t_pstretch, windowsize);
    CLASS_ATTR_FILTER_MIN(c, "windowsize", 1.0);
    CLASS_ATTR_LABEL(c, "windowsize", 0, "Record Window (ms)");

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    s_pstretch_class = c;
}

void* pstretch_new(t_symbol*, long argc, t_atom* argv)
{
    t_pstretch* x = (t_pstretch*)object_alloc(s_pstretch_class);
    if (!x) {
        return nullptr;
    }

    dsp_setup((t_pxobject*)x, 1);
    outlet_new((t_object*)x, "signal");

    x->base.engines = new maxpaulstretch::Engine*[1]{ new maxpaulstretch::Engine() };
    x->base.num_engines = 1;
    x->ring = new std::vector<float>();
    x->ring_len = 0;
    x->ring_write = 0;
    x->ring_read = 0;
    x->base.sr = sys_getsr();

    x->base.stretch = 8.0;
    x->base.fftsize = 4096;
    x->windowsize = 5000.0;
    x->base.window = 2; // Hann (library default)
    x->base.onset = 0.0; // onset detection off
    maxpaulstretch::process_attrs_init(&x->base.procattrs);

    attr_args_process(x, (short)argc, argv);
    maxpaulstretch::update_attr_disabled((t_object*)x, x->base.procattrs);
    return x;
}

void pstretch_free(t_pstretch* x)
{
    dsp_free((t_pxobject*)x);
    delete x->base.engines[0];
    delete[] x->base.engines;
    delete x->ring;
}

void pstretch_assist(t_pstretch*, void*, long m, long a, char* dst)
{
    if (m == ASSIST_INLET) {
        snprintf(dst, 512, "(signal) Input to stretch");
    }
    else {
        snprintf(dst, 512, "(signal) Stretched output");
    }
}

void pstretch_dsp64(t_pstretch* x, t_object* dsp64, short*, double samplerate, long, long)
{
    x->base.sr = samplerate;

    long need = (long)(x->windowsize * 0.001 * samplerate);
    if (need < 1) {
        need = 1;
    }
    x->ring->assign((size_t)need, 0.0f);
    x->ring_len = need;
    x->ring_write = 0;
    x->ring_read = 0;

    if (!x->base.engines[0]->configure(samplerate, x->base.stretch, x->base.fftsize, x->base.window, x->base.onset)) {
        object_error((t_object*)x, "failed to initialize the stretcher");
    }
    maxpaulstretch::apply_process_options(x->base.engines[0], x->base.procattrs); // re-apply hot state to the fresh stretcher

    object_method(dsp64, gensym("dsp_add64"), x, (method)pstretch_perform64, 0, nullptr);
}

void pstretch_perform64(t_pstretch* x, t_object*, double** ins, long, double** outs, long, long sampleframes, long, void*)
{
    double* in = ins[0];
    double* out = outs[0];
    const long L = x->ring_len;

    // Record incoming signal into the circular window.
    if (L > 0) {
        float* ring = x->ring->data();
        long w = x->ring_write;
        for (long k = 0; k < sampleframes; ++k) {
            ring[w] = (float)in[k];
            if (++w >= L) {
                w = 0;
            }
        }
        x->ring_write = w;
    }

    if (L <= 0 || !x->base.engines[0]->ready()) {
        std::fill_n(out, sampleframes, 0.0);
        return;
    }

    const float* ring = x->ring->data();
    auto fill = [x, ring, L](float* d, int count) -> float {
        long rc = x->ring_read;
        if (d) {
            for (int k = 0; k < count; ++k) {
                d[k] = ring[rc];
                if (++rc >= L) {
                    rc = 0;
                }
            }
        }
        else if (count > 0) {
            rc = (rc + count) % L;
        }
        x->ring_read = rc;
        return 100.0f * (float)rc / (float)L;
    };

    x->base.engines[0]->pull(out, sampleframes, fill);
}
