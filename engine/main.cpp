#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <chrono>
#include <cstdint>

#include "opening_book.h"

using namespace std;

enum Color : int { WHITE = 0, BLACK = 1 };

enum PieceType : int {
  EMPTY = 0,
  PAWN = 1,
  KNIGHT = 2,
  BISHOP = 3,
  ROOK = 4,
  QUEEN = 5,
  KING = 6
};

// piece encoding: 0 empty, +1..+6 white, -1..-6 black
static inline Color color_of(int8_t p) { return p > 0 ? WHITE : BLACK; }
static inline int abs_piece(int8_t p) { return p >= 0 ? p : -p; }

static inline int file_of(int sq) { return sq & 7; }
static inline int rank_of(int sq) { return sq >> 3; }
static inline bool on_board(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }
static inline int sq_of(int f, int r) { return r * 8 + f; }

static inline string sq_to_str(int sq) {
  string s = "a1";
  s[0] = char('a' + file_of(sq));
  s[1] = char('1' + rank_of(sq));
  return s;
}
static inline int str_to_sq(const string& s) {
  if (s.size() != 2) return -1;
  int f = s[0] - 'a';
  int r = s[1] - '1';
  if (!on_board(f, r)) return -1;
  return sq_of(f, r);
}

static inline char promo_to_char(int promo) {
  switch (promo) {
    case QUEEN: return 'q';
    case ROOK:  return 'r';
    case BISHOP:return 'b';
    case KNIGHT:return 'n';
    default:    return '\0';
  }
}
static inline int char_to_promo(char c) {
  c = (char)tolower((unsigned char)c);
  if (c == 'q') return QUEEN;
  if (c == 'r') return ROOK;
  if (c == 'b') return BISHOP;
  if (c == 'n') return KNIGHT;
  return EMPTY;
}

static const string START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Precomputed step moves
static array<vector<int>, 64> KNIGHT_STEPS;
static array<vector<int>, 64> KING_STEPS;
static array<array<vector<int>, 64>, 2> PAWN_ATTACKS;

static void init_attack_tables() {
  for (int sq = 0; sq < 64; sq++) {
    int f = file_of(sq), r = rank_of(sq);

    static const int kdf[8] = { -2,-1, 1, 2, 2, 1,-1,-2 };
    static const int kdr[8] = {  1, 2, 2, 1,-1,-2,-2,-1 };
    for (int i = 0; i < 8; i++) {
      int nf = f + kdf[i], nr = r + kdr[i];
      if (on_board(nf, nr)) KNIGHT_STEPS[sq].push_back(sq_of(nf, nr));
    }

    for (int df = -1; df <= 1; df++) {
      for (int dr = -1; dr <= 1; dr++) {
        if (df == 0 && dr == 0) continue;
        int nf = f + df, nr = r + dr;
        if (on_board(nf, nr)) KING_STEPS[sq].push_back(sq_of(nf, nr));
      }
    }

    if (on_board(f - 1, r + 1)) PAWN_ATTACKS[WHITE][sq].push_back(sq_of(f - 1, r + 1));
    if (on_board(f + 1, r + 1)) PAWN_ATTACKS[WHITE][sq].push_back(sq_of(f + 1, r + 1));
    if (on_board(f - 1, r - 1)) PAWN_ATTACKS[BLACK][sq].push_back(sq_of(f - 1, r - 1));
    if (on_board(f + 1, r - 1)) PAWN_ATTACKS[BLACK][sq].push_back(sq_of(f + 1, r - 1));
  }
}

// ---------------- Zobrist ----------------
static array<array<uint64_t, 64>, 12> Z_PIECE; // 12 piece kinds
static array<uint64_t, 16> Z_CASTLE;
static array<uint64_t, 8>  Z_EPFILE;
static uint64_t Z_SIDE;

static inline int pidx(int8_t p) {
  int ap = abs_piece(p);
  if (p > 0) return ap - 1;          // 0..5
  return 6 + (ap - 1);              // 6..11
}

// SplitMix64 RNG
static inline uint64_t splitmix64(uint64_t &x) {
  uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

static void init_zobrist(uint64_t seed = 0xC0FFEEULL) {
  uint64_t x = seed;
  for (int i = 0; i < 12; i++)
    for (int sq = 0; sq < 64; sq++)
      Z_PIECE[i][sq] = splitmix64(x);

  for (int i = 0; i < 16; i++) Z_CASTLE[i] = splitmix64(x);
  for (int i = 0; i < 8; i++)  Z_EPFILE[i] = splitmix64(x);
  Z_SIDE = splitmix64(x);
}

// ---------------- Move ----------------
struct Move {
  int from = 0;
  int to = 0;
  int promo = EMPTY;
  uint8_t flags = 0;

  static constexpr uint8_t CAPTURE   = 1 << 0;
  static constexpr uint8_t ENPASSANT = 1 << 1;
  static constexpr uint8_t CASTLE    = 1 << 2;
  static constexpr uint8_t DOUBLEP   = 1 << 3;
  static constexpr uint8_t PROMO     = 1 << 4;
};

static inline string move_to_uci(const Move& m) {
  string s;
  s += sq_to_str(m.from);
  s += sq_to_str(m.to);
  if (m.promo != EMPTY) s += promo_to_char(m.promo);
  return s;
}

static inline uint32_t encode_move(const Move& m) {
  // from (6) | to (6) | promo (3)
  uint32_t p = (m.promo == EMPTY) ? 0u : (uint32_t)m.promo; // 2..5 mostly
  return (uint32_t)m.from | ((uint32_t)m.to << 6) | (p << 12);
}
static inline Move decode_move(uint32_t em) {
  Move m;
  m.from = (int)(em & 63u);
  m.to   = (int)((em >> 6) & 63u);
  uint32_t p = (em >> 12) & 7u;
  m.promo = (p == 0 ? EMPTY : (int)p);
  m.flags = 0;
  return m;
}

// ---------------- Board ----------------
struct Undo {
  int8_t captured = 0;
  int castling = 0;
  int ep = -1;
  int halfmove = 0;
  int fullmove = 1;

  int ep_cap_sq = -1;
  int8_t ep_cap_piece = 0;

  uint64_t key = 0;
};

struct Board {
  array<int8_t, 64> sq{};
  Color side = WHITE;
  int castling = 0; // 1=K,2=Q,4=k,8=q
  int ep = -1;
  int halfmove = 0;
  int fullmove = 1;

  uint64_t key = 0;
  vector<uint64_t> key_hist; // for repetition

  Board() { sq.fill(0); }

  void set_startpos() { set_fen(START_FEN); }

  uint64_t compute_key() const {
    uint64_t k = 0;
    for (int i = 0; i < 64; i++) {
      int8_t p = sq[i];
      if (p) k ^= Z_PIECE[pidx(p)][i];
    }
    k ^= Z_CASTLE[castling & 15];
    if (ep != -1) k ^= Z_EPFILE[file_of(ep)];
    if (side == BLACK) k ^= Z_SIDE;
    return k;
  }

  bool set_fen(const string& fen) {
    sq.fill(0);
    side = WHITE;
    castling = 0;
    ep = -1;
    halfmove = 0;
    fullmove = 1;

    istringstream iss(fen);
    string placement, stm, cast, eps;
    if (!(iss >> placement >> stm >> cast >> eps >> halfmove >> fullmove)) return false;

    int r = 7, f = 0;
    for (char c : placement) {
      if (c == '/') { r--; f = 0; continue; }
      if (isdigit((unsigned char)c)) { f += (c - '0'); continue; }
      if (!on_board(f, r)) return false;

      Color pc = isupper((unsigned char)c) ? WHITE : BLACK;
      char lc = (char)tolower((unsigned char)c);

      int pt = EMPTY;
      if (lc == 'p') pt = PAWN;
      else if (lc == 'n') pt = KNIGHT;
      else if (lc == 'b') pt = BISHOP;
      else if (lc == 'r') pt = ROOK;
      else if (lc == 'q') pt = QUEEN;
      else if (lc == 'k') pt = KING;
      else return false;

      int8_t val = (int8_t)(pc == WHITE ? pt : -pt);
      sq[sq_of(f, r)] = val;
      f++;
    }

    side = (stm == "w") ? WHITE : BLACK;

    castling = 0;
    if (cast != "-") {
      for (char c : cast) {
        if (c == 'K') castling |= 1;
        else if (c == 'Q') castling |= 2;
        else if (c == 'k') castling |= 4;
        else if (c == 'q') castling |= 8;
      }
    }

    ep = (eps != "-") ? str_to_sq(eps) : -1;

    key = compute_key();
    key_hist.clear();
    key_hist.push_back(key);
    return true;
  }

  int find_king(Color c) const {
    int8_t k = (int8_t)(c == WHITE ? KING : -KING);
    for (int i = 0; i < 64; i++) if (sq[i] == k) return i;
    return -1;
  }

  bool is_square_attacked(int target, Color by) const {
    int tf = file_of(target), tr = rank_of(target);

    if (by == WHITE) {
      int r = tr - 1;
      if (r >= 0) {
        if (tf - 1 >= 0) { int s = sq_of(tf - 1, r); if (sq[s] == (int8_t)PAWN) return true; }
        if (tf + 1 < 8)  { int s = sq_of(tf + 1, r); if (sq[s] == (int8_t)PAWN) return true; }
      }
    } else {
      int r = tr + 1;
      if (r < 8) {
        if (tf - 1 >= 0) { int s = sq_of(tf - 1, r); if (sq[s] == (int8_t)(-PAWN)) return true; }
        if (tf + 1 < 8)  { int s = sq_of(tf + 1, r); if (sq[s] == (int8_t)(-PAWN)) return true; }
      }
    }

    for (int s : KNIGHT_STEPS[target]) {
      int8_t p = sq[s];
      if (p && abs_piece(p) == KNIGHT && color_of(p) == by) return true;
    }

    for (int s : KING_STEPS[target]) {
      int8_t p = sq[s];
      if (p && abs_piece(p) == KING && color_of(p) == by) return true;
    }

    static const int dR[4][2] = { {0,1},{0,-1},{1,0},{-1,0} };
    for (auto &d : dR) {
      int f = tf + d[0], r = tr + d[1];
      while (on_board(f, r)) {
        int s = sq_of(f, r);
        int8_t p = sq[s];
        if (p) {
          if (color_of(p) == by) {
            int ap = abs_piece(p);
            if (ap == ROOK || ap == QUEEN) return true;
          }
          break;
        }
        f += d[0]; r += d[1];
      }
    }

    static const int dB[4][2] = { {1,1},{-1,1},{1,-1},{-1,-1} };
    for (auto &d : dB) {
      int f = tf + d[0], r = tr + d[1];
      while (on_board(f, r)) {
        int s = sq_of(f, r);
        int8_t p = sq[s];
        if (p) {
          if (color_of(p) == by) {
            int ap = abs_piece(p);
            if (ap == BISHOP || ap == QUEEN) return true;
          }
          break;
        }
        f += d[0]; r += d[1];
      }
    }

    return false;
  }

  bool in_check(Color c) const {
    int ksq = find_king(c);
    if (ksq < 0) return false;
    return is_square_attacked(ksq, (c == WHITE ? BLACK : WHITE));
  }

  void update_castling_rights_on_move(int from, int to, int8_t moved, int8_t captured) {
    if (abs_piece(moved) == KING) {
      if (color_of(moved) == WHITE) castling &= ~(1 | 2);
      else castling &= ~(4 | 8);
    }
    if (abs_piece(moved) == ROOK) {
      if (from == 0) castling &= ~2;
      else if (from == 7) castling &= ~1;
      else if (from == 56) castling &= ~8;
      else if (from == 63) castling &= ~4;
    }
    if (captured && abs_piece(captured) == ROOK) {
      if (to == 0) castling &= ~2;
      else if (to == 7) castling &= ~1;
      else if (to == 56) castling &= ~8;
      else if (to == 63) castling &= ~4;
    }
  }

  void add_promotion_moves(vector<Move>& out, int from, int to, uint8_t baseFlags) const {
    Move m;
    m.from = from; m.to = to; m.flags = baseFlags | Move::PROMO;
    m.promo = QUEEN;  out.push_back(m);
    m.promo = ROOK;   out.push_back(m);
    m.promo = BISHOP; out.push_back(m);
    m.promo = KNIGHT; out.push_back(m);
  }

  void gen_pseudo_legal(vector<Move>& out) const {
    out.clear();
    Color us = side;
    Color them = (us == WHITE ? BLACK : WHITE);

    for (int from = 0; from < 64; from++) {
      int8_t p = sq[from];
      if (!p || color_of(p) != us) continue;

      int pt = abs_piece(p);
      int f = file_of(from), r = rank_of(from);

      if (pt == PAWN) {
        if (us == WHITE) {
          int one = from + 8;
          if (one < 64 && sq[one] == 0) {
            if (rank_of(one) == 7) add_promotion_moves(out, from, one, 0);
            else {
              out.push_back(Move{from, one, EMPTY, 0});
              if (r == 1) {
                int two = from + 16;
                if (sq[two] == 0) out.push_back(Move{from, two, EMPTY, Move::DOUBLEP});
              }
            }
          }
          for (int to : PAWN_ATTACKS[WHITE][from]) {
            if (sq[to] && color_of(sq[to]) == them) {
              uint8_t fl = Move::CAPTURE;
              if (rank_of(to) == 7) add_promotion_moves(out, from, to, fl);
              else out.push_back(Move{from, to, EMPTY, fl});
            }
            if (to == ep) out.push_back(Move{from, to, EMPTY, (uint8_t)(Move::ENPASSANT | Move::CAPTURE)});
          }
        } else {
          int one = from - 8;
          if (one >= 0 && sq[one] == 0) {
            if (rank_of(one) == 0) add_promotion_moves(out, from, one, 0);
            else {
              out.push_back(Move{from, one, EMPTY, 0});
              if (r == 6) {
                int two = from - 16;
                if (sq[two] == 0) out.push_back(Move{from, two, EMPTY, Move::DOUBLEP});
              }
            }
          }
          for (int to : PAWN_ATTACKS[BLACK][from]) {
            if (sq[to] && color_of(sq[to]) == them) {
              uint8_t fl = Move::CAPTURE;
              if (rank_of(to) == 0) add_promotion_moves(out, from, to, fl);
              else out.push_back(Move{from, to, EMPTY, fl});
            }
            if (to == ep) out.push_back(Move{from, to, EMPTY, (uint8_t)(Move::ENPASSANT | Move::CAPTURE)});
          }
        }
      }
      else if (pt == KNIGHT) {
        for (int to : KNIGHT_STEPS[from]) {
          int8_t q = sq[to];
          if (!q) out.push_back(Move{from, to, EMPTY, 0});
          else if (color_of(q) == them) out.push_back(Move{from, to, EMPTY, Move::CAPTURE});
        }
      }
      else if (pt == BISHOP || pt == ROOK || pt == QUEEN) {
        static const int dB[4][2] = { {1,1},{-1,1},{1,-1},{-1,-1} };
        static const int dR[4][2] = { {0,1},{0,-1},{1,0},{-1,0} };

        auto slide = [&](int df, int dr) {
          int nf = f + df, nr = r + dr;
          while (on_board(nf, nr)) {
            int to = sq_of(nf, nr);
            if (!sq[to]) out.push_back(Move{from, to, EMPTY, 0});
            else {
              if (color_of(sq[to]) == them) out.push_back(Move{from, to, EMPTY, Move::CAPTURE});
              break;
            }
            nf += df; nr += dr;
          }
        };

        if (pt == BISHOP || pt == QUEEN) for (auto &d : dB) slide(d[0], d[1]);
        if (pt == ROOK   || pt == QUEEN) for (auto &d : dR) slide(d[0], d[1]);
      }
      else if (pt == KING) {
        for (int to : KING_STEPS[from]) {
          int8_t q = sq[to];
          if (!q) out.push_back(Move{from, to, EMPTY, 0});
          else if (color_of(q) == them) out.push_back(Move{from, to, EMPTY, Move::CAPTURE});
        }

        // Castling (requires not in check + squares empty + squares not attacked)
        if (us == WHITE) {
          if (!in_check(WHITE)) {
            if ((castling & 1) && sq[5] == 0 && sq[6] == 0 && sq[7] == (int8_t)ROOK) {
              if (!is_square_attacked(5, BLACK) && !is_square_attacked(6, BLACK))
                out.push_back(Move{4, 6, EMPTY, Move::CASTLE});
            }
            if ((castling & 2) && sq[3] == 0 && sq[2] == 0 && sq[1] == 0 && sq[0] == (int8_t)ROOK) {
              if (!is_square_attacked(3, BLACK) && !is_square_attacked(2, BLACK))
                out.push_back(Move{4, 2, EMPTY, Move::CASTLE});
            }
          }
        } else {
          if (!in_check(BLACK)) {
            if ((castling & 4) && sq[61] == 0 && sq[62] == 0 && sq[63] == (int8_t)(-ROOK)) {
              if (!is_square_attacked(61, WHITE) && !is_square_attacked(62, WHITE))
                out.push_back(Move{60, 62, EMPTY, Move::CASTLE});
            }
            if ((castling & 8) && sq[59] == 0 && sq[58] == 0 && sq[57] == 0 && sq[56] == (int8_t)(-ROOK)) {
              if (!is_square_attacked(59, WHITE) && !is_square_attacked(58, WHITE))
                out.push_back(Move{60, 58, EMPTY, Move::CASTLE});
            }
          }
        }
      }
    }
  }

  void gen_legal(vector<Move>& out) {
    vector<Move> pseudo;
    gen_pseudo_legal(pseudo);
    out.clear();

    Color us = side;
    for (const auto& m : pseudo) {
      Undo u;
      make_move(m, u);
      bool ok = !in_check(us);
      unmake_move(m, u);
      if (ok) out.push_back(m);
    }
  }

  void make_move(const Move& m, Undo& u) {
    u.captured = sq[m.to];
    u.castling = castling;
    u.ep = ep;
    u.halfmove = halfmove;
    u.fullmove = fullmove;
    u.ep_cap_sq = -1;
    u.ep_cap_piece = 0;
    u.key = key;

    int8_t moved = sq[m.from];
    int8_t captured = sq[m.to];

    // Remove old state hashes
    key ^= Z_CASTLE[castling & 15];
    if (ep != -1) key ^= Z_EPFILE[file_of(ep)];

    // Halfmove
    if (abs_piece(moved) == PAWN || (m.flags & Move::CAPTURE)) halfmove = 0;
    else halfmove++;

    if (side == BLACK) fullmove++;

    // Clear ep by default
    ep = -1;

    // Piece hash: remove moved from 'from'
    key ^= Z_PIECE[pidx(moved)][m.from];

    // Capture handling
    if (m.flags & Move::ENPASSANT) {
      int capSq = (side == WHITE) ? (m.to - 8) : (m.to + 8);
      u.ep_cap_sq = capSq;
      u.ep_cap_piece = sq[capSq];
      // remove captured pawn hash
      key ^= Z_PIECE[pidx(u.ep_cap_piece)][capSq];
      sq[capSq] = 0;
      captured = u.ep_cap_piece;
    } else if (captured) {
      key ^= Z_PIECE[pidx(captured)][m.to];
    }

    // Move piece on board
    sq[m.to] = moved;
    sq[m.from] = 0;

    // Promotion
    if (m.promo != EMPTY) {
      int8_t prom = (int8_t)(side == WHITE ? m.promo : -m.promo);
      sq[m.to] = prom;
      key ^= Z_PIECE[pidx(prom)][m.to];
    } else {
      key ^= Z_PIECE[pidx(moved)][m.to];
    }

    // Castling rook move
    if (m.flags & Move::CASTLE) {
      if (m.to == 6) { // white O-O
        int8_t rook = sq[7];
        key ^= Z_PIECE[pidx(rook)][7];
        sq[5] = rook; sq[7] = 0;
        key ^= Z_PIECE[pidx(rook)][5];
      } else if (m.to == 2) { // white O-O-O
        int8_t rook = sq[0];
        key ^= Z_PIECE[pidx(rook)][0];
        sq[3] = rook; sq[0] = 0;
        key ^= Z_PIECE[pidx(rook)][3];
      } else if (m.to == 62) { // black O-O
        int8_t rook = sq[63];
        key ^= Z_PIECE[pidx(rook)][63];
        sq[61] = rook; sq[63] = 0;
        key ^= Z_PIECE[pidx(rook)][61];
      } else if (m.to == 58) { // black O-O-O
        int8_t rook = sq[56];
        key ^= Z_PIECE[pidx(rook)][56];
        sq[59] = rook; sq[56] = 0;
        key ^= Z_PIECE[pidx(rook)][59];
      }
    }

    // Update castling rights
    update_castling_rights_on_move(m.from, m.to, moved, captured);

    // Double pawn push ep
    if (m.flags & Move::DOUBLEP) {
      ep = (side == WHITE) ? (m.from + 8) : (m.from - 8);
    }

    // Add new state hashes
    key ^= Z_CASTLE[castling & 15];
    if (ep != -1) key ^= Z_EPFILE[file_of(ep)];

    // Toggle side
    side = (side == WHITE ? BLACK : WHITE);
    key ^= Z_SIDE;

    key_hist.push_back(key);
  }

  bool has_non_pawn_material(Color c) const {
  for (int i = 0; i < 64; i++) {
    int8_t p = sq[i];
    if (!p) continue;
    if (color_of(p) != c) continue;
    int ap = abs_piece(p);
    if (ap != KING && ap != PAWN) return true;
    }
    return false;
  }

  void make_null(Undo& u) {
    u.castling = castling;
    u.ep = ep;
    u.halfmove = halfmove;
    u.fullmove = fullmove;
    u.key = key;

    // remove old state hashes
    key ^= Z_CASTLE[castling & 15];
    if (ep != -1) key ^= Z_EPFILE[file_of(ep)];

    ep = -1;
    halfmove++;

    // add new state hashes
    key ^= Z_CASTLE[castling & 15];
    // ep none
    side = (side == WHITE ? BLACK : WHITE);
    key ^= Z_SIDE;

    key_hist.push_back(key);
  }

  void unmake_null(const Undo& u) {
    side = (side == WHITE ? BLACK : WHITE); // restore side toggle effect
    castling = u.castling;
    ep = u.ep;
    halfmove = u.halfmove;
    fullmove = u.fullmove;
    key = u.key;
    key_hist.pop_back();
  }

  void unmake_move(const Move& m, const Undo& u) {
    // restore by reversing board state the same as before
    side = (side == WHITE ? BLACK : WHITE);

    castling = u.castling;
    ep = u.ep;
    halfmove = u.halfmove;
    fullmove = u.fullmove;

    // undo castling rook
    if (m.flags & Move::CASTLE) {
      if (m.to == 6) { sq[7] = sq[5]; sq[5] = 0; }
      else if (m.to == 2) { sq[0] = sq[3]; sq[3] = 0; }
      else if (m.to == 62) { sq[63] = sq[61]; sq[61] = 0; }
      else if (m.to == 58) { sq[56] = sq[59]; sq[59] = 0; }
    }

    int8_t movedBack = sq[m.to];

    if (m.promo != EMPTY) movedBack = (int8_t)(side == WHITE ? PAWN : -PAWN);

    sq[m.from] = movedBack;
    sq[m.to] = u.captured;

    if (m.flags & Move::ENPASSANT) {
      sq[m.to] = 0;
      sq[u.ep_cap_sq] = u.ep_cap_piece;
    }

    key = u.key;
    key_hist.pop_back();
  }

  bool is_repetition() const {
    // check repetition for same side-to-move positions; limit scan by halfmove (irreversible bound)
    int reps = 0;
    int limit = halfmove; // upper bound
    // Compare only positions with same side to move => step by 2 plies
    for (int i = (int)key_hist.size() - 1 - 2, steps = 0; i >= 0 && steps <= limit; i -= 2, steps += 2) {
      if (key_hist[i] == key) {
        reps++;
        if (reps >= 2) return true; // current + two previous occurrences => threefold
      }
    }
    return false;
  }
};

// ---------------- Perft ----------------
static uint64_t perft(Board& b, int depth) {
  if (depth == 0) return 1ULL;
  vector<Move> moves;
  b.gen_legal(moves);

  uint64_t nodes = 0;
  for (const auto& m : moves) {
    Undo u;
    b.make_move(m, u);
    nodes += perft(b, depth - 1);
    b.unmake_move(m, u);
  }
  return nodes;
}

static void perft_divide(Board& b, int depth) {
  vector<Move> moves;
  b.gen_legal(moves);

  uint64_t total = 0;
  for (const auto& m : moves) {
    Undo u;
    b.make_move(m, u);
    uint64_t n = perft(b, depth - 1);
    b.unmake_move(m, u);
    cout << move_to_uci(m) << ": " << n << "\n";
    total += n;
  }
  cout << "Total: " << total << "\n";
}


// ---------------- UCI helpers ----------------
static vector<string> split_ws(const string& s) {
  istringstream iss(s);
  vector<string> t;
  string x;
  while (iss >> x) t.push_back(x);
  return t;
}

static bool apply_uci_move(Board& b, const string& uci) {
  if (uci.size() < 4) return false;
  int from = str_to_sq(uci.substr(0,2));
  int to = str_to_sq(uci.substr(2,2));
  int promo = (uci.size() == 5) ? char_to_promo(uci[4]) : EMPTY;

  vector<Move> moves;
  b.gen_legal(moves);

  for (const auto& m : moves) {
    if (m.from == from && m.to == to) {
      if ((m.promo == EMPTY && promo == EMPTY) || (m.promo != EMPTY && m.promo == promo)) {
        Undo u;
        b.make_move(m, u);
        return true;
      }
    }
  }
  return false;
}

static void uci_position(Board& b, const string& line, vector<string>& move_history) {
  auto tok = split_ws(line);
  if (tok.size() < 2) return;

  move_history.clear();

  size_t i = 1;
  if (tok[i] == "startpos") {
    b.set_startpos();
    i++;
  } else if (tok[i] == "fen") {
    if (tok.size() < i + 7) return;
    string fen = tok[i+1] + " " + tok[i+2] + " " + tok[i+3] + " " + tok[i+4] + " " + tok[i+5] + " " + tok[i+6];
    b.set_fen(fen);
    i += 7;
  } else return;

  if (i < tok.size() && tok[i] == "moves") {
    i++;
    for (; i < tok.size(); i++) {
      if (apply_uci_move(b, tok[i])) move_history.push_back(tok[i]);
    }
  }
}

static bool has_critical_tactics(Board& b, const vector<Move>& legal) {
  if (b.in_check(b.side)) return true;
  for (const auto& m : legal) {
    if ((m.flags & Move::CAPTURE) || m.promo != EMPTY) return true;
  }
  return false;
}

static bool is_early_queen_move_uci(const string& uci, size_t ply) {
  if (ply > 6 || uci.size() < 2) return false;
  return uci.rfind("d1", 0) == 0 || uci.rfind("d8", 0) == 0;
}

static bool move_keeps_king_safe(Board& b, const Move& m) {
  Undo u;
  b.make_move(m, u);
  const int us = b.side ^ 1;
  const bool inCheck = b.in_check(static_cast<Color>(us));
  b.unmake_move(m, u);
  return !inCheck;
}

struct GoParams {
  int depth = 0;          // 0 => iterative until time
  int movetime_ms = 0;    // 0 => derive from clocks
  int wtime = -1, btime = -1, winc = 0, binc = 0;
};

static GoParams parse_go(const string& line) {
  GoParams p;
  auto tok = split_ws(line);
  for (size_t i = 0; i + 1 < tok.size(); i++) {
    if (tok[i] == "depth") p.depth = stoi(tok[i+1]);
    else if (tok[i] == "movetime") p.movetime_ms = stoi(tok[i+1]);
    else if (tok[i] == "wtime") p.wtime = stoi(tok[i+1]);
    else if (tok[i] == "btime") p.btime = stoi(tok[i+1]);
    else if (tok[i] == "winc") p.winc = stoi(tok[i+1]);
    else if (tok[i] == "binc") p.binc = stoi(tok[i+1]);
  }
  return p;
}

static int choose_movetime_ms(const Board& b, const GoParams& p) {
  if (p.movetime_ms > 0) return p.movetime_ms;

  int rem = (b.side == WHITE ? p.wtime : p.btime);
  int inc = (b.side == WHITE ? p.winc : p.binc);
  if (rem <= 0) return 200;

  // Blitz-friendly: usa ~1/28 del reloj + una fracción del increment, con clamps.
  int t = rem / 28 + inc / 2;
  t = max(30, min(1200, t));
  return t;
}

// ---------------- Eval ----------------
static constexpr int PV[7] = {0, 100, 320, 330, 500, 900, 0};

static inline int center_bonus(int sq) {
  // bonus por cercanía al centro (d4/e4/d5/e5)
  int f = file_of(sq), r = rank_of(sq);
  int df = abs(f - 3) + abs(f - 4);
  int dr = abs(r - 3) + abs(r - 4);
  int d = min(df, dr);
  return (3 - min(3, d)) * 6; // 0..18
}

static int eval(const Board& b) {
  int score = 0;

  int wpawns_file[8] = {0}, bpawns_file[8] = {0};
  bool wB = false, wB2 = false, bB = false, bB2 = false;

  int wKingSq = -1, bKingSq = -1;
  bool wQueenMoved = true, bQueenMoved = true;

  for (int sq = 0; sq < 64; sq++) {
    int8_t p = b.sq[sq];
    if (!p) continue;

    int pt = abs_piece(p);
    int sgn = (p > 0) ? +1 : -1;

    // material
    score += sgn * PV[pt];

    // tracking
    if (pt == PAWN) {
      if (p > 0) wpawns_file[file_of(sq)]++;
      else bpawns_file[file_of(sq)]++;
    } else if (pt == BISHOP) {
      if (p > 0) { if (!wB) wB = true; else wB2 = true; }
      else { if (!bB) bB = true; else bB2 = true; }
    } else if (pt == KING) {
      if (p > 0) wKingSq = sq; else bKingSq = sq;
    } else if (pt == QUEEN) {
      if (p > 0 && sq == str_to_sq("d1")) wQueenMoved = false;
      if (p < 0 && sq == str_to_sq("d8")) bQueenMoved = false;
    }

    // positional “barato”
    if (pt == KNIGHT || pt == BISHOP) {
      score += sgn * center_bonus(sq); // centralización
    }

    if (pt == PAWN) {
      int r = rank_of(sq);
      int adv = (p > 0) ? r : (7 - r); // avance relativo
      int bonus = adv * 4; // avance de peón
      // peones centrales (d/e)
      int f = file_of(sq);
      if (f == 3 || f == 4) bonus += 8;
      score += sgn * bonus;
    }
  }

  // bishop pair
  if (wB && wB2) score += 25;
  if (bB && bB2) score -= 25;

  // pawn structure: doubled + isolated (simple)
  for (int f = 0; f < 8; f++) {
    if (wpawns_file[f] >= 2) score -= 10 * (wpawns_file[f] - 1);
    if (bpawns_file[f] >= 2) score += 10 * (bpawns_file[f] - 1);

    bool wIso = wpawns_file[f] > 0 &&
                ( (f == 0 ? 0 : wpawns_file[f-1]) == 0 ) &&
                ( (f == 7 ? 0 : wpawns_file[f+1]) == 0 );
    bool bIso = bpawns_file[f] > 0 &&
                ( (f == 0 ? 0 : bpawns_file[f-1]) == 0 ) &&
                ( (f == 7 ? 0 : bpawns_file[f+1]) == 0 );

    if (wIso) score -= 8;
    if (bIso) score += 8;
  }

  // king safety: bonus por enrocar; penalización si no enrocó “tarde”
  auto castled = [&](Color c, int ksq)->bool {
    if (c == WHITE) return ksq == str_to_sq("g1") || ksq == str_to_sq("c1");
    return ksq == str_to_sq("g8") || ksq == str_to_sq("c8");
  };

  if (wKingSq != -1) {
    if (castled(WHITE, wKingSq)) score += 18;
    else if (b.fullmove >= 10) score -= 18;
  }
  if (bKingSq != -1) {
    if (castled(BLACK, bKingSq)) score -= 18;
    else if (b.fullmove >= 10) score += 18;
  }

  // no salir con la dama demasiado temprano (suave)
  if (b.fullmove <= 8) {
    if (!wQueenMoved) score -= 8;
    if (!bQueenMoved) score += 8;
  }

  // perspectiva del que mueve (negamax)
  return (b.side == WHITE) ? score : -score;
}


// ---------------- TT ----------------
static constexpr int MATE = 30000;
static constexpr int INF  = 32000;

static inline int score_to_tt(int s, int ply) {
  if (s > MATE - 1000) return s + ply;
  if (s < -MATE + 1000) return s - ply;
  return s;
}
static inline int score_from_tt(int s, int ply) {
  if (s > MATE - 1000) return s - ply;
  if (s < -MATE + 1000) return s + ply;
  return s;
}

enum TTFlag : uint8_t { TT_EXACT = 0, TT_ALPHA = 1, TT_BETA = 2 };

struct TTEntry {
  uint64_t key = 0;
  int16_t depth = -1;
  int16_t flag = TT_EXACT;
  int32_t score = 0;
  uint32_t best = 0; // encoded move
};

struct TranspositionTable {
  vector<TTEntry> t;
  size_t mask = 0;

  void resize_mb(int mb) {
    if (mb < 1) mb = 1;
    size_t bytes = (size_t)mb * 1024ULL * 1024ULL;
    size_t n = bytes / sizeof(TTEntry);
    // power of two
    size_t pow2 = 1;
    while (pow2 < n) pow2 <<= 1;
    t.assign(pow2, TTEntry{});
    mask = pow2 - 1;
  }

  TTEntry* probe(uint64_t k) {
    if (t.empty()) return nullptr;
    return &t[(size_t)k & mask];
  }
};

static TranspositionTable TT;

// ---------------- Search ----------------
struct SearchState {
  chrono::steady_clock::time_point stop;
  bool use_time_limit = true;
  uint64_t nodes = 0;

  // killer moves: [ply][2]
  array<array<uint32_t, 2>, 128> killers{};
  // history: [color][from][to]
  array<array<array<int, 64>, 64>, 2> history{};

  uint32_t tt_move = 0;

  bool time_up() const {
    return use_time_limit && chrono::steady_clock::now() >= stop;
  }
};

static int mvv_lva(const Board& b, const Move& m) {
  int8_t att = b.sq[m.from];
  int8_t vic = b.sq[m.to];
  if (m.flags & Move::ENPASSANT) vic = (int8_t)(b.side == WHITE ? -PAWN : PAWN);
  int av = PV[abs_piece(att)];
  int vv = PV[abs_piece(vic)];
  return vv * 10 - av;
}

static int score_move(const Board& b, const Move& m, const SearchState& st, int ply, uint32_t ttBest) {
  uint32_t em = encode_move(m);
  if (ttBest && em == ttBest) return 1000000;

  if (m.flags & Move::CAPTURE) return 500000 + mvv_lva(b, m);

  // killers
  if (st.killers[ply][0] == em) return 490000;
  if (st.killers[ply][1] == em) return 480000;

  // history
  return st.history[b.side][m.from][m.to];
}

static void order_moves(const Board& b, vector<Move>& moves, const SearchState& st, int ply, uint32_t ttBest) {
  stable_sort(moves.begin(), moves.end(), [&](const Move& a, const Move& c) {
    return score_move(b, a, st, ply, ttBest) > score_move(b, c, st, ply, ttBest);
  });
}

static int qsearch(Board& b, int alpha, int beta, SearchState& st, int ply) {
  if (st.time_up()) return eval(b);
  st.nodes++;

  int stand = eval(b);
  if (stand >= beta) return beta;
  if (stand > alpha) alpha = stand;

  vector<Move> moves;
  b.gen_legal(moves);

  // Only captures (and promotions via capture already included)
  vector<Move> caps;
  caps.reserve(moves.size());
  for (auto &m : moves) if (m.flags & Move::CAPTURE) caps.push_back(m);

  order_moves(b, caps, st, ply, 0);

  for (auto &m : caps) {
    if (st.time_up()) break;
    Undo u;
    b.make_move(m, u);
    int score = -qsearch(b, -beta, -alpha, st, ply + 1);
    b.unmake_move(m, u);

    if (score >= beta) return beta;
    if (score > alpha) alpha = score;
  }
  return alpha;
}

static int negamax(Board& b, int depth, int alpha, int beta, SearchState& st, int ply, uint32_t& outBest) {
  if (st.time_up()) return eval(b);
  st.nodes++;

  if (b.halfmove >= 100) return 0;         // 50-move draw
  if (b.is_repetition()) return 0;

  bool inCheck = b.in_check(b.side);

  if (depth <= 0) return qsearch(b, alpha, beta, st, ply);

  // TT probe
  uint32_t ttBest = 0;
  if (auto e = TT.probe(b.key)) {
    if (e->key == b.key && e->depth >= depth) {
      int ttScore = score_from_tt(e->score, ply);
      ttBest = e->best;
      if (e->flag == TT_EXACT) return ttScore;
      if (e->flag == TT_ALPHA && ttScore <= alpha) return ttScore;
      if (e->flag == TT_BETA  && ttScore >= beta)  return ttScore;
    } else if (e->key == b.key) {
      ttBest = e->best;
    }
  }

  vector<Move> moves;
  b.gen_legal(moves);

  if (moves.empty()) {
    if (inCheck) return -MATE + ply;
    return 0; // stalemate
  }

  order_moves(b, moves, st, ply, ttBest);

  int bestScore = -INF;
  uint32_t bestMoveEnc = 0;

  int origAlpha = alpha;

  for (auto &m : moves) {
    if (st.time_up()) break;

    Undo u;
    b.make_move(m, u);

    uint32_t childBest = 0;
    int score = -negamax(b, depth - 1, -beta, -alpha, st, ply + 1, childBest);

    b.unmake_move(m, u);

    if (score > bestScore) {
      bestScore = score;
      bestMoveEnc = encode_move(m);
    }
    if (score > alpha) alpha = score;
    if (alpha >= beta) {
      // beta cutoff: update killers/history for quiet moves
      uint32_t em = encode_move(m);
      if (!(m.flags & Move::CAPTURE)) {
        if (st.killers[ply][0] != em) {
          st.killers[ply][1] = st.killers[ply][0];
          st.killers[ply][0] = em;
        }
        st.history[b.side][m.from][m.to] += depth * depth;
      }
      break;
    }
  }

  outBest = bestMoveEnc;

  // Store TT
  if (auto e = TT.probe(b.key)) {
    TTFlag flag = TT_EXACT;
    if (bestScore <= origAlpha) flag = TT_ALPHA;
    else if (bestScore >= beta) flag = TT_BETA;

    e->key = b.key;
    e->depth = (int16_t)depth;
    e->flag = (int16_t)flag;
    e->score = score_to_tt(bestScore, ply);
    e->best = bestMoveEnc;
  }

  return bestScore;
}


static Move search_bestmove(Board& b, int movetime_ms, int maxDepth, int& outScoreCp) {
  SearchState st;
  auto start = chrono::steady_clock::now();
  st.use_time_limit = movetime_ms > 0;
  st.stop = st.use_time_limit ? (start + chrono::milliseconds(movetime_ms)) : chrono::steady_clock::time_point::max();
  st.nodes = 0;
  Move best{};
  uint32_t bestEnc = 0;
  int bestScore = -INF;

  int prev = 0;                 // score del depth anterior para aspiration
  const int WINDOW = 80;      // centipawns (50-80 suele ir bien)

  vector<Move> root;
  b.gen_legal(root);
  if (root.empty()) {
    outScoreCp = 0;
    return Move{}; // terminal real
  }

  // fallback: siempre una jugada legal
  best = root[0];
  bestEnc = encode_move(best);
  bestScore = 0;

  for (int depth = 1; depth <= maxDepth; depth++) {
    if (st.time_up()) break;

    int alpha = -INF, beta = INF;
    if (depth >= 2) {
      alpha = prev - WINDOW;
      beta  = prev + WINDOW;
    }
    uint32_t pv = bestEnc;  // fallback: si corta por tiempo, no perdés PV
    int score = negamax(b, depth, alpha, beta, st, 0, pv);

    // si falla la ventana y aún hay tiempo, re-search con ventana completa
    if (!st.time_up() && depth >= 2) {
      if (score <= alpha || score >= beta) {
      pv = bestEnc;           // mismo motivo: no borres el PV
      score = negamax(b, depth, -INF, INF, st, 0, pv);
      }
    }

    if (st.time_up()) break;

    // actualizar “prev” con el score final de este depth (post re-search)
    prev = score;

    if (pv != 0) {
      bestEnc = pv;
      bestScore = score;
      best = decode_move(bestEnc);
    }

    // NPS real (tiempo transcurrido)
    auto now = chrono::steady_clock::now();
    double sec = chrono::duration<double>(now - start).count();
    int nps = (sec > 0.0) ? (int)(st.nodes / sec) : 0;

    outScoreCp = bestScore;
    cout << "info depth " << depth
         << " score cp " << bestScore
         << " nodes " << st.nodes
         << " nps " << nps
         << "\n";
  }

  outScoreCp = bestScore;
  return best;
}




  // ---------------- main ----------------
int main(int argc, char** argv) {
  ios::sync_with_stdio(false);
  cin.tie(nullptr);
  cout.setf(std::ios::unitbuf);

  init_attack_tables();
  init_zobrist();

  if (TT.t.empty()) TT.resize_mb(64);

  // CLI tools
  if (argc >= 2) {
    string cmd = argv[1];
    if (cmd == "perft" && argc >= 3) {
      int depth = stoi(argv[2]);
      Board b; b.set_startpos();
      auto t0 = chrono::steady_clock::now();
      uint64_t nodes = perft(b, depth);
      auto t1 = chrono::steady_clock::now();
      double ms = chrono::duration<double, milli>(t1 - t0).count();
      cout << "perft(" << depth << ") = " << nodes << "  [" << ms << " ms]\n";
      return 0;
    }
    if (cmd == "perftfen" && argc >= 4) {
      string fen = argv[2];
      int depth = stoi(argv[3]);
      Board b; b.set_fen(fen);
      auto t0 = chrono::steady_clock::now();
      uint64_t nodes = perft(b, depth);
      auto t1 = chrono::steady_clock::now();
      double ms = chrono::duration<double, milli>(t1 - t0).count();
      cout << "perftfen(" << depth << ") = " << nodes << "  [" << ms << " ms]\n";
      return 0;
    }
    if (cmd == "divide" && argc >= 3) {
      int depth = stoi(argv[2]);
      Board b; b.set_startpos();
      perft_divide(b, depth);
      return 0;
    }
    if (cmd == "dividefen" && argc >= 4) {
      string fen = argv[2];
      int depth = stoi(argv[3]);
      Board b; b.set_fen(fen);
      perft_divide(b, depth);
      return 0;
    }
  }

  // UCI mode
  Board board;
  board.set_startpos();
  vector<string> move_history;

  string line;
  while (getline(cin, line)) {
    if (line == "uci") {
      cout << "id name BM-Engine\n";
      cout << "id author Benja\n";
      cout << "option name Hash type spin default 64 min 1 max 2048\n";
      cout << "option name Threads type spin default 1 min 1 max 32\n";
      cout << "uciok\n";
    }
    else if (line == "isready") {
      cout << "readyok\n";
    }
    else if (line == "ucinewgame") {
      board.set_startpos();
      move_history.clear();
    }
    else if (line.rfind("setoption name Hash value", 0) == 0) {
      auto tok = split_ws(line);
      int mb = 64;
      if (!tok.empty()) mb = stoi(tok.back());
      TT.resize_mb(mb);
    }
    else if (line.rfind("position", 0) == 0) {
      uci_position(board, line, move_history);
    }
    else if (line.rfind("go", 0) == 0) {
      GoParams gp = parse_go(line);
      int movetime = (gp.depth > 0 ? 0 : choose_movetime_ms(board, gp));
      int maxDepth = (gp.depth > 0 ? gp.depth : 20);
      int score = 0;

      vector<Move> legal;
      board.gen_legal(legal);

      vector<string> legal_uci;
      legal_uci.reserve(legal.size());
      for (const auto& m : legal) legal_uci.push_back(move_to_uci(m));

      constexpr size_t BOOK_MAX_PLIES = 12;
      auto book_move = opening_book_pick(move_history, legal_uci);
      if (book_move && move_history.size() <= BOOK_MAX_PLIES && !has_critical_tactics(board, legal)) {
        auto itLegal = find(legal_uci.begin(), legal_uci.end(), *book_move);
        bool valid_book_move = (itLegal != legal_uci.end());

        if (valid_book_move && is_early_queen_move_uci(*book_move, move_history.size())) {
          valid_book_move = false;
        }

        if (valid_book_move) {
          size_t idx = static_cast<size_t>(distance(legal_uci.begin(), itLegal));
          if (idx < legal.size() && !move_keeps_king_safe(board, legal[idx])) {
            valid_book_move = false;
          }
        }

        if (valid_book_move) {
          cout << "info string bookhit move=" << *book_move << "\n";
          cout << "bestmove " << *book_move << "\n";
          continue;
        }
      }

      Move bm = search_bestmove(board, movetime, maxDepth, score);

      // Validate best move exists; if none, 0000
      string out = legal.empty() ? "0000" : move_to_uci(legal[0]); // fallback legal
      bool matched = false;

      for (auto &m : legal) {
        if (m.from == bm.from && m.to == bm.to && m.promo == bm.promo) {
          out = move_to_uci(m);
          matched = true;
          break;
        }
      }

      // Si no matcheó pero había legales, NO es terminal. Solo fue “bestmove inválido”.
      if (!matched && !legal.empty()) {
        cout << "info string fallback_bestmove_used\n";
      }

      cout << "bestmove " << out << "\n";
    }
    else if (line == "quit") {
      break;
    }
  }
  return 0;
}
