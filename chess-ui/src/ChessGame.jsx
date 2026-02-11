import React, { useEffect, useMemo, useRef, useState } from "react";
import { Chess } from "chess.js";
import { Chessboard } from "react-chessboard";

const LEVEL_PRESETS = {
  rapido: { label: "Rápido", skill: "blitz", minMovetimeMs: 150, maxMovetimeMs: 250, depth: undefined, hashMb: 64 },
  medio: { label: "Medio", skill: "rapid", minMovetimeMs: 500, maxMovetimeMs: 800, depth: undefined, hashMb: 96 },
  fuerte: { label: "Fuerte", skill: "strong", minMovetimeMs: 1200, maxMovetimeMs: 2000, depth: 10, hashMb: 192 },
};

const NO_PROGRESS_TIMEOUT_MS = 3000;

function uciToMove(uci) {
  return { from: uci.slice(0, 2), to: uci.slice(2, 4), promotion: uci.length === 5 ? uci[4] : undefined };
}

function createRequestId() {
  if (globalThis.crypto?.randomUUID) return globalThis.crypto.randomUUID();
  return `req-${Date.now()}-${Math.floor(Math.random() * 1_000_000)}`;
}

function randomInt(min, max) {
  return Math.floor(Math.random() * (max - min + 1)) + min;
}

function historyToUci(game) {
  return game
    .history({ verbose: true })
    .map((m) => `${m.from}${m.to}${m.promotion ?? ""}`);
}

function makeCacheKey(fen, movesUci) {
  return `${fen}|${movesUci.join(" ")}`;
}

async function fetchBotMove({ fen, movesUci, options, requestId, signal }) {
  const { skill, movetime_ms, depth, hash_mb } = options;
  const res = await fetch("/api/move", {
    method: "POST",
    headers: { "Content-Type": "application/json", "x-request-id": requestId },
    signal,
    body: JSON.stringify({ fen, moves_uci: movesUci, skill, movetime_ms, depth, hash_mb }),
  });
  if (!res.ok) throw new Error(`api/move failed: ${res.status}`);
  return res.json();
}

async function fetchMoveStatus(requestId) {
  const res = await fetch(`/api/move/status/${requestId}`);
  if (!res.ok) throw new Error(`api/move/status failed: ${res.status}`);
  return res.json();
}

async function fetchHint({ fen, movesUci, multipv, movetimeMs, signal }) {
  const res = await fetch("/api/hint", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    signal,
    body: JSON.stringify({ fen, moves_uci: movesUci, multipv, movetime_ms: movetimeMs }),
  });
  if (!res.ok) throw new Error(`api/hint failed: ${res.status}`);
  return res.json();
}

function formatEvaluation(score) {
  if (!score) return "—";
  if (score.type === "cp") return `${score.value >= 0 ? "+" : ""}${(score.value / 100).toFixed(2)}`;
  if (score.type === "mate") return `Mate ${score.value >= 0 ? "+" : ""}${score.value}`;
  return "—";
}

export default function ChessGame() {
  const game = useMemo(() => new Chess(), []);
  const [fen, setFen] = useState(game.fen());
  const [playerColor, setPlayerColor] = useState("w");
  const [thinking, setThinking] = useState(false);
  const [level, setLevel] = useState("rapido");
  const [movetimeMs, setMovetimeMs] = useState(200);
  const [thinkingStartedAt, setThinkingStartedAt] = useState(null);
  const [elapsedMs, setElapsedMs] = useState(0);
  const [engineStatus, setEngineStatus] = useState({ depth: null, score: null, pv: "", lastInfoAt: null });
  const [engineError, setEngineError] = useState("");
  const [showHint, setShowHint] = useState(true);
  const [hintLines, setHintLines] = useState(2);
  const [hintData, setHintData] = useState({ best: null, lines: [] });

  const inFlightControllersRef = useRef(new Set());
  const prefetchCacheRef = useRef(new Map());
  const hintRequestSeqRef = useRef(0);

  const turn = fen.split(" ")[1];
  const isPlayersTurn = turn === playerColor;
  const levelPreset = LEVEL_PRESETS[level] ?? LEVEL_PRESETS.rapido;

  const noProgressReported =
    thinking && thinkingStartedAt && Date.now() - (engineStatus.lastInfoAt ?? thinkingStartedAt) >= NO_PROGRESS_TIMEOUT_MS;

  useEffect(() => {
    if (!thinking || !thinkingStartedAt) {
      setElapsedMs(0);
      return;
    }

    setElapsedMs(Date.now() - thinkingStartedAt);
    const timer = setInterval(() => setElapsedMs(Date.now() - thinkingStartedAt), 250);
    return () => clearInterval(timer);
  }, [thinking, thinkingStartedAt]);

  useEffect(() => {
    setMovetimeMs(levelPreset.minMovetimeMs);
  }, [levelPreset.minMovetimeMs]);

  useEffect(() => {
    return () => {
      for (const controller of inFlightControllersRef.current) controller.abort();
      inFlightControllersRef.current.clear();
    };
  }, []);

  function sync() {
    setFen(game.fen());
  }

  function abortAllRequests() {
    hintRequestSeqRef.current += 1;
    for (const controller of inFlightControllersRef.current) controller.abort();
    inFlightControllersRef.current.clear();
  }

  function addController(controller) {
    inFlightControllersRef.current.add(controller);
    return () => inFlightControllersRef.current.delete(controller);
  }

  async function refreshHintAndPrefetch() {
    if (!showHint || game.isGameOver()) {
      setHintData({ best: null, lines: [] });
      return;
    }

    if (!isPlayersTurn) {
      return;
    }

    const requestSeq = ++hintRequestSeqRef.current;
    const historyUci = historyToUci(game);
    const fenAtRequest = game.fen();
    const shouldSkipPrefetch = hintLines < 2;
    const hintController = new AbortController();
    const cleanupHint = addController(hintController);

    try {
      const hint = await fetchHint({
        fen: game.fen(),
        movesUci: historyUci,
        multipv: hintLines,
        movetimeMs: 100,
        signal: hintController.signal,
      });

      const fenStillMatches = game.fen() === fenAtRequest;
      const sameHistory = makeCacheKey(fenAtRequest, historyUci) === makeCacheKey(game.fen(), historyToUci(game));
      if (requestSeq !== hintRequestSeqRef.current || !fenStillMatches || !sameHistory || !showHint || !isPlayersTurn) {
        return;
      }

      const topLines = (hint.lines ?? []).slice(0, hintLines);
      setHintData({ best: hint.best ?? topLines[0]?.uci ?? null, lines: topLines });

      if (shouldSkipPrefetch) return;

      const candidates = topLines.map((line) => line.uci).filter(Boolean);
      for (const candidateMove of candidates) {
        const preview = new Chess(game.fen());
        const parsed = uciToMove(candidateMove);
        const candidateApplied = preview.move({ from: parsed.from, to: parsed.to, promotion: parsed.promotion ?? "q" });
        if (!candidateApplied) continue;

        const movesUci2 = [...historyUci, candidateMove];
        const fenAfterCandidate = preview.fen();
        const key = makeCacheKey(fenAfterCandidate, movesUci2);
        if (prefetchCacheRef.current.has(key)) continue;

        const moveController = new AbortController();
        const cleanupMove = addController(moveController);
        try {
          const response = await fetchBotMove({
            fen: fenAfterCandidate,
            movesUci: movesUci2,
            options: {
              skill: levelPreset.skill,
              movetime_ms: 120,
              depth: undefined,
              hash_mb: levelPreset.hashMb,
            },
            requestId: createRequestId(),
            signal: moveController.signal,
          });

          if (response?.uci) {
            prefetchCacheRef.current.set(key, {
              uci: response.uci,
              createdAt: Date.now(),
            });
          }
        } catch {
          // ignore prefetch failures
        } finally {
          cleanupMove();
        }
      }
    } catch (error) {
      if (error?.name !== "AbortError") {
        setHintData({ best: null, lines: [] });
      }
    } finally {
      cleanupHint();
    }
  }

  async function botPlayIfNeeded() {
    if (game.isGameOver()) return;
    if (game.turn() === playerColor) return;

    const historyUci = historyToUci(game);
    const key = makeCacheKey(game.fen(), historyUci);
    const cached = prefetchCacheRef.current.get(key);

    setThinking(true);
    setThinkingStartedAt(Date.now());
    setEngineStatus({ depth: null, score: null, pv: "", lastInfoAt: null });
    setEngineError("");

    if (cached?.uci) {
      const mv = uciToMove(cached.uci);
      const result = game.move({ from: mv.from, to: mv.to, promotion: mv.promotion ?? "q" });
      if (result) {
        sync();
        setThinking(false);
        await refreshHintAndPrefetch();
        return;
      }
    }

    const timeoutMs = Math.max(movetimeMs + 1500, 2500);
    const controller = new AbortController();
    const cleanupController = addController(controller);
    const timeoutId = setTimeout(() => controller.abort(), timeoutMs);

    const requestId = createRequestId();
    let pollTimer = null;
    let keepPolling = true;

    const pollStatus = async () => {
      if (!keepPolling) return;
      try {
        const status = await fetchMoveStatus(requestId);
        setEngineStatus({ depth: status.depth, score: status.score, pv: status.pv, lastInfoAt: status.lastInfoAt });
      } catch {
        // ignore
      } finally {
        if (keepPolling) pollTimer = setTimeout(pollStatus, 250);
      }
    };
    pollTimer = setTimeout(pollStatus, 100);

    try {
      const realMovetime = randomInt(levelPreset.minMovetimeMs, levelPreset.maxMovetimeMs);
      const response = await fetchBotMove({
        fen: game.fen(),
        movesUci: historyUci,
        options: {
          skill: levelPreset.skill,
          movetime_ms: realMovetime,
          depth: levelPreset.depth,
          hash_mb: levelPreset.hashMb,
        },
        requestId,
        signal: controller.signal,
      });

      if (!response?.uci) {
        setEngineError("No se pudo obtener una jugada del motor. Intentá nuevamente.");
        return;
      }

      const mv = uciToMove(response.uci);
      const result = game.move({ from: mv.from, to: mv.to, promotion: mv.promotion ?? "q" });
      if (!result) {
        setEngineError("El motor devolvió una jugada inválida. Reintentá.");
        return;
      }
      sync();
    } catch (error) {
      if (error?.name === "AbortError") setEngineError("El motor tardó demasiado en responder. Reintentá.");
      else setEngineError("No se pudo consultar al motor. Reintentá.");
    } finally {
      keepPolling = false;
      if (pollTimer) clearTimeout(pollTimer);
      clearTimeout(timeoutId);
      cleanupController();
      setThinking(false);
      await refreshHintAndPrefetch();
    }
  }

  async function onPieceDrop(sourceSquare, targetSquare) {
    if (thinking || !isPlayersTurn) return false;

    abortAllRequests();

    const isPromotion =
      (sourceSquare[1] === "7" && targetSquare[1] === "8" && game.get(sourceSquare)?.type === "p" && playerColor === "w") ||
      (sourceSquare[1] === "2" && targetSquare[1] === "1" && game.get(sourceSquare)?.type === "p" && playerColor === "b");

    const move = game.move({ from: sourceSquare, to: targetSquare, promotion: isPromotion ? "q" : undefined });
    if (!move) return false;

    sync();
    await botPlayIfNeeded();
    return true;
  }

  function newGame(color) {
    abortAllRequests();
    prefetchCacheRef.current.clear();
    game.reset();
    setPlayerColor(color);
    setEngineError("");
    setHintData({ best: null, lines: [] });
    sync();
  }

  useEffect(() => {
    (async () => {
      abortAllRequests();
      if (game.history().length === 0) {
        await botPlayIfNeeded();
      } else {
        await refreshHintAndPrefetch();
      }
    })();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [playerColor]);

  useEffect(() => {
    if (!thinking && isPlayersTurn && showHint && !game.isGameOver()) {
      refreshHintAndPrefetch();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [fen, thinking, isPlayersTurn, showHint, hintLines]);

  const hintArrows = showHint
    ? (hintData.lines ?? [])
        .slice(0, hintLines)
        .filter((line) => typeof line?.uci === "string" && line.uci.length >= 4)
        .map((line, idx) => [line.uci.slice(0, 2), line.uci.slice(2, 4), idx === 0 ? "#2e7d32" : "#1e88e5"])
    : [];

  const hintStatus = !showHint
    ? "off"
    : game.isGameOver()
      ? "game-over"
      : isPlayersTurn
        ? "active-turn"
        : "waiting-turn";

  return (
    <div style={{ maxWidth: 560, margin: "24px auto", fontFamily: "system-ui" }}>
      <div style={{ display: "flex", gap: 12, alignItems: "center", marginBottom: 12, flexWrap: "wrap" }}>
        <button onClick={() => newGame("w")} disabled={thinking}>Jugar Blancas</button>
        <button onClick={() => newGame("b")} disabled={thinking}>Jugar Negras</button>

        <label style={{ display: "flex", gap: 8, alignItems: "center" }}>
          Nivel
          <select value={level} onChange={(e) => setLevel(e.target.value)} disabled={thinking}>
            <option value="rapido">Rápido</option>
            <option value="medio">Medio</option>
            <option value="fuerte">Fuerte</option>
          </select>
        </label>

        <label style={{ display: "flex", gap: 6, alignItems: "center" }}>
          <input type="checkbox" checked={showHint} onChange={(e) => setShowHint(e.target.checked)} />
          Show Hint
        </label>

        <label style={{ display: "flex", gap: 8, alignItems: "center" }}>
          Hint Lines
          <select value={hintLines} onChange={(e) => setHintLines(Number(e.target.value))}>
            <option value={1}>1</option>
            <option value={2}>2</option>
          </select>
        </label>
      </div>

      <div style={{ marginBottom: 12, fontSize: 13, lineHeight: 1.35, opacity: 0.92 }}>
        <b>{levelPreset.label}</b>: respuesta estimada entre <b>{levelPreset.minMovetimeMs}</b> y <b>{levelPreset.maxMovetimeMs} ms</b>.
      </div>

      <div style={{ marginBottom: 8, opacity: 0.8 }}>
        Turno: <b>{turn === "w" ? "Blancas" : "Negras"}</b> {thinking ? `(pensando ${elapsedMs}ms…)` : ""}
      </div>

      {engineError ? <div style={{ marginBottom: 12, color: "#b00020", fontSize: 13 }}>{engineError}</div> : null}

      <Chessboard
        position={fen}
        onPieceDrop={onPieceDrop}
        boardOrientation={playerColor === "w" ? "white" : "black"}
        arePiecesDraggable={!thinking && isPlayersTurn}
        customArrows={hintArrows}
      />

      {showHint ? (
        <div style={{ marginTop: 12, fontSize: 13 }}>
          <div style={{ display: "flex", gap: 8, alignItems: "center" }}>
            <b>Coach (Stockfish)</b>
            {hintStatus === "waiting-turn" ? (
              <span
                style={{
                  fontSize: 11,
                  padding: "2px 6px",
                  borderRadius: 999,
                  background: "#eceff1",
                  color: "#455a64",
                  textTransform: "uppercase",
                  letterSpacing: 0.2,
                }}
              >
                Hint vigente · esperando turno
              </span>
            ) : null}
          </div>
          {(hintData.lines ?? []).length === 0 ? (
            <div style={{ marginTop: 6, opacity: 0.8 }}>Sin hint disponible.</div>
          ) : (
            <ul style={{ marginTop: 6, paddingLeft: 18 }}>
              {(hintData.lines ?? []).slice(0, hintLines).map((line, idx) => (
                <li key={`${line.uci}-${idx}`}>
                  {line.uci} | cp {line.scoreCp ?? "?"} | pv {(line.pvMoves ?? []).slice(0, 6).join(" ")}
                </li>
              ))}
            </ul>
          )}
        </div>
      ) : null}

      <div style={{ marginTop: 10, fontSize: 12, opacity: 0.8 }}>Eval: {formatEvaluation(engineStatus.score)}</div>
      <div style={{ marginTop: 6, fontSize: 12, opacity: 0.8 }}>FEN: {fen}</div>
      {noProgressReported ? <div style={{ marginTop: 6, color: "#8a6d3b", fontSize: 12 }}>Sin progreso reciente del motor…</div> : null}
    </div>
  );
}
