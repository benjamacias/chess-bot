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

function uciToMove(uci: string) {
  const from = uci.slice(0, 2);
  const to = uci.slice(2, 4);
  const promotion = uci.length === 5 ? uci[4] : undefined;
  return { from, to, promotion };
}

async function fetchBotMove(fen: string, movetimeMs: number) {
  const res = await fetch("/api/move", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ fen, movetime_ms: movetimeMs }),
  });

  if (!res.ok) {
    throw new Error(`/api/move failed: ${res.status}`);
  }

  const data = (await res.json()) as { uci: string | null };
  return data.uci;
}

export default function App() {
  const gameRef = useRef(new Chess());
  const inFlightRef = useRef(false);

  const [movetimeMs, setMovetimeMs] = useState(200);
  const [playerColor, setPlayerColor] = useState<Color>("white");
  const [boardView, setBoardView] = useState<Color>("white");
  const [fen, setFen] = useState(gameRef.current.fen());
  const [busy, setBusy] = useState(false);

  function resetGame(nextPlayerColor = playerColor) {
    gameRef.current = new Chess();
    setPlayerColor(nextPlayerColor);
    setFen(gameRef.current.fen());
    setBusy(false);
    inFlightRef.current = false;
  }

  function isPlayersTurn() {
    return gameRef.current.turn() === toTurnChar(playerColor);
  }

  async function makeBotMoveExplicit(botColor: Color) {
    if (inFlightRef.current) return;
    if (gameRef.current.turn() !== toTurnChar(botColor)) return;

    inFlightRef.current = true;
    setBusy(true);

    try {
      const currentFen = gameRef.current.fen();
      const uci = await fetchBotMove(currentFen, movetimeMs);
      if (!uci) return;

      const move = gameRef.current.move(uciToMove(uci));
      if (!move) {
        throw new Error(`Invalid UCI from backend: ${uci}`);
      }

      setFen(gameRef.current.fen());
    } catch (error) {
      console.error("Bot move error", error);
    } finally {
      inFlightRef.current = false;
      setBusy(false);
    }
  }

  const chessboardOptions = useMemo(() => {
    return {
      id: "chess-ui",
      position: fen,
      boardOrientation: boardView,
      allowDragging: true,
      allowDragOffBoard: false,
      canDragPiece: ({ isSparePiece, piece }: { isSparePiece: boolean; piece: { pieceType: string } }) => {
        if (isSparePiece) return false;
        if (busy) return false;
        if (!isPlayersTurn()) return false;

        const myPrefix = playerColor === "white" ? "w" : "b";
        return piece.pieceType.startsWith(myPrefix);
      },
      onPieceDrop: ({ sourceSquare, targetSquare, piece }: any) => {
        if (busy) return false;
        if (!targetSquare) return false;
        if (!isPlayersTurn()) return false;

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
        void makeBotMoveExplicit(opposite(playerColor));
        return true;
      },
    };
  }, [fen, boardView, busy, playerColor]);

  return (
    <div style={{ maxWidth: 520, margin: "24px auto", padding: 16 }}>
      <div style={{ display: "flex", gap: 12, alignItems: "center", marginBottom: 12, flexWrap: "wrap" }}>
        <label>
          Movetime ms
          <input
            type="number"
            min={20}
            max={2000}
            value={movetimeMs}
            onChange={(e) => setMovetimeMs(Math.max(20, Number(e.target.value) || 200))}
            style={{ width: 90, marginLeft: 8 }}
            disabled={busy}
          />
        </label>

        <label>
          Jugás:
          <select
            value={playerColor}
            onChange={(e) => {
              const nextPlayerColor = e.target.value as Color;
              resetGame(nextPlayerColor);
              void makeBotMoveExplicit(opposite(nextPlayerColor));
            }}
            style={{ marginLeft: 8 }}
            disabled={busy}
          >
            <option value="white">Blancas</option>
            <option value="black">Negras</option>
          </select>
        </label>

        <button onClick={() => resetGame(playerColor)} disabled={busy}>
          Reiniciar
        </button>

        <button onClick={() => setBoardView((prev) => opposite(prev))} disabled={busy}>
          Rotar tablero
        </button>

        <span style={{ opacity: 0.8 }}>
          Turno: {gameRef.current.turn() === "w" ? "Blancas" : "Negras"}
          {busy ? " (bot…)" : ""}
        </span>
      </div>

      <Chessboard options={chessboardOptions} />
    </div>
  );
}
