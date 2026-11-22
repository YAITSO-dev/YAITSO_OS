use std::env;
use std::path::PathBuf;
use yat::crypto;

fn main() {
    let mut priv_path = PathBuf::from("key.sec");
    let mut pub_path = PathBuf::from("key.pub");

    let args: Vec<String> = env::args().collect();
    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "-s" if i + 1 < args.len() => {
                priv_path = PathBuf::from(&args[i + 1]);
                i += 2;
            }
            "-p" if i + 1 < args.len() => {
                pub_path = PathBuf::from(&args[i + 1]);
                i += 2;
            }
            _ => i += 1,
        }
    }

    let (sk, vk) = crypto::generate_keypair();
    let seed = sk.to_bytes();

    if let Err(e) = crypto::save_key(&seed, &priv_path) {
        eprintln!("yat-keygen: {}", e);
        std::process::exit(1);
    }
    if let Err(e) = crypto::save_pubkey(&vk, &pub_path) {
        eprintln!("yat-keygen: {}", e);
        std::process::exit(1);
    }

    println!("private key: {}", priv_path.display());
    println!("public key:  {}", pub_path.display());
    println!("public hex:  {}", hex::encode(vk.to_bytes()));
}
