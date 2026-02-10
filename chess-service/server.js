import express from "express";
import { spawn } from "node:child_process";

const app = express();
app.use(express.json());

const ENGINE_PATH = "../engine/build/bm_engine";

const engine = spawn(ENGINE_PATH, [], { stdio: ["pipe", "pipe", "inherit"] });

let buffer = "";
let pendingResolve = null;

engine.stdout.on("data", (d) => {
  buffer += d.toString();
  // UCI es line-based
  const lines = buffer.split("\n");
  buffer = lines.pop() ?? "";
  for (const ln of lines) {
    const line = ln.trim();
    if (!line) continue;
    if (pendingResolve && (line.startsWith("bestmove ") || line === "uciok" || line === "readyok")) {
      const r = pendingResolve;
      pendingResolve = null;
      r(line);
    }
  }
});

function send(cmd) {
  engine.stdin.write(cmd + "\n");
}

function waitForOneLine(timeoutMs = 2000) {
  return new Promise((resolve, reject) => {
    const t = setTimeout(() => {
      if (pendingResolve) pendingResolve = null;
      reject(new Error("engine timeout"));
    }, timeoutMs);
    pendingResolve = (line) => { clearTimeout(t); resolve(line); };
  });
}

async function ensureUci() {
  send("uci");
  const line = await waitForOneLine();
  if (line !== "uciok") {
    // si no llegó justo uciok como primera respuesta, no es grave (el engine puede imprimir id first),
    // en engine real deberías leer hasta "uciok". Lo dejamos simple por ahora.
  }
  send("isready");
  await waitForOneLine();
}

await ensureUci();

app.post("/api/move", async (req, res) => {
  const { fen, wtime_ms, btime_ms, movetime_ms } = req.body ?? {};
  if (!fen) return res.status(400).json({ error: "missing fen" });

  // posición (FEN) + go con movetime si querés
  send(`position fen ${fen}`);
  if (movetime_ms) send(`go movetime ${movetime_ms}`);
  else if (wtime_ms != null && btime_ms != null) send(`go wtime ${wtime_ms} btime ${btime_ms}`);
  else send(`go movetime 200`);

  try {
    const line = await waitForOneLine(5000);
    if (!line.startsWith("bestmove ")) return res.status(500).json({ error: "bad engine response" });
    const uci = line.split(" ")[1];
    return res.json({ uci: uci === "0000" ? null : uci });
  } catch (e) {
    return res.status(500).json({ error: e.message });
  }
});

app.listen(8000, () => console.log("service on http://localhost:8000"));

