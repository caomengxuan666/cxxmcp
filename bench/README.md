# Benchmark

Node.js driver for benchmarking cxxmcp Streamable HTTP servers.
Zero external dependencies — uses Node's built-in `http` module.

## Usage

1. Build and start a cxxmcp HTTP server (e.g. the streamable HTTP example)
2. Run the benchmark:

```bash
node bench/mcp-bench.mjs --url http://127.0.0.1:3000/mcp
```

## Options

| Flag | Default | Description |
|------|---------|-------------|
| `--url` | `http://127.0.0.1:3000/mcp` | MCP server endpoint |
| `--concurrency` | `10` | Number of parallel clients |
| `--duration` | `5` | Test duration in seconds |
| `--op` | `echo` | Operation: `tools-list`, `echo`, `ping` |
| `--warmup` | `1` | Warmup duration in seconds |
| `--payload` | `256` | Echo payload size in bytes |

## Example

```bash
# Light load — 4 clients, 10 seconds
node bench/mcp-bench.mjs --concurrency 4 --duration 10

# Stress test — 50 clients, large payloads
node bench/mcp-bench.mjs --concurrency 50 --payload 4096

# Protocol overhead only (ping)
node bench/mcp-bench.mjs --op ping --concurrency 20
```

## Output

```
cxxmcp benchmark driver
  url:         http://127.0.0.1:3000/mcp
  operation:   echo
  concurrency: 10
  duration:    5s
  warmup:      1s
  payload:     256 bytes

MCP session established
Warming up...
Running benchmark...

--- Results ---
  requests:     12340
  errors:       0
  throughput:   2468 req/s
  latency p50:  3.82 ms
  latency p95:  7.21 ms
  latency p99:  12.45 ms
  latency max:  28.91 ms
  latency mean: 4.05 ms
```
