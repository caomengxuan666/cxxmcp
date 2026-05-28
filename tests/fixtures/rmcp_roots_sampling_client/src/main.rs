use anyhow::{Context, Result, ensure};
use rmcp::{
    ClientHandler, ErrorData, RoleClient, ServiceExt, model::*, service::RequestContext,
    transport::StreamableHttpClientTransport,
};

struct RootsSamplingClientHandler;

impl ClientHandler for RootsSamplingClientHandler {
    fn get_info(&self) -> ClientInfo {
        let mut info = ClientInfo::default();
        info.capabilities.roots = Some(RootsCapabilities {
            list_changed: Some(true),
        });
        info.capabilities.sampling = Some(SamplingCapability {
            tools: None,
            context: None,
        });
        info
    }

    async fn list_roots(
        &self,
        _context: RequestContext<RoleClient>,
    ) -> Result<ListRootsResult, ErrorData> {
        Ok(ListRootsResult::new(vec![
            Root::new("file:///rmcp-root").with_name("rmcp-root"),
        ]))
    }

    async fn create_message(
        &self,
        params: CreateMessageRequestParams,
        _context: RequestContext<RoleClient>,
    ) -> Result<CreateMessageResult, ErrorData> {
        let prompt_text = params
            .messages
            .first()
            .and_then(|message| message.content.first())
            .and_then(|content| content.as_text())
            .map(|text| text.text.clone())
            .unwrap_or_default();
        Ok(CreateMessageResult::new(
            SamplingMessage::new(
                Role::Assistant,
                SamplingMessageContent::text(format!("RMCP sampled: {prompt_text}")),
            ),
            "rmcp-fixture-model".into(),
        )
        .with_stop_reason("endTurn"))
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    let server_url = std::env::args()
        .nth(1)
        .context("expected cxxmcp Streamable HTTP server URL")?;

    let transport = StreamableHttpClientTransport::from_uri(server_url);
    let client = RootsSamplingClientHandler.serve(transport).await?;

    let tools = client.list_tools(Default::default()).await?;
    ensure!(
        tools
            .tools
            .iter()
            .any(|tool| tool.name.as_ref() == "test_roots_roundtrip"),
        "roots round-trip tool should be advertised"
    );
    ensure!(
        tools
            .tools
            .iter()
            .any(|tool| tool.name.as_ref() == "test_sampling_roundtrip"),
        "sampling round-trip tool should be advertised"
    );

    let roots = client
        .call_tool(CallToolRequestParams::new("test_roots_roundtrip"))
        .await?;
    ensure!(
        roots.is_error != Some(true),
        "roots round-trip tool should not return an error"
    );

    let sampling = client
        .call_tool(CallToolRequestParams::new("test_sampling_roundtrip"))
        .await?;
    ensure!(
        sampling.is_error != Some(true),
        "sampling round-trip tool should not return an error"
    );

    client.cancel().await?;
    Ok(())
}
