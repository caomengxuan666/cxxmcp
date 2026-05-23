from mcp.server.fastmcp import FastMCP


mcp = FastMCP("process-stdio-child-py")


@mcp.tool()
def echo(message: str) -> str:
    return message


if __name__ == "__main__":
    mcp.run()
