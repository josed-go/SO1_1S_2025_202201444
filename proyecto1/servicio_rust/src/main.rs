use std::fs;
use std::time::Duration;
use serde::{Deserialize, Serialize};
use chrono::Utc;
use docker_api::Docker;
use tokio::time::sleep;
use reqwest::Client;
use ctrlc;
use std::process::Command;

#[derive(Debug, Serialize, Deserialize)]
struct SysInfo {
    memory: MemoryInfo,
    cpu_usage_percent: f32,
    processes: Vec<ProcessInfo>,
}

#[derive(Debug, Serialize, Deserialize)]
struct MemoryInfo {
    total: MemoryUnit,
    free: MemoryUnit,
    used: MemoryUnit,
}

#[derive(Debug, Serialize, Deserialize)]
struct MemoryUnit {
    kb: u64,
    mb: u64,
    gb: u64,
}

#[derive(Debug, Serialize, Deserialize)]
struct ProcessInfo {
    pid: u32,
    name: String,
    container_id: String,
    command: String,
    cpu_usage_percent: f32,
    memory_usage_percent: f32,
    disk_usage_mb: u64,
    block_io: BlockIO,
}

#[derive(Debug, Serialize, Deserialize)]
struct BlockIO {
    read_mb: u64,
    write_mb: u64,
}

#[derive(Debug, Serialize)]
struct LogEntry {
    container_name: String,
    container_id: String,
    category: String,
    created_at: String,
    deleted_at: Option<String>,
    cpu_usage_percent: f32,          // Nuevo campo
    memory_usage_percent: f32,       // Nuevo campo
    disk_usage_mb: u64,              // Nuevo campo
    block_io_read_mb: u64,           // Nuevo campo (de block_io)
    block_io_write_mb: u64,          // Nuevo campo (de block_io)
}

#[tokio::main]
async fn main() {
    let docker = Docker::new("unix:///var/run/docker.sock").unwrap();
    let http_client = Client::new();

    create_cronjob();

    let log_container_id = start_log_container(&docker).await;

    let running = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(true));
    let r = running.clone();
    ctrlc::set_handler(move || {
        r.store(false, std::sync::atomic::Ordering::SeqCst);
        remove_cronjob();
    }).expect("Error setting Ctrl+C handler");

    while running.load(std::sync::atomic::Ordering::SeqCst) {
        manage_containers(&docker, &http_client, &log_container_id).await;
        sleep(Duration::from_secs(10)).await;
    }

    finalize(&docker, &http_client, &log_container_id).await;
}

fn create_cronjob() {
    let script_path = "/home/jd/labsopes/SO1_1S_2025_202201444/proyecto1/containers/containers.sh";
    let cron_entry = format!(
        "* * * * * /bin/bash {} && sleep 30 && /bin/bash {} >> /tmp/containers.log 2>&1\n",
        script_path, script_path
    );

    let current_crontab = Command::new("crontab")
        .arg("-l")
        .output()
        .map(|output| String::from_utf8_lossy(&output.stdout).to_string())
        .unwrap_or_default();

    if !current_crontab.contains(script_path) {
        let updated_crontab = format!("{}{}", current_crontab.trim_end(), cron_entry);
        let status = Command::new("sh")
            .arg("-c")
            .arg(format!("echo '{}' | crontab -", updated_crontab))
            .status()
            .expect("Error al intentar crear el cronjob");

        if status.success() {
            println!("Cronjob creado: ejecuta containers.sh cada 30 segundos");
        } else {
            eprintln!("Fallo al crear el cronjob");
        }
    } else {
        println!("El cronjob ya existe, no se creará uno nuevo");
    }
}

fn remove_cronjob() {
    let script_path = "/home/jd/labsopes/SO1_1S_2025_202201444/proyecto1/containers/containers.sh";
    let current_crontab = Command::new("crontab")
        .arg("-l")
        .output()
        .map(|output| String::from_utf8_lossy(&output.stdout).to_string())
        .unwrap_or_default();

    let updated_crontab = current_crontab
        .lines()
        .filter(|line| !line.contains(script_path))
        .collect::<Vec<&str>>()
        .join("\n");

    let status = Command::new("sh")
        .arg("-c")
        .arg(format!("echo '{}' | crontab -", updated_crontab))
        .status()
        .expect("Error al intentar eliminar el cronjob");

    if status.success() {
        println!("Cronjob eliminado correctamente");
    } else {
        eprintln!("Fallo al eliminar el cronjob");
    }
}

async fn start_log_container(docker: &Docker) -> String {
    let log_container_name = "log-container";

    if let Ok(_container) = docker.containers().get(log_container_name).inspect().await {
        println!("Contenedor de logs ya existe. Eliminándolo...");
        let remove_opts = docker_api::opts::ContainerRemoveOpts::builder()
            .force(true)
            .build();
        if let Err(e) = docker.containers()
            .get(log_container_name)
            .remove(&remove_opts)
            .await
        {
            println!("Error al eliminar el contenedor de logs: {}", e);
        }
    }

    let opts = docker_api::opts::ContainerCreateOpts::builder()
        .image("log-container-image")
        .name(log_container_name)
        .volumes(vec![
            "/home/jd/labsopes/SO1_1S_2025_202201444/proyecto1/log-container/logs:/container/logs"
        ])
        .expose(docker_api::opts::PublishPort::tcp(8080), 8080)
        .build();

    let container = docker.containers().create(&opts).await.unwrap();
    container.start().await.unwrap();
    println!("Contenedor de logs iniciado con ID: {}", container.id());

    sleep(Duration::from_secs(2)).await;
    container.id().to_string()
}

async fn manage_containers(docker: &Docker, client: &Client, log_container_id: &str) {
    let content = match fs::read_to_string("/proc/sysinfo_202201444") {
        Ok(content) => content,
        Err(e) => {
            println!("Error al leer /proc/sysinfo_202201444: {}", e);
            return;
        }
    };
    let sys_info: SysInfo = serde_json::from_str(&content).unwrap();

    println!("=== Información del Sistema ===");
    println!("Memoria Total: {} MB", sys_info.memory.total.mb);
    println!("Memoria Libre: {} MB", sys_info.memory.free.mb);
    println!("Memoria Usada: {} MB", sys_info.memory.used.mb);
    println!("Uso de CPU: {:.2}%", sys_info.cpu_usage_percent);

    let mut cpu_containers = Vec::new();
    let mut ram_containers = Vec::new();
    let mut io_containers = Vec::new();
    let mut disk_containers = Vec::new();

    for process in &sys_info.processes {
        if process.command.contains("--vm") {
            ram_containers.push(process);
        } else if process.command.contains("--hdd") {
            disk_containers.push(process);
        } else if process.command.contains("--io") {
            io_containers.push(process);
        } else if process.command.contains("stress") {
            cpu_containers.push(process);
        }
    }

    let mut logs = Vec::new();
    let mut deleted_containers = Vec::new();

    manage_category(docker, &mut cpu_containers, "CPU", log_container_id, &mut logs, &mut deleted_containers).await;
    manage_category(docker, &mut ram_containers, "RAM", log_container_id, &mut logs, &mut deleted_containers).await;
    manage_category(docker, &mut io_containers, "I/O", log_container_id, &mut logs, &mut deleted_containers).await;
    manage_category(docker, &mut disk_containers, "Disk", log_container_id, &mut logs, &mut deleted_containers).await;

    println!("\n=== Contenedores por Categoría ===");
    println!("CPU: {:?}", cpu_containers.iter().map(|c| &c.container_id[..12]).collect::<Vec<_>>());
    println!("RAM: {:?}", ram_containers.iter().map(|c| &c.container_id[..12]).collect::<Vec<_>>());
    println!("I/O: {:?}", io_containers.iter().map(|c| &c.container_id[..12]).collect::<Vec<_>>());
    println!("Disk: {:?}", disk_containers.iter().map(|c| &c.container_id[..12]).collect::<Vec<_>>());
    println!("\nEliminados: {:?}", deleted_containers);

    send_logs_to_container(client, logs).await;
}

async fn manage_category(
    docker: &Docker,
    containers: &mut Vec<&ProcessInfo>,
    category: &str,
    log_container_id: &str,
    logs: &mut Vec<LogEntry>,
    deleted: &mut Vec<String>,
) {
    if containers.len() > 1 {
        containers.sort_by(|a, b| a.container_id.cmp(&b.container_id));
        while containers.len() > 1 {
            let old_container = containers.remove(0);
            if old_container.container_id != log_container_id {
                let remove_opts = docker_api::opts::ContainerRemoveOpts::builder()
                    .force(true)
                    .build();
                docker.containers()
                    .get(&old_container.container_id)
                    .remove(&remove_opts)
                    .await
                    .unwrap();
                logs.push(LogEntry {
                    container_name: old_container.name.clone(),
                    container_id: old_container.container_id.clone(),
                    category: category.to_string(),
                    created_at: Utc::now().to_string(),
                    deleted_at: Some(Utc::now().to_string()),
                    cpu_usage_percent: old_container.cpu_usage_percent,         // Agregar consumo de CPU
                    memory_usage_percent: old_container.memory_usage_percent,   // Agregar consumo de memoria
                    disk_usage_mb: old_container.disk_usage_mb,                 // Agregar uso de disco
                    block_io_read_mb: old_container.block_io.read_mb,           // Agregar lectura de block_io
                    block_io_write_mb: old_container.block_io.write_mb,         // Agregar escritura de block_io
                });
                deleted.push(old_container.container_id[..12].to_string());
            }
        }
    }
}

async fn send_logs_to_container(client: &Client, logs: Vec<LogEntry>) {
    let url = "http://localhost:8080/logs";
    if let Err(e) = client.post(url).json(&logs).send().await {
        eprintln!("Error al enviar logs: {}", e);
    } else {
        println!("Logs enviados correctamente");
    }
}

async fn finalize(_docker: &Docker, client: &Client, _log_container_id: &str) {
    let url = "http://localhost:8080/generate_graphs";
    match client.post(url).send().await {
        Ok(_) => println!("Solicitud para generar gráficos enviada correctamente"),
        Err(e) => eprintln!("Error al enviar solicitud para generar gráficos: {}", e),
    }

    match std::process::Command::new("crontab")
        .arg("-r")
        .output()
    {
        Ok(_) => println!("Servicio finalizado. Cronjob eliminado."),
        Err(e) => eprintln!("Error al eliminar cronjob: {}", e),
    }
}