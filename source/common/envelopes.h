// Breakpoint-envelope messages shared by paulstretch.stream~ and paulstretch~.
//
// Two libpaulstretch features take a Breakpoint curve (position/value pairs)
// rather than a scalar, so they are driven by messages instead of attributes:
//
//   stretchenv <pos val ...>   stretch-multiplier curve vs. input position;
//                              empty list clears it.
//   arbfilter  <pos val ...>   arbitrary spectral gain curve; empty list clears
//                              it. Non-empty also flips
//                              ProcessOptions::arbitrary_filter_enabled on (empty
//                              one off) via procattrs.
//
// The Engine caches both curves and re-applies them after a DSP restart, so
// only the arbfilter enable flag rides the procattrs/apply_process_options path.

#pragma once

#include "ext.h"
#include "ext_obex.h"

#include <vector>

#include "paulstretch/paulstretch.h"
#include "paulstretch_engine.h"
#include "process_attrs.h"

namespace maxpaulstretch {

// Parse a flat list of alternating (position, value) floats into breakpoints.
// Positions are clamped to 0..1; a trailing unpaired atom is ignored.
inline std::vector<paulstretch::Breakpoint> atoms_to_breakpoints(long argc, t_atom* argv)
{
    std::vector<paulstretch::Breakpoint> bps;
    for (long i = 0; i + 1 < argc; i += 2) {
        float pos = static_cast<float>(atom_getfloat(argv + i));
        float val = static_cast<float>(atom_getfloat(argv + i + 1));
        pos = pos < 0.0f ? 0.0f : (pos > 1.0f ? 1.0f : pos);
        bps.push_back({ pos, val });
    }
    return bps;
}

} // namespace maxpaulstretch
