use ed25519_dalek::{Signature, Signer, SigningKey, Verifier, VerifyingKey};
use rand::rngs::OsRng;
use sha2::{Digest, Sha256};
use std::fs;
use std::path::Path;

use crate::error::{Result, YatError};

pub fn generate_keypair() -> (SigningKey, VerifyingKey) {
    let mut csprng = OsRng;
    let signing = SigningKey::generate(&mut csprng);
    let verifying = signing.verifying_key();
    (signing, verifying)
}

pub fn sign(msg: &[u8], key: &SigningKey) -> Vec<u8> {
    let sig = key.sign(msg);
    sig.to_bytes().to_vec()
}

pub fn verify(msg: &[u8], sig: &[u8], pk_hex: &str) -> Result<()> {
    let pk_bytes =
        hex::decode(pk_hex.trim()).map_err(|e| YatError::Crypto(format!("pk hex: {}", e)))?;

    if pk_bytes.len() != 32 {
        return Err(YatError::Crypto("pk length != 32".into()));
    }

    let mut pk_arr = [0u8; 32];
    pk_arr.copy_from_slice(&pk_bytes);

    let vk =
        VerifyingKey::from_bytes(&pk_arr).map_err(|e| YatError::Crypto(format!("vk: {}", e)))?;

    if sig.len() != 64 {
        return Err(YatError::Crypto("sig length != 64".into()));
    }

    let mut sig_arr = [0u8; 64];
    sig_arr.copy_from_slice(sig);

    let signature = Signature::from_bytes(&sig_arr);

    vk.verify(msg, &signature)
        .map_err(|_| YatError::SignatureInvalid)
}

pub fn verify_any(msg: &[u8], sig: &[u8], trusted_keys_path: &Path) -> Result<()> {
    let content = fs::read_to_string(trusted_keys_path)?;

    for line in content.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if verify(msg, sig, line).is_ok() {
            return Ok(());
        }
    }

    Err(YatError::SignatureInvalid)
}

pub fn save_key(seed: &[u8; 32], path: &Path) -> Result<()> {
    let hex_str = hex::encode(seed);
    fs::write(path, format!("{}\n", hex_str))?;
    set_perm_600(path)?;
    Ok(())
}

pub fn load_key(path: &Path) -> Result<SigningKey> {
    let content = fs::read_to_string(path)?;
    let trimmed = content.trim();

    let bytes = hex::decode(trimmed).map_err(|e| YatError::Crypto(format!("key hex: {}", e)))?;

    if bytes.len() != 32 {
        return Err(YatError::Crypto("key length != 32".into()));
    }

    let mut arr = [0u8; 32];
    arr.copy_from_slice(&bytes);
    Ok(SigningKey::from_bytes(&arr))
}

pub fn save_pubkey(vk: &VerifyingKey, path: &Path) -> Result<()> {
    let hex_str = hex::encode(vk.to_bytes());
    fs::write(path, format!("{}\n", hex_str))?;
    Ok(())
}

pub fn sha256_hex(data: &[u8]) -> String {
    let mut hasher = Sha256::new();
    hasher.update(data);
    hex::encode(hasher.finalize())
}

#[cfg(unix)]
fn set_perm_600(path: &Path) -> Result<()> {
    use std::os::unix::fs::PermissionsExt;
    let mut perm = fs::metadata(path)?.permissions();
    perm.set_mode(0o600);
    fs::set_permissions(path, perm)?;
    Ok(())
}

#[cfg(not(unix))]
fn set_perm_600(_path: &Path) -> Result<()> {
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sign_verify_roundtrip() {
        let (sk, vk) = generate_keypair();
        let msg = b"hello yaitso";
        let sig = sign(msg, &sk);
        let pk_hex = hex::encode(vk.to_bytes());
        assert!(verify(msg, &sig, &pk_hex).is_ok());
    }

    #[test]
    fn verify_wrong_msg_fails() {
        let (sk, vk) = generate_keypair();
        let sig = sign(b"a", &sk);
        let pk_hex = hex::encode(vk.to_bytes());
        assert!(verify(b"b", &sig, &pk_hex).is_err());
    }

    #[test]
    fn rfc8032_vector_1() {
        let seed_hex = "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60";
        let pk_hex = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
        let expected_sig = "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b";

        let seed_bytes = hex::decode(seed_hex).unwrap();
        let mut seed = [0u8; 32];
        seed.copy_from_slice(&seed_bytes);

        let sk = SigningKey::from_bytes(&seed);
        let vk = sk.verifying_key();

        assert_eq!(hex::encode(vk.to_bytes()), pk_hex);

        let msg: &[u8] = b"";
        let sig = sk.sign(msg);
        assert_eq!(hex::encode(sig.to_bytes()), expected_sig);
    }

    #[test]
    fn rfc8032_vector_2() {
        let seed_hex = "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb";
        let pk_hex = "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c";
        let msg: &[u8] = &[0x72];

        let seed_bytes = hex::decode(seed_hex).unwrap();
        let mut seed = [0u8; 32];
        seed.copy_from_slice(&seed_bytes);

        let sk = SigningKey::from_bytes(&seed);
        let vk = sk.verifying_key();

        assert_eq!(hex::encode(vk.to_bytes()), pk_hex);

        let sig = sk.sign(msg);
        assert!(vk.verify(msg, &sig).is_ok());
    }
}
