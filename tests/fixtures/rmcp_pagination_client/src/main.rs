use anyhow::{Context, Result, bail, ensure};
use rmcp::{
    ServiceExt,
    model::{ClientRequest, ListTasksRequest, PaginatedRequestParams, ServerResult},
    transport::StreamableHttpClientTransport,
};

fn page(cursor: Option<String>) -> PaginatedRequestParams {
    PaginatedRequestParams::default().with_cursor(cursor)
}

#[tokio::main]
async fn main() -> Result<()> {
    let server_url = std::env::args()
        .nth(1)
        .context("expected cxxmcp Streamable HTTP server URL")?;

    let transport = StreamableHttpClientTransport::from_uri(server_url);
    let client = ().serve(transport).await?;

    let tools_page_one = client.list_tools(Some(page(None))).await?;
    ensure!(
        tools_page_one.tools.len() == 1,
        "tools page one size mismatch"
    );
    ensure!(
        tools_page_one.tools[0].name.as_ref() == "page-tool-one",
        "tools page one item mismatch"
    );
    ensure!(
        tools_page_one.next_cursor.as_deref() == Some("tools-page-2"),
        "tools page one cursor mismatch"
    );
    let tools_page_two = client
        .list_tools(Some(page(tools_page_one.next_cursor.clone())))
        .await?;
    ensure!(
        tools_page_two.tools.len() == 1,
        "tools page two size mismatch"
    );
    ensure!(
        tools_page_two.tools[0].name.as_ref() == "page-tool-two",
        "tools page two item mismatch"
    );
    ensure!(
        tools_page_two.next_cursor.is_none(),
        "tools page two should finish pagination"
    );
    ensure!(
        client
            .list_tools(Some(page(Some("bad-tools-cursor".to_string()))))
            .await
            .is_err(),
        "invalid tools cursor should fail"
    );
    let all_tools = client.list_all_tools().await?;
    ensure!(all_tools.len() == 2, "list_all_tools size mismatch");

    let prompts_page_one = client.list_prompts(Some(page(None))).await?;
    ensure!(
        prompts_page_one.prompts.len() == 1,
        "prompts page one size mismatch"
    );
    ensure!(
        prompts_page_one.prompts[0].name == "page-prompt-one",
        "prompts page one item mismatch"
    );
    ensure!(
        prompts_page_one.next_cursor.as_deref() == Some("prompts-page-2"),
        "prompts page one cursor mismatch"
    );
    let prompts_page_two = client
        .list_prompts(Some(page(prompts_page_one.next_cursor.clone())))
        .await?;
    ensure!(
        prompts_page_two.prompts.len() == 1,
        "prompts page two size mismatch"
    );
    ensure!(
        prompts_page_two.prompts[0].name == "page-prompt-two",
        "prompts page two item mismatch"
    );
    ensure!(
        prompts_page_two.next_cursor.is_none(),
        "prompts page two should finish pagination"
    );
    ensure!(
        client
            .list_prompts(Some(page(Some("bad-prompts-cursor".to_string()))))
            .await
            .is_err(),
        "invalid prompts cursor should fail"
    );
    let all_prompts = client.list_all_prompts().await?;
    ensure!(all_prompts.len() == 2, "list_all_prompts size mismatch");

    let resources_page_one = client.list_resources(Some(page(None))).await?;
    ensure!(
        resources_page_one.resources.len() == 1,
        "resources page one size mismatch"
    );
    ensure!(
        resources_page_one.resources[0].uri == "file:///page-one",
        "resources page one item mismatch"
    );
    ensure!(
        resources_page_one.next_cursor.as_deref() == Some("resources-page-2"),
        "resources page one cursor mismatch"
    );
    let resources_page_two = client
        .list_resources(Some(page(resources_page_one.next_cursor.clone())))
        .await?;
    ensure!(
        resources_page_two.resources.len() == 1,
        "resources page two size mismatch"
    );
    ensure!(
        resources_page_two.resources[0].uri == "file:///page-two",
        "resources page two item mismatch"
    );
    ensure!(
        resources_page_two.next_cursor.is_none(),
        "resources page two should finish pagination"
    );
    ensure!(
        client
            .list_resources(Some(page(Some("bad-resources-cursor".to_string()))))
            .await
            .is_err(),
        "invalid resources cursor should fail"
    );
    let all_resources = client.list_all_resources().await?;
    ensure!(all_resources.len() == 2, "list_all_resources size mismatch");

    let templates_page_one = client.list_resource_templates(Some(page(None))).await?;
    ensure!(
        templates_page_one.resource_templates.len() == 1,
        "templates page one size mismatch"
    );
    ensure!(
        templates_page_one.resource_templates[0].name == "page-template-one",
        "templates page one item mismatch"
    );
    ensure!(
        templates_page_one.next_cursor.as_deref() == Some("templates-page-2"),
        "templates page one cursor mismatch"
    );
    let templates_page_two = client
        .list_resource_templates(Some(page(templates_page_one.next_cursor.clone())))
        .await?;
    ensure!(
        templates_page_two.resource_templates.len() == 1,
        "templates page two size mismatch"
    );
    ensure!(
        templates_page_two.resource_templates[0].name == "page-template-two",
        "templates page two item mismatch"
    );
    ensure!(
        templates_page_two.next_cursor.is_none(),
        "templates page two should finish pagination"
    );
    ensure!(
        client
            .list_resource_templates(Some(page(Some("bad-templates-cursor".to_string()))))
            .await
            .is_err(),
        "invalid templates cursor should fail"
    );
    let all_templates = client.list_all_resource_templates().await?;
    ensure!(
        all_templates.len() == 2,
        "list_all_resource_templates size mismatch"
    );

    let tasks_page_one = client
        .send_request(ClientRequest::ListTasksRequest(
            ListTasksRequest::with_param(page(None)),
        ))
        .await?;
    let ServerResult::ListTasksResult(tasks_page_one) = tasks_page_one else {
        bail!("tasks/list page one returned unexpected result variant");
    };
    ensure!(
        tasks_page_one.tasks.len() == 1,
        "tasks page one size mismatch"
    );
    ensure!(
        tasks_page_one.tasks[0].task_id == "page-task-one",
        "tasks page one item mismatch"
    );
    ensure!(
        tasks_page_one.next_cursor.as_deref() == Some("tasks-page-2"),
        "tasks page one cursor mismatch"
    );

    let tasks_page_two = client
        .send_request(ClientRequest::ListTasksRequest(
            ListTasksRequest::with_param(page(tasks_page_one.next_cursor.clone())),
        ))
        .await?;
    let ServerResult::ListTasksResult(tasks_page_two) = tasks_page_two else {
        bail!("tasks/list page two returned unexpected result variant");
    };
    ensure!(
        tasks_page_two.tasks.len() == 1,
        "tasks page two size mismatch"
    );
    ensure!(
        tasks_page_two.tasks[0].task_id == "page-task-two",
        "tasks page two item mismatch"
    );
    ensure!(
        tasks_page_two.next_cursor.is_none(),
        "tasks page two should finish pagination"
    );
    ensure!(tasks_page_two.total == Some(2), "tasks total mismatch");
    ensure!(
        client
            .send_request(ClientRequest::ListTasksRequest(
                ListTasksRequest::with_param(page(Some("bad-tasks-cursor".to_string()))),
            ))
            .await
            .is_err(),
        "invalid tasks cursor should fail"
    );

    client.cancel().await?;
    Ok(())
}
