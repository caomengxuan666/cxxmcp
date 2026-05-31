#!/usr/bin/env node
// cxxmcp Streamable HTTP benchmark driver.
// Uses Node's built-in http module — zero external dependencies.
//
// Usage:
//   node bench/mcp-bench.mjs [options]
//
// Options:
//   --url <url>        MCP server endpoint (default: http://127.0.0.1:3000/mcp)
//   --concurrency <n>  Parallel clients (default: 10)
//   --duration <s>     Test duration in seconds (default: 5)
//   --op <operation>   Operation to benchmark: tools-list, echo, ping (default: echo)
//   --warmup <s>       Warmup duration in seconds (default: 1)
//   --payload <bytes>  Echo payload size in bytes (default: 256)

import http from "node:http";

// ---------------------------------------------------------------------------
// CLI args
// ---------------------------------------------------------------------------

function parseArgs() {
  const args = process.argv.slice(2);
  const opts = {
    url: "http://127.0.0.1:3000/mcp",
    concurrency: 10,
    duration: 5,
    op: "echo",
    warmup: 1,
    payload: 256,
  };
  for (let i = 0; i < args.length; i += 2) {
    const key = args[i].replace(/^--/, "");
    const val = args[i + 1];
    if (key in opts) {
      opts[key] = typeof opts[key] === "number" ? Number(val) : val;
    }
  }
  return opts;
}

// ---------------------------------------------------------------------------
// MCP protocol helpers
// ---------------------------------------------------------------------------

function mcpPost(url, body, headers = {}) {
  return new Promise((resolve, reject) => {
    const parsed = new URL(url);
    const req = http.request(
      {
        hostname: parsed.hostname,
        port: parsed.port,
        path: parsed.pathname,
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Accept: "application/json, text/event-stream",
          ...headers,
        },
      },
      (res) => {
        const chunks = [];
        res.on("data", (c) => chunks.push(c));
        res.on("end", () => {
          const raw = Buffer.concat(chunks).toString();
          resolve({
            status: res.statusCode,
            headers: res.headers,
            body: raw ? JSON.parse(raw) : null,
          });
        });
      }
    );
    req.on("error", reject);
    req.write(JSON.stringify(body));
    req.end();
  });
}

async function mcpHandshake(url) {
  const initRes = await mcpPost(url, {
    jsonrpc: "2.0",
    id: 1,
    method: "initialize",
    params: {
      protocolVersion: "2025-11-25",
      capabilities: {},
      clientInfo: { name: "mcp-bench", version: "1.0.0" },
    },
  });
  const sessionId = initRes.headers["mcp-session-id"];
  if (!sessionId) {
    throw new Error("Server did not return Mcp-Session-Id header");
  }

  const sessionHeaders = {
    "Mcp-Session-Id": sessionId,
    "MCP-Protocol-Version": "2025-11-25",
  };

  await mcpPost(
    url,
    { jsonrpc: "2.0", method: "notifications/initialized" },
    sessionHeaders
  );

  return sessionHeaders;
}

// ---------------------------------------------------------------------------
// Request builders
// ---------------------------------------------------------------------------

let reqCounter = 0;

function makeRequest(op, payloadSize) {
  const id = ++reqCounter;
  switch (op) {
    case "tools-list":
      return { jsonrpc: "2.0", id, method: "tools/list", params: {} };
    case "ping":
      return { jsonrpc: "2.0", id, method: "ping" };
    case "echo":
    default:
      return {
        jsonrpc: "2.0",
        id,
        method: "tools/call",
        params: {
          name: "echo",
          arguments: { data: "x".repeat(payloadSize) },
        },
      };
  }
}

// ---------------------------------------------------------------------------
// Latency tracking
// ---------------------------------------------------------------------------

class LatencyTracker {
  constructor() {
    this.values = [];
  }
  record(ns) {
    this.values.push(ns);
  }
  sorted() {
    if (!this._sorted) {
      this.values.sort((a, b) => a - b);
      this._sorted = true;
    }
    return this.values;
  }
  percentile(p) {
    const s = this.sorted();
    if (s.length === 0) return 0;
    const idx = Math.ceil((p / 100) * s.length) - 1;
    return s[Math.max(0, idx)];
  }
  get count() {
    return this.values.length;
  }
  get sum() {
    return this.values.reduce((a, b) => a + b, 0);
  }
  get min() {
    return this.sorted()[0] ?? 0;
  }
  get max() {
    return this.sorted()[this.sorted().length - 1] ?? 0;
  }
  get mean() {
    return this.count > 0 ? this.sum / this.count : 0;
  }
}

// ---------------------------------------------------------------------------
// Worker: establishes its own session, then sends requests for the duration
// ---------------------------------------------------------------------------

function runWorker(url, opts, isWarmup) {
  return new Promise(async (resolve) => {
    const latencies = new LatencyTracker();
    let requests = 0;
    let errors = 0;

    // Each worker gets its own MCP session.
    let sessionHeaders;
    try {
      sessionHeaders = await mcpHandshake(url);
    } catch {
      resolve({ latencies, requests: 1, errors: 1 });
      return;
    }

    const deadline =
      Date.now() + (isWarmup ? opts.warmup : opts.duration) * 1000;

    while (Date.now() < deadline) {
      const body = makeRequest(opts.op, opts.payload);
      const t0 = process.hrtime.bigint();
      try {
        const res = await mcpPost(url, body, sessionHeaders);
        const t1 = process.hrtime.bigint();
        if (!isWarmup) {
          latencies.record(Number(t1 - t0));
        }
        requests++;
        if (res.status !== 200) errors++;
      } catch {
        errors++;
        requests++;
      }
    }
    resolve({ latencies, requests, errors });
  });
}

// ---------------------------------------------------------------------------
// Aggregation
// ---------------------------------------------------------------------------

function mergeResults(results) {
  const merged = new LatencyTracker();
  let totalReqs = 0;
  let totalErrors = 0;
  for (const r of results) {
    for (const v of r.latencies.values) merged.record(v);
    totalReqs += r.requests;
    totalErrors += r.errors;
  }
  return { merged, totalReqs, totalErrors };
}

function formatNs(ns) {
  if (ns >= 1e6) return (ns / 1e6).toFixed(2) + " ms";
  if (ns >= 1e3) return (ns / 1e3).toFixed(1) + " us";
  return ns + " ns";
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

async function main() {
  const opts = parseArgs();
  console.log(`cxxmcp benchmark driver`);
  console.log(`  url:         ${opts.url}`);
  console.log(`  operation:   ${opts.op}`);
  console.log(`  concurrency: ${opts.concurrency}`);
  console.log(`  duration:    ${opts.duration}s`);
  console.log(`  warmup:      ${opts.warmup}s`);
  if (opts.op === "echo") console.log(`  payload:     ${opts.payload} bytes`);
  console.log();

  // Verify server is reachable.
  try {
    await mcpHandshake(opts.url);
  } catch (err) {
    console.error(`MCP handshake failed: ${err.message}`);
    process.exit(1);
  }
  console.log(`Server reachable`);

  // warmup
  console.log(`Warming up...`);
  const warmupWorkers = [];
  for (let i = 0; i < opts.concurrency; i++) {
    warmupWorkers.push(runWorker(opts.url, opts, true));
  }
  await Promise.all(warmupWorkers);

  // benchmark
  console.log(`Running benchmark...`);
  const benchWorkers = [];
  for (let i = 0; i < opts.concurrency; i++) {
    benchWorkers.push(runWorker(opts.url, opts, false));
  }
  const workerResults = await Promise.all(benchWorkers);

  // report
  const { merged, totalReqs, totalErrors } = mergeResults(workerResults);
  const rps = totalReqs / opts.duration;

  console.log();
  console.log(`--- Results ---`);
  console.log(`  requests:     ${totalReqs}`);
  console.log(`  errors:       ${totalErrors}`);
  console.log(`  throughput:   ${rps.toFixed(0)} req/s`);
  console.log(`  latency p50:  ${formatNs(merged.percentile(50))}`);
  console.log(`  latency p95:  ${formatNs(merged.percentile(95))}`);
  console.log(`  latency p99:  ${formatNs(merged.percentile(99))}`);
  console.log(`  latency max:  ${formatNs(merged.max)}`);
  console.log(`  latency mean: ${formatNs(merged.mean)}`);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
