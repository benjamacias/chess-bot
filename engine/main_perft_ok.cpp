#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <chrono>

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

static inline int file_of(int sq) { return sq & 7; }      // 0..7
static inline int rank_of(int sq) { return sq >> 3; }     // 0..7
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
static array<array<vector<int>, 64>, 2> PAWN_ATTACKS; // [color][sq] squares attacked FROM sq by that color pawn

static void init_attack_tables() {
  for (int sq = 0; sq < 64; sq++) {
    int f = file_of(sq), r = rank_of(sq);

    // Knight
    static const int kdf[8] = { -2,-1, 1, 2, 2, 1,-1,-2 };
    static const int kdr[8] = {  1, 2, 2, 1,-1,-2,-2,-1 };
    for (int i = 0; i < 8; i++) {
      int nf = f + kdf[i], nr = r + kdr[i];
      if (on_board(nf, nr)) KNIGHT_STEPS[sq].push_back(sq_of(nf, nr));
    }

    // King
    for (int df = -1; df <= 1; df++) {
      for (int dr = -1; dr <= 1; dr++) {
        if (df == 0 && dr == 0) continue;
        int nf = f + df, nr = r + dr;
        if (on_board(nf, nr)) KING_STEPS[sq].push_back(sq_of(nf, nr));
      }
    }

    // Pawn attacks FROM sq
    // White pawns attack to (f-1, r+1) and (f+1, r+1)
    if (on_board(f - 1, r + 1)) PAWN_ATTACKS[WHITE][sq].push_back(sq_of(f - 1, r + 1));
    if (on_board(f + 1, r + 1)) PAWN_ATTACKS[WHITE][sq].push_back(sq_of(f + 1, r + 1));
    // Black pawns attack to (f-1, r-1) and (f+1, r-1)
    if (on_board(f - 1, r - 1)) PAWN_ATTACKS[BLACK][sq].push_back(sq_of(f - 1, r - 1));
    if (on_board(f + 1, r - 1)) PAWN_ATTACKS[BLACK][sq].push_back(sq_of(f + 1, r - 1));
  }
}

struct Move {
  int from = 0;
  int to = 0;
  int promo = EMPTY;      // QUEEN/ROOK/BISHOP/KNIGHT or EMPTY
  uint8_t flags = 0;      // bitflags

  // flags
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

struct Undo {
  int8_t captured = 0;
  int castling = 0;
  int ep = -1;
  int halfmove = 0;
  int fullmove = 1;

  int ep_cap_sq = -1;
  int8_t ep_cap_piece = 0;
};

struct Board {
  array<int8_t, 64> sq{};
  Color side = WHITE;
  int castling = 0; // 1=K,2=Q,4=k,8=q
  int ep = -1;      // en passant target square or -1
  int halfmove = 0;
  int fullmove = 1;

  Board() { sq.fill(0); }

  void set_startpos() { set_fen(START_FEN); }

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

    // placement: ranks 8->1
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

    if (eps != "-") ep = str_to_sq(eps);
    else ep = -1;

    return true;
  }

  int find_king(Color c) const {
    int8_t k = (int8_t)(c == WHITE ? KING : -KING);
    for (int i = 0; i < 64; i++) if (sq[i] == k) return i;
    return -1;
  }

  bool is_square_attacked(int target, Color by) const {
    // pawns: check if any pawn of 'by' attacks target
    // easier: scan pawns and use pawn attack table, but we can reverse-check:
    // We'll do reverse-check to avoid scanning all pawns.
    int tf = file_of(target), tr = rank_of(target);

    if (by == WHITE) {
      // white pawn from (tf-1,tr-1) or (tf+1,tr-1) attacks target
      int r = tr - 1;
      if (r >= 0) {
        if (tf - 1 >= 0) {
          int s = sq_of(tf - 1, r);
          if (sq[s] == (int8_t)PAWN) return true;
        }
        if (tf + 1 < 8) {
          int s = sq_of(tf + 1, r);
          if (sq[s] == (int8_t)PAWN) return true;
        }
      }
    } else {
      // black pawn from (tf-1,tr+1) or (tf+1,tr+1)
      int r = tr + 1;
      if (r < 8) {
        if (tf - 1 >= 0) {
          int s = sq_of(tf - 1, r);
          if (sq[s] == (int8_t)(-PAWN)) return true;
        }
        if (tf + 1 < 8) {
          int s = sq_of(tf + 1, r);
          if (sq[s] == (int8_t)(-PAWN)) return true;
        }
      }
    }

    // knights
    for (int s : KNIGHT_STEPS[target]) {
      int8_t p = sq[s];
      if (p == 0) continue;
      if (abs_piece(p) == KNIGHT && color_of(p) == by) return true;
    }

    // king
    for (int s : KING_STEPS[target]) {
      int8_t p = sq[s];
      if (p == 0) continue;
      if (abs_piece(p) == KING && color_of(p) == by) return true;
    }

    // sliders
    // rook/queen lines: N,S,E,W
    static const int dR[4][2] = { {0,1},{0,-1},{1,0},{-1,0} };
    for (auto &d : dR) {
      int f = tf + d[0], r = tr + d[1];
      while (on_board(f, r)) {
        int s = sq_of(f, r);
        int8_t p = sq[s];
        if (p != 0) {
          if (color_of(p) == by) {
            int ap = abs_piece(p);
            if (ap == ROOK || ap == QUEEN) return true;
          }
          break;
        }
        f += d[0]; r += d[1];
      }
    }

    // bishop/queen diagonals: NE,NW,SE,SW
    static const int dB[4][2] = { {1,1},{-1,1},{1,-1},{-1,-1} };
    for (auto &d : dB) {
      int f = tf + d[0], r = tr + d[1];
      while (on_board(f, r)) {
        int s = sq_of(f, r);
        int8_t p = sq[s];
        if (p != 0) {
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
    // King move
    if (abs_piece(moved) == KING) {
      if (color_of(moved) == WHITE) castling &= ~(1 | 2);
      else castling &= ~(4 | 8);
    }

    // Rook move from original squares
    if (abs_piece(moved) == ROOK) {
      if (from == 0) castling &= ~2;      // a1
      else if (from == 7) castling &= ~1; // h1
      else if (from == 56) castling &= ~8;// a8
      else if (from == 63) castling &= ~4;// h8
    }

    // Rook captured on original squares
    if (captured != 0 && abs_piece(captured) == ROOK) {
      if (to == 0) castling &= ~2;
      else if (to == 7) castling &= ~1;
      else if (to == 56) castling &= ~8;
      else if (to == 63) castling &= ~4;
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

    int8_t moved = sq[m.from];
    int8_t captured = sq[m.to];

    // halfmove clock
    if (abs_piece(moved) == PAWN || (m.flags & Move::CAPTURE)) halfmove = 0;
    else halfmove++;

    // fullmove increments after black move
    if (side == BLACK) fullmove++;

    // clear ep by default
    ep = -1;

    // special: en passant capture
    if (m.flags & Move::ENPASSANT) {
      // target square is empty; captured pawn behind it
      int capSq = (side == WHITE) ? (m.to - 8) : (m.to + 8);
      u.ep_cap_sq = capSq;
      u.ep_cap_piece = sq[capSq];
      sq[capSq] = 0;
      captured = u.ep_cap_piece;
    }

    // move piece
    sq[m.to] = moved;
    sq[m.from] = 0;

    // promotion
    if (m.promo != EMPTY) {
      int8_t p = (int8_t)(side == WHITE ? m.promo : -m.promo);
      sq[m.to] = p;
    }

    // castling rook move
    if (m.flags & Move::CASTLE) {
      // king has moved already; move rook accordingly
      if (m.to == 6) { // white king side e1->g1
        sq[5] = sq[7];
        sq[7] = 0;
      } else if (m.to == 2) { // white queen side e1->c1
        sq[3] = sq[0];
        sq[0] = 0;
      } else if (m.to == 62) { // black king side e8->g8
        sq[61] = sq[63];
        sq[63] = 0;
      } else if (m.to == 58) { // black queen side e8->c8
        sq[59] = sq[56];
        sq[56] = 0;
      }
    }

    // update castling rights
    update_castling_rights_on_move(m.from, m.to, moved, captured);

    // set ep square on double pawn push
    if (m.flags & Move::DOUBLEP) {
      ep = (side == WHITE) ? (m.from + 8) : (m.from - 8);
    }

    // toggle side
    side = (side == WHITE ? BLACK : WHITE);
  }

  void unmake_move(const Move& m, const Undo& u) {
    // toggle side back
    side = (side == WHITE ? BLACK : WHITE);

    castling = u.castling;
    ep = u.ep;
    halfmove = u.halfmove;
    fullmove = u.fullmove;

    // undo castling rook move
    if (m.flags & Move::CASTLE) {
      if (m.to == 6) { // white king side
        sq[7] = sq[5];
        sq[5] = 0;
      } else if (m.to == 2) { // white queen side
        sq[0] = sq[3];
        sq[3] = 0;
      } else if (m.to == 62) { // black king side
        sq[63] = sq[61];
        sq[61] = 0;
      } else if (m.to == 58) { // black queen side
        sq[56] = sq[59];
        sq[59] = 0;
      }
    }

    int8_t movedBack = sq[m.to];

    // undo promotion: restore pawn
    if (m.promo != EMPTY) {
      movedBack = (int8_t)(side == WHITE ? PAWN : -PAWN);
    }

    sq[m.from] = movedBack;
    sq[m.to] = u.captured;

    // undo en passant: restore captured pawn
    if (m.flags & Move::ENPASSANT) {
      sq[m.to] = 0; // target square was empty
      sq[u.ep_cap_sq] = u.ep_cap_piece;
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
      if (p == 0) continue;
      if (color_of(p) != us) continue;

      int pt = abs_piece(p);
      int f = file_of(from), r = rank_of(from);

      if (pt == PAWN) {
        if (us == WHITE) {
          int one = from + 8;
          if (one < 64 && sq[one] == 0) {
            if (rank_of(one) == 7) {
              add_promotion_moves(out, from, one, 0);
            } else {
              Move m{from, one, EMPTY, 0};
              out.push_back(m);
              // double push
              if (r == 1) {
                int two = from + 16;
                if (sq[two] == 0) {
                  Move dm{from, two, EMPTY, Move::DOUBLEP};
                  out.push_back(dm);
                }
              }
            }
          }
          // captures
          for (int to : PAWN_ATTACKS[WHITE][from]) {
            if (sq[to] != 0 && color_of(sq[to]) == them) {
              uint8_t fl = Move::CAPTURE;
              if (rank_of(to) == 7) add_promotion_moves(out, from, to, fl);
              else out.push_back(Move{from, to, EMPTY, fl});
            }
            // en passant
            if (to == ep) {
              out.push_back(Move{from, to, EMPTY, (uint8_t)(Move::ENPASSANT | Move::CAPTURE)});
            }
          }
        } else { // BLACK
          int one = from - 8;
          if (one >= 0 && sq[one] == 0) {
            if (rank_of(one) == 0) {
              add_promotion_moves(out, from, one, 0);
            } else {
              out.push_back(Move{from, one, EMPTY, 0});
              if (r == 6) {
                int two = from - 16;
                if (sq[two] == 0) {
                  out.push_back(Move{from, two, EMPTY, Move::DOUBLEP});
                }
              }
            }
          }
          for (int to : PAWN_ATTACKS[BLACK][from]) {
            if (sq[to] != 0 && color_of(sq[to]) == them) {
              uint8_t fl = Move::CAPTURE;
              if (rank_of(to) == 0) add_promotion_moves(out, from, to, fl);
              else out.push_back(Move{from, to, EMPTY, fl});
            }
            if (to == ep) {
              out.push_back(Move{from, to, EMPTY, (uint8_t)(Move::ENPASSANT | Move::CAPTURE)});
            }
          }
        }
      }

      else if (pt == KNIGHT) {
        for (int to : KNIGHT_STEPS[from]) {
          int8_t q = sq[to];
          if (q == 0) out.push_back(Move{from, to, EMPTY, 0});
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
            if (sq[to] == 0) {
              out.push_back(Move{from, to, EMPTY, 0});
            } else {
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
          if (q == 0) out.push_back(Move{from, to, EMPTY, 0});
          else if (color_of(q) == them) out.push_back(Move{from, to, EMPTY, Move::CAPTURE});
        }

        // Castling
        if (us == WHITE) {
          bool inC = in_check(WHITE);
          if (!inC) {
            // King side: squares f1(5), g1(6) empty, not attacked; rook h1(7) present
            if ((castling & 1) && sq[5] == 0 && sq[6] == 0 && sq[7] == (int8_t)ROOK) {
              if (!is_square_attacked(5, BLACK) && !is_square_attacked(6, BLACK)) {
                out.push_back(Move{4, 6, EMPTY, Move::CASTLE});
              }
            }
            // Queen side: squares d1(3), c1(2), b1(1) empty; not attacked d1,c1; rook a1(0)
            if ((castling & 2) && sq[3] == 0 && sq[2] == 0 && sq[1] == 0 && sq[0] == (int8_t)ROOK) {
              if (!is_square_attacked(3, BLACK) && !is_square_attacked(2, BLACK)) {
                out.push_back(Move{4, 2, EMPTY, Move::CASTLE});
              }
            }
          }
        } else {
          bool inC = in_check(BLACK);
          if (!inC) {
            // King side: f8(61), g8(62) empty; rook h8(63)
            if ((castling & 4) && sq[61] == 0 && sq[62] == 0 && sq[63] == (int8_t)(-ROOK)) {
              if (!is_square_attacked(61, WHITE) && !is_square_attacked(62, WHITE)) {
                out.push_back(Move{60, 62, EMPTY, Move::CASTLE});
              }
            }
            // Queen side: d8(59), c8(58), b8(57) empty; rook a8(56)
            if ((castling & 8) && sq[59] == 0 && sq[58] == 0 && sq[57] == 0 && sq[56] == (int8_t)(-ROOK)) {
              if (!is_square_attacked(59, WHITE) && !is_square_attacked(58, WHITE)) {
                out.push_back(Move{60, 58, EMPTY, Move::CASTLE});
              }
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
};

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

static void uci_position(Board& b, const string& line) {
  auto tok = split_ws(line);
  // position startpos [moves ...]
  // position fen <6 fields> [moves ...]
  if (tok.size() < 2) return;

  size_t i = 1;
  if (tok[i] == "startpos") {
    b.set_startpos();
    i++;
  } else if (tok[i] == "fen") {
    if (tok.size() < i + 7) return; // fen + 6 fields
    string fen;
    // fen fields are 6 tokens
    fen = tok[i+1] + " " + tok[i+2] + " " + tok[i+3] + " " + tok[i+4] + " " + tok[i+5] + " " + tok[i+6];
    b.set_fen(fen);
    i += 7;
  } else {
    return;
  }

  if (i < tok.size() && tok[i] == "moves") {
    i++;
    for (; i < tok.size(); i++) {
      apply_uci_move(b, tok[i]);
    }
  }
}

static int parse_go_movetime_ms(const string& line, int fallback = 200) {
  auto tok = split_ws(line);
  for (size_t i = 0; i + 1 < tok.size(); i++) {
    if (tok[i] == "movetime") {
      try { return stoi(tok[i+1]); } catch (...) { return fallback; }
    }
  }
  return fallback;
}

int main(int argc, char** argv) {
  ios::sync_with_stdio(false);
  cin.tie(nullptr);
  cout.setf(std::ios::unitbuf);

  init_attack_tables();

  // CLI mode for perft:
  // ./bm_engine perft 5
  // ./bm_engine perftfen "<fen>" 5
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
    }
    else if (line.rfind("position", 0) == 0) {
      uci_position(board, line);
    }
    else if (line.rfind("go", 0) == 0) {
      // por ahora: devolvemos una jugada legal (sin search). Search viene después de perft.
      int movetime = parse_go_movetime_ms(line, 200);
      (void)movetime;

      vector<Move> moves;
      board.gen_legal(moves);
      if (moves.empty()) {
        cout << "bestmove 0000\n";
      } else {
        cout << "bestmove " << move_to_uci(moves[0]) << "\n";
      }
    }
    else if (line.rfind("perft ", 0) == 0) {
      // comando debug no-UCI: "perft 4"
      int depth = stoi(line.substr(6));
      Board b = board;
      uint64_t nodes = perft(b, depth);
      cout << "info string perft(" << depth << ")=" << nodes << "\n";
    }
    else if (line == "quit") {
      break;
    }
  }

  return 0;
}
