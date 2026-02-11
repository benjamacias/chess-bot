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

function uciToMove(uci) {
  const from = uci.slice(0, 2);
  const to = uci.slice(2, 4);
  const promotion = uci.length === 5 ? uci[4] : undefined; // q r b n
  return { from, to, promotion };
}

function randomInt(min, max) {
  return Math.floor(Math.random() * (max - min + 1)) + min;
}

async function fetchBotMove(fen, options) {
  const res = await fetch("/api/move", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ fen, ...options }),
  });
  if (!res.ok) throw new Error(`api/move failed: ${res.status}`);
  const data = await res.json();
  return data.uci; // string o null
}

export default function ChessGame() {
  const game = useMemo(() => new Chess(), []);
  const [fen, setFen] = useState(game.fen());
  const [playerColor, setPlayerColor] = useState("w"); // "w" o "b"
  const [thinking, setThinking] = useState(false);
  const [level, setLevel] = useState("rapido");

  const turn = fen.split(" ")[1]; // "w" o "b"
  const isPlayersTurn = turn === playerColor;
  const levelPreset = LEVEL_PRESETS[level] ?? LEVEL_PRESETS.rapido;

  function sync() {
    setFen(game.fen());
  }

  async function botPlayIfNeeded() {
    if (game.isGameOver()) return;
    const t = game.turn(); // "w" o "b"
    if (t === playerColor) return;

    setThinking(true);
    try {
      const movetimeMs = randomInt(levelPreset.minMovetimeMs, levelPreset.maxMovetimeMs);
      const uci = await fetchBotMove(game.fen(), {
        skill: levelPreset.skill,
        movetime_ms: movetimeMs,
        depth: levelPreset.depth,
        hash_mb: levelPreset.hashMb,
      });
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
