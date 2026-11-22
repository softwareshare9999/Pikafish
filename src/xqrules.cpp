/*
  Extended Xiangqi repetition / chase / draw rules.

  Implemented as Position members so they can reach private board state, but
  kept out of official position.cpp to avoid merge conflicts with upstream.
*/

#include "position.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <memory>
#include <vector>

#include "attacks.h"
#include "movegen.h"
#include "xqconfig.h"

namespace Stockfish {

using namespace Attacks;

bool Position::chase_legal(Move m, Bitboard b) const {

    assert(m.is_ok());

    Color    us       = sideToMove;
    Square   from     = m.from_sq();
    Square   to       = m.to_sq();
    Bitboard occupied = (pieces() ^ from) | to;

    assert(color_of(moved_piece(m)) == us);
    assert(piece_on(king_square(us)) == make_piece(us, KING));

    if (type_of(piece_on(from)) == KING)
        return !(checkers_to(~us, to, occupied) & ~b);

    return !((checkers_to(~us, king_square(us), occupied) & ~square_bb(to)) & ~b);
}

SkyChaseMap Position::sky_chased(Color c) {

    SkyChaseMap chase;

    Bitboard checkUs   = st->checkersBB;
    Bitboard checkThem = checkers_to(sideToMove, king_square(~sideToMove));
    if (c != sideToMove)
        std::swap(checkUs, checkThem);

    std::swap(c, sideToMove);

    Bitboard attackers = pieces(sideToMove) ^ pieces(sideToMove, KING, PAWN);
    while (attackers)
    {
        Square    from         = pop_lsb(attackers);
        int       attackerId   = idBoard[from];
        PieceType attackerType = type_of(piece_on(from));
        Bitboard  attacks      = attacks_bb(attackerType, from, pieces());

        if (blockers_for_king(sideToMove) & from)
            attacks &= pinners(~sideToMove) & ~pieces(KING);
        else
            attacks &= (pieces(~sideToMove) ^ pieces(~sideToMove, KING, PAWN))
                     | (pieces(~sideToMove, PAWN) & HalfBB[sideToMove]);

        Bitboard candidates = 0;
        if (attackerType == KNIGHT || attackerType == CANNON)
            candidates = attacks & pieces(~sideToMove, ROOK);
        if (RuleConfig::chinese_like() && (attackerType == ADVISOR || attackerType == BISHOP))
            candidates |= attacks & pieces(~sideToMove, ROOK, KNIGHT, CANNON);

        attacks ^= candidates;
        while (candidates)
        {
            Square to = pop_lsb(candidates);
            if (chase_legal(Move(from, to), checkUs))
                chase.add(idBoard[to], attackerId);
        }

        while (attacks)
        {
            Square to = pop_lsb(attacks);
            Move   m(from, to);
            if (!chase_legal(m, checkUs))
                continue;

            bool     trueChase  = true;
            int      id         = 0;
            Piece    captured   = do_move(m, id);
            Bitboard recaptures = attackers_to(to) & pieces(sideToMove);
            while (recaptures)
            {
                Square sq = pop_lsb(recaptures);
                if (chase_legal(Move(sq, to), checkThem))
                {
                    trueChase = false;
                    break;
                }
            }
            undo_move(m, captured, id);

            if (!trueChase)
                continue;

            if (attackerType == type_of(piece_on(to)))
            {
                sideToMove = ~sideToMove;
                if ((attackerType == KNIGHT && ((between_bb(from, to) ^ to) & pieces()))
                    || !chase_legal(Move(to, from), checkThem))
                    chase.add(idBoard[to], attackerId);
                sideToMove = ~sideToMove;
            }
            else
                chase.add(idBoard[to], attackerId);
        }
    }

    std::swap(c, sideToMove);
    return chase;
}

ChaseMap Position::xq_chased(Color c) {

    ChaseMap chase;

    if (st->move == Move::none())
        return chase;

    Bitboard checkUs   = st->checkersBB;
    Bitboard checkThem = checkers_to(sideToMove, king_square(~sideToMove));
    if (c != sideToMove)
        std::swap(checkUs, checkThem);

    std::swap(c, sideToMove);

    Bitboard attackers = pieces(sideToMove) ^ pieces(sideToMove, KING, PAWN);
    while (attackers)
    {
        Square    from         = pop_lsb(attackers);
        PieceType attackerType = type_of(piece_on(from));
        Bitboard  attacks      = attacks_bb(attackerType, from, pieces());

        if (blockers_for_king(sideToMove) & from)
            attacks &= pinners(~sideToMove) & ~pieces(KING);
        else
            attacks &= (pieces(~sideToMove) ^ pieces(~sideToMove, KING, PAWN))
                     | (pieces(~sideToMove, PAWN) & HalfBB[sideToMove]);

        Bitboard candidates = 0;
        if (attackerType == KNIGHT || attackerType == CANNON)
            candidates = attacks & pieces(~sideToMove, ROOK);
        if (RuleConfig::chinese_like() && (attackerType == ADVISOR || attackerType == BISHOP))
            candidates |= attacks & pieces(~sideToMove, ROOK, KNIGHT, CANNON);

        attacks ^= candidates;
        while (candidates)
        {
            Square to = pop_lsb(candidates);
            if (chase_legal(Move(from, to), checkUs))
                chase |= make_chase(idBoard[to], idBoard[from]);
        }

        while (attacks)
        {
            Square to = pop_lsb(attacks);
            Move   m  = Move(from, to);

            if (chase_legal(m, checkUs))
            {
                bool     trueChase  = true;
                int      id         = 0;
                Piece    captured   = do_move(m, id);
                Bitboard recaptures = attackers_to(to) & pieces(sideToMove);
                while (recaptures)
                {
                    Square s = pop_lsb(recaptures);
                    if (chase_legal(Move(s, to), checkThem))
                    {
                        trueChase = false;
                        break;
                    }
                }
                undo_move(m, captured, id);

                if (trueChase)
                {
                    if (attackerType == type_of(piece_on(to)))
                    {
                        sideToMove = ~sideToMove;
                        if ((attackerType == KNIGHT && ((between_bb(from, to) ^ to) & pieces()))
                            || !chase_legal(Move(to, from), checkThem))
                            chase |= make_chase(idBoard[to], idBoard[from]);
                        sideToMove = ~sideToMove;
                    }
                    else
                        chase |= make_chase(idBoard[to], idBoard[from]);
                }
            }
        }
    }

    std::swap(c, sideToMove);
    return chase;
}

// Creates an isolated copy of src (board plus its current state) so that a
// branch can be searched without touching the shared state ring.
void Position::init_xq_branch(const Position& src) {

    std::memset(this, 0, sizeof(Position));
    std::memcpy((void*) this, (const void*) &src, offsetof(Position, filter));
    std::memcpy((void*) idBoard, (const void*) src.idBoard, sizeof(idBoard));

    stateInfos[0] = *src.st;
    stateIdx      = 0;
    st            = &stateInfos[0];
}

bool Position::has_mate_threat(Depth d) {

    if (d == -1)
    {
        // Walk the threats on an isolated copy: chasing detection has already
        // rolled the board of this position back, so do_move() must not touch
        // the shared state ring.
        auto branch = std::make_unique<Position>();
        branch->init_xq_branch(*this);
        branch->do_null_move();
        return branch->has_mate_threat(0);
    }

    if (d >= RuleConfig::mateThreatDepth)
        return false;

    for (const auto& check : MoveList<ALL>(*this))
    {
        if (!gives_check(check))
            continue;

        do_move(check);
        bool solvable = false;

        for (const auto& evasion : MoveList<ALL>(*this))
        {
            do_move(evasion);
            solvable = !has_mate_threat(d + 1);
            undo_move(evasion);
            if (solvable)
                break;
        }

        undo_move(check);
        if (!solvable)
            return true;
    }

    return false;
}

void Position::set_sky_info(int d) {

    int whiteId = 0, blackId = 0;
    std::fill(std::begin(idBoard), std::end(idBoard), 0);
    for (Square sq = SQ_A0; sq <= SQ_I9; ++sq)
        if (board[sq] != NO_PIECE)
            idBoard[sq] = color_of(board[sq]) == WHITE ? whiteId++ : blackId++;

    const int maxBack = std::min<int>(st->pliesFromNull, MAX_HIST - 1);

    for (int i = 0; i < d && i < maxBack; ++i)
    {
        StateInfo* cur = &stateInfos[(stateIdx - i) & (MAX_HIST - 1)];
        st             = cur;

        if (cur->capturedPiece != NO_PIECE)
            break;

        cur->skyVictims  = 0;
        cur->skyCheckers = 0;
        cur->skyChasers  = 0;

        const Color mover = ~sideToMove;

        if (cur->checkersBB)
        {
            cur->skyVictims = 0xFFFF;
            Bitboard checks = cur->checkersBB;
            while (checks)
            {
                Square sq = pop_lsb(checks);
                int    id = idBoard[sq];
                if (id >= 0 && id < 16)
                    cur->skyCheckers |= u16(1u << id);
            }

            undo_move(cur->move, cur->capturedPiece, 0);
            st = &stateInfos[(stateIdx - i - 1) & (MAX_HIST - 1)];
            continue;
        }

        SkyChaseMap after = sky_chased(mover);
        undo_move(cur->move, cur->capturedPiece, 0);
        st                 = &stateInfos[(stateIdx - i - 1) & (MAX_HIST - 1)];
        SkyChaseMap exact  = after.exact_diff(sky_chased(mover));
        cur->skyVictims    = exact.victims();
        cur->skyChasers    = exact.chasers();
    }
}

Value Position::detect_sky_cycle(int d, int ply) {

    if (d < 4 || !st)
        return VALUE_DRAW;

    const Color      currentSide = sideToMove;
    const u16        startIdx    = stateIdx;
    StateInfo* const start       = st;

    // Snapshot of the board before rolling back, used by the deep history pass.
    std::byte saved[offsetof(Position, filter)];
    std::memcpy(saved, (const void*) this, sizeof(saved));
    int savedIdBoard[SQUARE_NB];
    std::memcpy(savedIdBoard, (const void*) idBoard, sizeof(savedIdBoard));

    set_sky_info(d);

    std::vector<StateInfo*> q;
    q.reserve(d);
    for (int k = 0; k < d && k < start->pliesFromNull; ++k)
    {
        StateInfo* p = &stateInfos[(startIdx - k) & (MAX_HIST - 1)];
        if (p->capturedPiece != NO_PIECE)
            break;
        q.push_back(p);
    }
    if (int(q.size()) < d)
        return VALUE_DRAW;

    std::vector<StateInfo*> deepQ;
    auto                    ensure_deep_history = [&]() -> const std::vector<StateInfo*>& {
        if (!deepQ.empty())
            return deepQ;
        const int historyDepth = std::min(start->pliesFromNull, 128);

        // Rewind to the snapshot and redo the bookkeeping further back
        std::memcpy((void*) this, saved, sizeof(saved));
        std::memcpy((void*) idBoard, savedIdBoard, sizeof(savedIdBoard));
        stateIdx = startIdx;
        st       = start;
        set_sky_info(std::max(d, historyDepth));

        deepQ.reserve(historyDepth);
        for (int k = 0; k < historyDepth && k < start->pliesFromNull; ++k)
        {
            StateInfo* p = &stateInfos[(startIdx - k) & (MAX_HIST - 1)];
            if (p->capturedPiece != NO_PIECE)
                break;
            deepQ.push_back(p);
        }
        return deepQ;
    };

    auto checked  = [](const StateInfo* s) { return bool(s->checkersBB); };
    auto loss_for = [&](Color offender) {
        return offender == currentSide ? mated_in(ply) : mate_in(ply);
    };

    const Color AColor = ~currentSide;
    const Color BColor = currentSide;

    const StateInfo* s0 = q[0];
    const StateInfo* s1 = q[1];
    const StateInfo* s2 = q[2];
    const StateInfo* s3 = q[3];

    u16 A = s0->skyVictims & s2->skyVictims;
    u16 B = s1->skyVictims & s3->skyVictims;

    const bool splitA     = s0->skyVictims && s2->skyVictims && A == 0;
    const bool splitB     = s1->skyVictims && s3->skyVictims && B == 0;
    const bool checkIdleA = (checked(s0) && s2->skyVictims == 0)
                         || (checked(s2) && s0->skyVictims == 0);
    const bool checkIdleB = (checked(s1) && s3->skyVictims == 0)
                         || (checked(s3) && s1->skyVictims == 0);

    const bool mixedA     = A && (checked(s0) != checked(s2));
    const bool mixedB     = B && (checked(s1) != checked(s3));
    const bool differentA = mixedA
                         && ((s2->skyChasers & u16(~s0->skyCheckers))
                             || (s0->skyChasers & u16(~s2->skyCheckers)));
    const bool differentB = mixedB
                         && ((s3->skyChasers & u16(~s1->skyCheckers))
                             || (s1->skyChasers & u16(~s3->skyCheckers)));

    for (int i = 4; i < d; i += 2)
    {
        A &= q[i]->skyVictims;
        if (i + 1 < d)
            B &= q[i + 1]->skyVictims;
    }

    if (!A && !B)
    {
        auto current_piece_type = [&](const StateInfo* sm) {
            Move m = sm->move;
            if (!m.is_ok())
                return NO_PIECE_TYPE;
            Square to = m.to_sq();
            Piece  pc = piece_on(to);
            return pc == NO_PIECE ? NO_PIECE_TYPE : type_of(pc);
        };

        if (splitA && checkIdleB && current_piece_type(s0) != KING)
            return loss_for(AColor);
        if (splitB && checkIdleA && current_piece_type(s1) != KING)
            return loss_for(BColor);
        return VALUE_DRAW;
    }

    if (A && !B)
        return loss_for(AColor);
    if (!A && B)
        return loss_for(BColor);

    if (mixedA && !mixedB && differentA)
        return loss_for(BColor);
    if (!mixedA && mixedB && differentB)
        return loss_for(AColor);

    if (mixedA != mixedB)
    {
        const Color mixedColor = mixedA ? AColor : BColor;
        const Color pureColor  = mixedA ? BColor : AColor;
        const int   parity     = mixedA ? 0 : 1;
        const u16   common     = mixedA ? A : B;

        const auto& hist           = ensure_deep_history();
        int         count          = 0;
        u16         identities     = 0;
        bool        oldestWasCheck = false;
        for (int i = parity; i < int(hist.size()); i += 2)
        {
            StateInfo* x = hist[i];
            if (checked(x))
            {
                identities |= x->skyCheckers;
                oldestWasCheck = true;
            }
            else if (x->skyVictims & common)
            {
                identities |= x->skyChasers;
                oldestWasCheck = false;
            }
            else
                break;
            ++count;
        }

        if (count >= 6 && (identities & u16(identities - 1)))
            return loss_for(pureColor);
        return loss_for(oldestWasCheck ? mixedColor : pureColor);
    }

    if (mixedA && mixedB)
    {
        const bool phaseB = (checked(s1) && s0->skyChasers) || (checked(s3) && s2->skyChasers);
        return phaseB ? loss_for(BColor) : loss_for(AColor);
    }

    auto extended_multi = [&](Color side, u16 commonVictims) {
        const auto& hist       = ensure_deep_history();
        int         count      = 0;
        u16         identities = 0;
        for (int i = side == AColor ? 0 : 1; i < int(hist.size()); i += 2)
        {
            StateInfo* x = hist[i];
            if (checked(x))
                identities |= x->skyCheckers;
            else
            {
                if (!(x->skyVictims & commonVictims))
                    break;
                identities |= x->skyChasers;
            }
            ++count;
            if (count >= 6 && (identities & u16(identities - 1)))
                return true;
        }
        return false;
    };

    const bool multiA = extended_multi(AColor, A);
    const bool multiB = extended_multi(BColor, B);
    if (multiA != multiB)
        return loss_for(multiA ? BColor : AColor);

    return VALUE_DRAW;
}

Value Position::xq_detect_chases(int d, int ply) {

    using RR = RuleConfig::RepetitionRule;

    if (RuleConfig::repetitionRule == RR::ALLOW_CHASE
        || RuleConfig::repetitionRule == RR::NO_JUDGEMENT)
        return VALUE_DRAW;

    int whiteId = 0;
    int blackId = 0;
    for (Square s = SQ_A0; s <= SQ_I9; ++s)
        if (board[s] != NO_PIECE)
            idBoard[s] = color_of(board[s]) == WHITE ? whiteId++ : blackId++;

    Color us = sideToMove, them = ~us;

    if (RuleConfig::repetitionRule == RR::COMPUTER)
        return detect_chases(d, ply);

    const bool chineseLike = RuleConfig::chinese_like();
    const bool chineseRule = RuleConfig::repetitionRule == RR::CHINESE;

    u16      rooks[COLOR_NB] = {0xFFFF, 0xFFFF};
    u16      chase[COLOR_NB] = {0xFFFF, 0xFFFF};
    ChaseMap newChase[COLOR_NB];
    newChase[us] = xq_chased(us);

    for (int i = 0; i < d; ++i)
    {
        StateInfo* cur = &stateInfos[(stateIdx - i) & (MAX_HIST - 1)];
        st             = cur;

        if (!chase[~sideToMove])
        {
            if (!chase[sideToMove])
                break;
            undo_move(cur->move, cur->capturedPiece);
            st = &stateInfos[(stateIdx - i - 1) & (MAX_HIST - 1)];
        }
        else
        {
            bool mateThreat = cur->checkersBB;
            if (!mateThreat && chineseRule && RuleConfig::mateThreatDepth > 0)
                mateThreat = has_mate_threat();

            if (mateThreat)
            {
                chase[~sideToMove] &= chineseLike ? 0xFFFF : 0;
                rooks[~sideToMove] = 0;
                undo_move(cur->move, cur->capturedPiece);
                st = &stateInfos[(stateIdx - i - 1) & (MAX_HIST - 1)];
            }
            else
            {
                ChaseMap oldChase = xq_chased(~sideToMove);
                u16      flag     = 0;

                if (!chineseLike && rooks[~sideToMove]
                    && (blockers_for_king(sideToMove) & pieces(sideToMove, ROOK)))
                {
                    Bitboard knights = pinners(~sideToMove) & pieces(KNIGHT);
                    while (knights)
                    {
                        Square   s = pop_lsb(knights);
                        Bitboard b = between_bb(king_square(sideToMove), s) ^ s;
                        s          = pop_lsb(b);
                        if (piece_on(s) == make_piece(sideToMove, ROOK))
                            flag |= u16(1 << idBoard[s]);
                    }
                }

                undo_move(cur->move, cur->capturedPiece);
                st = &stateInfos[(stateIdx - i - 1) & (MAX_HIST - 1)];

                ChaseMap chases = oldChase;
                (void) (chases & newChase[sideToMove]);
                u16 chasesU16        = u16(chases);
                newChase[sideToMove] = xq_chased(sideToMove);

                if (chineseLike)
                {
                    chases = oldChase;
                    (void) (chases & newChase[sideToMove]);
                    chasesU16 = u16(chases);
                }
                else if (i == d - 2)
                    chasesU16 &= ~u16(newChase[sideToMove]);

                rooks[sideToMove] &= chasesU16 & flag;
                chase[sideToMove] &= chineseLike && chasesU16 ? 0xFFFF : chasesU16;
            }
        }
    }

    if ((!chase[us] && !chase[them]) || (rooks[us] && rooks[them]))
        return VALUE_DRAW;
    if (rooks[us])
        return mated_in(ply);
    if (rooks[them])
        return mate_in(ply);

    return !chase[us] ? mate_in(ply) : !chase[them] ? mated_in(ply) : VALUE_DRAW;
}

bool Position::xq_rule_judge(Value& result, int ply) {

    using RR = RuleConfig::RepetitionRule;
    using DR = RuleConfig::DrawRule;

    auto apply_draw_rule = [&](bool repetition) {
        if (result != VALUE_DRAW)
            return;

        Color winner = COLOR_NB;
        if (RuleConfig::drawRule == DR::BLACK_WIN
            || (repetition && RuleConfig::drawRule == DR::REP_BLACK_WIN))
            winner = BLACK;
        else if (RuleConfig::drawRule == DR::RED_WIN
                 || (repetition && RuleConfig::drawRule == DR::REP_RED_WIN))
            winner = WHITE;

        if (winner != COLOR_NB)
            result = winner == sideToMove ? mate_in(ply) : mated_in(ply);
    };

    const auto previous = [this](int d) -> const StateInfo& {
        return stateInfos[(stateIdx - d) & (MAX_HIST - 1)];
    };

    int end = std::min(
      {st->rule60 + std::max(0, st->check10[WHITE] - 10) + std::max(0, st->check10[BLACK] - 10),
       st->pliesFromNull, MAX_HIST - MAX_PLY - 10 - 1});

    if (end >= 4 && filter[st->key] >= 1)
    {
        int  cnt       = 0;
        bool checkThem = st->checkersBB && previous(2).checkersBB;
        bool checkUs   = previous(1).checkersBB && previous(3).checkersBB;

        for (int i = 4; i <= end; i += 2)
        {
            const StateInfo* stp = &previous(i);
            checkThem &= bool(stp->checkersBB);

            if (stp->key == st->key)
            {
                ++cnt;

                // Same gate as official Pikafish: a 2-fold is only terminal
                // if the earlier occurrence is strictly after the search root
                // (`ply > i`). A repeat that started in game history waits for
                // a third occurrence (`cnt == 2`), so the side to move can still
                // stop chasing. AsianRule used to fire on `cnt >= 1` regardless
                // of ply, which made 炮六平五 a false mate-in-1 in issue #146.
                if (cnt == 2 || ply > i)
                {
                    if (RuleConfig::repetitionRule == RR::NO_JUDGEMENT)
                        result = VALUE_DRAW;
                    else if (!checkThem && !checkUs)
                    {
                        // Detection rolls the board and the state pointer back, so
                        // snapshot them and restore afterwards.
                        std::byte saved[offsetof(Position, filter)];
                        std::memcpy(saved, (const void*) this, sizeof(saved));
                        int savedIdBoard[SQUARE_NB];
                        std::memcpy(savedIdBoard, (const void*) idBoard, sizeof(savedIdBoard));
                        const u16  savedStateIdx = stateIdx;
                        StateInfo* const savedSt = st;

                        result = RuleConfig::repetitionRule == RR::SKY
                                 ? detect_sky_cycle(i, ply)
                                 : xq_detect_chases(i, ply);

                        std::memcpy((void*) this, saved, sizeof(saved));
                        std::memcpy((void*) idBoard, savedIdBoard, sizeof(savedIdBoard));
                        stateIdx = savedStateIdx;
                        st       = savedSt;
                    }
                    else
                        result = !checkUs ? mate_in(ply)
                               : !checkThem ? mated_in(ply)
                                            : VALUE_DRAW;

                    const bool judgedDraw = result == VALUE_DRAW;
                    apply_draw_rule(true);

                    if (RuleConfig::repetitionRule != RR::COMPUTER)
                        return true;

                    if (judgedDraw || cnt == 2)
                        return true;

                    if (filter[st->key] <= 1)
                    {
                        const int maxPly = std::max(1, RuleConfig::rule60MaxPly);
                        if (st->rule60 < maxPly && previous(1).key == previous(i + 1).key)
                        {
                            int j = 2;
                            while (j < i && filter[previous(j).key] <= 1)
                                ++j;
                            if (j == i)
                                return true;
                        }
                        break;
                    }
                }
            }

            if (i + 1 <= end)
                checkUs &= bool(previous(i + 1).checkersBB);
        }
    }

    if (RuleConfig::sixtyMoveRule && RuleConfig::rule60MaxPly > 0
        && st->rule60 >= RuleConfig::rule60MaxPly)
    {
        result = MoveList<ALL>(*this).size() ? VALUE_DRAW : mated_in(ply);
        apply_draw_rule(false);
        return true;
    }

    if (count<PAWN>() == 0)
    {
        enum DrawLevel : int {
            NO_DRAW,
            DIRECT_DRAW,
            MATE_DRAW
        };

        int level = [&]() {
            if (!major_material())
                return DIRECT_DRAW;

            if (major_material() == CannonValue)
            {
                Color cannonSide = major_material(WHITE) == CannonValue ? WHITE : BLACK;
                if (count<ADVISOR>(cannonSide) == 0)
                {
                    if (count<ADVISOR>(~cannonSide) == 0)
                        return DIRECT_DRAW;

                    if (count<ADVISOR>(~cannonSide) == 1)
                        return count<BISHOP>(cannonSide) == 0 ? DIRECT_DRAW : MATE_DRAW;

                    if (count<BISHOP>(cannonSide) == 0)
                        return MATE_DRAW;
                }
            }

            if (major_material(WHITE) == CannonValue && major_material(BLACK) == CannonValue
                && count<ADVISOR>() == 0)
                return count<BISHOP>() == 0 ? DIRECT_DRAW : MATE_DRAW;

            return NO_DRAW;
        }();

        if (level != NO_DRAW)
        {
            if (level == MATE_DRAW)
            {
                MoveList<ALL> moves(*this);
                if (moves.size() == 0)
                {
                    result = mated_in(ply);
                    return true;
                }
                for (const auto& move : moves)
                {
                    do_move(move);
                    bool mate = MoveList<ALL>(*this).size() == 0;
                    undo_move(move);
                    if (mate)
                        return false;
                }
            }
            result = VALUE_DRAW;
            apply_draw_rule(false);
            return true;
        }
    }

    return false;
}

}  // namespace Stockfish
