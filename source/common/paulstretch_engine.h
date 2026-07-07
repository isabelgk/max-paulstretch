// Shared driver around paulstretch::StreamingStretcher.
//
// The stretcher works at its own block rate (each step() consumes
// next_input_size() frames and emits bufsize() frames); an MSP perform routine
// must produce an arbitrary signal-vector count per call. This class bridges
// the two: it buffers one step's output and runs further steps on demand.
//
// Input is supplied through a caller-provided `fill` callback so the live-ring
// (paulstretch.stream~) vs. buffer-cursor (paulstretch~) sourcing stays out of here:
//
//   float fill(float* dst, int count)
//     - dst != nullptr : write `count` source frames into dst (zero-pad if dry)
//     - dst == nullptr : advance the source cursor by `count` without output
//                        (count == 0 is a position-only query)
//     - returns        : the input cursor as a percent 0..100, which the
//                         stretcher feeds to its stretch envelope
//
// Not thread-safe; all calls expected on the audio thread except configure()
// (meant for the dsp64 setup pass) and the envelope/filter setters below,
// which are safe to call from the message thread. reset() still requires the
// caller to already be on the audio thread.

#pragma once

#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

#include "paulstretch/paulstretch.h"

namespace maxpaulstretch {

class Engine
{
  public:
    Engine() = default;

    // Map a Max enumindex (0..4) to the library window type. Anything out of
    // range falls back to Hann (the library default).
    static paulstretch::Window to_window(long index)
    {
        switch (index) {
            case 0: return paulstretch::Window::Rectangular;
            case 1: return paulstretch::Window::Hamming;
            case 3: return paulstretch::Window::Blackman;
            case 4: return paulstretch::Window::BlackmanHarris;
            default: return paulstretch::Window::Hann;
        }
    }

    // (Re)build the stretcher for the given format. Allocates memory, so call
    // from dsp64, not from perform. `window` is a Max enumindex (see
    // to_window). Returns false if allocation fails, otherwise true.
    bool configure(double sample_rate, double stretch, long fft_size, long window, double onset)
    {
        paulstretch::RenderOptions opts;
        opts.sample_rate = static_cast<float>(sample_rate);
        opts.stretch = static_cast<float>(stretch < 1.0 ? 1.0 : stretch);
        opts.fft_size = static_cast<int>(fft_size);
        opts.window = to_window(window);
        opts.onset_detection_sensitivity = static_cast<float>(onset);

        try {
            stretcher_ = std::make_unique<paulstretch::StreamingStretcher>(opts);
        } catch (...) {
            stretcher_.reset();
            return false;
        }

        // Re-apply caller-set envelopes lost when the stretcher was rebuilt.
        if (!stretch_env_.empty()) {
            stretcher_->set_stretch_envelope(stretch_env_);
        }
        if (!arb_filter_.empty()) {
            stretcher_->set_arbitrary_filter(arb_filter_);
        }

        step_out_.assign(static_cast<std::size_t>(stretcher_->bufsize()), 0.0f);
        in_chunk_.assign(static_cast<std::size_t>(stretcher_->max_input_chunk()), 0.0f);
        out_head_ = step_out_.size(); // force a step on the first pull()
        first_ = true;
        return true;
    }

    bool ready() const
    {
        return static_cast<bool>(stretcher_);
    }

    // Hot-swap the base stretch factor without resetting DSP state.
    void set_stretch(double stretch)
    {
        if (stretcher_) {
            stretcher_->set_stretch_factor(static_cast<float>(stretch < 1.0 ? 1.0 : stretch));
        }
    }

    // Hot-set the onset-detection sensitivity without resetting DSP state.
    void set_onset(double onset)
    {
        if (stretcher_) {
            stretcher_->set_onset_detection_sensitivity(static_cast<float>(onset));
        }
    }

    // Hot-swap the spectral processing options without resetting DSP state.
    void set_process_options(const paulstretch::ProcessOptions& opts)
    {
        if (stretcher_) {
            stretcher_->set_process_options(opts);
        }
    }

    // Stretch envelope: a position(0..1)->stretch-multiplier breakpoint curve
    // evaluated against the input cursor. Cached so it survives DSP restarts.
    //
    // Safe to call from the message thread while pull() runs on the audio
    // thread: the new curve is stashed and applied by pull(), not written
    // into the live stretcher here.
    void set_stretch_envelope(std::vector<paulstretch::Breakpoint> env)
    {
        stretch_env_ = env;
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_stretch_env_ = std::move(env);
        stretch_env_pending_ = true;
    }

    void clear_stretch_envelope()
    {
        stretch_env_.clear();
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_stretch_env_.clear();
        stretch_env_pending_ = true;
    }

    // Cached breakpoint curves, exposed so a newly built per-channel engine can be
    // seeded from an existing one (see mc.paulstretch~ engine rebuild).
    const std::vector<paulstretch::Breakpoint>& stretch_envelope() const { return stretch_env_; }

    // Arbitrary filter: a position(0..1)->gain breakpoint curve over the
    // spectrum. Gated by ProcessOptions::arbitrary_filter_enabled. Cached so it
    // survives DSP restarts. Same message/audio thread handoff as
    // set_stretch_envelope() above.
    void set_arbitrary_filter(std::vector<paulstretch::Breakpoint> filter)
    {
        arb_filter_ = filter;
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_arb_filter_ = std::move(filter);
        arb_filter_pending_ = true;
    }

    void clear_arbitrary_filter()
    {
        arb_filter_.clear();
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_arb_filter_.clear();
        arb_filter_pending_ = true;
    }

    const std::vector<paulstretch::Breakpoint>& arbitrary_filter() const { return arb_filter_; }

    // Clear DSP state (e.g. on transport seek / loop).
    void reset()
    {
        if (stretcher_) {
            stretcher_->reset();
        }
        out_head_ = step_out_.size();
        first_ = true;
    }

    // Produce `n` output frames into `out`, sourcing input via `fill`.
    template <typename Fill>
    void pull(double* out, long n, Fill&& fill)
    {
        apply_pending();
        if (!stretcher_) {
            std::fill_n(out, n, 0.0);
            return;
        }
        for (long i = 0; i < n; ++i) {
            if (out_head_ >= step_out_.size()) {
                run_step(fill);
            }
            out[i] = static_cast<double>(step_out_[out_head_++]);
        }
    }

  private:
    // Pick up any envelope/filter swapped in from the message thread. Uses
    // try_lock so the audio thread never blocks; a missed lock just defers
    // the pickup to the next block. This (and reset(), called the same way
    // from perform64) can allocate on the audio thread, which is fine since
    // it only happens at a preset/reset moment that's already discontinuous.
    void apply_pending()
    {
        std::unique_lock<std::mutex> lock(pending_mutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            return;
        }
        if (stretch_env_pending_) {
            if (stretcher_) {
                stretcher_->set_stretch_envelope(pending_stretch_env_);
            }
            stretch_env_pending_ = false;
        }
        if (arb_filter_pending_) {
            if (stretcher_) {
                stretcher_->set_arbitrary_filter(pending_arb_filter_);
            }
            arb_filter_pending_ = false;
        }
    }

    template <typename Fill>
    void run_step(Fill& fill)
    {
        const int want = first_ ? stretcher_->max_input_chunk() : stretcher_->next_input_size();
        const float pct = (want > 0) ? fill(in_chunk_.data(), want) : fill(nullptr, 0);
        stretcher_->step(want > 0 ? in_chunk_.data() : nullptr, pct, step_out_.data());
        const int skip = stretcher_->skip_after_step();
        if (skip > 0) {
            fill(nullptr, skip);
        }
        first_ = false;
        out_head_ = 0;
    }

    std::unique_ptr<paulstretch::StreamingStretcher> stretcher_;
    std::vector<float> step_out_; // one step's output, drained by pull()
    std::vector<float> in_chunk_; // scratch for the input chunk handed to step()
    std::vector<paulstretch::Breakpoint> stretch_env_; // cached, re-applied on configure()
    std::vector<paulstretch::Breakpoint> arb_filter_; // cached, re-applied on configure()
    std::size_t out_head_ = 0; // read cursor into step_out_
    bool first_ = true;

    // Message-thread -> audio-thread handoff for hot envelope/filter swaps
    // (see set_stretch_envelope() / set_arbitrary_filter() above).
    std::mutex pending_mutex_;
    std::vector<paulstretch::Breakpoint> pending_stretch_env_;
    bool stretch_env_pending_ = false;
    std::vector<paulstretch::Breakpoint> pending_arb_filter_;
    bool arb_filter_pending_ = false;
};

} // namespace maxpaulstretch
