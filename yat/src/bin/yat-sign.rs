use std::env;
use std::fs;
use std::path::PathBuf;
use yat::crypto;

fn main() {
    let mut key_path = PathBuf::from("key.sec");
    let mut in_path: Option<PathBuf> = None;
    let mut out_path: Option<PathBuf> = None;

    let args: Vec<String> = env::args().collect();
    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "-k" if i + 1 < args.len() => {
                key_path = PathBuf::from(&args[i + 1]);
                i += 2;
            }
            _ => {
                if in_path.is_none() {
                    in_path = Some(PathBuf::from(&args[i]));
                } else if out_path.is_none() {
                    out_path = Some(PathBuf::from(&args[i]));
                }
                i += 1;
            }
        }
    }

    let in_path = match in_path {
        Some(p) => p,
        None => {
            eprintln!("usage: yat-sign [-k key.sec] <file> [out.sig]");
            std::process::exit(2);
        }
    };
    let out_path = out_path.unwrap_or_else(|| {
        let mut p = in_path.clone();
        let mut name = p.file_name().unwrap_or_default().to_os_string();
        name.push(".sig");
        p.set_file_name(name);
        p
    });

    let sk = match crypto::load_key(&key_path) {
        Ok(k) => k,
        Err(e) => {
            eprintln!("yat-sign: {}", e);
            std::process::exit(1);
        }
    };

    let data = match fs::read(&in_path) {
        Ok(d) => d,
        Err(e) => {
            eprintln!("yat-sign: {}: {}", in_path.display(), e);
            std::process::exit(1);
        }
    };

    let sig = crypto::sign(&data, &sk);
    if let Err(e) = fs::write(&out_path, &sig) {
        eprintln!("yat-sign: {}: {}", out_path.display(), e);
        std::process::exit(1);
    }

    println!("подписано: {} -> {}", in_path.display(), out_path.display());
}
