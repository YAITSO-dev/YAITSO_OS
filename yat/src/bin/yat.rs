use std::cmp::Ordering;
use std::env;
use std::fs;
use std::path::Path;
use yat::crypto;
use yat::http;
use yat::meta::{valid_token, PkgMeta};
use yat::pkg;

fn usage() {
    eprintln!(
        "yat {} — пакетный менеджер Yaitso OS\n\
         usage: yat <command> [args]\n\
         \x20 install <pkg>...   — установить\n\
         \x20 remove <pkg>...    — удалить\n\
         \x20 upgrade            — обновить всё\n\
         \x20 list               — список установленных\n\
         \x20 search <query>     — поиск в репозитории\n\
         \x20 info <pkg>         — информация\n\
         \x20 update             — обновить индекс\n\
         \x20 verify             — проверить подпись индекса\n\
         \x20 why <pkg>          — кто зависит от пакета\n\
         \x20 graph              — граф зависимостей\n\
         \x20 pin <pkg> <ver>    — закрепить версию\n\
         \x20 rollback           — откатить последнюю транзакцию",
        yat::VERSION
    );
}

fn cmd_install(args: &[String]) -> i32 {
    if args.is_empty() {
        usage();
        return 2;
    }
    let mut rc = 0;
    for name in args {
        if let Err(e) = pkg::install_one(name, None, false) {
            eprintln!("yat: {}: {}", name, e);
            rc = 1;
        }
    }
    rc
}

fn cmd_remove(args: &[String]) -> i32 {
    if args.is_empty() {
        usage();
        return 2;
    }
    let mut rc = 0;
    for name in args {
        if let Err(e) = pkg::remove_package(name) {
            eprintln!("yat: {}: {}", name, e);
            rc = 1;
        }
    }
    rc
}

fn cmd_upgrade(_args: &[String]) -> i32 {
    let list = match pkg::list_installed() {
        Ok(l) => l,
        Err(e) => {
            eprintln!("yat: {}", e);
            return 1;
        }
    };

    let mut rc = 0;
    for m in list {
        let pin = Path::new(yat::INSTALLED_DIR).join(&m.name).join("PIN");
        if pin.exists() {
            continue;
        }
        match pkg::latest_repo_version(&m.name) {
            Ok(Some(latest)) => {
                if pkg::version_cmp(&latest, &m.version) == Ordering::Greater {
                    println!(">>> обновление {}: {} -> {}", m.name, m.version, latest);
                    if let Err(e) = pkg::install_one(&m.name, Some(&latest), true) {
                        eprintln!("yat: {}: {}", m.name, e);
                        rc = 1;
                    }
                }
            }
            Ok(None) => {}
            Err(e) => {
                eprintln!("yat: {}: {}", m.name, e);
                rc = 1;
            }
        }
    }
    rc
}

fn cmd_list(_args: &[String]) -> i32 {
    match pkg::list_installed() {
        Ok(list) => {
            for m in list {
                println!("{:<30} {:<15} {}", m.name, m.version, m.libc);
            }
            0
        }
        Err(e) => {
            eprintln!("yat: {}", e);
            1
        }
    }
}

fn cmd_search(args: &[String]) -> i32 {
    if args.is_empty() {
        usage();
        return 2;
    }
    let repo = Path::new(yat::REPO_DIR);
    if !repo.exists() {
        eprintln!("yat: нет репозитория {}", yat::REPO_DIR);
        return 1;
    }
    let query = &args[0];
    let mut found = false;
    match fs::read_dir(repo) {
        Ok(rd) => {
            let mut names: Vec<String> = rd
                .filter_map(|e| e.ok())
                .filter_map(|e| {
                    let name = e.file_name().to_string_lossy().to_string();
                    name.strip_suffix(".yat").map(|s| s.to_string())
                })
                .filter(|name| name.contains(query))
                .collect();
            names.sort();
            for name in names {
                println!("{}", name);
                found = true;
            }
        }
        Err(e) => {
            eprintln!("yat: {}", e);
            return 1;
        }
    }
    if !found {
        eprintln!("yat: ничего не найдено по запросу {}", query);
    }
    0
}

fn cmd_info(args: &[String]) -> i32 {
    if args.is_empty() {
        usage();
        return 2;
    }
    match PkgMeta::read_installed(&args[0]) {
        Ok(m) => {
            println!("name:    {}", m.name);
            println!("version: {}", m.version);
            println!("arch:    {}", m.arch);
            println!("size:    {}", m.size);
            println!("depends: {}", m.depends.join(" "));
            println!("desc:    {}", m.desc);
            if !m.libc.is_empty() {
                println!("libc:    {}", m.libc);
            }
            0
        }
        Err(_) => {
            let repo = Path::new(yat::REPO_DIR);
            if let Ok(rd) = fs::read_dir(repo) {
                let mut found = false;
                for entry in rd.flatten() {
                    let name = entry.file_name().to_string_lossy().to_string();
                    if name.starts_with(&args[0]) {
                        println!("(repo) {}", name);
                        found = true;
                    }
                }
                if found {
                    return 0;
                }
            }
            eprintln!("yat: пакет {} не найден", args[0]);
            1
        }
    }
}

fn read_repo_url() -> Result<String, yat::error::YatError> {
    let path = Path::new(yat::REPOS_CONF);
    if !path.exists() {
        return Ok("https://repo.yaitso.local".into());
    }
    let content = fs::read_to_string(path)?;
    for line in content.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if let Some((k, v)) = line.split_once('=') {
            let k = k.trim();
            let v = v.trim();
            if k == "local" || k == "default" || k == "url" {
                return Ok(v.to_string());
            }
        }
    }
    Ok("https://repo.yaitso.local".into())
}

fn cmd_update(_args: &[String]) -> i32 {
    let url = match read_repo_url() {
        Ok(u) => u,
        Err(e) => {
            eprintln!("yat: {}", e);
            return 1;
        }
    };

    println!(">>> обновление индекса с {}", url);
    let index_url = format!("{}/index.txt", url);
    let sig_url = format!("{}/index.sig", url);

    let body = match http::fetch(&index_url) {
        Ok(b) => b,
        Err(e) => {
            eprintln!("yat: {}", e);
            return 1;
        }
    };
    let sig = http::fetch(&sig_url).ok();
    let trusted = Path::new(yat::TRUSTED_KEYS);

    if trusted.exists() && pkg::signing_required() {
        match &sig {
            Some(s) if crypto::verify_any(&body, s, trusted).is_ok() => {}
            _ => {
                eprintln!("yat: индекс отклонён: подпись не проходит проверку");
                return 1;
            }
        }
    }

    let repo_dir = Path::new(yat::REPO_DIR);
    if let Err(e) = fs::create_dir_all(repo_dir) {
        eprintln!("yat: {}", e);
        return 1;
    }

    let idx_tmp = repo_dir.join("index.txt.new");
    let sig_tmp = repo_dir.join("index.sig.new");
    if let Err(e) = fs::write(&idx_tmp, &body) {
        eprintln!("yat: {}", e);
        return 1;
    }
    if let Some(s) = &sig {
        if let Err(e) = fs::write(&sig_tmp, s) {
            let _ = fs::remove_file(&idx_tmp);
            eprintln!("yat: {}", e);
            return 1;
        }
    }

    if let Err(e) = fs::rename(&idx_tmp, repo_dir.join("index.txt")) {
        let _ = fs::remove_file(&idx_tmp);
        let _ = fs::remove_file(&sig_tmp);
        eprintln!("yat: {}", e);
        return 1;
    }
    if sig.is_some() {
        if let Err(e) = fs::rename(&sig_tmp, repo_dir.join("index.sig")) {
            eprintln!("yat: {}", e);
            return 1;
        }
    } else {
        let _ = fs::remove_file(repo_dir.join("index.sig"));
    }

    println!(">>> индекс обновлён");
    0
}

fn cmd_verify(_args: &[String]) -> i32 {
    let repo = Path::new(yat::REPO_DIR);
    let idx = repo.join("index.txt");
    let sig = repo.join("index.sig");
    let trusted = Path::new(yat::TRUSTED_KEYS);
    if !idx.exists() || !sig.exists() {
        eprintln!("yat: нет index.txt или index.sig");
        return 1;
    }
    if !trusted.exists() {
        eprintln!("yat: нет доверенных ключей {}", yat::TRUSTED_KEYS);
        return 1;
    }
    match pkg::verify_index(&idx, &sig, trusted) {
        Ok(()) => {
            println!("yat: подпись OK");
            0
        }
        Err(e) => {
            eprintln!("yat: {}", e);
            1
        }
    }
}

fn cmd_why(args: &[String]) -> i32 {
    if args.is_empty() {
        usage();
        return 2;
    }
    let target = &args[0];
    match pkg::list_installed() {
        Ok(list) => {
            println!("пакеты, зависящие от {}:", target);
            for m in list {
                if m.depends.iter().any(|d| d == target) {
                    println!("  {}", m.name);
                }
            }
            0
        }
        Err(e) => {
            eprintln!("yat: {}", e);
            1
        }
    }
}

fn cmd_graph(_args: &[String]) -> i32 {
    match pkg::list_installed() {
        Ok(list) => {
            println!("digraph deps {{");
            for m in list {
                for d in &m.depends {
                    println!("  \"{}\" -> \"{}\";", m.name, d);
                }
            }
            println!("}}");
            0
        }
        Err(e) => {
            eprintln!("yat: {}", e);
            1
        }
    }
}

fn cmd_pin(args: &[String]) -> i32 {
    if args.len() < 2 {
        usage();
        return 2;
    }
    if !valid_token(&args[0], 127) || !valid_token(&args[1], 63) {
        eprintln!("yat: недопустимое имя или версия");
        return 1;
    }
    let inst = Path::new(yat::INSTALLED_DIR).join(&args[0]);
    if !inst.exists() {
        eprintln!("yat: {} не установлен", args[0]);
        return 1;
    }
    match fs::write(inst.join("PIN"), format!("{}\n", args[1])) {
        Ok(()) => {
            println!(">>> {} pinned to {}", args[0], args[1]);
            0
        }
        Err(e) => {
            eprintln!("yat: {}", e);
            1
        }
    }
}

fn cmd_rollback(_args: &[String]) -> i32 {
    match pkg::rollback_last() {
        Ok(()) => 0,
        Err(e) => {
            eprintln!("yat: {}", e);
            1
        }
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        usage();
        std::process::exit(2);
    }

    let cmd = args[1].as_str();
    let rest = &args[2..];

    let rc = match cmd {
        "install" => cmd_install(rest),
        "remove" | "uninstall" => cmd_remove(rest),
        "upgrade" => cmd_upgrade(rest),
        "list" => cmd_list(rest),
        "search" => cmd_search(rest),
        "info" => cmd_info(rest),
        "update" => cmd_update(rest),
        "verify" => cmd_verify(rest),
        "why" => cmd_why(rest),
        "graph" => cmd_graph(rest),
        "pin" => cmd_pin(rest),
        "rollback" => cmd_rollback(rest),
        _ => {
            usage();
            2
        }
    };

    std::process::exit(rc);
}
