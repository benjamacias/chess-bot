import express from "express";
import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import readline from "node:readline";
import { randomUUID } from "node:crypto";

const app = express();
app.use(express.json());

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
function resolveEnginePath() {
  if (process.env.ENGINE_PATH) return process.env.ENGINE_PATH;

  const candidates = [
    path.resolve(__dirname, "../engine/build_local/bm_engine"),
    path.resolve(__dirname, "../engine/build/bm_engine"),
  ];

  const existing = candidates
    .filter((candidate) => fs.existsSync(candidate))
    .map((candidate) => ({ candidate, mtime: fs.statSync(candidate).mtimeMs }));

  if (existing.length === 0) return candidates[1];
  existing.sort((a, b) => b.mtime - a.mtime);
  return existing[0].candidate;
}

const ENGINE_PATH = resolveEnginePath();
function resolveStockfishPath() {
  if (process.env.STOCKFISH_PATH) return process.env.STOCKFISH_PATH;

  const absoluteCandidates = [
    "/usr/games/stockfish",
    "/usr/bin/stockfish",
    "/snap/bin/stockfish",
  ];

  for (const candidate of absoluteCandidates) {
    if (fs.existsSync(candidate)) return candidate;
  }

  return "stockfish";
}

const STOCKFISH_PATH = resolveStockfishPath();

const MOVE_TIMEOUT_MS = 5000;
const HINT_TIMEOUT_MS = 4000;

function truncateFen(fen = "", maxLen = 64) {
  if (fen.length <= maxLen) return fen;
  return `${fen.slice(0, maxLen)}...`;
}

function toPositiveInt(value) {
  if (value === undefined || value === null || value === "") return undefined;
  const n = Number(value);
  if (!Number.isFinite(n)) return undefined;
  const i = Math.floor(n);
  return i > 0 ? i : undefined;
}

function buildPositionCommand({ fen, moves_uci }) {
  if (Array.isArray(moves_uci)) {
    if (moves_uci.length === 0) return "position startpos";
    return `position startpos moves ${moves_uci.join(" ")}`;
  }
  return `position fen ${fen}`;
}

function parseInfoLine(line) {
  if (!line.startsWith("info ")) return null;

  const tokens = line.split(/\s+/);
  const depthIdx = tokens.indexOf("depth");
  const scoreIdx = tokens.indexOf("score");
  const pvIdx = tokens.indexOf("pv");

  const depth = depthIdx >= 0 ? Number(tokens[depthIdx + 1]) : null;

  let score = null;
  if (scoreIdx >= 0) {
    const scoreType = tokens[scoreIdx + 1];
    const scoreValue = Number(tokens[scoreIdx + 2]);
    if ((scoreType === "cp" || scoreType === "mate") && Number.isFinite(scoreValue)) {
      score = { type: scoreType, value: scoreValue };
    }
  }

  const pv = pvIdx >= 0 ? tokens.slice(pvIdx + 1).join(" ") : "";

  return {
    depth: Number.isFinite(depth) ? depth : null,
    score,
    pv,
    raw: line,
  };
}

function parseStockfishMultipv(line) {
  if (!line.startsWith("info ") || !line.includes(" pv ")) return null;
  const tokens = line.split(/\s+/);
  const pvIdx = tokens.indexOf("pv");
  if (pvIdx < 0 || pvIdx === tokens.length - 1) return null;

  const mpIdx = tokens.indexOf("multipv");
  const scoreIdx = tokens.indexOf("score");

  const multipv = mpIdx >= 0 ? Number(tokens[mpIdx + 1]) : 1;
  if (!Number.isFinite(multipv) || multipv <= 0) return null;

  let scoreCp = null;
  if (scoreIdx >= 0) {
    const scoreType = tokens[scoreIdx + 1];
    const scoreValue = Number(tokens[scoreIdx + 2]);
    if (scoreType === "cp" && Number.isFinite(scoreValue)) {
      scoreCp = scoreValue;
    } else if (scoreType === "mate" && Number.isFinite(scoreValue)) {
      scoreCp = scoreValue > 0 ? 100000 - Math.abs(scoreValue) : -100000 + Math.abs(scoreValue);
    }
  }

  const pvMoves = tokens.slice(pvIdx + 1).filter(Boolean);
  if (pvMoves.length === 0) return null;

  return {
    multipv,
    scoreCp,
    pvMoves,
    uci: pvMoves[0],
  };
}

function createUciClient(binaryPath, args = []) {
  const proc = spawn(binaryPath, args, { stdio: ["pipe", "pipe", "inherit"] });
  const rl = readline.createInterface({ input: proc.stdout });
  let waiters = [];
  let onLineHandlers = new Set();
  let startupError = null;

  proc.on("error", (error) => {
    startupError = error;
    const currentWaiters = waiters.slice();
    waiters = [];
    for (const waiter of currentWaiters) {
      waiter.reject(error);
    }
  });

  rl.on("line", (rawLine) => {
    const line = rawLine.trim();
    if (!line) return;

    for (const handler of onLineHandlers) {
      handler(line);
    }

    for (let i = 0; i < waiters.length; i++) {
      const { predicate, resolve } = waiters[i];
      if (predicate(line)) {
        waiters.splice(i, 1);
        resolve(line);
        return;
      }
    }
  });

  const send = (cmd) => {
    proc.stdin.write(`${cmd}\n`);
  };

  const waitFor = (predicate, timeoutMs = 3000, requestId = "system") =>
    new Promise((resolve, reject) => {
      if (startupError) {
        reject(startupError);
        return;
      }

      const waiter = {
        requestId,
        predicate,
        resolve: (line) => {
          clearTimeout(timer);
          resolve(line);
        },
        reject: (error) => {
          clearTimeout(timer);
          reject(error);
        },
      };

      const timer = setTimeout(() => {
        waiters = waiters.filter((w) => w !== waiter);
        reject(new Error("engine timeout"));
      }, timeoutMs);

      waiters.push(waiter);
    });

  const cleanupWaitersForRequest = (requestId) => {
    waiters = waiters.filter((w) => w.requestId !== requestId);
  };

  const onLine = (handler) => {
    onLineHandlers.add(handler);
    return () => onLineHandlers.delete(handler);
  };

  return {
    proc,
    send,
    waitFor,
    onLine,
    cleanupWaitersForRequest,
  };
}

const engineClient = createUciClient(ENGINE_PATH);
const stockfishClient = createUciClient(STOCKFISH_PATH);
let stockfishReady = false;

let activeRequestId = null;
const requestStates = new Map();
let engineQueue = Promise.resolve();
let stockfishQueue = Promise.resolve();
let currentHashMb = 64;

engineClient.onLine((line) => {
  if (!activeRequestId) return;
  const state = requestStates.get(activeRequestId);
  if (!state) return;

  const parsedInfo = parseInfoLine(line);
  if (parsedInfo) {
    state.lastInfo = parsedInfo;
    state.lastInfoAt = Date.now();
    return;
  }

  if (line.startsWith("info string bookhit")) {
    state.bookhit = line;
    return;
  }

  if (line.startsWith("bestmove ")) {
    state.bestmove = line.split(/\s+/)[1] ?? "0000";
    state.active = false;
    state.finishedAt = Date.now();
    activeRequestId = null;
  }
});

function enqueueEngineTask(fn) {
  const task = engineQueue.then(() => fn());
  engineQueue = task.catch(() => {});
  return task;
}

function enqueueStockfishTask(fn) {
  const task = stockfishQueue.then(() => fn());
  stockfishQueue = task.catch(() => {});
  return task;
}

function terminalReasonFromInfo(lastInfo) {
  if (lastInfo?.score?.type === "mate") return "CHECKMATE";
  return "NO_LEGAL_MOVES";
}

function serializeBestmove(state) {
  const rawBestmove = state?.bestmove ?? null;
  const uci = rawBestmove && rawBestmove !== "0000" ? rawBestmove : null;
  const terminal = rawBestmove === "0000";

  return {
    uci,
    terminal,
    reason: terminal ? terminalReasonFromInfo(state?.lastInfo) : null,
    depth: state?.lastInfo?.depth ?? null,
    score: state?.lastInfo?.score ?? null,
    pv: state?.lastInfo?.pv ?? "",
    bookhit: Boolean(state?.bookhit),
  };
}

function serializeState(state) {
  const bestmove = serializeBestmove(state);
  return {
    id: state.id,
    active: state.active,
    startedAt: state.startedAt,
    finishedAt: state.finishedAt,
    lastInfoAt: state.lastInfoAt,
    depth: bestmove.depth,
    score: bestmove.score,
    pv: bestmove.pv,
    uci: bestmove.uci,
    terminal: bestmove.terminal,
    reason: bestmove.reason,
    bestmove: bestmove.uci,
    bookhit: bestmove.bookhit,
    error: state.error ?? null,
  };
}

function cleanupOldStates() {
  const now = Date.now();
  for (const [id, state] of requestStates) {
    if (!state.active && state.finishedAt && now - state.finishedAt > 60_000) {
      requestStates.delete(id);
    }
  }
}

async function initClients() {
  engineClient.send("uci");
  await engineClient.waitFor((l) => l === "uciok", 3000);
  engineClient.send("isready");
  await engineClient.waitFor((l) => l === "readyok", 3000);

  try {
    stockfishClient.send("uci");
    await stockfishClient.waitFor((l) => l === "uciok", 3000);
    stockfishClient.send("isready");
    await stockfishClient.waitFor((l) => l === "readyok", 3000);
    stockfishReady = true;
  } catch (error) {
    stockfishReady = false;
    console.warn(`stockfish disabled: ${error?.message ?? "init failed"}`);
  }
}

await initClients();

const SKILL_PRESETS = {
  blitz: { movetime_ms: 200, depth: undefined, hash_mb: 64 },
  rapid: { movetime_ms: 700, depth: undefined, hash_mb: 96 },
  strong: { movetime_ms: 1600, depth: 10, hash_mb: 192 },
};

function resolveMoveOptions(payload = {}) {
  const skillRaw = typeof payload.skill === "string" ? payload.skill.toLowerCase().trim() : undefined;
  const preset = skillRaw ? SKILL_PRESETS[skillRaw] : undefined;

  const movetime_ms = toPositiveInt(payload.movetime_ms) ?? preset?.movetime_ms ?? 200;
  const depth = toPositiveInt(payload.depth) ?? preset?.depth;
  const hash_mb = toPositiveInt(payload.hash_mb) ?? preset?.hash_mb;

  return { movetime_ms, depth, hash_mb, skill: skillRaw };
}

app.get("/api/health", (_req, res) => res.json({ ok: true }));

app.get("/api/move/status/:id", (req, res) => {
  cleanupOldStates();
  const state = requestStates.get(req.params.id);
  if (!state) return res.status(404).json({ error: "unknown request id" });
  return res.json(serializeState(state));
});

app.post("/api/move", async (req, res) => {
  const headerRequestId = req.headers["x-request-id"];
  const requestId = typeof headerRequestId === "string" && headerRequestId.trim() ? headerRequestId : randomUUID();


  const { fen, moves_uci } = req.body ?? {};
  if (!fen) return res.status(400).json({ error: "missing fen", code: "MISSING_FEN" });
  if (moves_uci !== undefined && !Array.isArray(moves_uci)) {
    return res.status(400).json({ error: "invalid moves_uci", code: "INVALID_MOVES_UCI" });
  }

  const opts = resolveMoveOptions(req.body ?? {});
  const numericMoveTime = Number(opts.movetime_ms);
  if (!Number.isFinite(numericMoveTime) || numericMoveTime <= 0) {
    return res.status(400).json({ error: "invalid movetime_ms", code: "INVALID_MOVETIME" });
  }

  const state = {
    id: requestId,
    active: true,
    startedAt: Date.now(),
    finishedAt: null,
    lastInfoAt: null,
    lastInfo: null,
    bestmove: null,
    error: null,
    bookhit: null,
  };
  requestStates.set(requestId, state);
  cleanupOldStates();

  console.log(
    `[${requestId}] move:start fen="${truncateFen(fen)}" moves=${Array.isArray(moves_uci) ? moves_uci.length : "fen"} movetime=${numericMoveTime}`
  );

  try {
    const result = await enqueueEngineTask(async () => {
      activeRequestId = requestId;
      let sawBookHit = false;
      const detachBookListener = engineClient.onLine((line) => {
        if (line.startsWith("info string bookhit")) sawBookHit = true;
      });

      try {
        if (opts.hash_mb && opts.hash_mb !== currentHashMb) {
          engineClient.send(`setoption name Hash value ${opts.hash_mb}`);
          engineClient.send("isready");
          await engineClient.waitFor((l) => l === "readyok", 3000, requestId);
          currentHashMb = opts.hash_mb;
        }

        engineClient.send(buildPositionCommand({ fen, moves_uci }));
        const go = opts.depth ? `go depth ${opts.depth}` : `go movetime ${numericMoveTime}`;
        engineClient.send(go);

        const bestTimeout = Math.max(MOVE_TIMEOUT_MS, numericMoveTime + 4000);
        const best = await engineClient.waitFor((l) => l.startsWith("bestmove "), bestTimeout, requestId);
        const uci = best.split(/\s+/)[1] ?? "0000";

        state.bestmove = uci;
        state.bookhit = state.bookhit || sawBookHit;
        state.active = false;
        state.finishedAt = Date.now();

        return serializeBestmove(state);
      } finally {
        detachBookListener();
      }
    });

    console.log(`[${requestId}] move:done bestmove=${result.uci ?? "null"} bookhit=${result.bookhit}`);
    return res.json({ ...result, timeout: false });
  } catch (e) {
    const timeout = e?.message === "engine timeout";
    state.active = false;
    state.finishedAt = Date.now();
    state.error = timeout ? "ENGINE_TIMEOUT" : "ENGINE_ERROR";
    activeRequestId = null;

    console.warn(`[${requestId}] move:error timeout=${timeout} reason=${e?.message ?? "unknown"}`);
    if (timeout) {
      return res.json({ uci: null, timeout: true, terminal: false, reason: null, depth: null, score: null, pv: "", bookhit: false });
    }
    return res.status(500).json({ error: e?.message ?? "internal error", code: "ENGINE_ERROR" });
  } finally {
    engineClient.cleanupWaitersForRequest(requestId);
  }
});

app.post("/api/hint", async (req, res) => {
  if (!stockfishReady) {
    return res.status(503).json({
      best: null,
      lines: [],
      timeout: false,
      error: "stockfish unavailable",
      code: "STOCKFISH_UNAVAILABLE",
    });
  }

  const { fen, moves_uci } = req.body ?? {};
  if (!fen) return res.status(400).json({ error: "missing fen", code: "MISSING_FEN" });
  if (moves_uci !== undefined && !Array.isArray(moves_uci)) {
    return res.status(400).json({ error: "invalid moves_uci", code: "INVALID_MOVES_UCI" });
  }

  const multipv = Math.min(8, Math.max(1, toPositiveInt(req.body?.multipv) ?? 3));
  const movetimeMs = Math.min(2000, Math.max(50, toPositiveInt(req.body?.movetime_ms) ?? 120));
  const requestId = randomUUID();

  try {
    const result = await enqueueStockfishTask(async () => {
      const linesByPv = new Map();
      const detach = stockfishClient.onLine((line) => {
        const parsed = parseStockfishMultipv(line);
        if (!parsed) return;
        linesByPv.set(parsed.multipv, parsed);
      });

      try {
        stockfishClient.send(`setoption name MultiPV value ${multipv}`);
        stockfishClient.send("isready");
        await stockfishClient.waitFor((l) => l === "readyok", 3000, requestId);

        stockfishClient.send(buildPositionCommand({ fen, moves_uci }));
        stockfishClient.send(`go movetime ${movetimeMs}`);
        await stockfishClient.waitFor((l) => l.startsWith("bestmove "), Math.max(HINT_TIMEOUT_MS, movetimeMs + 2500), requestId);

        const lines = [...linesByPv.values()]
          .sort((a, b) => a.multipv - b.multipv)
          .slice(0, multipv)
          .map((entry) => ({ uci: entry.uci, scoreCp: entry.scoreCp, pvMoves: entry.pvMoves }))
          .filter((entry) => typeof entry.uci === "string" && entry.uci.length >= 4);

        const best = lines.find((l) => l.uci)?.uci ?? null;
        return { best, lines };
      } finally {
        detach();
      }
    });

    return res.json(result);
  } catch (e) {
    const timeout = e?.message === "engine timeout";
    console.warn(`[hint] error timeout=${timeout} reason=${e?.message ?? "unknown"}`);
    if (timeout) return res.json({ best: null, lines: [], timeout: true });
    return res.status(500).json({ error: e?.message ?? "internal error", code: "HINT_ERROR" });
  } finally {
    stockfishClient.cleanupWaitersForRequest(requestId);
  }
});

app.listen(8000, () => {
  console.log("service on http://localhost:8000");
  console.log("engine:", ENGINE_PATH);
  console.log("stockfish:", STOCKFISH_PATH, stockfishReady ? "(ready)" : "(disabled)");
});
