import { copyFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawn, spawnSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const serverPath = process.argv[2];
if (!serverPath) {
  throw new Error('usage: node process_stdio_client_node.mjs <server-executable>');
}

const fixtureDir = dirname(fileURLToPath(import.meta.url));
const runtimeRoot = mkdtempSync(join(tmpdir(), 'mcp-ts-client-fixture-'));
const sourceScript = join(fixtureDir, 'process_stdio_client.mjs');
const runtimeScript = join(runtimeRoot, 'process_stdio_client.mjs');

try {
  copyFileSync(sourceScript, runtimeScript);

  const installCommand =
    `npm install --silent --no-fund --no-audit --prefix "${runtimeRoot}" @modelcontextprotocol/sdk`;
  const install = spawnSync(installCommand, { encoding: 'utf8', shell: true });
  if (install.status !== 0) {
    throw new Error(
      `failed to install TypeScript MCP SDK fixture dependencies ` +
        `(status=${install.status}, signal=${install.signal ?? ''})` +
        `${install.error ? `: ${install.error.message}` : ''}\n` +
        `${install.stderr ?? ''}`,
    );
  }

  const child = spawn(process.execPath, [runtimeScript, serverPath], {
    stdio: 'inherit',
  });
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
