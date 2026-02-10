#include <bits/stdc++.h>
using namespace std;

// TODO: implementar Board real + movegen.
// Por ahora: respondemos "bestmove 0000" (sin jugada) para verificar cableado.
static void trim(string &s){
  while(!s.empty() && (s.back()=='\r' || s.back()=='\n')) s.pop_back();
}

int main() {
  ios::sync_with_stdio(false);
  cin.tie(nullptr);

  string line;
  while (getline(cin, line)) {
    trim(line);

    if (line == "uci") {
      cout << "id name BM-Engine\n";
      cout << "id author Benja\n";
      // options típicas (las podés implementar después)
      cout << "option name Hash type spin default 64 min 1 max 2048\n";
      cout << "option name Threads type spin default 1 min 1 max 32\n";
      cout << "uciok\n";
    }
    else if (line == "isready") {
      cout << "readyok\n";
    }
    else if (line.rfind("ucinewgame", 0) == 0) {
      // reset estado interno si hace falta
    }
    else if (line.rfind("position", 0) == 0) {
      // parsear "position startpos moves ..." o "position fen ... moves ..."
      // TODO
    }
    else if (line.rfind("go", 0) == 0) {
      // parsear tiempos/limits: wtime, btime, movetime, depth, nodes, etc.
      // TODO: llamar a tu search y devolver un movimiento.
      cout << "bestmove 0000\n";
    }
    else if (line == "quit") {
      break;
    }
  }
  return 0;
}

