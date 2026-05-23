import { copyFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawn, spawnSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const fixtureDir = dirname(fileURLToPath(import.meta.url));
const runtimeRoot = mkdtempSync(join(tmpdir(), 'mcp-ts-fixture-'));
const sourceScript = join(fixtureDir, 'process_stdio_child.mjs');
const runtimeScript = join(runtimeRoot, 'process_stdio_child.mjs');

try {
  copyFileSync(sourceScript, runtimeScript);

  const installCommand = `npm install --silent --no-fund --no-audit --prefix "${runtimeRoot}" @modelcontextprotocol/server zod @cfworker/json-schema`;
  const install = spawnSync(installCommand, { encoding: 'utf8', shell: true });
  if (install.status !== 0) {
    throw new Error(`failed to install Node fixture dependencies (status=${install.status}, signal=${install.signal ?? ''})${install.error ? `: ${install.error.message}` : ''}\n${install.stderr ?? ''}`);
  }

  const child = spawn(process.execPath, [runtimeScript], { stdio: 'inherit' });
  const exitCode = await new Promise((resolve, reject) => {
    child.on('error', reject);
    child.on('exit', (code, signal) => {
      resolve(code ?? (signal ? 1 : 0));
    });
  });
  process.exitCode = exitCode;
} finally {
  rmSync(runtimeRoot, { recursive: true, force: true });
}
