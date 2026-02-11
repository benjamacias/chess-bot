import express from "express";
import { spawn } from "node:child_process";
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

app.post("/api/move", async (req, res) => {
  const { fen } = req.body ?? {};
  if (!fen) return res.status(400).json({ error: "missing fen" });

  const opts = resolveMoveOptions(req.body ?? {});

  try {
    if (opts.hash_mb && opts.hash_mb !== currentHashMb) {
      send(`setoption name Hash value ${opts.hash_mb}`);
      send("isready");
      await waitFor((l) => l === "readyok", 3000);
      currentHashMb = opts.hash_mb;
    }

    send(`position fen ${fen}`);
    const go = opts.depth ? `go depth ${opts.depth}` : `go movetime ${opts.movetime_ms}`;
    send(go);

    const bestTimeout = Math.max(5000, (opts.movetime_ms ?? 0) + 4000);
    const best = await waitFor((l) => l.startsWith("bestmove "), bestTimeout);
    const uci = best.split(/\s+/)[1] ?? "0000";

    return res.json({ uci: uci === "0000" ? null : uci });
  } catch (e) {
    return res.status(500).json({ error: e.message });
  }
});

app.listen(8000, () => {
  console.log("service on http://localhost:8000");
  console.log("engine:", ENGINE_PATH);
});
