use anyhow::Result;
use rmcp::{
    handler::server::wrapper::Parameters, schemars, tool, tool_router, transport::stdio,
    ServiceExt,
};
use serde::Deserialize;

#[derive(Debug, Deserialize, schemars::JsonSchema)]
struct EchoParams {
    value: i32,
}

#[derive(Clone)]
struct RustProcessStdioChild;

#[tool_router(server_handler)]
impl RustProcessStdioChild {
    #[tool(description = "Echo test tool")]
    fn echo(&self, Parameters(EchoParams { value }): Parameters<EchoParams>) -> String {
        value.to_string()
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    RustProcessStdioChild.serve(stdio()).await?.waiting().await?;
    Ok(())
}
