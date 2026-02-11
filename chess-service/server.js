import express from "express";
import { spawn } from "node:child_process";
import crypto from "node:crypto";
import path from "node:path";
import { fileURLToPath } from "node:url";
import readline from "node:readline";

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
let engineQueue = Promise.resolve();

const MOVE_TIMEOUT_MS = 5000;

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

rl.on("line", (line) => {
  line = line.trim();
  if (!line) return;

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

async function initUci() {
  send("uci");
  await waitFor((l) => l === "uciok", 3000);
  send("isready");
  await waitFor((l) => l === "readyok", 3000);
}

await initUci();

app.get("/api/health", (_req, res) => res.json({ ok: true }));

app.post("/api/move", async (req, res) => {
  const requestId = req.headers["x-request-id"] || crypto.randomUUID();
  const { fen, movetime_ms = 200 } = req.body ?? {};
  const numericMoveTime = Number(movetime_ms);

  if (!fen) {
    return res.status(400).json({ error: "missing fen", code: "MISSING_FEN" });
  }

  if (!Number.isFinite(numericMoveTime) || numericMoveTime <= 0) {
    return res
      .status(400)
      .json({ error: "invalid movetime_ms", code: "INVALID_MOVETIME" });
  }

  console.log(
    `[${requestId}] move:start fen=\"${truncateFen(fen)}\" movetime=${numericMoveTime}`
  );

  try {
    const result = await enqueueEngineTask(async () => {
      send(`position fen ${fen}`);
      send(`go movetime ${numericMoveTime}`);

      const best = await waitFor(
        (l) => l.startsWith("bestmove "),
        MOVE_TIMEOUT_MS,
        requestId
      );
      const uci = best.split(/\s+/)[1] ?? "0000";

      return { uci: uci === "0000" ? null : uci };
    });

    console.log(
      `[${requestId}] move:done bestmove=${result.uci ?? "null"} timeout=false`
    );

    return res.json(result);
  } catch (e) {
    const timeout = e?.message === "engine timeout";
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
