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
  const promotion = uci.length === 5 ? uci[4] : undefined; // q r b n
  return { from, to, promotion };
}

async function fetchBotMove(fen: string, movetimeMs: number) {
  const res = await fetch("/api/move", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ fen, movetime_ms: movetimeMs }),
  });
  if (!res.ok) throw new Error(`api/move failed: ${res.status}`);
  const data = await res.json();
  return data.uci as string | null;
}


export default function App() {
  const gameRef = useRef(new Chess());
  const [movetimeMs, setMovetimeMs] = useState(200);
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

const inFlightRef = useRef(false);

async function makeBotMoveIfNeeded() {
  if (!isBotsTurn()) return;
  if (inFlightRef.current) return;

  inFlightRef.current = true;
  setBusy(true);
  try {
    const fen = gameRef.current.fen();
    const res = await fetch("/api/move", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ fen, movetime_ms: 200 }),
    });
    const data = await res.json();
    const uci = data.uci as string | null;
    if (!uci) return;

    gameRef.current.move({
      from: uci.slice(0,2),
      to: uci.slice(2,4),
      promotion: uci.length === 5 ? uci[4] : "q",
    });
    setFen(gameRef.current.fen());
  } catch (e) {
    console.error(e);
  } finally {
    setBusy(false);
    inFlightRef.current = false;
  }
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
        movetime ms
        <input
          type="number"
          min={20}
          max={2000}
          value={movetimeMs}
          onChange={(e) => setMovetimeMs(Number(e.target.value))}
          style={{ width: 90, marginLeft: 8 }}
          disabled={busy}
        />

          Jugás:
          <select
            value={playerColor}
            onChange={(e) => {
              const next = e.target.value as Color;
              resetGame(next);
              setTimeout(() => void makeBotMoveExplicit(opposite(next)), 0);
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
