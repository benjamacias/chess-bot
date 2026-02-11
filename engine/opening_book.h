#pragma once

#include <optional>
#include <string>
#include <vector>

struct BookCandidate {
  std::string uci;
  int weight;
};

// Devuelve jugada UCI del libro para la posición definida por la secuencia
// de jugadas desde startpos. Si no hay entrada o no hay jugada legal, nullopt.
std::optional<std::string> opening_book_pick(
    const std::vector<std::string>& move_history,
    const std::vector<std::string>& legal_moves_uci);
