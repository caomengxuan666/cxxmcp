# cxxmcp External Consumer Template

This is a minimal out-of-tree project for SDK consumers. It assumes cxxmcp has
already been installed or provided by a package manager that exposes
`cxxmcpConfig.cmake`.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/cxxmcp/install
cmake --build build
```

Choose the narrowest target for your application:

- `cxxmcp::protocol` for protocol models and serializers.
- `cxxmcp::client` for embeddable MCP clients.
- `cxxmcp::server` for embeddable MCP servers.
- `cxxmcp::sdk` only when one target intentionally needs the full SDK.
