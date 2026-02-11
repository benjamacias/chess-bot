import React, { useEffect, useMemo, useState } from "react";
import { Chess } from "chess.js";
import { Chessboard } from "react-chessboard";

const NO_PROGRESS_TIMEOUT_MS = 3000;

function uciToMove(uci) {
  const from = uci.slice(0, 2);
  const to = uci.slice(2, 4);
  const promotion = uci.length === 5 ? uci[4] : undefined; // q r b n
  return { from, to, promotion };
}

function createRequestId() {
  if (globalThis.crypto?.randomUUID) return globalThis.crypto.randomUUID();
  return `req-${Date.now()}-${Math.floor(Math.random() * 1_000_000)}`;
}

async function fetchBotMove(fen, movetimeMs, requestId) {
  const res = await fetch("/api/move", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ fen, movetime_ms: movetimeMs, request_id: requestId }),
  });
  if (!res.ok) throw new Error(`api/move failed: ${res.status}`);
  const data = await res.json();
  return data.uci; // string o null
}

async function fetchMoveStatus(requestId) {
  const res = await fetch(`/api/move/status/${requestId}`);
  if (!res.ok) throw new Error(`api/move/status failed: ${res.status}`);
  return res.json();
}

function formatEvaluation(score) {
  if (!score) return "—";
  if (score.type === "cp") {
    const pawns = score.value / 100;
    return `${pawns >= 0 ? "+" : ""}${pawns.toFixed(2)}`;
  }
  if (score.type === "mate") {
    return `Mate ${score.value >= 0 ? "+" : ""}${score.value}`;
  }
  return "—";
}

export default function ChessGame() {
  const game = useMemo(() => new Chess(), []);
  const [fen, setFen] = useState(game.fen());
  const [playerColor, setPlayerColor] = useState("w"); // "w" o "b"
  const [thinking, setThinking] = useState(false);
  const [movetimeMs, setMovetimeMs] = useState(200);
  const [thinkingStartedAt, setThinkingStartedAt] = useState(null);
  const [elapsedMs, setElapsedMs] = useState(0);
  const [engineStatus, setEngineStatus] = useState({ depth: null, score: null, pv: "", lastInfoAt: null });

  const turn = fen.split(" ")[1]; // "w" o "b"
  const isPlayersTurn = turn === playerColor;

  const noProgressReported =
    thinking &&
    thinkingStartedAt &&
    Date.now() - (engineStatus.lastInfoAt ?? thinkingStartedAt) >= NO_PROGRESS_TIMEOUT_MS;

  useEffect(() => {
    if (!thinking || !thinkingStartedAt) {
      setElapsedMs(0);
      return;
    }

    setElapsedMs(Date.now() - thinkingStartedAt);
    const timer = setInterval(() => {
      setElapsedMs(Date.now() - thinkingStartedAt);
    }, 250);

    return () => clearInterval(timer);
  }, [thinking, thinkingStartedAt]);

  function sync() {
    setFen(game.fen());
  }

  async function botPlayIfNeeded() {
    if (game.isGameOver()) return;
    const t = game.turn(); // "w" o "b"
    if (t === playerColor) return;

    setThinking(true);
    setThinkingStartedAt(Date.now());
    setEngineStatus({ depth: null, score: null, pv: "", lastInfoAt: null });

    const requestId = createRequestId();
    let pollTimer = null;
    let keepPolling = true;

    const pollStatus = async () => {
      if (!keepPolling) return;
      try {
        const status = await fetchMoveStatus(requestId);
        setEngineStatus({
          depth: status.depth,
          score: status.score,
          pv: status.pv,
          lastInfoAt: status.lastInfoAt,
        });
      } catch {
        // ignore polling errors to avoid romper flujo del juego
      } finally {
        if (keepPolling) {
          pollTimer = setTimeout(pollStatus, 250);
        }
      }
    };

    pollTimer = setTimeout(pollStatus, 100);

    try {
      const uci = await fetchBotMove(game.fen(), movetimeMs, requestId);
      if (!uci) return;

      const mv = uciToMove(uci);
      // Si el bot promociona sin letra, default a reina
      const result = game.move({
        from: mv.from,
        to: mv.to,
        promotion: mv.promotion ?? "q",
      });

      if (!result) {
        console.warn("Bot move inválido en UI:", uci, "fen:", game.fen());
      }
      sync();
    } finally {
      keepPolling = false;
      if (pollTimer) clearTimeout(pollTimer);
      setThinking(false);
    }
  }

  async function onPieceDrop(sourceSquare, targetSquare) {
    // Bloqueo si no es tu turno o si el bot está pensando
    if (thinking) return false;
    if (!isPlayersTurn) return false;

    // Promoción: default queen (para no frenar UX)
    const isPromotion =
      sourceSquare[1] === "7" && targetSquare[1] === "8" && game.get(sourceSquare)?.type === "p" && playerColor === "w"
        ? true
        : sourceSquare[1] === "2" && targetSquare[1] === "1" && game.get(sourceSquare)?.type === "p" && playerColor === "b";

    const move = game.move({
      from: sourceSquare,
      to: targetSquare,
      promotion: isPromotion ? "q" : undefined,
    });

    if (move === null) return false;

    sync();
    await botPlayIfNeeded();
    return true;
  }

  function newGame(color) {
    game.reset();
    setPlayerColor(color);
    sync();
  }

  // Si el usuario elige negras, el bot (blancas) juega al inicio
  useEffect(() => {
    // cada vez que cambia color o arranca
    (async () => {
      if (game.history().length === 0) {
        await botPlayIfNeeded();
      }
    })();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [playerColor]);

  return (
    <div style={{ maxWidth: 520, margin: "24px auto", fontFamily: "system-ui" }}>
      <div style={{ display: "flex", gap: 12, alignItems: "center", marginBottom: 12, flexWrap: "wrap" }}>
        <button onClick={() => newGame("w")} disabled={thinking}>
          Jugar Blancas
        </button>
        <button onClick={() => newGame("b")} disabled={thinking}>
          Jugar Negras
        </button>

        <label style={{ display: "flex", gap: 8, alignItems: "center" }}>
          movetime ms
          <input
            type="number"
            min={20}
            max={2000}
            value={movetimeMs}
            onChange={(e) => setMovetimeMs(Number(e.target.value))}
            style={{ width: 90 }}
            disabled={thinking}
          />
        </label>

        <div style={{ opacity: 0.8 }}>
          Turno: <b>{turn === "w" ? "Blancas" : "Negras"}</b> {thinking ? "(pensando…)" : ""}
        </div>
      </div>

      <Chessboard
        position={fen}
        onPieceDrop={onPieceDrop}
        boardOrientation={playerColor === "w" ? "white" : "black"}
        arePiecesDraggable={!thinking && isPlayersTurn}
      />

      <div style={{ marginTop: 12, padding: 10, border: "1px solid #ddd", borderRadius: 8, background: "#fafafa" }}>
        <div style={{ fontWeight: 600, marginBottom: 6 }}>Motor pensando</div>
        <div style={{ fontSize: 13 }}>Profundidad: {engineStatus.depth ?? "—"}</div>
        <div style={{ fontSize: 13 }}>Evaluación: {formatEvaluation(engineStatus.score)}</div>
        <div style={{ fontSize: 13, wordBreak: "break-word" }}>PV: {engineStatus.pv || "—"}</div>
        <div style={{ fontSize: 13 }}>Tiempo: {(elapsedMs / 1000).toFixed(1)}s</div>
        {noProgressReported ? (
          <div style={{ marginTop: 6, fontSize: 12, color: "#b45309" }}>sin progreso reportado</div>
        ) : null}
      </div>

      <div style={{ marginTop: 12, fontSize: 12, opacity: 0.8 }}>FEN: {fen}</div>
    </div>
  );
}
