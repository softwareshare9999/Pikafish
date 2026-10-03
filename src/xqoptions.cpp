/*
  Extended UCI options (Xiangqi rules + strength limit).
*/

#include "xqoptions.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "misc.h"
#include "types.h"
#include "ucioption.h"

namespace Stockfish {

namespace RuleConfig {
RepetitionRule repetitionRule  = RepetitionRule::ASIAN;
DrawRule       drawRule        = DrawRule::NONE;
int            mateThreatDepth = 10;
bool           sixtyMoveRule   = true;
int            rule60MaxPly    = 120;
Square         tiedSq1           = SQ_NONE;
Square         tiedSq2           = SQ_NONE;
bool           applyingPosition  = false;
}  // namespace RuleConfig

namespace {

Square parse_uci_square(char file, char rank) {
    if (file < 'a' || file > 'i' || rank < '0' || rank > '9')
        return SQ_NONE;
    return make_square(File(file - 'a'), Rank(rank - '0'));
}

// Parse two ICCS squares. The website cases after each pawn advances once:
//   a4a5  leftmost file (Red a4, Black a5)
//   i4i5  rightmost file (Red i4, Black i5)
//   e4e5  center file (Red e4, Black e5)
// Order, spaces, commas, and case are ignored. Empty clears the lock.
void set_tied_pieces(const std::string& raw) {
    std::string s;
    s.reserve(raw.size());
    for (unsigned char c : raw)
        if (!std::isspace(c) && c != ',' && c != '-' && c != '_')
            s.push_back(char(std::tolower(c)));

    RuleConfig::tiedSq1 = SQ_NONE;
    RuleConfig::tiedSq2 = SQ_NONE;

    if (s.empty() || s == "<empty>")
        return;

    if (s.size() < 4)
        return;

    const Square s1 = parse_uci_square(s[0], s[1]);
    const Square s2 = parse_uci_square(s[2], s[3]);
    if (!is_ok(s1) || !is_ok(s2) || s1 == s2)
        return;

    RuleConfig::tiedSq1 = s1;
    RuleConfig::tiedSq2 = s2;
}

}  // namespace

void add_extended_options(OptionsMap& options) {

    options.add("Skill Level", Option(20, 0, 20));

    options.add("UCI_LimitStrength", Option(false));

    options.add("UCI_Elo",
                Option(Search::Skill::LowestElo, Search::Skill::LowestElo, Search::Skill::HighestElo));

    options.add(  //
      "Mate Threat Depth", Option(10, 0, 10, [](const Option& o) {
          RuleConfig::mateThreatDepth = int(o);
          return std::nullopt;
      }));

    options.add(  //
      "Repetition Rule",
      Option("AsianRule var AsianRule var ChineseRule var SkyRule var ComputerRule "
             "var YitianRule var AllowChase var NoJudgement",
             "AsianRule", [](const Option& o) {
                 using RR = RuleConfig::RepetitionRule;

                 RuleConfig::repetitionRule =
                   o == "ChineseRule"    ? RR::CHINESE
                   : o == "SkyRule"      ? RR::SKY
                   : o == "ComputerRule" ? RR::COMPUTER
                   : o == "YitianRule"   ? RR::YITIAN
                   : o == "AllowChase"   ? RR::ALLOW_CHASE
                   : o == "NoJudgement"  ? RR::NO_JUDGEMENT
                                         : RR::ASIAN;

                 // AsianRule and SkyRule default to rule120; YitianRule to rule140.
                 if (RuleConfig::repetitionRule == RR::ASIAN
                     || RuleConfig::repetitionRule == RR::SKY)
                     RuleConfig::rule60MaxPly = 120;
                 else if (RuleConfig::repetitionRule == RR::YITIAN)
                     RuleConfig::rule60MaxPly = 140;

                 if (RuleConfig::repetitionRule == RR::YITIAN)
                     RuleConfig::sixtyMoveRule = false;

                 return std::nullopt;
             }));

    options.add(  //
      "Draw Rule",
      Option("None var None var DrawAsBlackWin var DrawAsRedWin var DrawRepAsBlackWin "
             "var DrawRepAsRedWin",
             "None", [](const Option& o) {
                 using DR = RuleConfig::DrawRule;
                 RuleConfig::drawRule =
                   o == "DrawAsBlackWin"      ? DR::BLACK_WIN
                   : o == "DrawAsRedWin"      ? DR::RED_WIN
                   : o == "DrawRepAsBlackWin" ? DR::REP_BLACK_WIN
                   : o == "DrawRepAsRedWin"   ? DR::REP_RED_WIN
                                              : DR::NONE;
                 return std::nullopt;
             }));

    options.add(  //
      "Sixty Move Rule", Option(true, [](const Option& o) {
          RuleConfig::sixtyMoveRule =
            int(o) != 0 && RuleConfig::repetitionRule != RuleConfig::RepetitionRule::YITIAN;
          return std::nullopt;
      }));

    options.add(  //
      "Rule60MaxPly", Option(120, 1, 150, [](const Option& o) {
          using RR = RuleConfig::RepetitionRule;
          RuleConfig::rule60MaxPly =
            (RuleConfig::repetitionRule == RR::ASIAN || RuleConfig::repetitionRule == RR::SKY)
              ? 120
              : int(o);
          return std::nullopt;
      }));

    options.add(  //
      "ScoreType",
      Option("Elo var Elo var PawnValueNormalized var Raw", "Elo", [](const Option& o) {
          scoreTypeMode = o == "Elo"  ? ScoreTypeMode::ELO
                        : o == "Raw" ? ScoreTypeMode::RAW
                                     : ScoreTypeMode::PAWN_VALUE_NORMALIZED;
          return std::nullopt;
      }));

    options.add(  //
      "TiedPieces", Option("", [](const Option& o) {
          set_tied_pieces(std::string(o));
          return std::nullopt;
      }));
}

namespace Search {

Move Skill::pick_best(const RootMoves& rootMoves, usize multiPV) {
    static PRNG rng(now());

    Value topScore = rootMoves[0].score;
    Value minScore = rootMoves[0].score;
    for (usize i = 1; i < multiPV; ++i)
    {
        topScore = std::max(topScore, rootMoves[i].score);
        minScore = std::min(minScore, rootMoves[i].score);
    }
    int    delta    = std::min(topScore - minScore, int(PawnValue));
    int    maxScore = -VALUE_INFINITE;
    double weakness = 120 - 2 * level;

    for (usize i = 0; i < multiPV; ++i)
    {
        int push = int(weakness * int(topScore - rootMoves[i].score)
                       + delta * (rng.rand<unsigned>() % unsigned(std::max(1, int(weakness)))))
                 / 128;

        if (rootMoves[i].score + push >= maxScore)
        {
            maxScore = rootMoves[i].score + push;
            best     = rootMoves[i].pv[0];
        }
    }

    return best;
}

}  // namespace Search

}  // namespace Stockfish
