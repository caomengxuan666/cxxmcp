import asyncio
import importlib.metadata
import sys

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def main() -> None:
    if len(sys.argv) != 2:
        raise RuntimeError("usage: python process_stdio_client.py <server-executable>")

    print(f"using mcp=={importlib.metadata.version('mcp')}")

    params = StdioServerParameters(command=sys.argv[1], args=[])
    async with stdio_client(params) as streams:
        read, write = streams
        async with ClientSession(read, write) as session:
            await session.initialize()

            tools = await session.list_tools()
            if not any(tool.name == "echo" for tool in tools.tools):
                raise RuntimeError(f"echo tool not found: {tools!r}")

            result = await session.call_tool("echo", arguments={"value": 42})
            text = getattr(result.content[0], "text", None) if result.content else None
            if text != "echo":
                raise RuntimeError(f"unexpected echo result: {result!r}")


if __name__ == "__main__":
    asyncio.run(main())
