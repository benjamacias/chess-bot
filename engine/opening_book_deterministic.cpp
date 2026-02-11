#include "opening_book.h"

#include <algorithm>
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

// ============================================================================
// REPERTORIO ÚNICO - SIEMPRE LAS MISMAS LÍNEAS
// ============================================================================
//
// BLANCAS: 1.e4 -> Sistema Italiano (sólido, posicional, profundo)
// NEGRAS vs 1.e4: Caro-Kann (sólido, confiable)
// NEGRAS vs 1.d4: Semi-Slav (teoría sólida, bien establecida)
// NEGRAS vs 1.c4: Transponer a Semi-Slav
// NEGRAS vs 1.Nf3: Transponer a estructuras conocidas
//
// NOTA: Solo hay UNA opción por posición = Siempre la misma línea
// ============================================================================

static const BookTable& opening_book_table() {
  static const BookTable table = {
      
      // ========================================================================
      // REPERTORIO BLANCAS: 1.e4 -> ITALIANO/GIUOCO PIANO
      // ========================================================================
      
      {"", {{"e2e4", 100}}},

      // 1.e4 - Respuestas del oponente
      {"e2e4", {{"e7e5", 100}}},  // Si juega otra cosa, salimos del libro pronto
      {"e2e4 c7c5", {{"g1f3", 100}}},  // Siciliana: jugaremos anti-abierta
      {"e2e4 c7c6", {{"d2d4", 100}}},  // Caro: Clásica o Advance
      {"e2e4 e7e6", {{"d2d4", 100}}},  // Francesa: Clásica
      {"e2e4 d7d5", {{"e4d5", 100}}},  // Escandinava
      {"e2e4 g8f6", {{"e4e5", 100}}},  // Alekhine
      {"e2e4 g7g6", {{"d2d4", 100}}},  // Moderna
      
      // LÍNEA PRINCIPAL: 1.e4 e5 2.Nf3 Nc6 3.Bc4 (ITALIANO)
      {"e2e4 e7e5", {{"g1f3", 100}}},
      {"e2e4 e7e5 g1f3", {{"b8c6", 100}}},
      {"e2e4 e7e5 g1f3 g8f6", {{"f3e5", 100}}},  // Si Petrov
      
      {"e2e4 e7e5 g1f3 b8c6", {{"f1c4", 100}}},
      
      // GIUOCO PIANO: 3...Bc5 4.c3
      {"e2e4 e7e5 g1f3 b8c6 f1c4", {{"f8c5", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5", {{"c2c3", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3", {{"g8f6", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6", {{"d2d4", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4", {{"e5d4", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4", {{"c3d4", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4", {{"c5b4", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4 c5b4", {{"b1c3", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4 c5b4 b1c3", {{"f6e4", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4 c5b4 b1c3 f6e4", {{"e1g1", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4 c5b4 b1c3 f6e4 e1g1", {{"b4c3", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4 c5b4 b1c3 f6e4 e1g1 b4c3", {{"b2c3", 100}}},
      
      // Si negras desvían en Giuoco
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 d8e7", {{"d2d4", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 d7d6", {{"d2d4", 100}}},
      
      // TWO KNIGHTS: 3...Nf6 -> 4.d3 (Quiet Italian, muy sólido)
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6", {{"d2d3", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3", {{"f8c5", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8c5", {{"c2c3", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8c5 c2c3", {{"d7d6", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8c5 c2c3 d7d6", {{"e1g1", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8c5 c2c3 d7d6 e1g1", {{"e8g8", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8c5 c2c3 d7d6 e1g1 e8g8", {{"b1d2", 100}}},
      
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8e7", {{"e1g1", 100}}},
      {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 h7h6", {{"e1g1", 100}}},
      
      // CONTRA CARO-KANN: 2.d4 d5 3.Nc3 (Exchange o Panov)
      {"e2e4 c7c6 d2d4", {{"d7d5", 100}}},
      {"e2e4 c7c6 d2d4 d7d5", {{"b1c3", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3", {{"d5e4", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4", {{"c3e4", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4", {{"c8f5", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5", {{"e4g3", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3", {{"f5g6", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6", {{"h2h4", 100}}},
      
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 g8f6", {{"e4f6", 100}}},
      
      // CONTRA FRANCESA: 2.d4 d5 3.Nc3 (Classical)
      {"e2e4 e7e6 d2d4", {{"d7d5", 100}}},
      {"e2e4 e7e6 d2d4 d7d5", {{"b1c3", 100}}},
      {"e2e4 e7e6 d2d4 d7d5 b1c3", {{"g8f6", 100}}},
      {"e2e4 e7e6 d2d4 d7d5 b1c3 g8f6", {{"c1g5", 100}}},
      {"e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 c1g5", {{"f8e7", 100}}},
      {"e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 c1g5 f8e7", {{"e4e5", 100}}},
      
      {"e2e4 e7e6 d2d4 d7d5 b1c3 f8b4", {{"e4e5", 100}}},
      
      // CONTRA SICILIANA: Anti-abierta con 2.Nf3 seguido de sistemas posicionales
      {"e2e4 c7c5 g1f3", {{"d7d6", 100}}},
      {"e2e4 c7c5 g1f3 d7d6", {{"d2d4", 100}}},  // Salimos pronto, dejamos que calcule
      {"e2e4 c7c5 g1f3 b8c6", {{"d2d4", 100}}},
      {"e2e4 c7c5 g1f3 e7e6", {{"d2d4", 100}}},
      
      // ========================================================================
      // REPERTORIO NEGRAS vs 1.e4: CARO-KANN
      // ========================================================================
      
      {"d2d4", {{"d7d5", 100}}},  // Contra 1.d4: ver más abajo
      {"e2e4", {{"c7c6", 100}}},  // Nuestra defensa elegida: CARO-KANN
      
      // CARO-KANN CLÁSICA: 1.e4 c6 2.d4 d5 3.Nc3 dxe4 4.Nxe4
      {"e2e4 c7c6", {{"d2d4", 100}}},
      {"e2e4 c7c6 d2d4", {{"d7d5", 100}}},
      {"e2e4 c7c6 d2d4 d7d5", {{"b1c3", 100}}},  // Si juegan otra cosa, ok
      {"e2e4 c7c6 d2d4 d7d5 b1c3", {{"d5e4", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4", {{"c3e4", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4", {{"c8f5", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5", {{"e4g3", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3", {{"f5g6", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6", {{"h2h4", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6 h2h4", {{"h7h6", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6 h2h4 h7h6", {{"g1f3", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6 h2h4 h7h6 g1f3", {{"b8d7", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6 h2h4 h7h6 g1f3 b8d7", {{"h4h5", 100}}},
      
      // Si blancas juegan Nf3 early
      {"e2e4 c7c6 g1f3", {{"d7d5", 100}}},
      {"e2e4 c7c6 g1f3 d7d5", {{"b1c3", 100}}},
      
      // ADVANCE CARO: 3.e5
      {"e2e4 c7c6 d2d4 d7d5 e4e5", {{"c8f5", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8f5", {{"f1e2", 100}}},  // O g1f3
      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 f1e2", {{"e7e6", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 f1e2 e7e6", {{"g1f3", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 g1f3", {{"e7e6", 100}}},
      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 g1f3 e7e6", {{"f1e2", 100}}},
      
      {"e2e4 c7c6 d2d4 d7d5 e4e5 c8g4", {{"f1e2", 100}}},
      
      // TWO KNIGHTS: 3.Nf3 Bg4 (una línea alternativa)
      {"e2e4 c7c6 b1c3", {{"d7d5", 100}}},
      {"e2e4 c7c6 b1c3 d7d5", {{"g1f3", 100}}},
      
      // ========================================================================
      // REPERTORIO NEGRAS vs 1.d4: SEMI-SLAV
      // ========================================================================
      
      {"d2d4", {{"d7d5", 100}}},
      {"d2d4 d7d5", {{"c2c4", 100}}},
      {"d2d4 d7d5 c2c4", {{"e7e6", 100}}},
      {"d2d4 d7d5 c2c4 e7e6", {{"b1c3", 100}}},
      {"d2d4 d7d5 c2c4 e7e6 b1c3", {{"g8f6", 100}}},
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6", {{"g1f3", 100}}},
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3", {{"c7c6", 100}}},  // SEMI-SLAV
      
      // SEMI-SLAV MAIN LINE: 5.e3
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 c7c6", {{"e2e3", 100}}},
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 c7c6 e2e3", {{"b8d7", 100}}},
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 c7c6 e2e3 b8d7", {{"f1d3", 100}}},
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 c7c6 e2e3 b8d7 f1d3", {{"d5c4", 100}}},
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 c7c6 e2e3 b8d7 f1d3 d5c4", {{"d3c4", 100}}},
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 c7c6 e2e3 b8d7 f1d3 d5c4 d3c4", {{"b7b5", 100}}},
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 c7c6 e2e3 b8d7 f1d3 d5c4 d3c4 b7b5", {{"c4d3", 100}}},
      
      // MERAN: si juegan e3 antes
      {"d2d4 d7d5 c2c4 e7e6 b1c3 c7c6", {{"g1f3", 100}}},
      {"d2d4 d7d5 c2c4 e7e6 b1c3 c7c6 g1f3", {{"g8f6", 100}}},
      {"d2d4 d7d5 c2c4 e7e6 b1c3 c7c6 e2e3", {{"g8f6", 100}}},
      {"d2d4 d7d5 c2c4 e7e6 b1c3 c7c6 e2e3 g8f6", {{"g1f3", 100}}},
      
      // Anti-Moscow Gambit
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 c7c6 c1g5", {{"h7h6", 100}}},
      {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5", {{"h7h6", 100}}},
      
      // Si blancas transponen
      {"d2d4 d7d5 g1f3", {{"g8f6", 100}}},
      {"d2d4 d7d5 g1f3 g8f6", {{"c2c4", 100}}},
      {"d2d4 d7d5 g1f3 g8f6 c2c4", {{"e7e6", 100}}},
      
      // LONDON SYSTEM (si evitan c4)
      {"d2d4 d7d5 g1f3 g8f6 c1f4", {{"c7c5", 100}}},
      {"d2d4 d7d5 c1f4", {{"g8f6", 100}}},
      {"d2d4 g8f6", {{"c2c4", 100}}},  // Si 1.d4 Nf6, preferimos c4
      {"d2d4 g8f6 c2c4", {{"e7e6", 100}}},
      {"d2d4 g8f6 c2c4 e7e6", {{"g1f3", 100}}},
      {"d2d4 g8f6 c2c4 e7e6 g1f3", {{"d7d5", 100}}},
      
      {"d2d4 g8f6 g1f3", {{"e7e6", 100}}},
      {"d2d4 g8f6 g1f3 e7e6", {{"c2c4", 100}}},
      
      {"d2d4 g8f6 c1f4", {{"d7d5", 100}}},
      
      // ========================================================================
      // REPERTORIO NEGRAS vs 1.c4: TRANSPONER A SEMI-SLAV
      // ========================================================================
      
      {"c2c4", {{"e7e6", 100}}},
      {"c2c4 e7e6", {{"d2d4", 100}}},  // Forzamos d4
      {"c2c4 e7e6 d2d4", {{"d7d5", 100}}},
      {"c2c4 e7e6 d2d4 d7d5", {{"b1c3", 100}}},
      {"c2c4 e7e6 g1f3", {{"d7d5", 100}}},
      {"c2c4 e7e6 g1f3 d7d5", {{"d2d4", 100}}},
      
      // Si blancas evitan d4 temprano
      {"c2c4 e7e6 b1c3", {{"d7d5", 100}}},
      {"c2c4 e7e6 g2g3", {{"d7d5", 100}}},
      
      // ========================================================================
      // REPERTORIO NEGRAS vs 1.Nf3: FLEXIBLE
      // ========================================================================
      
      {"g1f3", {{"d7d5", 100}}},  // Sistemas de d5
      {"g1f3 d7d5", {{"d2d4", 100}}},  // Forzar d4
      {"g1f3 d7d5 d2d4", {{"g8f6", 100}}},
      {"g1f3 d7d5 d2d4 g8f6", {{"c2c4", 100}}},
      {"g1f3 d7d5 c2c4", {{"e7e6", 100}}},
      {"g1f3 d7d5 c2c4 e7e6", {{"d2d4", 100}}},
      
      {"g1f3 d7d5 g2g3", {{"c7c6", 100}}},  // Catalán setup
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

  // CAMBIO CRÍTICO: Siempre elegir el PRIMER candidato (línea principal)
  // No hay aleatoriedad, siempre la misma línea
  const auto& candidates = it->second;
  
  // Buscar si el primer candidato es legal
  const std::string& preferred_move = candidates[0].uci;
  
  auto legal_it = std::find(legal_moves_uci.begin(), legal_moves_uci.end(), preferred_move);
  if (legal_it != legal_moves_uci.end()) {
    return preferred_move;
  }
  
  // Si por alguna razón el primer candidato no es legal (no debería pasar),
  // buscar el siguiente candidato legal
  for (const auto& candidate : candidates) {
    if (std::find(legal_moves_uci.begin(), legal_moves_uci.end(), candidate.uci) 
        != legal_moves_uci.end()) {
      return candidate.uci;
    }
  }

  return std::nullopt;
}
