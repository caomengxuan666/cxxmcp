use anyhow::{Context, Result, ensure};
use rmcp::{ServiceExt, transport::StreamableHttpClientTransport};

#[tokio::main]
async fn main() -> Result<()> {
    let server_url = std::env::args()
        .nth(1)
        .context("expected cxxmcp Streamable HTTP server URL")?;

    let transport = StreamableHttpClientTransport::from_uri(server_url);
    let client = ().serve(transport).await?;

    let prompt_values = client
        .complete_prompt_simple("summarize", "text", "hel")
        .await?;
    ensure!(
        prompt_values == vec!["hel-one".to_string(), "hel-two".to_string()],
        "prompt completion values mismatch: {prompt_values:?}"
    );

    let resource_values = client
        .complete_resource_simple("file:///workspace/{path}", "path", "REA")
        .await?;
    ensure!(
        resource_values == vec!["REA-one".to_string(), "REA-two".to_string()],
        "resource completion values mismatch: {resource_values:?}"
    );

    client.cancel().await?;
    Ok(())
}
