/*
  Fork-only UCI glue: non-standard command handling and score display.

  Kept out of uci.cpp so that syncing with official Pikafish only needs small
  hooks there.
*/

#ifndef XQUCI_H_INCLUDED
#define XQUCI_H_INCLUDED

#include <sstream>
#include <string>

#include "types.h"

namespace Stockfish {

// EThinker (auto) and a few other GUIs omit the "position" prefix and send a
// bare "fen ..." or "startpos" line. Returns true for such a line and rewinds
// the stream so position() parses it like "position fen/startpos ...".
bool xq_is_bare_position_command(const std::string& token, std::istringstream& is);

// ScoreType-aware conversion of an internal value to UCI centipawns.
// `a` is the material-dependent scale from win_rate_params().
int xq_to_cp(Value v, double a);

}  // namespace Stockfish

#endif  // #ifndef XQUCI_H_INCLUDED
