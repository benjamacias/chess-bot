# Análisis y Mejoras del Opening Book

## 🔍 Problemas Identificados

### 1. **Errores y Vulnerabilidades Actuales**

#### a) Secuencias Incompletas/Inconsistentes
- **e2e4 c7c6 g1f3**: Falta esta línea en el libro (saltas directamente a d2d4)
- **e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3**: Solo tiene continuación para Negras
- **Siciliana**: Cobertura muy limitada contra 2...d6 y 2...Nc6

#### b) Pesos Sin Fundamento
```cpp
{"e2e4", {{"c7c6", 70}, {"e7e5", 30}}}  // ¿Por qué 70/30?
```
Los pesos actuales son arbitrarios. En la práctica a nivel magistral:
- 1...e5 es más popular (~35-40%)
- 1...c5 Siciliana (~25-30%)
- 1...c6 Caro-Kann (~10-15%)

#### c) Transposiciones Perdidas
El libro usa secuencias de movimientos, no posiciones. Esto causa problemas:
- 1.d4 Nf6 2.c4 e6 3.Nf3 d5 = QGD
- 1.d4 d5 2.c4 e6 3.Nf3 Nf6 = QGD (MISMA POSICIÓN)
- Pero el libro las trata como diferentes

#### d) Líneas Débiles/Peligrosas
```cpp
{"e2e4 c7c6 d2d4 d7d5 e4e5 c8f5", {{"g1f3", 100}}}
```
Esta es la Defensa Caro-Kann Advance. La continuación Nf3 es válida pero:
- Falta 6.Be2 (más popular)
- Falta 6.Nbd2 
- No hay plan después de 6...e6

### 2. **Problemas Estructurales**

#### a) No Hay Validación
```cpp
// El libro podría tener typos como "e2e44" o "c7c77" y no se detectaría
{"e2e4 c7c77", {{"d2d4", 100}}}  // ERROR no detectado
```

#### b) Sin Manejo de Posiciones Críticas
No hay verificación de tácticas. Por ejemplo:
- ¿Hay colgadas de piezas?
- ¿Hay mates en 1 que debemos evitar?
- ¿Estamos en una trampa conocida?

#### c) Cobertura Insuficiente
Solo ~45 líneas. Un libro competente debería tener:
- Mínimo 200-500 posiciones para juego decente
- 1000+ para nivel club
- 10000+ para nivel maestro

---

## ✅ Soluciones Propuestas

### Mejora 1: **Sistema de Validación**

```cpp
// Agregar función de verificación
bool validate_opening_book() {
  Board test_board;
  const auto& table = opening_book_table();
  
  for (const auto& [key, candidates] : table) {
    test_board.set_startpos();
    
    // Reproducir movimientos
    vector<string> moves = split(key, ' ');
    for (const auto& uci : moves) {
      Move m = parse_uci_move(test_board, uci);
      if (!is_legal(test_board, m)) {
        cerr << "ERROR: Secuencia inválida en '" << key << "'\n";
        return false;
      }
      Undo u;
      test_board.make_move(m, u);
    }
    
    // Verificar candidatos
    for (const auto& cand : candidates) {
      Move m = parse_uci_move(test_board, cand.uci);
      if (!is_legal(test_board, m)) {
        cerr << "ERROR: Movimiento '" << cand.uci 
             << "' inválido en posición '" << key << "'\n";
        return false;
      }
    }
  }
  return true;
}
```

### Mejora 2: **Usar Zobrist Keys en vez de Strings**

```cpp
struct PositionBook {
  unordered_map<uint64_t, vector<BookCandidate>> positions;
  
  void add_line(const vector<string>& uci_moves, 
                const string& next_move, int weight) {
    Board b;
    b.set_startpos();
    
    for (const auto& uci : uci_moves) {
      Move m = parse_uci_move(b, uci);
      Undo u;
      b.make_move(m, u);
    }
    
    uint64_t key = b.key;
    positions[key].push_back({next_move, weight});
  }
  
  optional<string> probe(const Board& b, const vector<Move>& legal) {
    auto it = positions.find(b.key);
    if (it == positions.end()) return nullopt;
    
    // ... selección ponderada ...
  }
};
```

**Ventajas:**
- Maneja transposiciones automáticamente
- Más rápido (hash lookup vs string comparison)
- Más robusto

### Mejora 3: **Repertorio Expandido y Corregido**

#### Para BLANCAS (1.e4):

```cpp
// Contra Caro-Kann (más completo)
{"e2e4 c7c6", {{"d2d4", 60}, {"g1f3", 25}, {"b1c3", 15}}},
{"e2e4 c7c6 d2d4 d7d5", {{"e4e5", 50}, {"b1c3", 50}}},  // Ambas válidas

// Advance 3.e5
{"e2e4 c7c6 d2d4 d7d5 e4e5 c8f5", {{"f1e2", 40}, {"g1f3", 35}, {"b1d2", 25}}},
{"e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 f1e2", {{"e7e6", 60}, {"g8f6", 40}}},

// Exchange 3.exd5
{"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4", {{"b1e4", 100}}},
{"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 b1e4", {{"c8f5", 50}, {"g8f6", 50}}},

// Contra Siciliana (líneas anti-teóricas sólidas)
{"e2e4 c7c5", {{"g1f3", 50}, {"c2c3", 30}, {"b1c3", 20}}},

// Alapin 2.c3 (muy sólida para evitar teoría)
{"e2e4 c7c5 c2c3 d7d5", {{"e4d5", 70}, {"e4e5", 30}}},
{"e2e4 c7c5 c2c3 d7d5 e4d5", {{"d8d5", 100}}},
{"e2e4 c7c5 c2c3 d7d5 e4d5 d8d5", {{"d2d4", 100}}},

{"e2e4 c7c5 c2c3 g8f6", {{"e4e5", 100}}},
{"e2e4 c7c5 c2c3 g8f6 e4e5", {{"f6d5", 100}}},
{"e2e4 c7c5 c2c3 g8f6 e4e5 f6d5", {{"d2d4", 70}, {"g1f3", 30}}},

// Contra Francesa
{"e2e4 e7e6", {{"d2d4", 80}, {"g1f3", 20}}},
{"e2e4 e7e6 d2d4 d7d5", {{"b1c3", 50}, {"e4e5", 30}, {"e4d5", 20}}},

// Advance Francesa
{"e2e4 e7e6 d2d4 d7d5 e4e5 c7c5", {{"c2c3", 60}, {"g1f3", 40}}},

// Italiana (más profunda)
{"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8c5", {{"c2c3", 50}, {"e1g1", 50}}},
{"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8c5 c2c3", {{"a7a6", 40}, {"d7d6", 60}}},

{"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d4 e5d4", {{"e1g1", 60}, {"f3d4", 40}}},
```

#### Para NEGRAS (contra 1.d4):

```cpp
// Semi-Slav (más sólida que QGD puro)
{"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6", {{"g1f3", 60}, {"c1g5", 40}}},
{"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3", {{"c7c6", 70}, {"f8e7", 30}}},

// Semi-Slav puro
{"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 c7c6", {{"e2e3", 40}, {"c1g5", 35}, {"e2e4", 25}}},

// Meran (si 5.e3)
{"d2d4 d7d5 c2c4 e7e6 b1c3 c7c6", {{"g1f3", 60}, {"e2e3", 40}}},
{"d2d4 d7d5 c2c4 e7e6 b1c3 c7c6 g1f3", {{"g8f6", 100}}},
{"d2d4 d7d5 c2c4 e7e6 b1c3 c7c6 g1f3 g8f6", {{"e2e3", 50}, {"c1g5", 50}}},
{"d2d4 d7d5 c2c4 e7e6 b1c3 c7c6 e2e3", {{"g8f6", 100}}},

// Sistema Londres (muy común a nivel club)
{"d2d4 d7d5 g1f3 g8f6 c1f4", {{"c7c5", 40}, {"e7e6", 35}, {"c8f5", 25}}},
{"d2d4 g8f6 c1f4", {{"d7d5", 50}, {"e7e6", 30}, {"c7c5", 20}}},

// Contra 1.c4 (Inglesa)
{"c2c4 e7e5", {{"g1f3", 50}, {"b1c3", 50}}},
{"c2c4 e7e5 g1f3", {{"b8c6", 100}}},
{"c2c4 e7e5 b1c3", {{"g8f6", 70}, {"b8c6", 30}}},
```

### Mejora 4: **Sistema de Pesos Inteligente**

```cpp
enum WeightClass {
  MAIN_LINE   = 100,  // Línea principal del repertorio
  GOOD_ALT    = 70,   // Alternativa sólida
  PLAYABLE    = 40,   // Jugable pero no preferida
  SURPRISE    = 20,   // Arma sorpresa ocasional
  AVOID       = 5     // Solo si no hay otra opción
};

// Ejemplo de uso:
{"e2e4 c7c6", {
  {"d2d4", MAIN_LINE},      // Nuestra línea principal
  {"b1c3", GOOD_ALT},       // También sólida
  {"g1f3", PLAYABLE}        // Jugable
}},
```

### Mejora 5: **Salida Gradual del Libro**

```cpp
bool has_critical_tactics(const Board& b, const vector<Move>& legal) {
  // Si hay menos de 5 jugadas legales, probablemente táctica
  if (legal.size() < 5) return true;
  
  // Evaluación rápida
  int score = eval(b);
  
  // Si la evaluación es extrema, mejor calcular
  if (abs(score) > 200) return true;
  
  // Verificar si alguna pieza está en prise
  for (int sq = 0; sq < 64; sq++) {
    int8_t p = b.sq[sq];
    if (p && color_of(p) == b.side) {
      int piece_value = get_piece_value(abs_piece(p));
      if (piece_value >= 300) { // Pieza menor o mayor
        if (b.is_square_attacked(sq, Color(1 - b.side))) {
          // Está atacada, ¿defendida?
          if (!is_adequately_defended(b, sq)) {
            return true; // Pieza colgada
          }
        }
      }
    }
  }
  
  return false;
}

// En main.cpp (línea 1312):
auto book_move = opening_book_pick(move_history, legal_uci);
if (book_move && !has_critical_tactics(board, legal)) {
  cout << "bestmove " << *book_move << "\n";
  continue;
}
// Si hay táctica crítica, salir del libro y calcular
```

### Mejora 6: **Profundidad Variable por Línea**

```cpp
struct BookEntry {
  string uci;
  int weight;
  int max_depth;  // NUEVO: hasta qué profundidad seguir esta línea
  
  BookEntry(string u, int w, int d = 999) 
    : uci(u), weight(w), max_depth(d) {}
};

// Uso:
{"e2e4 c7c6 d2d4 d7d5 e4e5", {
  {{"c8f5", 50, 15}},  // Seguir hasta jugada 15
  {{"c8g4", 50, 12}}   // Esta línea solo hasta jugada 12
}},
```

---

## 📊 Métricas de Calidad

Para evaluar si el libro es bueno:

### 1. **Cobertura**
```
Posiciones después de:
- Jugada 2: ~10-15 variantes principales
- Jugada 5: ~30-50 posiciones
- Jugada 8: ~80-150 posiciones
- Jugada 12: ~150-300 posiciones
```

### 2. **Tasa de Error**
- Ejecutar validación: 0 errores
- Verificar con Stockfish: ninguna jugada con eval < -1.00

### 3. **Diversidad**
- Evitar monolines (solo 1 opción)
- Tener 2-3 alternativas en posiciones críticas

### 4. **Profundidad Efectiva**
```
Promedio de jugadas en libro:
- Apertura abierta (1.e4 e5): 8-12 jugadas
- Cerrada (1.d4 d5): 6-10 jugadas
- Siciliana: 5-8 jugadas (sale rápido por complejidad)
```

---

## 🚀 Plan de Implementación

### Fase 1: Corregir y Validar (1 día)
1. Crear función `validate_opening_book()`
2. Corregir todas las líneas con errores
3. Agregar test unitario

### Fase 2: Expandir Repertorio (2-3 días)
1. Completar Caro-Kann con todas las variantes del Advance
2. Expandir Italiana/Ruy López
3. Completar Semi-Slav/Meran
4. Agregar Londres (muy popular)

### Fase 3: Optimizar (1 día)
1. Cambiar a sistema de Zobrist keys
2. Implementar salida gradual
3. Ajustar pesos según estadísticas

### Fase 4: Testing (continuo)
1. Jugar 100+ partidas contra motores
2. Analizar con Stockfish/Leela
3. Refinar según resultados

---

## 🎯 Objetivos Finales

| Métrica | Actual | Objetivo |
|---------|--------|----------|
| Posiciones | ~45 | 300+ |
| Profundidad media | 5-6 | 8-10 |
| Errores tácticos | ??? | 0 |
| Cobertura 1.e4 | 30% | 80% |
| Cobertura 1.d4 | 20% | 70% |
| Validez | No verificado | 100% |

---

## 📚 Recursos Recomendados

Para crear un libro de calidad magistral:

1. **Bases de datos**:
   - Lichess Database (millones de partidas)
   - Chess.com Masters Database
   
2. **Análisis**:
   - Stockfish 16+ para verificar cada línea
   - ChessBase para estadísticas
   
3. **Libros teóricos**:
   - "Repertoire for Black against 1.e4" - Parimarjan Negi
   - "1.e4 vs The French, Caro-Kann and Philidor" - Vassilios Kotronias
   - "The Kaufman Repertoire for Black and White" - Larry Kaufman

4. **Automatización**:
   - Script Python para parsear PGN → generar opening_book_table()
   - Verificación automática con motor
