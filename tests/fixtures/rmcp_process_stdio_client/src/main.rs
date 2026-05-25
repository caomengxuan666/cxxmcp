use std::path::PathBuf;

use anyhow::{Context, Result, ensure};
use rmcp::{
    ServiceExt,
    model::CallToolRequestParams,
    transport::{ConfigureCommandExt, TokioChildProcess},
};
use serde_json::json;
use tokio::process::Command;

#[tokio::main]
async fn main() -> Result<()> {
    let server_exe = std::env::args_os()
        .nth(1)
        .map(PathBuf::from)
        .context("expected cxxmcp process-stdio server executable path")?;

    let transport = TokioChildProcess::new(Command::new(&server_exe).configure(|cmd| {
        cmd.kill_on_drop(true);
    }))
    .with_context(|| {
        format!(
            "failed to start cxxmcp process-stdio server at {}",
            server_exe.display()
        )
    })?;

    let client = ().serve(transport).await?;

    let server_info = client
        .peer_info()
        .context("RMCP client did not receive cxxmcp server initialize result")?;
    ensure!(
        server_info.server_info.name == "process-stdio-child",
        "unexpected server name: {}",
        server_info.server_info.name
    );

    let tools = client.list_tools(Default::default()).await?;
    ensure!(
        tools.tools.iter().any(|tool| tool.name.as_ref() == "echo"),
        "RMCP client did not discover the cxxmcp echo tool"
    );

    let arguments = json!({ "value": 42 })
        .as_object()
        .context("tool arguments must be a JSON object")?
        .clone();
    let result = client
        .call_tool(CallToolRequestParams::new("echo").with_arguments(arguments))
        .await?;

    ensure!(
        result.is_error != Some(true),
        "cxxmcp echo tool returned an error result"
    );
    ensure!(
        result
            .content
            .first()
            .and_then(|content| content.as_text())
            .map(|text| text.text.as_str())
            == Some("echo"),
        "cxxmcp echo tool returned unexpected text content"
    );
    ensure!(
        result.structured_content == Some(json!({ "value": 42 })),
        "cxxmcp echo tool returned unexpected structured content"
    );

    client.cancel().await?;
    Ok(())
}
