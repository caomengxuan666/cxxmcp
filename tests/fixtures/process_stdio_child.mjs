import { McpServer, StdioServerTransport } from '@modelcontextprotocol/server';
import * as z from 'zod/v4';

const server = new McpServer({
  name: 'process-stdio-child-js',
  version: '1.0.0',
});

server.registerTool(
  'echo',
  {
    description: 'Echo test tool',
    inputSchema: z.object({
      value: z.number(),
    }),
    outputSchema: z.object({
      value: z.number(),
    }),
  },
  async ({ value }) => ({
    content: [{ type: 'text', text: 'echo' }],
    structuredContent: { value },
  }),
);

await server.connect(new StdioServerTransport());
