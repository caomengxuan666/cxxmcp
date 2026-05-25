import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';

const serverPath = process.argv[2];
if (!serverPath) {
  throw new Error('usage: node process_stdio_client.mjs <server-executable>');
}

const client = new Client({
  name: 'cxxmcp-ts-process-client',
  version: '1.0.0',
});

const transport = new StdioClientTransport({
  command: serverPath,
  args: [],
});

await client.connect(transport);

try {
  const tools = await client.listTools();
  if (!tools.tools.some((tool) => tool.name === 'echo')) {
    throw new Error(`echo tool not found: ${JSON.stringify(tools)}`);
  }

  const result = await client.callTool({
    name: 'echo',
    arguments: { value: 42 },
  });
  const text = result.content?.[0]?.text;
  if (text !== 'echo') {
    throw new Error(`unexpected echo result: ${JSON.stringify(result)}`);
  }
  if (result.structuredContent?.value !== 42) {
    throw new Error(`unexpected structured content: ${JSON.stringify(result)}`);
  }
} finally {
  await client.close();
}
