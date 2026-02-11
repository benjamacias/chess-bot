#include "opening_book.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <sstream>
#include <random>

namespace {

using BookTable = std::unordered_map<std::string, std::vector<BookCandidate>>;

static std::string make_key(const std::vector<std::string>& moves) {
  std::string key;
  for (size_t i = 0; i < moves.size(); ++i) {
    if (i) key += ' ';
    key += moves[i];
  }
  return key;
}

// Pesos estandarizados para mejor legibilidad
constexpr int MAIN_LINE = 100;   // Línea principal del repertorio
constexpr int GOOD_ALT = 70;     // Alternativa sólida
constexpr int PLAYABLE = 40;     // Jugable pero no preferida
constexpr int SURPRISE = 20;     // Arma sorpresa ocasional

static const BookTable& opening_book_table() {
  static const BookTable table = {
      // ========================================
      // APERTURA INICIAL
      // ========================================
      {"", {{"e2e4", MAIN_LINE}}},

      // ========================================
      // REPERTORIO BLANCAS: 1.e4
      // ========================================
      
      // 1.e4 - Respuestas principales
      {"e2e4", {
        {"c7c5", PLAYABLE},   // Siciliana (35%)
        {"e7e5", PLAYABLE},   // King's Pawn (30%)
        {"c7c6", GOOD_ALT},   // Caro-Kann (15%)
        {"e7e6", PLAYABLE},   // Francesa (10%)
        {"g7g6", SURPRISE}    // Moderna/Pirc (5%)
      }},

      // ----------------------------------------
      // CONTRA CARO-KANN (1.e4 c6)
      // ----------------------------------------
      {"e2e4 c7c6", {
        {"d2d4", MAIN_LINE},
        {"b1c3", GOOD_ALT},
        {"g1f3", PLAYABLE}
      }},

      // Línea principal: 2.d4 d5
      {"e2e4 c7c6 d2d4", {{"d7d5", MAIN_LINE}}},
      {"e2e4 c7c6 d2d4 d7d5", {
        {"b1c3", MAIN_LINE},    // Exchange variation
        {"e4e5", GOOD_ALT}      // Advance variation
      }},

      // ADVANCE VARIATION: 3.e5
      {"e2e4 c7c6 d2d4 d7d5 e4e5", {
        {"c8f5", MAIN_LINE},
        {"c8g4", GOOD_ALT}
      }},
      
      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8f5", {
        {"f1e2", MAIN_LINE},
        {"b1d2", GOOD_ALT},
        {"g1f3", PLAYABLE}
      }},
      
      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 f1e2", {
        {"e7e6", MAIN_LINE},
        {"g8f6", GOOD_ALT},
        {"h7h5", PLAYABLE}
      }},
      
      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 f1e2 e7e6", {
        {"g1f3", MAIN_LINE},
        {"h2h4", GOOD_ALT}
      }},

      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8g4", {
        {"f1e2", MAIN_LINE},
        {"g1f3", GOOD_ALT}
      }},
      
      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8g4 f1e2", {{"g4e2", MAIN_LINE}}},
      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8g4 f1e2 g4e2", {
        {"d1e2", MAIN_LINE},
        {"g1e2", PLAYABLE}
      }},

      // EXCHANGE VARIATION: 3.exd5 o 3.Nc3
      {"e2e4 c7c6 d2d4 d7d5 b1c3", {{"d5e4", MAIN_LINE}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4", {{"c3e4", MAIN_LINE}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4", {
        {"c8f5", MAIN_LINE},
        {"g8f6", GOOD_ALT}
      }},

      // PANOV-BOTVINNIK: 3.exd5 cxd5 4.c4
      {"e2e4 c7c6 d2d4 d7d5 e4d5", {{"c6d5", MAIN_LINE}}},
      {"e2e4 c7c6 d2d4 d7d5 e4d5 c6d5", {
        {"c2c4", MAIN_LINE},
        {"b1c3", GOOD_ALT}
      }},

      // ----------------------------------------
      // CONTRA SICILIANA (1.e4 c5)
      // ----------------------------------------
      {"e2e4 c7c5", {
        {"g1f3", MAIN_LINE},
        {"c2c3", GOOD_ALT},    // Alapin - muy sólida para evitar teoría
        {"b1c3", PLAYABLE}      // Closed Sicilian
      }},

      // ALAPIN: 2.c3 (Recomendada para evitar teoría)
      {"e2e4 c7c5 c2c3", {
        {"d7d5", MAIN_LINE},
        {"g8f6", GOOD_ALT},
        {"b8c6", PLAYABLE}
      }},
      
      {"e2e4 c7c5 c2c3 d7d5", {
        {"e4d5", MAIN_LINE},
        {"e4e5", PLAYABLE}
      }},
      
      {"e2e4 c7c5 c2c3 d7d5 e4d5", {{"d8d5", MAIN_LINE}}},
      {"e2e4 c7c5 c2c3 d7d5 e4d5 d8d5", {
        {"d2d4", MAIN_LINE},
        {"g1f3", GOOD_ALT}
      }},
      
      {"e2e4 c7c5 c2c3 g8f6", {{"e4e5", MAIN_LINE}}},
      {"e2e4 c7c5 c2c3 g8f6 e4e5", {{"f6d5", MAIN_LINE}}},
      {"e2e4 c7c5 c2c3 g8f6 e4e5 f6d5", {
        {"d2d4", MAIN_LINE},
        {"g1f3", GOOD_ALT}
      }},

      // OPEN SICILIAN: 2.Nf3
      {"e2e4 c7c5 g1f3", {
        {"d7d6", MAIN_LINE},
        {"b8c6", GOOD_ALT},
        {"e7e6", PLAYABLE}
      }},
      
      {"e2e4 c7c5 g1f3 d7d6", {
        {"d2d4", MAIN_LINE},
        {"f1b5", PLAYABLE}  // Moscow variation
      }},
      
      {"e2e4 c7c5 g1f3 b8c6", {
        {"d2d4", MAIN_LINE},
        {"f1b5", GOOD_ALT}  // Rossolimo
      }},

      // ----------------------------------------
      // CONTRA FRANCESA (1.e4 e6)
      // ----------------------------------------
      {"e2e4 e7e6", {
        {"d2d4", MAIN_LINE},
        {"g1f3", PLAYABLE}
      }},
      
      {"e2e4 e7e6 d2d4", {{"d7d5", MAIN_LINE}}},
      {"e2e4 e7e6 d2d4 d7d5", {
        {"b1c3", MAIN_LINE},    // Classical
        {"e4e5", GOOD_ALT},     // Advance
        {"e4d5", PLAYABLE}      // Exchange
      }},

      // ADVANCE FRANCESA: 3.e5
      {"e2e4 e7e6 d2d4 d7d5 e4e5", {{"c7c5", MAIN_LINE}}},
      {"e2e4 e7e6 d2d4 d7d5 e4e5 c7c5", {
        {"c2c3", MAIN_LINE},
        {"g1f3", GOOD_ALT}
      }},
      
      {"e2e4 e7e6 d2d4 d7d5 e4e5 c7c5 c2c3", {
        {"b8c6", MAIN_LINE},
        {"d8b6", GOOD_ALT}
      }},

      // CLASSICAL FRANCESA: 3.Nc3
      {"e2e4 e7e6 d2d4 d7d5 b1c3", {
        {"g8f6", MAIN_LINE},
        {"f8b4", GOOD_ALT},
        {"d5e4", PLAYABLE}
      }},

      // ----------------------------------------
      // CONTRA 1...e5 (ITALIAN/RUY LOPEZ)
      // ----------------------------------------
      {"e2e4 e7e5", {{"g1f3", MAIN_LINE}}},
      {"e2e4 e7e5 g1f3", {
        {"b8c6", MAIN_LINE},
        {"g8f6", PLAYABLE}  // Petrov
      }},

      // ITALIAN GAME: 3.Bc4
      {"e2e4 e7e5 g1f3 b8c6", {
        {"f1c4", MAIN_LINE},
        {"f1b5", GOOD_ALT}  // Ruy Lopez
      }},
      
      {"e2e4 e7e5 g1f3 b8c6 f1c4", {
        {"g8f6", MAIN_LINE},    // Two Knights
        {"f8c5", GOOD_ALT}      // Giuoco Piano
      }},

      // GIUOCO PIANO: 3...Bc5
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5", {
        {"c2c3", MAIN_LINE},    // Main line
        {"d2d3", GOOD_ALT},     // Giuoco Pianissimo
        {"b2b4", SURPRISE}      // Evans Gambit
      }},
      
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3", {
        {"g8f6", MAIN_LINE},
        {"d8e7", GOOD_ALT}
      }},
      
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6", {
        {"d2d4", MAIN_LINE},
        {"d2d3", PLAYABLE}
      }},

      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 d2d3", {
        {"g8f6", MAIN_LINE},
        {"d7d6", GOOD_ALT}
      }},

      // TWO KNIGHTS: 3...Nf6
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6", {
        {"d2d3", MAIN_LINE},    // Quiet Italian
        {"d2d4", GOOD_ALT},     // Scotch Gambit
        {"e1g1", PLAYABLE}      // Immediate castle
      }},
      
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3", {
        {"f8c5", MAIN_LINE},
        {"f8e7", GOOD_ALT},
        {"h7h6", PLAYABLE}
      }},
      
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8c5", {
        {"c2c3", MAIN_LINE},
        {"e1g1", GOOD_ALT}
      }},

      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d4", {{"e5d4", MAIN_LINE}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d4 e5d4", {
        {"e1g1", MAIN_LINE},
        {"f3d4", GOOD_ALT}
      }},

      // RUY LOPEZ: 3.Bb5
      {"e2e4 e7e5 g1f3 b8c6 f1b5", {
        {"a7a6", MAIN_LINE},
        {"g8f6", GOOD_ALT}
      }},
      
      {"e2e4 e7e5 g1f3 b8c6 f1b5 a7a6", {
        {"b5a4", MAIN_LINE},
        {"b5c6", PLAYABLE}  // Exchange variation
      }},

      // PETROV: 2...Nf6
      {"e2e4 e7e5 g1f3 g8f6", {
        {"f3e5", MAIN_LINE},
        {"d2d4", PLAYABLE}
      }},

      // ========================================
      // REPERTORIO NEGRAS vs 1.d4
      // ========================================
      
      {"d2d4", {
        {"d7d5", MAIN_LINE},
        {"g8f6", GOOD_ALT}
      }},

      // ----------------------------------------
      // QUEEN'S GAMBIT DECLINED
      // ----------------------------------------
      {"d2d4 d7d5", {
        {"c2c4", MAIN_LINE},
        {"g1f3", GOOD_ALT},
        {"c1f4", PLAYABLE}  // London System
      }},
      
      {"d2d4 d7d5 c2c4", {
        {"e7e6", MAIN_LINE},    // QGD/Semi-Slav
        {"c7c6", GOOD_ALT},     // Slav
        {"g8f6", PLAYABLE}
      }},

      // SEMI-SLAV: 1.d4 d5 2.c4 e6 3.Nc3 Nf6 4.Nf3 c6
      {"d2d4 d7d5 c2c4 e7e6", {
        {"b1c3", MAIN_LINE},
        {"g1f3", GOOD_ALT}
      }},
      
      {"d2d4 d7d5 c2c4 e7e6 b1c3", {
        {"g8f6", MAIN_LINE},
        {"f8e7", GOOD_ALT}
      }},
      
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6", {
        {"g1f3", MAIN_LINE},
        {"c1g5", GOOD_ALT}
      }},
      
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3", {
        {"c7c6", MAIN_LINE},    // Semi-Slav proper
        {"f8e7", GOOD_ALT}      // QGD
      }},

      // SEMI-SLAV CONTINUATIONS
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 c7c6", {
        {"e2e3", MAIN_LINE},
        {"c1g5", GOOD_ALT},
        {"c4d5", PLAYABLE}
      }},
      
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 c7c6 e2e3", {
        {"b8d7", MAIN_LINE},
        {"a7a6", GOOD_ALT}
      }},

      // QGD ORTHODOX
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 f8e7", {
        {"c1f4", MAIN_LINE},
        {"c1g5", GOOD_ALT}
      }},

      // ----------------------------------------
      // SLAV DEFENSE: 1.d4 d5 2.c4 c6
      // ----------------------------------------
      {"d2d4 d7d5 c2c4 c7c6", {
        {"b1c3", MAIN_LINE},
        {"g1f3", GOOD_ALT}
      }},
      
      {"d2d4 d7d5 c2c4 c7c6 b1c3", {
        {"g8f6", MAIN_LINE},
        {"d5c4", GOOD_ALT}
      }},
      
      {"d2d4 d7d5 c2c4 c7c6 b1c3 g8f6", {
        {"g1f3", MAIN_LINE},
        {"e2e3", GOOD_ALT}
      }},

      // ----------------------------------------
      // INDIAN DEFENSES: 1.d4 Nf6
      // ----------------------------------------
      {"d2d4 g8f6", {
        {"c2c4", MAIN_LINE},
        {"g1f3", GOOD_ALT},
        {"c1f4", PLAYABLE}  // London
      }},
      
      {"d2d4 g8f6 c2c4", {
        {"e7e6", MAIN_LINE},    // QGD/Nimzo
        {"g7g6", GOOD_ALT},     // King's Indian
        {"e7e5", PLAYABLE}      // Budapest
      }},

      // QGD VIA 1.d4 Nf6
      {"d2d4 g8f6 c2c4 e7e6", {
        {"g1f3", MAIN_LINE},
        {"b1c3", GOOD_ALT}
      }},
      
      {"d2d4 g8f6 c2c4 e7e6 g1f3", {
        {"d7d5", MAIN_LINE},
        {"f8b4", GOOD_ALT}  // Nimzo-Indian
      }},
      
      {"d2d4 g8f6 c2c4 e7e6 g1f3 d7d5", {
        {"b1c3", MAIN_LINE},
        {"c1g5", GOOD_ALT}
      }},

      // NIMZO-INDIAN: 1.d4 Nf6 2.c4 e6 3.Nc3 Bb4
      {"d2d4 g8f6 c2c4 e7e6 b1c3", {
        {"f8b4", MAIN_LINE},
        {"d7d5", GOOD_ALT}
      }},

      // ----------------------------------------
      // LONDON SYSTEM (muy común a nivel club)
      // ----------------------------------------
      {"d2d4 d7d5 g1f3", {
        {"g8f6", MAIN_LINE},
        {"c7c6", GOOD_ALT}
      }},
      
      {"d2d4 d7d5 g1f3 g8f6", {
        {"c1f4", MAIN_LINE},
        {"c2c4", GOOD_ALT}
      }},
      
      {"d2d4 d7d5 g1f3 g8f6 c1f4", {
        {"c7c5", MAIN_LINE},
        {"e7e6", GOOD_ALT},
        {"c8f5", PLAYABLE}
      }},

      {"d2d4 g8f6 c1f4", {
        {"d7d5", MAIN_LINE},
        {"e7e6", GOOD_ALT},
        {"c7c5", PLAYABLE}
      }},

      {"d2d4 g8f6 g1f3", {
        {"d7d5", MAIN_LINE},
        {"e7e6", GOOD_ALT},
        {"g7g6", PLAYABLE}
      }},

      // ========================================
      // REPERTORIO NEGRAS vs 1.c4 (ENGLISH)
      // ========================================
      
      {"c2c4", {
        {"e7e5", MAIN_LINE},    // Reverse Sicilian
        {"g8f6", GOOD_ALT},
        {"c7c5", PLAYABLE}      // Symmetrical
      }},

      // REVERSED SICILIAN: 1.c4 e5
      {"c2c4 e7e5", {
        {"g1f3", MAIN_LINE},
        {"b1c3", GOOD_ALT}
      }},
      
      {"c2c4 e7e5 g1f3", {
        {"b8c6", MAIN_LINE},
        {"g8f6", GOOD_ALT}
      }},
      
      {"c2c4 e7e5 b1c3", {
        {"g8f6", MAIN_LINE},
        {"b8c6", GOOD_ALT}
      }},

      // TRANSPOSITIONS TO QGD
      {"c2c4 e7e6", {
        {"d2d4", MAIN_LINE},
        {"g1f3", GOOD_ALT}
      }},
      
      {"c2c4 e7e6 d2d4", {{"d7d5", MAIN_LINE}}},
      {"c2c4 e7e6 d2d4 d7d5", {
        {"b1c3", MAIN_LINE},
        {"g1f3", GOOD_ALT}
      }},

      // ========================================
      // APERTURAS NEUTRALES (1.Nf3)
      // ========================================
      
      {"g1f3", {
        {"d7d5", MAIN_LINE},
        {"g8f6", GOOD_ALT},
        {"c7c5", PLAYABLE}
      }},
      
      {"g1f3 d7d5", {
        {"d2d4", MAIN_LINE},
        {"c2c4", GOOD_ALT}
      }},
      
      {"g1f3 g8f6", {
        {"d2d4", MAIN_LINE},
        {"c2c4", GOOD_ALT}
      }},
      
      {"g1f3 d7d5 d2d4", {
        {"g8f6", MAIN_LINE},
        {"e7e6", GOOD_ALT}
      }},
      
      {"g1f3 d7d5 d2d4 g8f6", {
        {"c2c4", MAIN_LINE},
        {"e2e3", GOOD_ALT}
      }},
  };
  return table;
}

}  // namespace

static bool is_early_queen_move(const std::string& uci, std::size_t ply) {
  if (ply > 6 || uci.size() < 2) return false;
  return uci.rfind("d1", 0) == 0 || uci.rfind("d8", 0) == 0;
}

static int principle_bonus(const std::string& move, bool white_to_move, std::size_t ply) {
  int bonus = 0;

  if (white_to_move) {
    if (move == "e2e4") bonus += 40;
    else if (move == "d2d4") bonus += 36;
    else if (move == "g1f3") bonus += 28;
    else if (move == "b1c3") bonus += 24;
    else if (move == "f1c4") bonus += 20;
    else if (move == "f1b5") bonus += 18;
    else if (move == "c1g5") bonus += 14;
  } else {
    if (move == "e7e6") bonus += 34;
    else if (move == "c7c6") bonus += 33;
    else if (move == "d7d5") bonus += 32;
    else if (move == "g8f6") bonus += 24;
    else if (move == "c7c5") bonus -= 10;
  }

  if (is_early_queen_move(move, ply)) bonus -= 35;

  return bonus;
}

static int consistency_bonus(int weight, std::size_t prefix_ply, std::size_t current_ply) {
  const std::size_t deviation = (current_ply >= prefix_ply) ? (current_ply - prefix_ply) : 0;

  int bonus = 0;
  if (weight >= MAIN_LINE) bonus += 40;
  else if (weight >= GOOD_ALT) bonus += 20;
  else bonus += 8;

  // Preferir continuidad en líneas profundas y penalizar desviaciones del rival.
  bonus += static_cast<int>(prefix_ply * 2);
  bonus -= static_cast<int>(deviation * 12);

  return bonus;
}

std::optional<std::string> opening_book_pick(
    const std::vector<std::string>& move_history,
    const std::vector<std::string>& legal_moves_uci) {
  const auto& table = opening_book_table();

  const bool white_to_move = (move_history.size() % 2 == 0);
  const std::size_t ply = move_history.size();

  struct ScoredMove {
    std::string uci;
    int score;
  };

  std::vector<ScoredMove> legal_candidates;
  legal_candidates.reserve(16);

  auto score_candidates_for_prefix = [&](std::size_t prefix_len) {
    if ((prefix_len % 2) != (ply % 2)) return;
    const std::vector<std::string> prefix(move_history.begin(), move_history.begin() + static_cast<std::ptrdiff_t>(prefix_len));
    const auto it = table.find(make_key(prefix));
    if (it == table.end() || it->second.empty()) return;

    for (const auto& candidate : it->second) {
      if (candidate.weight <= 0) continue;
      if (std::find(legal_moves_uci.begin(), legal_moves_uci.end(), candidate.uci) != legal_moves_uci.end()) {
        const int score = candidate.weight + principle_bonus(candidate.uci, white_to_move, ply) +
                          consistency_bonus(candidate.weight, prefix_len, ply);
        legal_candidates.push_back({candidate.uci, score});
      }
    }
  };

  // 1) Intentar match exacto del historial.
  score_candidates_for_prefix(move_history.size());

  // 2) Si no hay match exacto legal, degradar por prefijo para tolerar micro-desvíos.
  if (legal_candidates.empty()) {
    for (std::size_t prefix_len = move_history.size(); prefix_len > 0; --prefix_len) {
      score_candidates_for_prefix(prefix_len - 1);
      if (!legal_candidates.empty()) break;
    }
  }

  if (legal_candidates.empty()) return std::nullopt;

  std::sort(legal_candidates.begin(), legal_candidates.end(), [](const ScoredMove& a, const ScoredMove& b) {
    if (a.score != b.score) return a.score > b.score;
    return a.uci < b.uci;
  });

  const int best_score = legal_candidates.front().score;
  std::vector<ScoredMove> shortlist;
  shortlist.reserve(legal_candidates.size());

  for (const auto& entry : legal_candidates) {
    if (entry.score < best_score - 25) break;
    shortlist.push_back(entry);
  }

  if (shortlist.size() == 1) return shortlist.front().uci;

  int total_weight = 0;
  for (const auto& entry : shortlist) {
    total_weight += std::max(1, entry.score - (best_score - 30));
  }

  static thread_local std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<int> dist(1, std::max(1, total_weight));
  int pick = dist(rng);

  for (const auto& entry : shortlist) {
    pick -= std::max(1, entry.score - (best_score - 30));
    if (pick <= 0) return entry.uci;
  }

  return shortlist.front().uci;
}

// ========================================
// FUNCIÓN DE VALIDACIÓN (OPCIONAL)
// ========================================
// Esta función puede ser llamada en debug mode para verificar
// que todas las líneas del libro son válidas.
// Requiere acceso a Board class, así que debería estar en main.cpp

/*
// Ejemplo de uso en main.cpp:

#include "opening_book.h"

bool validate_opening_book(Board& test_board) {
  const auto& table = opening_book_table();
  int errors = 0;
  
  for (const auto& [key, candidates] : table) {
    test_board.set_startpos();
    
    // Parse y ejecutar la secuencia de movimientos
    if (!key.empty()) {
      std::istringstream iss(key);
      std::string uci;
      while (iss >> uci) {
        // Buscar el movimiento en jugadas legales
        std::vector<Move> legal;
        test_board.gen_legal(legal);
        
        bool found = false;
        for (const auto& m : legal) {
          if (move_to_uci(m) == uci) {
            Undo u;
            test_board.make_move(m, u);
            found = true;
            break;
          }
        }
        
        if (!found) {
          std::cerr << "ERROR: Movimiento '" << uci 
                    << "' inválido en secuencia '" << key << "'\n";
          errors++;
          break;
        }
      }
    }
    
    // Verificar que los candidatos sean legales
    std::vector<Move> legal;
    test_board.gen_legal(legal);
    
    for (const auto& cand : candidates) {
      bool found = false;
      for (const auto& m : legal) {
        if (move_to_uci(m) == cand.uci) {
          found = true;
          break;
        }
      }
      
      if (!found) {
        std::cerr << "ERROR: Candidato '" << cand.uci 
                  << "' inválido después de '" << key << "'\n";
        errors++;
      }
    }
  }
  
  if (errors == 0) {
    std::cout << "✓ Opening book validation passed: " 
              << table.size() << " positions validated\n";
    return true;
  } else {
    std::cerr << "✗ Opening book validation failed with " 
              << errors << " errors\n";
    return false;
  }
}

// En main(), agregar:
// if (!validate_opening_book(board)) {
//   std::cerr << "Warning: Opening book has errors!\n";
// }
*/
