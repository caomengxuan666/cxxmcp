use std::sync::{
    Arc,
    atomic::{AtomicBool, Ordering},
};
use std::time::Duration;

use anyhow::{Context, Result, bail, ensure};
use rmcp::{
    ClientHandler, RoleClient, ServiceExt, model::*, service::NotificationContext,
    transport::StreamableHttpClientTransport,
};

struct TaskLifecycleClientHandler {
    saw_progress: Arc<AtomicBool>,
}

impl ClientHandler for TaskLifecycleClientHandler {
    async fn on_progress(
        &self,
        params: ProgressNotificationParam,
        _context: NotificationContext<RoleClient>,
    ) {
        if params.message.as_deref() == Some("rmcp progress") {
            self.saw_progress.store(true, Ordering::SeqCst);
        }
    }

    async fn on_cancelled(
        &self,
        _params: CancelledNotificationParam,
        _context: NotificationContext<RoleClient>,
    ) {
    }
}

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

async fn wait_for_progress(saw_progress: &AtomicBool) -> bool {
    for _ in 0..50 {
        if saw_progress.load(Ordering::SeqCst) {
            return true;
        }
        tokio::time::sleep(Duration::from_millis(20)).await;
    }
    saw_progress.load(Ordering::SeqCst)
}

#[tokio::main]
async fn main() -> Result<()> {
    let server_url = std::env::args()
        .nth(1)
        .context("expected cxxmcp Streamable HTTP server URL")?;
    let saw_progress = Arc::new(AtomicBool::new(false));

    let transport = StreamableHttpClientTransport::from_uri(server_url);
    let client = TaskLifecycleClientHandler {
        saw_progress: saw_progress.clone(),
    }
    .serve(transport)
    .await?;

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
    ensure!(
        tasks
            .tasks
            .iter()
            .any(|task| task.task_id == "task-failed" && task.status == TaskStatus::Failed),
        "tasks/list should expose a failed task"
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

    let progress = client
        .call_tool(CallToolRequestParams::new("test_progress_roundtrip"))
        .await?;
    ensure!(
        progress.is_error != Some(true),
        "progress round-trip tool should not return an error"
    );
    ensure!(
        wait_for_progress(&saw_progress).await,
        "RMCP client should observe notifications/progress"
    );

    client.cancel().await?;
    Ok(())
}
