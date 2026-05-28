use std::path::PathBuf;

use anyhow::{Context, Result, bail, ensure};
use rmcp::{
    ServiceExt,
    model::{
        CallToolRequestParams, CancelTaskParams, CancelTaskRequest, ClientRequest,
        GetPromptRequestParams, GetTaskInfoParams, GetTaskInfoRequest, ListTasksRequest,
        PaginatedRequestParams, PromptMessageContent, ReadResourceRequestParams, ResourceContents,
        ServerResult, TaskStatus,
    },
    transport::{ConfigureCommandExt, TokioChildProcess},
};
use serde_json::json;
use tokio::process::Command;

fn no_page() -> PaginatedRequestParams {
    PaginatedRequestParams::default()
}

fn get_task(task_id: &str) -> GetTaskInfoParams {
    GetTaskInfoParams {
        meta: None,
        task_id: task_id.to_string(),
    }
}

fn cancel_task(task_id: &str) -> CancelTaskParams {
    CancelTaskParams {
        meta: None,
        task_id: task_id.to_string(),
    }
}

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

    let prompts = client.list_prompts(Default::default()).await?;
    ensure!(
        prompts
            .prompts
            .iter()
            .any(|prompt| prompt.name == "summarize"),
        "RMCP client did not discover the cxxmcp summarize prompt"
    );

    let prompt_arguments = json!({ "text": "interop" })
        .as_object()
        .context("prompt arguments must be a JSON object")?
        .clone();
    let prompt = client
        .get_prompt(GetPromptRequestParams::new("summarize").with_arguments(prompt_arguments))
        .await?;
    ensure!(
        prompt.description.as_deref() == Some("Summarize test prompt"),
        "cxxmcp prompt description mismatch"
    );
    let prompt_text = prompt
        .messages
        .first()
        .and_then(|message| match &message.content {
            PromptMessageContent::Text { text } => Some(text.as_str()),
            _ => None,
        });
    ensure!(
        prompt_text == Some("Summarize interop"),
        "cxxmcp prompt returned unexpected content"
    );

    let resources = client.list_resources(Default::default()).await?;
    ensure!(
        resources
            .resources
            .iter()
            .any(|resource| resource.uri == "file:///workspace/README.md"),
        "RMCP client did not discover the cxxmcp readme resource"
    );

    let readme = client
        .read_resource(ReadResourceRequestParams::new(
            "file:///workspace/README.md",
        ))
        .await?;
    let readme_text = readme.contents.first().and_then(|content| match content {
        ResourceContents::TextResourceContents { text, .. } => Some(text.as_str()),
        _ => None,
    });
    ensure!(
        readme_text == Some("hello from readme"),
        "cxxmcp resource returned unexpected text"
    );
    ensure!(
        client
            .read_resource(ReadResourceRequestParams::new("file:///missing.md"))
            .await
            .is_err(),
        "missing cxxmcp resource should return an error"
    );

    let tasks = client
        .send_request(ClientRequest::ListTasksRequest(
            ListTasksRequest::with_param(no_page()),
        ))
        .await?;
    let ServerResult::ListTasksResult(tasks) = tasks else {
        bail!("tasks/list returned unexpected result variant");
    };
    ensure!(
        tasks
            .tasks
            .iter()
            .any(|task| task.task_id == "task-working" && task.status == TaskStatus::Working),
        "tasks/list should expose a working task"
    );
    ensure!(
        tasks
            .tasks
            .iter()
            .any(|task| task.task_id == "task-completed" && task.status == TaskStatus::Completed),
        "tasks/list should expose a completed task"
    );

    let completed = client
        .send_request(ClientRequest::GetTaskInfoRequest(GetTaskInfoRequest::new(
            get_task("task-completed"),
        )))
        .await?;
    let ServerResult::GetTaskResult(completed) = completed else {
        bail!("tasks/get returned unexpected result variant");
    };
    ensure!(
        completed.task.status == TaskStatus::Completed,
        "tasks/get completed status mismatch"
    );

    let cancelled = client
        .send_request(ClientRequest::CancelTaskRequest(CancelTaskRequest::new(
            cancel_task("task-cancelled"),
        )))
        .await?;
    let cancelled_task = match cancelled {
        ServerResult::CancelTaskResult(cancelled) => cancelled.task,
        ServerResult::GetTaskResult(cancelled) => cancelled.task,
        _ => bail!("tasks/cancel returned unexpected result variant"),
    };
    ensure!(
        cancelled_task.status == TaskStatus::Cancelled,
        "tasks/cancel status mismatch"
    );
    ensure!(
        client
            .send_request(ClientRequest::GetTaskInfoRequest(GetTaskInfoRequest::new(
                get_task("missing-task"),
            )))
            .await
            .is_err(),
        "missing task should return an error"
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
