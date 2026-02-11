import React, { useEffect, useMemo, useState } from "react";
import { Chess } from "chess.js";
import { Chessboard } from "react-chessboard";

function uciToMove(uci) {
  const from = uci.slice(0, 2);
  const to = uci.slice(2, 4);
  const promotion = uci.length === 5 ? uci[4] : undefined; // q r b n
  return { from, to, promotion };
}

async function fetchBotMove(fen, movetimeMs, signal) {
  const res = await fetch("/api/move", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ fen, movetime_ms: movetimeMs }),
    signal,
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
  const [movetimeMs, setMovetimeMs] = useState(200);
  const [engineError, setEngineError] = useState("");

  const turn = fen.split(" ")[1]; // "w" o "b"
  const isPlayersTurn = turn === playerColor;

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
    setEngineError("");
    try {
      const uci = await fetchBotMove(game.fen(), movetimeMs, controller.signal);
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
        setEngineError("El motor no respondió, reintentá");
        return;
      }
      sync();
    } catch (error) {
      console.error("Error consultando al motor:", error);
      setEngineError("El motor no respondió, reintentá");
    } finally {
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
          Turno: <b>{turn === "w" ? "Blancas" : "Negras"}</b>{" "}
          {thinking ? "(pensando…)" : ""}
        </div>
      </div>

      <Chessboard
        position={fen}
        onPieceDrop={onPieceDrop}
        boardOrientation={playerColor === "w" ? "white" : "black"}
        arePiecesDraggable={!thinking && isPlayersTurn}
      />

      {engineError && (
        <div
          style={{
            marginTop: 12,
            padding: 12,
            borderRadius: 8,
            background: "#ffe8e8",
            border: "1px solid #ffb3b3",
          }}
        >
          <div style={{ marginBottom: 8, color: "#8a1f11", fontWeight: 600 }}>{engineError}</div>
          <div style={{ display: "flex", gap: 8, flexWrap: "wrap" }}>
            <button onClick={botPlayIfNeeded} disabled={thinking || game.isGameOver()}>
              Reintentar jugada del bot
            </button>
            <button onClick={() => newGame(playerColor)} disabled={thinking}>
              Nueva partida
            </button>
          </div>
        </div>
      )}

      <div style={{ marginTop: 12, fontSize: 12, opacity: 0.8 }}>
        FEN: {fen}
      </div>
    </div>
  );
}
