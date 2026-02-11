import React, { useEffect, useMemo, useState } from "react";
import { Chess } from "chess.js";
import { Chessboard } from "react-chessboard";

const LEVEL_PRESETS = {
  rapido: {
    label: "Rápido",
    skill: "blitz",
    minMovetimeMs: 150,
    maxMovetimeMs: 250,
    depth: undefined,
    hashMb: 64,
  },
  medio: {
    label: "Medio",
    skill: "rapid",
    minMovetimeMs: 500,
    maxMovetimeMs: 800,
    depth: undefined,
    hashMb: 96,
  },
  fuerte: {
    label: "Fuerte",
    skill: "strong",
    minMovetimeMs: 1200,
    maxMovetimeMs: 2000,
    depth: 10,
    hashMb: 192,
  },
};
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

function randomInt(min, max) {
  return Math.floor(Math.random() * (max - min + 1)) + min;
}

async function fetchBotMove(fen, options, requestId, signal) {
  const { skill, movetime_ms, depth, hash_mb } = options;
  const res = await fetch("/api/move", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "x-request-id": requestId,
    },
    signal,
    body: JSON.stringify({ fen, skill, movetime_ms, depth, hash_mb }),
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
  const [level, setLevel] = useState("rapido");
  const [movetimeMs, setMovetimeMs] = useState(200);
  const [thinkingStartedAt, setThinkingStartedAt] = useState(null);
  const [elapsedMs, setElapsedMs] = useState(0);
  const [engineStatus, setEngineStatus] = useState({ depth: null, score: null, pv: "", lastInfoAt: null });
  const [engineError, setEngineError] = useState("");

  const turn = fen.split(" ")[1]; // "w" o "b"
  const isPlayersTurn = turn === playerColor;
  const levelPreset = LEVEL_PRESETS[level] ?? LEVEL_PRESETS.rapido;

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

    const timeoutMs = Math.max(movetimeMs + 1500, 2500);
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), timeoutMs);

    setThinking(true);
    setThinkingStartedAt(Date.now());
    setEngineStatus({ depth: null, score: null, pv: "", lastInfoAt: null });
    setEngineError("");

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
      const movetimeMs = randomInt(levelPreset.minMovetimeMs, levelPreset.maxMovetimeMs);
      const uci = await fetchBotMove(
        game.fen(),
        {
          skill: levelPreset.skill,
          movetime_ms: movetimeMs,
          depth: levelPreset.depth,
          hash_mb: levelPreset.hashMb,
        },
        requestId,
        controller.signal
      );
      if (!uci) {
        setEngineError("No se pudo obtener una jugada del motor. Intentá nuevamente.");
        return;
      }

      const mv = uciToMove(uci);
      // Si el bot promociona sin letra, default a reina
      const result = game.move({
        from: mv.from,
        to: mv.to,
        promotion: mv.promotion ?? "q",
      });

      if (!result) {
        console.warn("Bot move inválido en UI:", uci, "fen:", game.fen());
        setEngineError("El motor devolvió una jugada inválida. Reintentá.");
        return;
      }
      sync();
    } catch (error) {
      console.error("Error consultando al motor:", error);
      if (error?.name === "AbortError") {
        setEngineError("El motor tardó demasiado en responder. Reintentá.");
      } else {
        setEngineError("No se pudo consultar al motor. Reintentá.");
      }
    } finally {
      keepPolling = false;
      if (pollTimer) clearTimeout(pollTimer);
      clearTimeout(timeoutId);
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
    setEngineError("");
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
          Nivel
          <select value={level} onChange={(e) => setLevel(e.target.value)} disabled={thinking}>
            <option value="rapido">Rápido</option>
            <option value="medio">Medio</option>
            <option value="fuerte">Fuerte</option>
          </select>
        </label>

        <div style={{ opacity: 0.8 }}>
          Turno: <b>{turn === "w" ? "Blancas" : "Negras"}</b> {thinking ? "(pensando…)" : ""}
        </div>
      </div>

      <div style={{ marginBottom: 12, fontSize: 13, lineHeight: 1.35, opacity: 0.92 }}>
        <b>{levelPreset.label}</b>: respuesta estimada entre <b>{levelPreset.minMovetimeMs}</b> y <b>{levelPreset.maxMovetimeMs} ms</b>
        {levelPreset.depth ? (
          <>
            {" "}(profundidad mínima <b>{levelPreset.depth}</b>)
          </>
        ) : null}
        . Mayor precisión implica más tiempo de respuesta.
      </div>

      {engineError ? (
        <div style={{ marginBottom: 12, color: "#b00020", fontSize: 13 }} role="alert">
          {engineError}
        </div>
      ) : null}

      <Chessboard
        position={fen}
        onPieceDrop={onPieceDrop}
        boardOrientation={playerColor === "w" ? "white" : "black"}
        arePiecesDraggable={!thinking && isPlayersTurn}
      />

      <div style={{ marginTop: 12, fontSize: 12, opacity: 0.8 }}>FEN: {fen}</div>
    </div>
  );
}
