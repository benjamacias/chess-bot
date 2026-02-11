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

// Leer stdout por líneas
const rl = readline.createInterface({ input: engine.stdout });

let waiters = [];
let activeRequestId = null;
const requestStates = new Map();

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

rl.on("line", (line) => {
  line = line.trim();
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
  engine.stdin.write(cmd + "\n");
}

function waitFor(predicate, timeoutMs = 3000) {
  return new Promise((resolve, reject) => {
    const t = setTimeout(() => {
      // eliminar waiter si seguía
      waiters = waiters.filter((w) => w.resolve !== resolve);
      reject(new Error("engine timeout"));
    }, timeoutMs);

    waiters.push({
      predicate,
      resolve: (line) => {
        clearTimeout(t);
        resolve(line);
      },
    });
  });
}

function serializeState(state) {
  return {
    id: state.id,
    active: state.active,
    startedAt: state.startedAt,
    finishedAt: state.finishedAt,
    lastInfoAt: state.lastInfoAt,
    depth: state.lastInfo?.depth ?? null,
    score: state.lastInfo?.score ?? null,
    pv: state.lastInfo?.pv ?? "",
    bestmove: state.bestmove && state.bestmove !== "0000" ? state.bestmove : null,
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

app.get("/api/health", (_req, res) => res.json({ ok: true }));

app.get("/api/move/status/:id", (req, res) => {
  cleanupOldStates();
  const state = requestStates.get(req.params.id);
  if (!state) return res.status(404).json({ error: "unknown request id" });
  return res.json(serializeState(state));
});

app.post("/api/move", async (req, res) => {
  const { fen, movetime_ms = 200, request_id } = req.body ?? {};
  if (!fen) return res.status(400).json({ error: "missing fen" });

  const requestId = typeof request_id === "string" && request_id.trim() ? request_id : randomUUID();

  try {
    cleanupOldStates();
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
    activeRequestId = requestId;

    send(`position fen ${fen}`);
    send(`go movetime ${movetime_ms}`);

    const best = await waitFor((l) => l.startsWith("bestmove "), 5000);
    const uci = best.split(/\s+/)[1] ?? "0000";

    state.active = false;
    state.bestmove = uci;
    state.finishedAt = Date.now();
    if (activeRequestId === requestId) activeRequestId = null;

    return res.json({ request_id: requestId, uci: uci === "0000" ? null : uci });
  } catch (e) {
    const state = requestStates.get(requestId);
    if (state) {
      state.active = false;
      state.error = e.message;
      state.finishedAt = Date.now();
    }
    if (activeRequestId === requestId) activeRequestId = null;
    return res.status(500).json({ request_id: requestId, error: e.message });
  }
});

app.listen(8000, () => {
  console.log("service on http://localhost:8000");
  console.log("engine:", ENGINE_PATH);
});
