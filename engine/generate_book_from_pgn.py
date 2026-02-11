#!/usr/bin/env python3
"""
Generador de Opening Book desde archivos PGN.

Analiza partidas de bases de datos PGN y genera un opening_book.cpp
basado en estadísticas reales de jugadores fuertes.

Requiere: pip install python-chess

Uso:
  python generate_book_from_pgn.py --input masters.pgn --output opening_book_generated.cpp
  python generate_book_from_pgn.py --input masters.pgn --min-elo 2500 --min-games 10
"""

import re
import sys
import argparse
from collections import defaultdict, Counter
from typing import Dict, List, Tuple
from dataclasses import dataclass

try:
    import chess
    import chess.pgn
except ImportError:
    print("ERROR: Se requiere python-chess")
    print("Instala con: pip install python-chess")
    sys.exit(1)


@dataclass
class MoveStats:
    """Estadísticas de un movimiento en una posición."""
    uci: str
    wins: int = 0
    draws: int = 0
    losses: int = 0
    count: int = 0
    
    @property
    def score(self) -> float:
        """Score ponderado: wins=1, draws=0.5, losses=0"""
        if self.count == 0:
            return 0.5
        return (self.wins + 0.5 * self.draws) / self.count
    
    @property
    def weight(self) -> int:
        """Peso para el libro basado en popularidad y rendimiento."""
        # Fórmula: popularidad (log scale) * rendimiento
        import math
        popularity = min(100, int(20 * math.log(self.count + 1)))
        performance = int(self.score * 100)
        return min(100, (popularity + performance) // 2)


class BookGenerator:
    def __init__(self, min_elo: int = 2200, min_games: int = 5, max_depth: int = 15):
        """
        Args:
            min_elo: ELO mínimo para considerar partidas
            min_games: Mínimo de partidas para incluir un movimiento
            max_depth: Profundidad máxima del libro (número de jugadas)
        """
        self.min_elo = min_elo
        self.min_games = min_games
        self.max_depth = max_depth
        
        # Estructura: position_key -> {move_uci -> MoveStats}
        self.positions: Dict[str, Dict[str, MoveStats]] = defaultdict(lambda: defaultdict(MoveStats))
        
        self.total_games = 0
        self.processed_games = 0
    
    def process_pgn_file(self, filepath: str):
        """Procesa un archivo PGN y extrae estadísticas."""
        print(f"Procesando {filepath}...")
        
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as pgn_file:
            while True:
                game = chess.pgn.read_game(pgn_file)
                if game is None:
                    break
                
                self.total_games += 1
                
                if self.total_games % 1000 == 0:
                    print(f"Procesadas {self.total_games} partidas, "
                          f"usadas {self.processed_games}...", end='\r')
                
                # Filtrar por ELO
                white_elo = game.headers.get("WhiteElo", "0")
                black_elo = game.headers.get("BlackElo", "0")
                
                try:
                    white_elo = int(white_elo)
                    black_elo = int(black_elo)
                except ValueError:
                    continue
                
                if white_elo < self.min_elo or black_elo < self.min_elo:
                    continue
                
                # Determinar resultado
                result = game.headers.get("Result", "*")
                if result not in ["1-0", "0-1", "1/2-1/2"]:
                    continue
                
                self.process_game(game, result)
                self.processed_games += 1
        
        print(f"\n✓ Procesadas {self.total_games} partidas, usadas {self.processed_games}")
    
    def process_game(self, game: chess.pgn.Game, result: str):
        """Procesa una partida individual."""
        board = game.board()
        move_sequence = []
        
        for i, move in enumerate(game.mainline_moves()):
            if i >= self.max_depth * 2:  # 2 movimientos por jugada (blanco+negro)
                break
            
            # Clave de posición (secuencia de movimientos UCI)
            pos_key = ' '.join(move_sequence)
            move_uci = move.uci()
            
            # Actualizar estadísticas
            stats = self.positions[pos_key][move_uci]
            if stats.uci == '':
                stats.uci = move_uci
            
            stats.count += 1
            
            # Actualizar W/D/L según el resultado y el turno
            if result == "1/2-1/2":
                stats.draws += 1
            elif board.turn == chess.WHITE:  # Movimiento de blancas
                if result == "1-0":
                    stats.wins += 1
                else:
                    stats.losses += 1
            else:  # Movimiento de negras
                if result == "0-1":
                    stats.wins += 1
                else:
                    stats.losses += 1
            
            board.push(move)
            move_sequence.append(move_uci)
    
    def filter_positions(self) -> Dict[str, List[MoveStats]]:
        """Filtra posiciones manteniendo solo movimientos con suficientes partidas."""
        filtered = {}
        
        for pos_key, moves in self.positions.items():
            # Filtrar movimientos con pocas partidas
            good_moves = [
                stats for stats in moves.values() 
                if stats.count >= self.min_games
            ]
            
            if not good_moves:
                continue
            
            # Ordenar por score y popularidad
            good_moves.sort(key=lambda s: (s.score, s.count), reverse=True)
            
            # Limitar a top 5 movimientos
            filtered[pos_key] = good_moves[:5]
        
        return filtered
    
    def generate_cpp_code(self, output_path: str):
        """Genera el código C++ del opening book."""
        filtered = self.filter_positions()
        
        print(f"\nGenerando código C++...")
        print(f"  Posiciones incluidas: {len(filtered)}")
        print(f"  Movimientos totales: {sum(len(v) for v in filtered.values())}")
        
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(self._generate_header())
            f.write(self._generate_table(filtered))
            f.write(self._generate_footer())
        
        print(f"✓ Archivo generado: {output_path}")
    
    def _generate_header(self) -> str:
        return '''#include "opening_book.h"

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

// Opening book generado automáticamente desde base de datos PGN
// Criterios:
//   - ELO mínimo: {min_elo}
//   - Partidas mínimas por movimiento: {min_games}
//   - Profundidad máxima: {max_depth} jugadas
//   - Total de posiciones: {{total_positions}}
//   - Fecha de generación: {{timestamp}}

static const BookTable& opening_book_table() {{
  static const BookTable table = {{
'''.format(min_elo=self.min_elo, min_games=self.min_games, max_depth=self.max_depth)
    
    def _generate_table(self, filtered: Dict[str, List[MoveStats]]) -> str:
        from datetime import datetime
        
        lines = []
        
        # Ordenar posiciones por longitud (startpos primero)
        sorted_positions = sorted(filtered.items(), key=lambda x: len(x[0]))
        
        for pos_key, moves in sorted_positions:
            # Comentario con estadísticas
            total_games = sum(m.count for m in moves)
            avg_score = sum(m.score * m.count for m in moves) / total_games if total_games > 0 else 0.5
            
            comment = f"      // Games: {total_games}, Avg score: {avg_score:.3f}"
            
            # Generar línea del libro
            candidates = ', '.join(
                f'{{\"{m.uci}\", {m.weight}}}'
                for m in moves
            )
            
            line = f'      {{\"{pos_key}\", {{{candidates}}}}},'
            
            lines.append(comment)
            lines.append(line)
            lines.append('')
        
        # Reemplazar timestamp y total_positions en el header
        result = '\n'.join(lines)
        header = self._generate_header()
        header = header.replace('{{total_positions}}', str(len(filtered)))
        header = header.replace('{{timestamp}}', datetime.now().strftime('%Y-%m-%d %H:%M:%S'))
        
        return result
    
    def _generate_footer(self) -> str:
        return '''  };
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
'''
    
    def print_statistics(self):
        """Imprime estadísticas del libro generado."""
        filtered = self.filter_positions()
        
        print(f"\n{'='*70}")
        print("ESTADÍSTICAS DEL LIBRO GENERADO")
        print(f"{'='*70}\n")
        
        print(f"📊 GENERAL:")
        print(f"  Partidas procesadas: {self.processed_games} / {self.total_games}")
        print(f"  Posiciones únicas: {len(filtered)}")
        print(f"  Movimientos totales: {sum(len(v) for v in filtered.values())}")
        print()
        
        # Distribución por profundidad
        depth_dist = defaultdict(int)
        for pos_key in filtered.keys():
            depth = len(pos_key.split()) if pos_key else 0
            depth_dist[depth] += 1
        
        print(f"📈 DISTRIBUCIÓN POR PROFUNDIDAD:")
        for depth in sorted(depth_dist.keys())[:15]:
            print(f"  Jugada {depth:2d}: {depth_dist[depth]:4d} posiciones")
        print()
        
        # Top movimientos
        all_moves = []
        for moves in filtered.values():
            all_moves.extend(moves)
        
        move_counter = Counter(m.uci for m in all_moves)
        
        print(f"🔝 MOVIMIENTOS MÁS COMUNES:")
        for uci, count in move_counter.most_common(15):
            print(f"  {uci}: {count} apariciones")
        print()


def main():
    parser = argparse.ArgumentParser(
        description='Genera opening book desde archivos PGN'
    )
    parser.add_argument('--input', '-i', required=True,
                       help='Archivo PGN de entrada')
    parser.add_argument('--output', '-o', default='opening_book_generated.cpp',
                       help='Archivo de salida (default: opening_book_generated.cpp)')
    parser.add_argument('--min-elo', type=int, default=2200,
                       help='ELO mínimo (default: 2200)')
    parser.add_argument('--min-games', type=int, default=5,
                       help='Mínimo de partidas por movimiento (default: 5)')
    parser.add_argument('--max-depth', type=int, default=15,
                       help='Profundidad máxima en jugadas (default: 15)')
    
    args = parser.parse_args()
    
    generator = BookGenerator(
        min_elo=args.min_elo,
        min_games=args.min_games,
        max_depth=args.max_depth
    )
    
    generator.process_pgn_file(args.input)
    generator.generate_cpp_code(args.output)
    generator.print_statistics()
    
    print(f"\n✅ Listo! Ahora puedes compilar con el nuevo archivo:")
    print(f"   cp {args.output} opening_book.cpp")
    print(f"   make")


if __name__ == '__main__':
    main()
