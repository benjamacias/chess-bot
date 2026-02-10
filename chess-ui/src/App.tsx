import { useMemo, useRef, useState } from "react";
import { Chess } from "chess.js";
import { Chessboard } from "react-chessboard";

type Color = "white" | "black";

function opposite(c: Color): Color {
  return c === "white" ? "black" : "white";
}

function toTurnChar(c: Color) {
  return c === "white" ? "w" : "b";
}

export default function App() {
  const gameRef = useRef(new Chess());

  const [playerColor, setPlayerColor] = useState<Color>("white");
  const [fen, setFen] = useState(gameRef.current.fen());
  const [busy, setBusy] = useState(false);

  const botColor = opposite(playerColor);

  function resetGame(nextPlayerColor = playerColor) {
    gameRef.current = new Chess();
    setPlayerColor(nextPlayerColor);
    setFen(gameRef.current.fen());
    setBusy(false);
  }

  function isPlayersTurn() {
    return gameRef.current.turn() === toTurnChar(playerColor);
  }

  function isBotsTurn() {
    return gameRef.current.turn() === toTurnChar(botColor);
  }

  function pickRandomBotMove() {
    const moves = gameRef.current.moves({ verbose: true });
    if (moves.length === 0) return null;
    return moves[Math.floor(Math.random() * moves.length)];
  }

  async function makeBotMoveIfNeeded() {
    if (!isBotsTurn()) return;

    setBusy(true);
    // Simula “pensar” un toque (también ayuda a que se vea el move del usuario antes)
    await new Promise((r) => setTimeout(r, 150));

    // TODO: reemplazá esto por tu bot local (fetch a tu backend)
    const m = pickRandomBotMove();
    if (!m) {
      setBusy(false);
      return;
    }

    gameRef.current.move(m);
    setFen(gameRef.current.fen());
    setBusy(false);
  }

  const chessboardOptions = useMemo(() => {
    return {
      id: "chess-ui",
      position: fen,
      boardOrientation: playerColor, // orienta la vista según tu color
      allowDragging: true,
      allowDragOffBoard: false,

      // Bloquea drags si no es tu turno o si estás esperando al bot
      canDragPiece: ({ isSparePiece, piece }: { isSparePiece: boolean; piece: { pieceType: string } }) => {
        if (isSparePiece) return false;
        if (busy) return false;
        if (!isPlayersTurn()) return false;

        // pieceType en v5 es string (ej: "wP", "bK") :contentReference[oaicite:2]{index=2}
        const myPrefix = playerColor === "white" ? "w" : "b";
        return piece.pieceType.startsWith(myPrefix);
      },

      // Drag & drop handler (v5)
      onPieceDrop: ({ sourceSquare, targetSquare, piece }: any) => {
        if (busy) return false;
        if (!targetSquare) return false;
        if (!isPlayersTurn()) return false;

        // Promoción simple: siempre a dama si corresponde
        const isPawn = piece?.pieceType?.endsWith("P");
        const targetRank = targetSquare[1];
        const needsPromotion =
          isPawn && ((playerColor === "white" && targetRank === "8") || (playerColor === "black" && targetRank === "1"));

        const move = gameRef.current.move({
          from: sourceSquare,
          to: targetSquare,
          promotion: needsPromotion ? "q" : undefined,
        });

        if (!move) return false;

        setFen(gameRef.current.fen());
        void makeBotMoveIfNeeded();
        return true;
      },
    };
  }, [fen, playerColor, busy]);

  return (
    <div style={{ maxWidth: 520, margin: "24px auto", padding: 16 }}>
      <div style={{ display: "flex", gap: 12, alignItems: "center", marginBottom: 12, flexWrap: "wrap" }}>
        <label>
          Jugás:
          <select
            value={playerColor}
            onChange={(e) => {
              const next = e.target.value as Color;
              // Reinicio para evitar estados raros al cambiar de lado
              resetGame(next);
              // Si elegís negras, el bot (blancas) arranca
              setTimeout(() => void makeBotMoveIfNeeded(), 0);
            }}
            style={{ marginLeft: 8 }}
          >
            <option value="white">Blancas</option>
            <option value="black">Negras</option>
          </select>
        </label>

        <button onClick={() => resetGame(playerColor)}>Reiniciar</button>

        <span style={{ opacity: 0.8 }}>
          Turno: {gameRef.current.turn() === "w" ? "Blancas" : "Negras"}
          {busy ? " (bot…)" : ""}
        </span>
      </div>

      <Chessboard options={chessboardOptions} />
    </div>
  );
}
