/*
  Extended Xiangqi rule configuration and chase types.

  Isolated from official Pikafish headers so upstream merges of position.h /
  types.h stay small. Included only where extended rules need these types.
*/

#ifndef XQCONFIG_H_INCLUDED
#define XQCONFIG_H_INCLUDED

#include "types.h"

namespace Stockfish {

// UCI-driven Xiangqi rule knobs. Default repetition rule is AsianRule
// (2-fold), with rule120 and the sixty-move rule on. ComputerRule restores
// official Pikafish 3-fold adjudication.
namespace RuleConfig {

enum class RepetitionRule {
    ASIAN,
    CHINESE,
    SKY,
    COMPUTER,
    YITIAN,
    ALLOW_CHASE,
    NO_JUDGEMENT
};

enum class DrawRule {
    NONE,
    BLACK_WIN,
    RED_WIN,
    REP_BLACK_WIN,
    REP_RED_WIN
};

extern RepetitionRule repetitionRule;
extern DrawRule       drawRule;
extern int            mateThreatDepth;
extern bool           sixtyMoveRule;
extern int            rule60MaxPly;

// Frozen squares from UCI TiedPieces (e.g. a4a5, i4i5, e4e5).
// Absolute ICCS squares: the same whether Red or Black moves first.
// SQ_NONE means the option is unset.
extern Square tiedSq1;
extern Square tiedSq2;

// True while UCI "position" is applying a move list, so setup moves that
// land on a locked square (a3a4, a6a5, ...) are not rejected.
extern bool applyingPosition;

inline bool is_tied_square(Square s) {
    return tiedSq1 != SQ_NONE && (s == tiedSq1 || s == tiedSq2);
}

inline bool chinese_like() {
    return repetitionRule == RepetitionRule::CHINESE || repetitionRule == RepetitionRule::SKY;
}

// Official Pikafish path: 3-fold ComputerRule, rule120, sixty-move on, no draw override.
inline bool uses_official_rules() {
    return repetitionRule == RepetitionRule::COMPUTER && drawRule == DrawRule::NONE
        && sixtyMoveRule && rule60MaxPly == 120;
}

}  // namespace RuleConfig

// (victim, attacker) id pairs so a victim chased by a different attacker is
// not confused with a continued chase by the original attacker.
union ChaseMap {
    u64 attacks[4]{};
    u16 victims[16];

    void operator|=(int id) { attacks[id >> 6] |= 1ULL << (id & 63); }

    ChaseMap& operator&(const ChaseMap& rhs) {
        attacks[0] &= ~rhs.attacks[0];
        attacks[1] &= ~rhs.attacks[1];
        attacks[2] &= ~rhs.attacks[2];
        attacks[3] &= ~rhs.attacks[3];
        return *this;
    }

    operator u16() {
        u16 ret = 0;
        for (int i = 0; i < 16; ++i)
            if (this->victims[i])
                ret |= u16(1 << i);
        return ret;
    }
};

constexpr int make_chase(int piece1, int piece2) { return (piece1 << 4) + piece2; }

struct SkyChaseMap {
    u16 attackers[16]{};

    void add(int victim, int attacker) {
        if (victim >= 0 && victim < 16 && attacker >= 0 && attacker < 16)
            attackers[victim] |= u16(1u << attacker);
    }

    SkyChaseMap exact_diff(const SkyChaseMap& before) const {
        SkyChaseMap ret;
        for (int i = 0; i < 16; ++i)
            ret.attackers[i] = attackers[i] & ~before.attackers[i];
        return ret;
    }

    u16 victims() const {
        u16 ret = 0;
        for (int i = 0; i < 16; ++i)
            if (attackers[i])
                ret |= u16(1u << i);
        return ret;
    }

    u16 chasers() const {
        u16 ret = 0;
        for (u16 a : attackers)
            ret |= a;
        return ret;
    }
};

}  // namespace Stockfish

#endif  // #ifndef XQCONFIG_H_INCLUDED
