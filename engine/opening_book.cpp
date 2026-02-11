#include "opening_book.h"

#include <algorithm>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

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

static const BookTable& opening_book_table() {
  static const BookTable table = {
      {"", {{"e2e4", 60}, {"d2d4", 40}}},

      // Repertorio blancas: 1.e4
      {"e2e4", {{"c7c6", 40}, {"e7e5", 35}, {"c7c5", 25}}},
      {"e2e4 c7c6", {{"d2d4", 60}, {"g1f3", 40}}},
      {"e2e4 c7c6 d2d4", {{"d7d5", 85}, {"g8f6", 15}}},
      {"e2e4 c7c6 d2d4 d7d5", {{"e4e5", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 e4e5", {{"c8f5", 55}, {"c8g4", 45}}},
      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8f5", {{"g1f3", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 g1f3", {{"e7e6", 60}, {"e7e5", 40}}},

      // Italiano / e4 e5
      {"e2e4 e7e5", {{"g1f3", 100}}},
      {"e2e4 e7e5 g1f3", {{"b8c6", 80}, {"g8f6", 20}}},
      {"e2e4 e7e5 g1f3 b8c6", {{"f1c4", 90}, {"d2d4", 10}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4", {{"g8f6", 65}, {"f8c5", 35}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6", {{"d2d3", 50}, {"d2d4", 50}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5", {{"c2c3", 60}, {"d2d3", 40}}},

      // Siciliana anti-open
      {"e2e4 c7c5", {{"g1f3", 55}, {"c2c3", 45}}},
      {"e2e4 c7c5 g1f3", {{"d7d6", 60}, {"b8c6", 40}}},
      {"e2e4 c7c5 c2c3", {{"d7d5", 70}, {"g8f6", 30}}},

      // Repertorio negras vs 1.d4 (QGD/Semi-Slav)
      {"d2d4", {{"d7d5", 80}, {"g8f6", 20}}},
      {"d2d4 d7d5", {{"c2c4", 75}, {"g1f3", 25}}},
      {"d2d4 d7d5 c2c4", {{"e7e6", 70}, {"c7c6", 30}}},
      {"d2d4 d7d5 c2c4 e7e6", {{"b1c3", 55}, {"g1f3", 45}}},
      {"d2d4 d7d5 c2c4 e7e6 b1c3", {{"g8f6", 80}, {"f8e7", 20}}},
      {"d2d4 d7d5 c2c4 c7c6", {{"b1c3", 60}, {"g1f3", 40}}},
      {"d2d4 d7d5 c2c4 c7c6 b1c3", {{"g8f6", 100}}},

      // Transposiciones comunes QGD
      {"d2d4 g8f6", {{"c2c4", 80}, {"g1f3", 20}}},
      {"d2d4 g8f6 c2c4", {{"e7e6", 70}, {"g7g6", 30}}},
      {"d2d4 g8f6 c2c4 e7e6", {{"g1f3", 60}, {"b1c3", 40}}},
      {"d2d4 g8f6 c2c4 e7e6 g1f3", {{"d7d5", 75}, {"b7b6", 25}}},

      // Transposición invertida: 1.c4 e6 2.d4 d5
      {"c2c4", {{"e7e5", 40}, {"e7e6", 35}, {"c7c5", 25}}},
      {"c2c4 e7e6", {{"d2d4", 80}, {"g1f3", 20}}},
      {"c2c4 e7e6 d2d4", {{"d7d5", 75}, {"g8f6", 25}}},
      {"c2c4 e7e6 d2d4 d7d5", {{"b1c3", 55}, {"g1f3", 45}}},

      // Repertorio negras vs 1.e4 adicional
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3", {{"f8c5", 70}, {"h7h6", 30}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d4", {{"e5d4", 85}, {"f8c5", 15}}},
      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8g4", {{"f1e2", 65}, {"g1f3", 35}}},
      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8g4 f1e2", {{"g4e2", 100}}},

      // Posiciones neutrales para mantener libro 20-50+
      {"g1f3", {{"d7d5", 50}, {"g8f6", 50}}},
      {"g1f3 d7d5", {{"d2d4", 65}, {"c2c4", 35}}},
      {"g1f3 g8f6", {{"d2d4", 60}, {"c2c4", 40}}},
      {"d2d4 d7d5 g1f3", {{"g8f6", 70}, {"e7e6", 30}}},
      {"d2d4 d7d5 g1f3 g8f6", {{"c2c4", 80}, {"e2e3", 20}}},
  };
  return table;
}

}  // namespace

std::optional<std::string> opening_book_pick(
    const std::vector<std::string>& move_history,
    const std::vector<std::string>& legal_moves_uci) {
  const auto& table = opening_book_table();
  const std::string key = make_key(move_history);

  const auto it = table.find(key);
  if (it == table.end() || it->second.empty()) return std::nullopt;

  std::vector<BookCandidate> legal_candidates;
  legal_candidates.reserve(it->second.size());
  for (const auto& candidate : it->second) {
    if (candidate.weight <= 0) continue;
    if (std::find(legal_moves_uci.begin(), legal_moves_uci.end(), candidate.uci) != legal_moves_uci.end()) {
      legal_candidates.push_back(candidate);
    }
  }

  if (legal_candidates.empty()) return std::nullopt;

  int total_weight = 0;
  for (const auto& c : legal_candidates) total_weight += c.weight;

  static thread_local std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<int> dist(1, total_weight);
  int roll = dist(rng);

  for (const auto& c : legal_candidates) {
    roll -= c.weight;
    if (roll <= 0) return c.uci;
  }

  return legal_candidates.front().uci;
}
