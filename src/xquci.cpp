/*
  Fork-only UCI glue. See xquci.h.
*/

#include "xquci.h"

#include <cmath>

#include "xqoptions.h"

namespace Stockfish {

bool xq_is_bare_position_command(const std::string& token, std::istringstream& is) {

    if (token != "fen" && token != "startpos")
        return false;

    is.seekg(0);
    return true;
}

int xq_to_cp(Value v, double a) {

    // Display-only conversion selected by the "ScoreType" UCI option.
    // The internal search value is untouched, and the WDL output always uses
    // win_rate_model regardless of this setting.
    switch (scoreTypeMode)
    {
    case ScoreTypeMode::RAW :
        return int(v);
    case ScoreTypeMode::PAWN_VALUE_NORMALIZED :
        return int(std::round(100 * int(v) / PawnValue));
    default :
        // Elo: official WDL conversion, doubled as a display-only calibration
        // so that a one-pawn edge reads around 70cp instead of ~35cp.
        return int(std::round(200.0 * int(v) / a));
    }
}

}  // namespace Stockfish
