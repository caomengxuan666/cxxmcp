use std::{
    collections::HashSet,
    sync::{
        Arc,
        atomic::{AtomicUsize, Ordering},
    },
    time::Duration,
};

use rmcp::{
    ErrorData, RoleServer, ServerHandler,
    model::*,
    service::{NotificationContext, RequestContext},
    transport::{
        StreamableHttpServerConfig, StreamableHttpService,
        streamable_http_server::session::local::LocalSessionManager,
    },
};
use serde_json::json;
use tokio::sync::Mutex;
use tracing_subscriber::EnvFilter;

#[derive(Clone)]
struct ReverseServer {
    subscriptions: Arc<Mutex<HashSet<String>>>,
    cancellation_notifications: Arc<AtomicUsize>,
    cancellable_cancelled: Arc<AtomicUsize>,
}

impl ReverseServer {
    fn new() -> Self {
        Self {
            subscriptions: Arc::new(Mutex::new(HashSet::new())),
            cancellation_notifications: Arc::new(AtomicUsize::new(0)),
            cancellable_cancelled: Arc::new(AtomicUsize::new(0)),
        }
    }
}

fn schema_object() -> JsonObject {
    json!({
        "type": "object",
        "properties": {}
    })
    .as_object()
    .expect("schema must be an object")
    .clone()
}

fn task(task_id: &str, status: TaskStatus) -> Task {
    Task::new(
        task_id.to_string(),
        status,
        "2026-05-25T00:00:00Z".to_string(),
        "2026-05-25T00:00:01Z".to_string(),
    )
    .with_status_message("reverse task")
    .with_poll_interval(1)
}

impl ServerHandler for ReverseServer {
    async fn initialize(
        &self,
        _request: InitializeRequestParams,
        _cx: RequestContext<RoleServer>,
    ) -> Result<InitializeResult, ErrorData> {
        let mut capabilities = ServerCapabilities::builder()
            .enable_prompts()
            .enable_resources()
            .enable_resources_subscribe()
            .enable_tools()
            .enable_logging()
            .enable_completions()
            .enable_tasks()
            .build();
        capabilities.tasks = Some(TasksCapability::server_default());

        Ok(InitializeResult::new(capabilities)
            .with_server_info(Implementation::new("rmcp-reverse-server", "0.1.0"))
            .with_instructions("RMCP reverse interop fixture"))
    }

    async fn list_tools(
        &self,
        request: Option<PaginatedRequestParams>,
        _cx: RequestContext<RoleServer>,
    ) -> Result<ListToolsResult, ErrorData> {
        if request.and_then(|params| params.cursor).as_deref() == Some("tools-page-2") {
            Ok(ListToolsResult {
                meta: None,
                next_cursor: None,
                tools: vec![
                    Tool::new("reverse_progress", "Reports progress", schema_object()),
                    Tool::new("reverse_sampling", "Requests sampling", schema_object()),
                    Tool::new("reverse_roots", "Requests client roots", schema_object()),
                    Tool::new(
                        "reverse_cancellable",
                        "Waits for request cancellation",
                        schema_object(),
                    ),
                    Tool::new("reverse_error", "Returns isError", schema_object()),
                ],
            })
        } else {
            Ok(ListToolsResult {
                meta: None,
                next_cursor: Some("tools-page-2".to_string()),
                tools: vec![
                    Tool::new("reverse_echo", "Returns text", schema_object()),
                    Tool::new("reverse_mixed", "Returns mixed content", schema_object()),
                ],
            })
        }
    }

    async fn call_tool(
        &self,
        request: CallToolRequestParams,
        cx: RequestContext<RoleServer>,
    ) -> Result<CallToolResult, ErrorData> {
        match request.name.as_ref() {
            "reverse_echo" => Ok(CallToolResult::success(vec![Content::text("reverse echo")])),
            "reverse_mixed" => Ok(CallToolResult::success(vec![
                Content::text("reverse mixed"),
                Content::resource(ResourceContents::TextResourceContents {
                    uri: "test://reverse/resource".into(),
                    mime_type: Some("text/plain".into()),
                    text: "reverse resource".into(),
                    meta: None,
                }),
            ])),
            "reverse_progress" => {
                if let Some(token) = cx.meta.get_progress_token() {
                    cx.peer
                        .notify_progress(ProgressNotificationParam {
                            progress_token: token,
                            progress: 50.0,
                            total: Some(100.0),
                            message: Some("reverse halfway".into()),
                        })
                        .await
                        .map_err(|err| ErrorData::internal_error(err.to_string(), None))?;
                }
                Ok(CallToolResult::success(vec![Content::text(
                    "reverse progress done",
                )]))
            }
            "reverse_sampling" => {
                let result = cx
                    .peer
                    .create_message(CreateMessageRequestParams::new(
                        vec![SamplingMessage::user_text("reverse sample")],
                        32,
                    ))
                    .await
                    .map_err(|err| ErrorData::internal_error(err.to_string(), None))?;
                let text = result
                    .message
                    .content
                    .first()
                    .and_then(|content| content.as_text())
                    .map(|content| content.text.clone())
                    .unwrap_or_else(|| "missing sampling text".into());
                Ok(CallToolResult::success(vec![Content::text(format!(
                    "reverse sampled: {text}"
                ))]))
            }
            "reverse_roots" => {
                let result = cx
                    .peer
                    .list_roots()
                    .await
                    .map_err(|err| ErrorData::internal_error(err.to_string(), None))?;
                let roots = result
                    .roots
                    .iter()
                    .map(|root| {
                        root.name
                            .as_ref()
                            .map(|name| format!("{} ({name})", root.uri))
                            .unwrap_or_else(|| root.uri.clone())
                    })
                    .collect::<Vec<_>>()
                    .join(", ");
                Ok(CallToolResult::success(vec![Content::text(format!(
                    "reverse roots: {roots}"
                ))]))
            }
            "reverse_cancellable" => {
                let mode = request
                    .arguments
                    .as_ref()
                    .and_then(|args| args.get("mode"))
                    .and_then(|value| value.as_str())
                    .unwrap_or("run");
                if mode == "status" {
                    return Ok(CallToolResult::success(vec![Content::text(format!(
                        "notifications={} cancelled={}",
                        self.cancellation_notifications.load(Ordering::SeqCst),
                        self.cancellable_cancelled.load(Ordering::SeqCst)
                    ))]));
                }

                tokio::select! {
                    _ = cx.ct.cancelled() => {
                        self.cancellable_cancelled.fetch_add(1, Ordering::SeqCst);
                        Ok(CallToolResult::success(vec![Content::text(
                            "reverse cancellable observed cancellation",
                        )]))
                    }
                    _ = tokio::time::sleep(Duration::from_secs(5)) => {
                        Ok(CallToolResult::success(vec![Content::text(
                            "reverse cancellable completed without cancellation",
                        )]))
                    }
                }
            }
            "reverse_error" => Ok(CallToolResult::error(vec![Content::text("reverse error")])),
            _ => Err(ErrorData::method_not_found::<CallToolRequestMethod>()),
        }
    }

    async fn on_cancelled(
        &self,
        _notification: CancelledNotificationParam,
        _cx: NotificationContext<RoleServer>,
    ) {
        self.cancellation_notifications
            .fetch_add(1, Ordering::SeqCst);
    }

    async fn list_prompts(
        &self,
        _request: Option<PaginatedRequestParams>,
        _cx: RequestContext<RoleServer>,
    ) -> Result<ListPromptsResult, ErrorData> {
        Ok(ListPromptsResult {
            meta: None,
            next_cursor: None,
            prompts: vec![Prompt::new(
                "reverse_prompt",
                Some("Reverse prompt"),
                Some(vec![PromptArgument::new("name").with_required(true)]),
            )],
        })
    }

    async fn get_prompt(
        &self,
        request: GetPromptRequestParams,
        _cx: RequestContext<RoleServer>,
    ) -> Result<GetPromptResult, ErrorData> {
        let name = request
            .arguments
            .as_ref()
            .and_then(|args| args.get("name"))
            .and_then(|value| value.as_str())
            .unwrap_or("unknown");
        Ok(GetPromptResult::new(vec![PromptMessage::new_text(
            PromptMessageRole::User,
            format!("hello {name}"),
        )]))
    }

    async fn complete(
        &self,
        request: CompleteRequestParams,
        _cx: RequestContext<RoleServer>,
    ) -> Result<CompleteResult, ErrorData> {
        let values = if request.argument.name == "name" {
            vec!["Alice".to_string(), "Alex".to_string()]
        } else {
            vec![]
        };
        Ok(CompleteResult::new(
            CompletionInfo::new(values).map_err(|err| ErrorData::internal_error(err, None))?,
        ))
    }

    async fn list_resources(
        &self,
        _request: Option<PaginatedRequestParams>,
        _cx: RequestContext<RoleServer>,
    ) -> Result<ListResourcesResult, ErrorData> {
        Ok(ListResourcesResult {
            meta: None,
            next_cursor: None,
            resources: vec![
                RawResource::new("test://reverse/static", "Reverse Static")
                    .with_mime_type("text/plain")
                    .no_annotation(),
            ],
        })
    }

    async fn read_resource(
        &self,
        request: ReadResourceRequestParams,
        _cx: RequestContext<RoleServer>,
    ) -> Result<ReadResourceResult, ErrorData> {
        if request.uri == "test://reverse/static" {
            Ok(ReadResourceResult::new(vec![
                ResourceContents::TextResourceContents {
                    uri: request.uri,
                    mime_type: Some("text/plain".into()),
                    text: "reverse static resource".into(),
                    meta: None,
                },
            ]))
        } else {
            Err(ErrorData::resource_not_found(
                format!("missing resource {}", request.uri),
                None,
            ))
        }
    }

    async fn subscribe(
        &self,
        request: SubscribeRequestParams,
        _cx: RequestContext<RoleServer>,
    ) -> Result<(), ErrorData> {
        self.subscriptions
            .lock()
            .await
            .insert(request.uri.to_string());
        Ok(())
    }

    async fn unsubscribe(
        &self,
        request: UnsubscribeRequestParams,
        _cx: RequestContext<RoleServer>,
    ) -> Result<(), ErrorData> {
        self.subscriptions.lock().await.remove(request.uri.as_str());
        Ok(())
    }

    async fn list_tasks(
        &self,
        _request: Option<PaginatedRequestParams>,
        _cx: RequestContext<RoleServer>,
    ) -> Result<ListTasksResult, ErrorData> {
        let mut result = ListTasksResult::new(vec![
            task("reverse-working", TaskStatus::Working),
            task("reverse-completed", TaskStatus::Completed),
        ]);
        result.total = Some(2);
        Ok(result)
    }

    async fn get_task_info(
        &self,
        request: GetTaskInfoParams,
        _cx: RequestContext<RoleServer>,
    ) -> Result<GetTaskResult, ErrorData> {
        Ok(GetTaskResult {
            meta: None,
            task: task(&request.task_id, TaskStatus::Completed),
        })
    }

    async fn cancel_task(
        &self,
        request: CancelTaskParams,
        _cx: RequestContext<RoleServer>,
    ) -> Result<CancelTaskResult, ErrorData> {
        Ok(CancelTaskResult {
            meta: None,
            task: task(&request.task_id, TaskStatus::Cancelled),
        })
    }
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::from_default_env().add_directive(tracing::Level::INFO.into()))
        .init();

    let port: u16 = std::env::var("PORT")
        .ok()
        .and_then(|value| value.parse().ok())
        .unwrap_or(8002);
    let bind_addr = format!("127.0.0.1:{port}");
    let server = ReverseServer::new();
    let service = StreamableHttpService::new(
        move || Ok(server.clone()),
        LocalSessionManager::default().into(),
        StreamableHttpServerConfig::default(),
    );
    let router = axum::Router::new().nest_service("/mcp", service);
    let listener = tokio::net::TcpListener::bind(&bind_addr).await?;
    axum::serve(listener, router).await?;
    Ok(())
}
