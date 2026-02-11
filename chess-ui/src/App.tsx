import { useCallback, useMemo, useRef, useState } from "react";
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
  const requestId =
    globalThis.crypto?.randomUUID?.() ?? `req-${Date.now()}-${Math.floor(Math.random() * 1_000_000)}`;

  const res = await fetch("/api/move", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "x-request-id": requestId,
    },
    body: JSON.stringify({ fen, movetime_ms: movetimeMs }),
  });

  if (!res.ok) {
    throw new Error(`/api/move failed: ${res.status}`);
  }

  const data = (await res.json()) as { uci: string | null; terminal?: boolean; reason?: string | null };
  return data;
}

function reasonToMessage(reason?: string | null) {
  if (reason === "CHECKMATE") return "Sin jugadas legales: jaque mate.";
  if (reason === "NO_LEGAL_MOVES") return "Sin jugadas legales: posición terminal (p. ej. ahogado).";
  return "No hay jugadas legales para el motor en esta posición.";
}

type PieceDrop = {
  sourceSquare: string;
  targetSquare: string | null;
  piece: { pieceType: string };
};

export default function App() {
  const gameRef = useRef(new Chess());
  const inFlightRef = useRef(false);

  const [movetimeMs, setMovetimeMs] = useState(200);
  const [playerColor, setPlayerColor] = useState<Color>("white");
  const [boardView, setBoardView] = useState<Color>("white");
  const [fen, setFen] = useState(gameRef.current.fen());
  const [busy, setBusy] = useState(false);
  const [engineMessage, setEngineMessage] = useState("");

  function resetGame(nextPlayerColor = playerColor) {
    gameRef.current = new Chess();
    setPlayerColor(nextPlayerColor);
    setFen(gameRef.current.fen());
    setBusy(false);
    setEngineMessage("");
    inFlightRef.current = false;
  }

  const isPlayersTurn = useCallback(() => {
    return gameRef.current.turn() === toTurnChar(playerColor);
  }, [playerColor]);

  const makeBotMoveExplicit = useCallback(async (botColor: Color) => {
    if (inFlightRef.current) return;
    if (gameRef.current.turn() !== toTurnChar(botColor)) return;

    inFlightRef.current = true;
    setBusy(true);

    try {
      const currentFen = gameRef.current.fen();
      const result = await fetchBotMove(currentFen, movetimeMs);
      if (!result.uci) {
        if (result.terminal) {
          setEngineMessage(reasonToMessage(result.reason));
        }
        return;
      }

      const move = gameRef.current.move(uciToMove(result.uci));
      if (!move) {
        throw new Error(`Invalid UCI from backend: ${result.uci}`);
      }

      setEngineMessage("");
      setFen(gameRef.current.fen());
    } catch (error) {
      console.error("Bot move error", error);
      setEngineMessage("No se pudo obtener jugada del motor. Reintentá.");
    } finally {
      inFlightRef.current = false;
      setBusy(false);
    }
  }, [movetimeMs]);

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
      onPieceDrop: ({ sourceSquare, targetSquare, piece }: PieceDrop) => {
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
  }, [fen, boardView, busy, playerColor, isPlayersTurn, makeBotMoveExplicit]);

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

      {engineMessage ? (
        <div style={{ marginTop: 12, color: "#b00020", fontSize: 13 }} role="alert">
          {engineMessage}
        </div>
      ) : null}
    </div>
  );
}
