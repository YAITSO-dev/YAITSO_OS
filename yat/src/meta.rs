use crate::error::{Result, YatError};
use std::fs;
use std::path::Path;

#[derive(Debug, Clone, Default)]
pub struct PkgMeta {
    pub name: String,
    pub version: String,
    pub arch: String,
    pub size: u64,
    pub depends: Vec<String>,
    pub desc: String,
    pub libc: String,
}

/// Имя пакета, версия зависимости или имя файла: только безопасные символы.
pub fn valid_token(s: &str, max_len: usize) -> bool {
    if s.is_empty() || s.len() > max_len || s.contains("..") {
        return false;
    }
    let mut chars = s.chars();
    let first = match chars.next() {
        Some(c) => c,
        None => return false,
    };
    if !first.is_ascii_alphanumeric() {
        return false;
    }
    s.chars()
        .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '+' | '-'))
}

/// Версия должна начинаться с цифры: это не даёт принять пакет
/// `base-devel-1.0` за версию пакета `base`.
pub fn valid_version(s: &str, max_len: usize) -> bool {
    valid_token(s, max_len) && s.chars().next().is_some_and(|c| c.is_ascii_digit())
}

impl PkgMeta {
    pub fn parse(content: &str) -> Result<Self> {
        let mut m = PkgMeta::default();
        for line in content.lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let (k, v) = match line.split_once(':') {
                Some(x) => x,
                None => continue,
            };
            let k = k.trim();
            let v = v.trim();
            match k {
                "name" => m.name = v.to_string(),
                "version" => m.version = v.to_string(),
                "arch" => m.arch = v.to_string(),
                "size" => m.size = v.parse().unwrap_or(0),
                "depends" => m.depends = v.split_whitespace().map(String::from).collect(),
                "desc" => m.desc = v.to_string(),
                "libc" => m.libc = v.to_string(),
                _ => {}
            }
        }
        if m.name.is_empty() {
            return Err(YatError::Meta("no name in META".into()));
        }
        if m.version.is_empty() {
            return Err(YatError::Meta("no version in META".into()));
        }
        if !valid_token(&m.name, 127) {
            return Err(YatError::Meta(format!(
                "invalid package name in META: {}",
                m.name
            )));
        }
        if !valid_version(&m.version, 63) {
            return Err(YatError::Meta(format!(
                "invalid package version in META: {}",
                m.version
            )));
        }
        for dep in &m.depends {
            if !valid_token(dep, 127) {
                return Err(YatError::Meta(format!(
                    "invalid dependency in META: {}",
                    dep
                )));
            }
        }
        Ok(m)
    }

    pub fn read_from_dir(dir: &Path) -> Result<Self> {
        let path = dir.join("META");
        let content = fs::read_to_string(&path)
            .map_err(|e| YatError::Meta(format!("{}: {}", path.display(), e)))?;
        Self::parse(&content)
    }

    pub fn read_installed(name: &str) -> Result<Self> {
        if !valid_token(name, 127) {
            return Err(YatError::Invalid(format!("invalid package name: {}", name)));
        }
        let path = Path::new(crate::INSTALLED_DIR).join(name).join("META");
        let content =
            fs::read_to_string(&path).map_err(|_| YatError::NotFound(name.to_string()))?;
        Self::parse(&content)
    }

    pub fn is_installed(name: &str) -> bool {
        valid_token(name, 127)
            && Path::new(crate::INSTALLED_DIR)
                .join(name)
                .join("META")
                .exists()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn token_rejects_unsafe_names() {
        assert!(valid_token("base", 127));
        assert!(valid_token("libx-1.2+git", 127));
        assert!(!valid_token("", 127));
        assert!(!valid_token("..", 127));
        assert!(!valid_token("../etc", 127));
        assert!(!valid_token("a/b", 127));
        assert!(!valid_token("-x", 127));
        assert!(!valid_token("x".repeat(128).as_str(), 127));
    }

    #[test]
    fn version_must_start_with_digit() {
        assert!(valid_version("0.3.31", 63));
        assert!(valid_version("1.0-rc1", 63));
        assert!(!valid_version("v1.0", 63));
        assert!(!valid_version("devel", 63));
    }
}
