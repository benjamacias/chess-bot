import express from "express";
import { spawn } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";
import readline from "node:readline";
import { randomUUID } from "node:crypto";

const app = express();
app.use(express.json());

// Ruta absoluta al engine para que no falle por cwd
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ENGINE_PATH = path.resolve(__dirname, "../engine/build/bm_engine");

const engine = spawn(ENGINE_PATH, [], { stdio: ["pipe", "pipe", "inherit"] });
const rl = readline.createInterface({ input: engine.stdout });

let waiters = [];
let activeRequestId = null;
const requestStates = new Map();
let engineQueue = Promise.resolve();

const MOVE_TIMEOUT_MS = 5000;

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

function enqueueEngineTask(fn) {
  const task = engineQueue.then(() => fn());
  engineQueue = task.catch(() => {
    // Evitar que una tarea fallida rompa el encadenado futuro.
  });
  return task;
}

function truncateFen(fen = "", maxLen = 64) {
  if (fen.length <= maxLen) return fen;
  return `${fen.slice(0, maxLen)}...`;
}

rl.on("line", (rawLine) => {
  const line = rawLine.trim();
  if (!line) return;

  if (activeRequestId) {
    const state = requestStates.get(activeRequestId);
    if (state) {
      const parsedInfo = parseInfoLine(line);
      if (parsedInfo) {
        state.lastInfo = parsedInfo;
        state.lastInfoAt = Date.now();
      } else if (line.startsWith("bestmove ")) {
        state.bestmove = line.split(/\s+/)[1] ?? "0000";
        state.active = false;
        state.finishedAt = Date.now();
        activeRequestId = null;
      }
    }
  }

  // Resolver el primer waiter que haga match
  for (let i = 0; i < waiters.length; i++) {
    const { predicate, resolve } = waiters[i];
    if (predicate(line)) {
      waiters.splice(i, 1);
      resolve(line);
      return;
    }
  }
});

function send(cmd) {
  engine.stdin.write(`${cmd}\n`);
}

function cleanupWaitersForRequest(requestId) {
  const before = waiters.length;
  waiters = waiters.filter((w) => w.requestId !== requestId);
  return before - waiters.length;
}

function waitFor(predicate, timeoutMs = 3000, requestId = "system") {
  return new Promise((resolve, reject) => {
    const waiter = {
      requestId,
      predicate,
      resolve: (line) => {
        clearTimeout(t);
        resolve(line);
      },
    };

    const t = setTimeout(() => {
      waiters = waiters.filter((w) => w !== waiter);
      reject(new Error("engine timeout"));
    }, timeoutMs);

    waiters.push(waiter);
  });
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

async function initUci() {
  send("uci");
  await waitFor((l) => l === "uciok", 3000);
  send("isready");
  await waitFor((l) => l === "readyok", 3000);
}

await initUci();

const SKILL_PRESETS = {
  blitz: { movetime_ms: 200, depth: undefined, hash_mb: 64 },
  rapid: { movetime_ms: 700, depth: undefined, hash_mb: 96 },
  strong: { movetime_ms: 1600, depth: 10, hash_mb: 192 },
};

let currentHashMb = 64;

function toPositiveInt(value) {
  if (value === undefined || value === null || value === "") return undefined;
  const n = Number(value);
  if (!Number.isFinite(n)) return undefined;
  const i = Math.floor(n);
  return i > 0 ? i : undefined;
}

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
  const requestId =
    typeof headerRequestId === "string" && headerRequestId.trim() ? headerRequestId : randomUUID();

  const { fen } = req.body ?? {};
  if (!fen) {
    return res.status(400).json({ error: "missing fen", code: "MISSING_FEN" });
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
  };
  requestStates.set(requestId, state);
  cleanupOldStates();

  console.log(
    `[${requestId}] move:start fen="${truncateFen(fen)}" movetime=${numericMoveTime} depth=${opts.depth ?? "-"}`
  );

  try {
    const result = await enqueueEngineTask(async () => {
      activeRequestId = requestId;

      if (opts.hash_mb && opts.hash_mb !== currentHashMb) {
        send(`setoption name Hash value ${opts.hash_mb}`);
        send("isready");
        await waitFor((l) => l === "readyok", 3000, requestId);
        currentHashMb = opts.hash_mb;
      }

      send(`position fen ${fen}`);
      const go = opts.depth ? `go depth ${opts.depth}` : `go movetime ${numericMoveTime}`;
      send(go);

      const bestTimeout = Math.max(MOVE_TIMEOUT_MS, numericMoveTime + 4000);
      const best = await waitFor((l) => l.startsWith("bestmove "), bestTimeout, requestId);
      const uci = best.split(/\s+/)[1] ?? "0000";

      state.bestmove = uci;
      state.active = false;
      state.finishedAt = Date.now();

      return serializeBestmove(state);
    });

    console.log(
      `[${requestId}] move:done bestmove=${result.uci ?? "null"} terminal=${result.terminal} timeout=false`
    );
    return res.json(result);
  } catch (e) {
    const timeout = e?.message === "engine timeout";
    state.active = false;
    state.finishedAt = Date.now();
    state.error = timeout ? "ENGINE_TIMEOUT" : "ENGINE_ERROR";
    activeRequestId = null;

    console.warn(
      `[${requestId}] move:error timeout=${timeout} reason=${e?.message ?? "unknown"}`
    );

    const status = timeout ? 504 : 500;
    const code = timeout ? "ENGINE_TIMEOUT" : "ENGINE_ERROR";
    return res.status(status).json({ error: e?.message ?? "internal error", code });
  } finally {
    cleanupWaitersForRequest(requestId);
  }
});

app.listen(8000, () => {
  console.log("service on http://localhost:8000");
  console.log("engine:", ENGINE_PATH);
});
