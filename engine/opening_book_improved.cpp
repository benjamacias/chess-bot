#include "opening_book.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct BookLine {
  int baseWeight;                 // 100 = línea principal, 70 = alternativa
  std::vector<std::string> pv;    // Secuencia completa UCI ply-by-ply
};

struct BookCandidatePlus {
  std::string uci;
  int weight;
  int contDepth;  // plies restantes en la línea si se juega esta jugada
};

using BookTable = std::unordered_map<std::string, std::vector<BookCandidatePlus>>;

constexpr int MAIN_LINE = 100;
constexpr int GOOD_ALT = 70;
constexpr int DECAY = 2;
constexpr int MIN_WEIGHT = 20;

static std::string make_key(const std::vector<std::string>& moves, std::size_t end) {
  std::string key;
  key.reserve(end * 5);
  for (std::size_t i = 0; i < end; ++i) {
    if (i > 0) key.push_back(' ');
    key += moves[i];
  }
  return key;
}

static const std::vector<BookLine>& book_lines() {
  // Para agregar una nueva línea: sumar una entrada BookLine con la PV completa
  // (desde startpos). build_book() compila automáticamente todos los prefijos.
  static const std::vector<BookLine> lines = {
      // Blancas: agresivo/clásico
      {MAIN_LINE, {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5", "c2c3", "g8f6", "d2d4", "e5d4", "c3d4", "c5b4", "b1c3", "f6e4"}},
      {GOOD_ALT,  {"e2e4", "e7e5", "g1f3", "b8c6", "d2d4", "e5d4", "f3d4", "g8f6", "b1c3", "f8b4", "d4c6", "b4c3", "b2c3", "b7c6"}},
      {MAIN_LINE, {"d2d4", "d7d5", "c2c4", "e7e6", "b1c3", "g8f6", "c1g5", "f8e7", "e2e3", "e8g8", "g1f3", "h7h6", "g5h4", "b7b6"}},

      // Negras sólidas vs 1.e4
      {MAIN_LINE, {"e2e4", "c7c6", "d2d4", "d7d5", "b1c3", "d5e4", "c3e4", "c8f5", "e4g3", "f5g6", "h2h4", "h7h6", "g1f3", "b8d7"}},
      {GOOD_ALT,  {"e2e4", "e7e6", "d2d4", "d7d5", "b1c3", "g8f6", "e4e5", "f6d7", "f2f4", "c7c5", "g1f3", "b8c6", "c1e3", "a7a6"}},

      // Negras sólidas vs 1.d4
      {MAIN_LINE, {"d2d4", "d7d5", "c2c4", "e7e6", "b1c3", "g8f6", "c1g5", "f8e7", "e2e3", "e8g8", "g1f3", "h7h6", "g5h4", "b7b6"}},
      {GOOD_ALT,  {"d2d4", "d7d5", "c2c4", "c7c6", "g1f3", "g8f6", "b1c3", "d5c4", "a2a4", "c8f5", "e2e3", "e7e6", "f1c4", "f8b4"}},
  };
  return lines;
}

static BookTable build_book() {
  BookTable table;

  for (const auto& line : book_lines()) {
    for (std::size_t i = 0; i < line.pv.size(); ++i) {
      const std::string key = make_key(line.pv, i);
      const std::string& next = line.pv[i];
      const int cont = static_cast<int>((line.pv.size() - 1) - i);
      const int rawWeight = line.baseWeight - static_cast<int>(i) * DECAY;
      const int weight = std::max(MIN_WEIGHT, rawWeight);

      auto& candidates = table[key];
      auto it = std::find_if(candidates.begin(), candidates.end(), [&](const BookCandidatePlus& c) {
        return c.uci == next;
      });

      if (it == candidates.end()) {
        candidates.push_back({next, weight, cont});
      } else {
        it->weight = std::max(it->weight, weight);
        it->contDepth = std::max(it->contDepth, cont);
      }
    }
  }

  return table;
}

static const BookTable& opening_book_table() {
  static const BookTable table = build_book();
  return table;
}

static int principles_bonus(const std::vector<std::string>& hist, const std::string& move) {
  const int ply = static_cast<int>(hist.size());
  const bool whiteToMove = (ply % 2 == 0);

  int bonus = 0;

  if (ply <= 10 && (move.rfind("d1", 0) == 0 || move.rfind("d8", 0) == 0)) {
    bonus -= 60;
  }

  if (ply <= 4 && (move == "a2a4" || move == "h2h4" || move == "a7a5" || move == "h7h5")) {
    bonus -= 25;
  }

  if (ply <= 10) {
    if (move == "e2e4") bonus += 45;
    else if (move == "d2d4") bonus += 42;
    else if (move == "g1f3") bonus += 36;
    else if (move == "b1c3") bonus += 32;
    else if (move == "f1c4") bonus += 30;
    else if (move == "f1b5") bonus += 28;
    else if (move == "c1g5") bonus += 28;

    else if (move == "c7c6") bonus += 38;
    else if (move == "e7e6") bonus += 36;
    else if (move == "d7d5") bonus += 36;
    else if (move == "g8f6") bonus += 30;
  }

  // Sesgo de estilo por color en turno
  if (whiteToMove) {
    if (move == "e2e4" || move == "d2d4") bonus += 10;
    if (move == "g1f3" || move == "b1c3" || move == "f1c4") bonus += 6;
  } else {
    if (move == "c7c6" || move == "e7e6" || move == "d7d5" || move == "g8f6") bonus += 8;
    if (move == "c7c5") bonus -= 8;
  }

  return bonus;
}

}  // namespace

std::optional<std::string> opening_book_pick(
    const std::vector<std::string>& move_history,
    const std::vector<std::string>& legal_moves_uci) {
  // Score final determinista:
  // total = weight + principles_bonus(history, move) + contDepth*2
  // desempate: UCI lexicográficamente menor.
  const auto& table = opening_book_table();
  const std::string key = make_key(move_history, move_history.size());

  const auto it = table.find(key);
  if (it == table.end() || it->second.empty() || legal_moves_uci.empty()) {
    return std::nullopt;
  }

  std::unordered_set<std::string> legal(legal_moves_uci.begin(), legal_moves_uci.end());

  bool found = false;
  std::string bestMove;
  int bestScore = 0;

  for (const auto& cand : it->second) {
    if (!legal.count(cand.uci)) continue;

    const int total = cand.weight + principles_bonus(move_history, cand.uci) + cand.contDepth * 2;
    if (!found || total > bestScore || (total == bestScore && cand.uci < bestMove)) {
      found = true;
      bestScore = total;
      bestMove = cand.uci;
    }
  }

  if (!found) return std::nullopt;
  return bestMove;
}
