#!/usr/bin/env python3
"""
Script para validar y analizar el opening book contra Stockfish.

Requiere: pip install python-chess stockfish

Uso:
  python validate_book.py opening_book.cpp
  python validate_book.py opening_book.cpp --stockfish-path /usr/bin/stockfish
  python validate_book.py opening_book.cpp --depth 15 --max-eval 100
"""

import re
import sys
import argparse
from collections import defaultdict
from typing import List, Dict, Tuple, Optional

try:
    import chess
    import chess.engine
except ImportError:
    print("ERROR: Se requiere python-chess")
    print("Instala con: pip install python-chess")
    sys.exit(1)


class BookAnalyzer:
    def __init__(self, stockfish_path: str = "stockfish", depth: int = 12, max_eval_cp: int = 150):
        """
        Args:
            stockfish_path: Ruta al ejecutable de Stockfish
            depth: Profundidad de análisis
            max_eval_cp: Evaluación máxima aceptable en centipawns (ej. 150 = 1.5 peones)
        """
        self.depth = depth
        self.max_eval_cp = max_eval_cp
        
        try:
            self.engine = chess.engine.SimpleEngine.popen_uci(stockfish_path)
        except Exception as e:
            print(f"ERROR: No se pudo iniciar Stockfish en '{stockfish_path}'")
            print(f"       {e}")
            print("\nIntenta especificar la ruta con --stockfish-path")
            sys.exit(1)
    
    def __del__(self):
        if hasattr(self, 'engine'):
            self.engine.quit()
    
    def parse_book_file(self, filepath: str) -> Dict[str, List[Tuple[str, int]]]:
        """Parse opening_book.cpp y extrae el mapa de posiciones."""
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Buscar el contenido del BookTable
        table_match = re.search(r'static const BookTable table = \{(.*?)\};', content, re.DOTALL)
        if not table_match:
            print("ERROR: No se encontró la tabla del libro")
            sys.exit(1)
        
        table_str = table_match.group(1)
        
        # Parse cada entrada
        book = {}
        # Regex para capturar: {"clave", {{"mov1", peso1}, {"mov2", peso2}}}
        pattern = r'\{"([^"]*)",\s*\{([^}]*(?:\}[^}]*)*)\}\}'
        
        for match in re.finditer(pattern, table_str):
            key = match.group(1)
            moves_str = match.group(2)
            
            # Parse los candidatos
            candidates = []
            cand_pattern = r'\{"([^"]+)",\s*(\d+)\}'
            for cand_match in re.finditer(cand_pattern, moves_str):
                move_uci = cand_match.group(1)
                weight = int(cand_match.group(2))
                candidates.append((move_uci, weight))
            
            book[key] = candidates
        
        return book
    
    def make_moves_on_board(self, board: chess.Board, move_sequence: str) -> bool:
        """Ejecuta una secuencia de movimientos UCI en el tablero."""
        if not move_sequence:
            return True
        
        moves = move_sequence.split()
        for uci in moves:
            try:
                move = chess.Move.from_uci(uci)
                if move not in board.legal_moves:
                    return False
                board.push(move)
            except ValueError:
                return False
        return True
    
    def analyze_position(self, board: chess.Board, move_uci: str) -> Optional[int]:
        """Analiza un movimiento y retorna su evaluación en centipawns."""
        try:
            move = chess.Move.from_uci(move_uci)
            if move not in board.legal_moves:
                return None
            
            board.push(move)
            result = self.engine.analyse(board, chess.engine.Limit(depth=self.depth))
            board.pop()
            
            score = result['score'].white()
            if score.is_mate():
                # Mate a favor: +30000, mate en contra: -30000
                mate_in = score.mate()
                return 30000 if mate_in > 0 else -30000
            else:
                return score.score()
        except Exception as e:
            print(f"ERROR analizando movimiento {move_uci}: {e}")
            return None
    
    def validate_book(self, book: Dict[str, List[Tuple[str, int]]]) -> Dict:
        """Valida el libro completo."""
        stats = {
            'total_positions': len(book),
            'total_candidates': sum(len(v) for v in book.values()),
            'invalid_sequences': [],
            'invalid_moves': [],
            'weak_moves': [],      # eval < -max_eval_cp
            'dubious_moves': [],   # eval entre -max_eval_cp y -50
            'good_moves': [],      # eval > -50
            'duplicate_keys': [],
        }
        
        seen_keys = set()
        
        print(f"\n{'='*70}")
        print(f"Validando {stats['total_positions']} posiciones...")
        print(f"Profundidad de análisis: {self.depth}")
        print(f"Umbral de evaluación: ±{self.max_eval_cp} cp")
        print(f"{'='*70}\n")
        
        for i, (key, candidates) in enumerate(book.items(), 1):
            if i % 10 == 0 or i == 1:
                print(f"Progreso: {i}/{stats['total_positions']} posiciones...", end='\r')
            
            # Verificar claves duplicadas
            if key in seen_keys:
                stats['duplicate_keys'].append(key)
            seen_keys.add(key)
            
            board = chess.Board()
            
            # Validar secuencia
            if not self.make_moves_on_board(board, key):
                stats['invalid_sequences'].append(key)
                continue
            
            # Validar cada candidato
            for move_uci, weight in candidates:
                try:
                    move = chess.Move.from_uci(move_uci)
                    if move not in board.legal_moves:
                        stats['invalid_moves'].append((key, move_uci))
                        continue
                except ValueError:
                    stats['invalid_moves'].append((key, move_uci))
                    continue
                
                # Analizar con Stockfish
                eval_cp = self.analyze_position(board, move_uci)
                if eval_cp is None:
                    continue
                
                # Normalizar evaluación según el turno
                if not board.turn:  # Si es turno de negras, invertir
                    eval_cp = -eval_cp
                
                move_info = {
                    'key': key,
                    'move': move_uci,
                    'weight': weight,
                    'eval': eval_cp
                }
                
                if eval_cp < -self.max_eval_cp:
                    stats['weak_moves'].append(move_info)
                elif eval_cp < -50:
                    stats['dubious_moves'].append(move_info)
                else:
                    stats['good_moves'].append(move_info)
        
        print()  # Nueva línea después del progreso
        return stats
    
    def print_report(self, stats: Dict):
        """Imprime un reporte detallado."""
        print(f"\n{'='*70}")
        print("REPORTE DE VALIDACIÓN")
        print(f"{'='*70}\n")
        
        # Resumen
        print(f"📊 RESUMEN:")
        print(f"  Total de posiciones: {stats['total_positions']}")
        print(f"  Total de candidatos: {stats['total_candidates']}")
        print(f"  Candidatos válidos: {len(stats['good_moves']) + len(stats['dubious_moves']) + len(stats['weak_moves'])}")
        print()
        
        # Errores críticos
        if stats['invalid_sequences']:
            print(f"❌ SECUENCIAS INVÁLIDAS ({len(stats['invalid_sequences'])}):")
            for seq in stats['invalid_sequences'][:10]:
                print(f"  - \"{seq}\"")
            if len(stats['invalid_sequences']) > 10:
                print(f"  ... y {len(stats['invalid_sequences']) - 10} más")
            print()
        
        if stats['invalid_moves']:
            print(f"❌ MOVIMIENTOS INVÁLIDOS ({len(stats['invalid_moves'])}):")
            for key, move in stats['invalid_moves'][:10]:
                print(f"  - \"{key}\" -> {move}")
            if len(stats['invalid_moves']) > 10:
                print(f"  ... y {len(stats['invalid_moves']) - 10} más")
            print()
        
        if stats['duplicate_keys']:
            print(f"⚠️  CLAVES DUPLICADAS ({len(stats['duplicate_keys'])}):")
            for key in stats['duplicate_keys']:
                print(f"  - \"{key}\"")
            print()
        
        # Evaluaciones
        if stats['weak_moves']:
            print(f"⛔ MOVIMIENTOS DÉBILES - eval < -{self.max_eval_cp}cp ({len(stats['weak_moves'])}):")
            for info in sorted(stats['weak_moves'], key=lambda x: x['eval'])[:15]:
                print(f"  - \"{info['key']}\" -> {info['move']} "
                      f"[eval: {info['eval']:+4d}cp, weight: {info['weight']}]")
            if len(stats['weak_moves']) > 15:
                print(f"  ... y {len(stats['weak_moves']) - 15} más")
            print()
        
        if stats['dubious_moves']:
            print(f"⚠️  MOVIMIENTOS DUDOSOS - eval entre -50 y -{self.max_eval_cp}cp ({len(stats['dubious_moves'])}):")
            for info in sorted(stats['dubious_moves'], key=lambda x: x['eval'])[:10]:
                print(f"  - \"{info['key']}\" -> {info['move']} "
                      f"[eval: {info['eval']:+4d}cp, weight: {info['weight']}]")
            if len(stats['dubious_moves']) > 10:
                print(f"  ... y {len(stats['dubious_moves']) - 10} más")
            print()
        
        print(f"✅ MOVIMIENTOS BUENOS - eval > -50cp: {len(stats['good_moves'])}")
        print()
        
        # Estadísticas de evaluación
        all_evals = [m['eval'] for m in stats['good_moves'] + stats['dubious_moves'] + stats['weak_moves']]
        if all_evals:
            avg_eval = sum(all_evals) / len(all_evals)
            print(f"📈 ESTADÍSTICAS DE EVALUACIÓN:")
            print(f"  Evaluación promedio: {avg_eval:+.1f} cp")
            print(f"  Mejor movimiento: {max(all_evals):+d} cp")
            print(f"  Peor movimiento: {min(all_evals):+d} cp")
            print()
        
        # Veredicto final
        print(f"{'='*70}")
        critical_errors = len(stats['invalid_sequences']) + len(stats['invalid_moves'])
        serious_errors = len(stats['weak_moves'])
        
        if critical_errors == 0 and serious_errors == 0:
            print("✅ VEREDICTO: El libro es VÁLIDO y de ALTA CALIDAD")
        elif critical_errors == 0 and serious_errors < 5:
            print("⚠️  VEREDICTO: El libro es VÁLIDO pero tiene algunas líneas débiles")
        elif critical_errors == 0:
            print("⚠️  VEREDICTO: El libro es VÁLIDO pero necesita MEJORAS significativas")
        else:
            print("❌ VEREDICTO: El libro tiene ERRORES CRÍTICOS que deben corregirse")
        print(f"{'='*70}\n")


def main():
    parser = argparse.ArgumentParser(description='Validar opening book con Stockfish')
    parser.add_argument('book_file', help='Archivo opening_book.cpp')
    parser.add_argument('--stockfish-path', default='stockfish', 
                       help='Ruta al ejecutable de Stockfish')
    parser.add_argument('--depth', type=int, default=12,
                       help='Profundidad de análisis (default: 12)')
    parser.add_argument('--max-eval', type=int, default=150,
                       help='Evaluación máxima aceptable en cp (default: 150)')
    
    args = parser.parse_args()
    
    analyzer = BookAnalyzer(
        stockfish_path=args.stockfish_path,
        depth=args.depth,
        max_eval_cp=args.max_eval
    )
    
    book = analyzer.parse_book_file(args.book_file)
    stats = analyzer.validate_book(book)
    analyzer.print_report(stats)


if __name__ == '__main__':
    main()
