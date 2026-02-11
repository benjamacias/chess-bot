// =============================================================================
// MEJORA PARA main.cpp - Sistema de detección de peligro profundo
// =============================================================================
//
// INSTRUCCIONES: Reemplazar la función has_critical_tactics() en main.cpp
// con esta versión mejorada que mira 2-3 jugadas adelante
//

// Función auxiliar: evaluar si una posición es tácticamente crítica
static bool is_position_tactical(const Board& b) {
  // Criterio 1: Pocas jugadas legales = posición forzada/táctica
  vector<Move> legal;
  b.gen_legal(legal);
  if (legal.size() < 5) return true;
  
  // Criterio 2: Estamos en jaque
  if (b.in_check(b.side)) return true;
  
  // Criterio 3: Evaluación extrema
  int ev = eval(b);
  if (abs(ev) > 200) return true;  // Más de 2 peones de ventaja
  
  return false;
}

// Función auxiliar: verificar si hay piezas colgadas
static bool has_hanging_pieces(const Board& b) {
  for (int sq = 0; sq < 64; sq++) {
    int8_t p = b.sq[sq];
    if (p == 0 || color_of(p) != b.side) continue;
    
    int piece_type = abs_piece(p);
    if (piece_type == PAWN) continue;  // Peones no cuentan como "colgadas"
    
    // Si la pieza está atacada
    if (b.is_square_attacked(sq, Color(1 - b.side))) {
      // Verificar si está defendida adecuadamente
      // Para simplificar: si está atacada y vale >=300cp, considerarla en peligro
      int piece_value = 0;
      switch (piece_type) {
        case KNIGHT: piece_value = 300; break;
        case BISHOP: piece_value = 320; break;
        case ROOK: piece_value = 500; break;
        case QUEEN: piece_value = 900; break;
        case KING: continue;  // Rey no cuenta
      }
      
      if (piece_value >= 300) {
        // Contar atacantes
        int attackers = 0;
        int defenders = 0;
        
        // Contar atacantes del oponente
        for (int asq = 0; asq < 64; asq++) {
          int8_t ap = b.sq[asq];
          if (ap != 0 && color_of(ap) == Color(1 - b.side)) {
            // Verificar si esta pieza ataca sq
            vector<Move> aMoves;
            // Generar pseudo-legales desde asq (simplificado)
            // Por simplicidad, solo contamos 1 atacante si is_square_attacked devuelve true
            attackers = 1;
            break;
          }
        }
        
        // Si hay atacantes y la pieza está colgada, es crítico
        if (attackers > 0) {
          return true;
        }
      }
    }
  }
  return false;
}

// FUNCIÓN PRINCIPAL MEJORADA
// Detecta peligros tácticos mirando 2-3 jugadas adelante
static bool has_critical_tactics(Board& b, const vector<Move>& legal) {
  // Paso 1: Verificar posición actual
  if (is_position_tactical(b)) return true;
  if (has_hanging_pieces(b)) return true;
  
  // Paso 2: Mirar 1 jugada adelante (búsqueda superficial)
  // Si alguna de nuestras jugadas cambia drásticamente la evaluación, es táctica
  int current_eval = eval(b);
  
  for (const auto& m : legal) {
    Undo u;
    b.make_move(m, u);
    
    // Verificar si después de nuestro movimiento:
    // 1. El oponente queda en una posición táctica
    if (b.in_check(b.side)) {
      b.unmake_move(m, u);
      return true;  // Estamos dando jaque = táctica
    }
    
    // 2. Hay un cambio drástico de evaluación
    int new_eval = -eval(b);  // Desde nuestro punto de vista
    if (abs(new_eval - current_eval) > 150) {
      b.unmake_move(m, u);
      return true;  // Cambio táctico grande
    }
    
    b.unmake_move(m, u);
  }
  
  // Paso 3: Mirar 2 jugadas adelante (nosotros -> oponente -> nosotros)
  // Solo si tenemos tiempo (opcional)
  // Buscar si el oponente tiene alguna táctica devastadora
  
  for (const auto& our_move : legal) {
    Undo u1;
    b.make_move(our_move, u1);
    
    vector<Move> opponent_moves;
    b.gen_legal(opponent_moves);
    
    // Ver si el oponente tiene alguna jugada táctica fuerte
    for (const auto& opp_move : opponent_moves) {
      Undo u2;
      b.make_move(opp_move, u2);
      
      // Desde el punto de vista del oponente
      int opp_eval = eval(b);
      
      // Si el oponente puede ganar mucho material o dar mate
      if (opp_eval > 300) {  // Oponente gana 3+ peones
        b.unmake_move(opp_move, u2);
        b.unmake_move(our_move, u1);
        return true;  // Peligro inminente
      }
      
      // Si nos dan jaque mate en 1
      vector<Move> our_responses;
      b.gen_legal(our_responses);
      if (our_responses.empty() && b.in_check(b.side)) {
        b.unmake_move(opp_move, u2);
        b.unmake_move(our_move, u1);
        return true;  // Mate en 2
      }
      
      b.unmake_move(opp_move, u2);
    }
    
    b.unmake_move(our_move, u1);
  }
  
  return false;  // No hay peligro táctico obvio
}

// =============================================================================
// CÓMO USAR ESTA FUNCIÓN
// =============================================================================
//
// En main.cpp, alrededor de la línea 1312, REEMPLAZAR:
//
// ANTES:
//   auto book_move = opening_book_pick(move_history, legal_uci);
//   if (book_move && !has_critical_tactics(board, legal)) {
//     cout << "bestmove " << *book_move << "\n";
//     continue;
//   }
//
// AHORA (mejorado):
//   auto book_move = opening_book_pick(move_history, legal_uci);
//   if (book_move) {
//     // Verificar si la jugada del libro es segura
//     if (!has_critical_tactics(board, legal)) {
//       cout << "bestmove " << *book_move << "\n";
//       continue;
//     } else {
//       // Hay táctica, pero verificar si la jugada del libro la resuelve
//       // Hacer la jugada del libro y ver si la táctica persiste
//       Move book_m;
//       bool found = false;
//       for (const auto& m : legal) {
//         if (move_to_uci(m) == *book_move) {
//           book_m = m;
//           found = true;
//           break;
//         }
//       }
//       
//       if (found) {
//         Undo u;
//         board.make_move(book_m, u);
//         
//         vector<Move> opp_legal;
//         board.gen_legal(opp_legal);
//         
//         // Si después de la jugada del libro ya no hay peligro, usarla
//         if (!has_critical_tactics(board, opp_legal)) {
//           board.unmake_move(book_m, u);
//           cout << "bestmove " << *book_move << "\n";
//           continue;
//         }
//         
//         board.unmake_move(book_m, u);
//       }
//       
//       // Si llegamos aquí, hay táctica que el libro no resuelve
//       // Salir del libro y calcular
//       cout << "info string tactical_position_detected\n";
//     }
//   }
//
// =============================================================================
