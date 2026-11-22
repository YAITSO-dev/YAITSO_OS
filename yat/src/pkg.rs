use crate::crypto;
use crate::error::{Result, YatError};
use crate::meta::{valid_token, valid_version, PkgMeta};
use flate2::read::GzDecoder;
use sha2::{Digest, Sha512};
use std::cmp::Ordering;
use std::collections::HashSet;
use std::fs::{self, File};
use std::io;
use std::path::{Component, Path, PathBuf};
use tar::Archive;
use walkdir::WalkDir;

pub fn safe_join(base: &Path, rel: &Path) -> Result<PathBuf> {
    if rel.is_absolute() {
        return Err(YatError::Invalid(format!(
            "absolute path rejected: {}",
            rel.display()
        )));
    }
    let mut out = base.to_path_buf();
    for comp in rel.components() {
        match comp {
            Component::Normal(c) => out.push(c),
            Component::CurDir => {}
            Component::ParentDir => {
                return Err(YatError::Invalid(format!(
                    "path traversal rejected: {}",
                    rel.display()
                )));
            }
            _ => {
                return Err(YatError::Invalid(format!(
                    "invalid component in {}",
                    rel.display()
                )));
            }
        }
    }
    Ok(out)
}

/// Родительские каталоги не должны быть символьными ссылками: иначе запись
/// файла может уйти за пределы каталога распаковки.
fn check_parent_chain(base: &Path, dest: &Path) -> Result<()> {
    let rel = dest
        .strip_prefix(base)
        .map_err(|_| YatError::Invalid("path outside of stage".into()))?;
    let comps: Vec<Component> = rel.components().collect();
    let mut cur = base.to_path_buf();
    for comp in comps.iter().take(comps.len().saturating_sub(1)) {
        cur.push(comp.as_os_str());
        if let Ok(md) = fs::symlink_metadata(&cur) {
            if md.file_type().is_symlink() {
                return Err(YatError::Invalid(format!(
                    "symlink in extraction path: {}",
                    cur.display()
                )));
            }
        }
    }
    Ok(())
}

fn remove_path(path: &Path) {
    if let Ok(md) = fs::symlink_metadata(path) {
        if md.is_dir() {
            let _ = fs::remove_dir_all(path);
        } else {
            let _ = fs::remove_file(path);
        }
    }
}

fn verify_tarball(tarball: &Path, sig_path: &Path, trusted: &Path) -> Result<()> {
    let data = fs::read(tarball)?;
    let sig = fs::read(sig_path)?;
    crypto::verify_any(&data, &sig, trusted)
}

pub fn verify_index(index: &Path, sig: &Path, trusted: &Path) -> Result<()> {
    let data = fs::read(index)?;
    let sig_bytes = fs::read(sig)?;
    crypto::verify_any(&data, &sig_bytes, trusted)
}

fn index_hash(index: &str, key: &str) -> Option<String> {
    for line in index.lines() {
        let mut it = line.split_whitespace();
        let name = match it.next() {
            Some(n) => n.trim_end_matches(".yat"),
            None => continue,
        };
        if name != key {
            continue;
        }
        if let Some(hash) = it.next() {
            if hash.len() == 128 && hash.chars().all(|c| c.is_ascii_hexdigit()) {
                return Some(hash.to_string());
            }
        }
    }
    None
}

pub fn signing_required() -> bool {
    !matches!(std::env::var("YAT_ALLOW_UNSIGNED"), Ok(v) if v == "1")
}

/// Проверяет отдельную подпись пакета (если она есть) и хеш из подписанного
/// индекса. Хеш проверяется только для индексов нового формата
/// (`<name>-<version> <sha512>`); старые индексы без хешей остаются
/// совместимыми.
fn verify_package(tarball: &Path, name: &str, version: &str) -> Result<()> {
    if !signing_required() {
        return Ok(());
    }

    let data = fs::read(tarball)?;
    let trusted = Path::new(crate::TRUSTED_KEYS);
    let sig_path = tarball.with_extension("yat.sig");

    if sig_path.exists() {
        if !trusted.exists() {
            return Err(YatError::Crypto(format!(
                "подпись есть, но нет {}",
                crate::TRUSTED_KEYS
            )));
        }
        verify_tarball(tarball, &sig_path, trusted)?;
    }

    let index_path = Path::new(crate::REPO_DIR).join("index.txt");
    if index_path.exists() {
        let index = fs::read(&index_path)?;
        let index_sig = Path::new(crate::REPO_DIR).join("index.sig");
        if trusted.exists() {
            if !index_sig.exists() {
                return Err(YatError::SignatureInvalid);
            }
            crypto::verify_any(&index, &fs::read(&index_sig)?, trusted)?;
        }

        let text = String::from_utf8_lossy(&index);
        let key = format!("{}-{}", name, version);
        if let Some(want) = index_hash(&text, &key) {
            let got = hex::encode(Sha512::digest(&data));
            if !got.eq_ignore_ascii_case(&want) {
                return Err(YatError::Invalid(format!(
                    "хеш {} не совпадает с индексом",
                    key
                )));
            }
        }
    } else if signing_required() && trusted.exists() {
        return Err(YatError::Invalid(format!(
            "нет индекса репозитория: {}",
            index_path.display()
        )));
    }

    Ok(())
}

fn extract_stage(tarball: &Path, stage: &Path) -> Result<()> {
    let file = File::open(tarball)?;
    let gz = GzDecoder::new(file);
    let mut archive = Archive::new(gz);

    fs::create_dir_all(stage)?;

    for entry in archive.entries()? {
        let mut entry = entry?;
        let rel = entry.path()?.into_owned();
        let dest = safe_join(stage, &rel)?;
        check_parent_chain(stage, &dest)?;

        let kind = entry.header().entry_type();
        if kind.is_dir() {
            if let Ok(md) = fs::symlink_metadata(&dest) {
                if md.file_type().is_symlink() {
                    fs::remove_file(&dest)?;
                }
            }
            fs::create_dir_all(&dest)?;
        } else if kind.is_symlink() {
            let target = match entry.link_name()? {
                Some(t) => t.into_owned(),
                None => continue,
            };
            if target.is_absolute()
                || target
                    .components()
                    .any(|c| matches!(c, Component::ParentDir))
            {
                continue;
            }
            if let Some(parent) = dest.parent() {
                fs::create_dir_all(parent)?;
            }
            remove_path(&dest);
            #[cfg(unix)]
            std::os::unix::fs::symlink(&target, &dest)?;
        } else if kind.is_file() {
            if let Some(parent) = dest.parent() {
                fs::create_dir_all(parent)?;
            }
            if let Ok(md) = fs::symlink_metadata(&dest) {
                if md.file_type().is_symlink() {
                    fs::remove_file(&dest)?;
                }
            }
            entry.unpack(&dest)?;
        } else if kind.is_hard_link() {
            let target = match entry.link_name()? {
                Some(t) => t.into_owned(),
                None => continue,
            };
            let src = safe_join(stage, &target)?;
            check_parent_chain(stage, &dest)?;
            /* `tar` sometimes stores `./META` as a hardlink to `META`.
             * После нормализации путей src и dest совпадают: файл уже
             * распакован, удалять и копировать его не нужно. */
            if src == dest {
                continue;
            }
            if let Some(parent) = dest.parent() {
                fs::create_dir_all(parent)?;
            }
            remove_path(&dest);
            if fs::hard_link(&src, &dest).is_err() {
                fs::copy(&src, &dest)?;
            }
        }
    }
    Ok(())
}

#[derive(Debug)]
struct TxnEntry {
    path: PathBuf,
    backup: Option<PathBuf>,
    backup_link: Option<PathBuf>,
    /// false для файлов, удалённых при обновлении: в `.FILES` они не попадают.
    keep: bool,
}

fn copy_dir_all(src: &Path, dst: &Path) -> Result<()> {
    fs::create_dir_all(dst)?;
    for entry in fs::read_dir(src)? {
        let entry = entry?;
        let to = dst.join(entry.file_name());
        let ty = entry.file_type()?;
        if ty.is_dir() {
            copy_dir_all(&entry.path(), &to)?;
        } else if ty.is_symlink() {
            #[cfg(unix)]
            std::os::unix::fs::symlink(fs::read_link(entry.path())?, &to)?;
        } else {
            fs::copy(entry.path(), &to)?;
        }
    }
    Ok(())
}

#[derive(Debug)]
pub struct Transaction {
    pub id: String,
    pub dir: PathBuf,
    entries: Vec<TxnEntry>,
    /// Каталог установленного пакета, который затронула транзакция.
    installed_dir: Option<PathBuf>,
}

impl Transaction {
    pub fn new() -> Result<Self> {
        let nanos = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or(0);
        let id = format!("{}-{}", std::process::id(), nanos);
        let dir = Path::new(crate::DB_DIR).join("txn").join(&id);
        fs::create_dir_all(&dir)?;
        Ok(Transaction {
            id,
            dir,
            entries: Vec::new(),
            installed_dir: None,
        })
    }

    fn backup_existing(&self, dest: &Path) -> Result<(Option<PathBuf>, Option<PathBuf>)> {
        let md = match fs::symlink_metadata(dest) {
            Ok(md) => md,
            Err(e) if e.kind() == io::ErrorKind::NotFound => {
                return Ok((None, None));
            }
            Err(e) => return Err(e.into()),
        };

        if md.file_type().is_dir() {
            return Err(YatError::Invalid(format!(
                "destination is a directory: {}",
                dest.display()
            )));
        }

        if md.file_type().is_symlink() {
            return Ok((None, Some(fs::read_link(dest)?)));
        }

        let rel = dest.strip_prefix("/").unwrap_or(dest);
        let backup = self.dir.join("backup").join(rel);
        if let Some(parent) = backup.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::copy(dest, &backup)?;
        Ok((Some(backup), None))
    }

    fn push_entry(
        &mut self,
        dest: &Path,
        backup: Option<PathBuf>,
        backup_link: Option<PathBuf>,
        keep: bool,
    ) {
        self.entries.push(TxnEntry {
            path: dest.to_path_buf(),
            backup,
            backup_link,
            keep,
        });
    }

    /// Удаляет файл старой версии пакета, сохраняя его для отката.
    pub fn remove_existing(&mut self, dest: &Path) -> Result<()> {
        let (backup, backup_link) = self.backup_existing(dest)?;
        remove_path(dest);
        self.push_entry(dest, backup, backup_link, false);
        Ok(())
    }

    pub fn install_file(&mut self, src: &Path, dest: &Path) -> Result<()> {
        if !dest.is_absolute() {
            return Err(YatError::Invalid(format!(
                "dest not absolute: {}",
                dest.display()
            )));
        }
        let (backup, backup_link) = self.backup_existing(dest)?;
        if backup.is_some() || backup_link.is_some() {
            remove_path(dest);
        }

        if let Some(p) = dest.parent() {
            fs::create_dir_all(p)?;
        }
        fs::copy(src, dest)?;

        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let mode = fs::metadata(src)?.permissions().mode();
            fs::set_permissions(dest, fs::Permissions::from_mode(mode))?;
        }

        self.push_entry(dest, backup, backup_link, true);
        Ok(())
    }

    pub fn install_symlink(&mut self, src: &Path, dest: &Path) -> Result<()> {
        if !dest.is_absolute() {
            return Err(YatError::Invalid(format!(
                "dest not absolute: {}",
                dest.display()
            )));
        }
        let target = fs::read_link(src)?;
        let (backup, backup_link) = self.backup_existing(dest)?;
        if backup.is_some() || backup_link.is_some() {
            remove_path(dest);
        }
        if let Some(p) = dest.parent() {
            fs::create_dir_all(p)?;
        }
        #[cfg(unix)]
        std::os::unix::fs::symlink(&target, dest)?;
        self.push_entry(dest, backup, backup_link, true);
        Ok(())
    }

    pub fn commit(&mut self, install_dir: &Path, meta_src: &Path) -> Result<()> {
        /* При обновлении старые META/.FILES/PIN нужно вернуть при откате,
         * иначе пакет после rollback останется «неустановленным». */
        if install_dir.exists() {
            let backup = self.dir.join("installed_backup");
            copy_dir_all(install_dir, &backup)?;
        }
        self.installed_dir = Some(install_dir.to_path_buf());

        fs::create_dir_all(install_dir)?;
        fs::copy(meta_src, install_dir.join("META"))?;

        let mut list = String::new();
        let mut current = String::new();
        let mut links = String::new();
        for e in &self.entries {
            list.push_str(&e.path.to_string_lossy());
            list.push('\n');
            if e.keep {
                current.push_str(&e.path.to_string_lossy());
                current.push('\n');
            }
            if let Some(target) = &e.backup_link {
                links.push_str(&e.path.to_string_lossy());
                links.push('\t');
                links.push_str(&target.to_string_lossy());
                links.push('\n');
            }
        }
        fs::write(install_dir.join(".FILES"), &current)?;
        fs::write(self.dir.join("files.list"), &list)?;
        fs::write(self.dir.join("links.list"), &links)?;
        fs::write(
            self.dir.join("installed_dir"),
            install_dir.to_string_lossy().as_bytes(),
        )?;
        Ok(())
    }

    pub fn rollback(self) -> Result<()> {
        for e in self.entries.iter().rev() {
            remove_path(&e.path);
            if let Some(b) = &e.backup {
                if let Some(p) = e.path.parent() {
                    let _ = fs::create_dir_all(p);
                }
                let _ = fs::copy(b, &e.path);
            } else if let Some(target) = &e.backup_link {
                if let Some(p) = e.path.parent() {
                    let _ = fs::create_dir_all(p);
                }
                #[cfg(unix)]
                let _ = std::os::unix::fs::symlink(target, &e.path);
            }
        }
        if let Some(dir) = &self.installed_dir {
            let backup = self.dir.join("installed_backup");
            let _ = fs::remove_dir_all(dir);
            if backup.exists() {
                let _ = copy_dir_all(&backup, dir);
            }
        }
        let _ = fs::remove_dir_all(&self.dir);
        Ok(())
    }
}

fn version_parts(v: &str) -> Vec<String> {
    let mut parts = Vec::new();
    let mut cur = String::new();
    let mut numeric: Option<bool> = None;
    for c in v.chars() {
        let n = c.is_ascii_digit();
        if let Some(prev) = numeric {
            if prev != n {
                parts.push(std::mem::take(&mut cur));
            }
        }
        numeric = Some(n);
        cur.push(c);
    }
    if !cur.is_empty() {
        parts.push(cur);
    }
    parts
}

/// Числовое сравнение версий: 0.10 > 0.9, 1.2.3 == 1.2.3.
pub fn version_cmp(a: &str, b: &str) -> Ordering {
    let pa = version_parts(a);
    let pb = version_parts(b);
    let n = pa.len().max(pb.len());
    for i in 0..n {
        let x = match pa.get(i) {
            Some(x) => x.as_str(),
            None => return Ordering::Less,
        };
        let y = match pb.get(i) {
            Some(y) => y.as_str(),
            None => return Ordering::Greater,
        };
        let ord = if x.chars().all(|c| c.is_ascii_digit()) && y.chars().all(|c| c.is_ascii_digit())
        {
            let xs = x.trim_start_matches('0');
            let ys = y.trim_start_matches('0');
            xs.len().cmp(&ys.len()).then_with(|| xs.cmp(ys))
        } else {
            x.cmp(y)
        };
        if ord != Ordering::Equal {
            return ord;
        }
    }
    Ordering::Equal
}

fn scan_repo(name: &str) -> Result<Option<(String, PathBuf)>> {
    if !valid_token(name, 127) {
        return Err(YatError::Invalid(format!("invalid package name: {}", name)));
    }
    let repo = Path::new(crate::REPO_DIR);
    if !repo.exists() {
        return Err(YatError::NotFound(format!(
            "repo {} missing",
            crate::REPO_DIR
        )));
    }

    let mut best: Option<(String, PathBuf)> = None;
    let prefix = format!("{}-", name);
    for entry in fs::read_dir(repo)? {
        let entry = entry?;
        let fname = entry.file_name().to_string_lossy().to_string();
        if !fname.ends_with(".yat") {
            continue;
        }
        let stem = &fname[..fname.len() - 4];
        let rest = match stem.strip_prefix(&prefix) {
            Some(r) => r,
            None => continue,
        };
        if rest.is_empty() || rest.contains('/') || !valid_version(rest, 63) {
            continue;
        }
        let cand = rest.to_string();
        let replace = match &best {
            Some((cur, _)) => version_cmp(&cand, cur) == Ordering::Greater,
            None => true,
        };
        if replace {
            best = Some((cand, entry.path()));
        }
    }
    Ok(best)
}

pub fn latest_repo_version(name: &str) -> Result<Option<String>> {
    Ok(scan_repo(name)?.map(|(v, _)| v))
}

pub fn resolve_package_path(name: &str, version: Option<&str>) -> Result<PathBuf> {
    let repo = Path::new(crate::REPO_DIR);
    if !valid_token(name, 127) {
        return Err(YatError::Invalid(format!("invalid package name: {}", name)));
    }
    if let Some(v) = version {
        if !valid_version(v, 63) {
            return Err(YatError::Invalid(format!("invalid package version: {}", v)));
        }
        let p = repo.join(format!("{}-{}.yat", name, v));
        if p.exists() {
            return Ok(p);
        }
        return Err(YatError::NotFound(format!("{}-{}", name, v)));
    }

    scan_repo(name)?
        .map(|(_, p)| p)
        .ok_or_else(|| YatError::NotFound(name.to_string()))
}

fn install_one_inner(
    name: &str,
    version: Option<&str>,
    upgrade: bool,
    seen: &mut HashSet<String>,
) -> Result<()> {
    if !valid_token(name, 127) {
        return Err(YatError::Invalid(format!("invalid package name: {}", name)));
    }
    if let Some(v) = version {
        if !valid_version(v, 63) {
            return Err(YatError::Invalid(format!("invalid package version: {}", v)));
        }
    }

    // Защита от циклов в зависимостях (A -> B -> A).
    if !seen.insert(name.to_string()) {
        return Ok(());
    }

    let tarball = resolve_package_path(name, version)?;
    let work = TempDir::new()?;
    let stage = work.path().join("stage");
    fs::create_dir_all(&stage)?;

    extract_stage(&tarball, &stage)?;

    let meta = PkgMeta::read_from_dir(&stage)?;
    if meta.name != name {
        return Err(YatError::Invalid(format!(
            "в пакете {} лежит META для {}",
            name, meta.name
        )));
    }

    verify_package(&tarball, &meta.name, &meta.version)?;

    let was_installed = PkgMeta::is_installed(&meta.name);
    if !upgrade && was_installed {
        return Err(YatError::AlreadyInstalled(meta.name));
    }

    for dep in &meta.depends {
        if !PkgMeta::is_installed(dep) {
            install_one_inner(dep, None, false, seen)?;
        }
    }

    if meta.libc == "glibc" && !PkgMeta::is_installed("glibc") {
        install_one_inner("glibc", None, false, seen)?;
    }

    let mut txn = Transaction::new()?;
    let inst_dir = Path::new(crate::INSTALLED_DIR).join(&meta.name);

    let result = (|| -> Result<()> {
        let mut current_paths: HashSet<PathBuf> = HashSet::new();
        for entry in WalkDir::new(&stage).min_depth(1) {
            let entry = entry.map_err(|e| YatError::Pkg(e.to_string()))?;
            let rel = entry
                .path()
                .strip_prefix(&stage)
                .map_err(|e| YatError::Pkg(e.to_string()))?;
            if rel == Path::new("META") || rel == Path::new(".FILES") {
                continue;
            }
            let ft = entry.file_type();
            if ft.is_dir() {
                continue;
            }
            let dest = Path::new("/").join(rel);
            current_paths.insert(dest.clone());
            if ft.is_symlink() {
                txn.install_symlink(entry.path(), &dest)?;
            } else if ft.is_file() {
                txn.install_file(entry.path(), &dest)?;
            }
        }

        /* Файлы старой версии, которых нет в новой, удаляем; их копии
         * остаются в транзакции и вернутся при `yat rollback`. */
        if upgrade && was_installed {
            let old_files = inst_dir.join(".FILES");
            if let Ok(text) = fs::read_to_string(&old_files) {
                for line in text.lines() {
                    let line = line.trim();
                    if line.is_empty() {
                        continue;
                    }
                    let raw = Path::new(line);
                    if raw.components().any(|c| matches!(c, Component::ParentDir)) {
                        continue;
                    }
                    let p = if raw.is_absolute() {
                        raw.to_path_buf()
                    } else {
                        Path::new("/").join(raw)
                    };
                    if p == inst_dir || p.starts_with(crate::INSTALLED_DIR) || p == Path::new("/") {
                        continue;
                    }
                    if !current_paths.contains(&p) {
                        txn.remove_existing(&p)?;
                    }
                }
            }
        }

        txn.commit(&inst_dir, &stage.join("META"))?;
        Ok(())
    })();

    match result {
        Ok(()) => {
            let _ = log_history(
                if upgrade { "upgrade" } else { "install" },
                &meta.name,
                &meta.version,
            );
            println!(">>> {} {} установлен", meta.name, meta.version);
            Ok(())
        }
        Err(e) => {
            let _ = txn.rollback();
            Err(e)
        }
    }
}

pub fn install_one(name: &str, version: Option<&str>, upgrade: bool) -> Result<()> {
    let mut seen = HashSet::new();
    install_one_inner(name, version, upgrade, &mut seen)
}

pub fn remove_package(name: &str) -> Result<()> {
    if !valid_token(name, 127) {
        return Err(YatError::Invalid(format!("invalid package name: {}", name)));
    }
    let inst = Path::new(crate::INSTALLED_DIR).join(name);
    if !inst.exists() {
        return Err(YatError::NotFound(name.to_string()));
    }

    let files = inst.join(".FILES");
    if files.exists() {
        let list = fs::read_to_string(&files)?;
        for line in list.lines() {
            let line = line.trim();
            if line.is_empty() {
                continue;
            }
            let raw = Path::new(line);
            if raw.components().any(|c| matches!(c, Component::ParentDir)) {
                continue;
            }
            let p = if raw.is_absolute() {
                raw.to_path_buf()
            } else {
                Path::new("/").join(raw)
            };
            if p == inst || p == Path::new("/") {
                continue;
            }
            let _ = fs::remove_file(&p);
        }
    }
    fs::remove_dir_all(&inst)?;
    let _ = log_history("remove", name, "");
    println!(">>> {} удалён", name);
    Ok(())
}

pub fn list_installed() -> Result<Vec<PkgMeta>> {
    let base = Path::new(crate::INSTALLED_DIR);
    if !base.exists() {
        return Ok(Vec::new());
    }
    let mut out = Vec::new();
    for entry in fs::read_dir(base)? {
        let entry = entry?;
        if !entry.file_type()?.is_dir() {
            continue;
        }
        if let Ok(m) = PkgMeta::read_from_dir(&entry.path()) {
            out.push(m);
        }
    }
    out.sort_by(|a, b| a.name.cmp(&b.name));
    Ok(out)
}

pub fn rollback_last() -> Result<()> {
    let base = Path::new(crate::DB_DIR).join("txn");
    if !base.exists() {
        return Err(YatError::Invalid("no transactions".into()));
    }
    let mut dirs: Vec<(std::time::SystemTime, PathBuf)> = Vec::new();
    for e in fs::read_dir(base)? {
        let e = e?;
        if !e.file_type()?.is_dir() {
            continue;
        }
        let mtime = e
            .metadata()
            .and_then(|m| m.modified())
            .unwrap_or(std::time::UNIX_EPOCH);
        dirs.push((mtime, e.path()));
    }
    dirs.sort_by_key(|a| a.0);
    let last = dirs
        .last()
        .map(|(_, p)| p)
        .ok_or_else(|| YatError::Invalid("no transactions".into()))?;

    let files = last.join("files.list");
    if !files.exists() {
        return Err(YatError::Invalid("incomplete transaction".into()));
    }
    let installed_dir = last.join("installed_dir");
    let list = fs::read_to_string(&files)?;
    let backup_root = last.join("backup");

    for line in list.lines().rev() {
        let line = line.trim();
        if line.is_empty() {
            continue;
        }
        let target = Path::new(line);
        let rel = target.strip_prefix("/").unwrap_or(target);
        let backup = backup_root.join(rel);
        remove_path(target);
        if backup.exists() {
            if let Some(p) = target.parent() {
                let _ = fs::create_dir_all(p);
            }
            fs::copy(&backup, target)?;
        }
    }

    let links = last.join("links.list");
    if links.exists() {
        let text = fs::read_to_string(&links)?;
        for line in text.lines() {
            let (path, target) = match line.split_once('\t') {
                Some(x) => x,
                None => continue,
            };
            let p = Path::new(path);
            remove_path(p);
            if let Some(parent) = p.parent() {
                let _ = fs::create_dir_all(parent);
            }
            #[cfg(unix)]
            let _ = std::os::unix::fs::symlink(Path::new(target), p);
        }
    }

    if installed_dir.exists() {
        let d = fs::read_to_string(&installed_dir)?;
        let d = Path::new(d.trim());
        let backup = last.join("installed_backup");
        let _ = fs::remove_dir_all(d);
        if backup.exists() {
            copy_dir_all(&backup, d)?;
        }
    }

    let _ = fs::remove_dir_all(last);
    println!(">>> rollback завершён");
    Ok(())
}

fn log_history(action: &str, name: &str, version: &str) -> Result<()> {
    use std::io::Write;
    let path = Path::new(crate::HISTORY_LOG);
    if let Some(p) = path.parent() {
        fs::create_dir_all(p)?;
    }
    let mut f = fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)?;
    let ts = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    writeln!(f, "{} {} {} {}", ts, action, name, version)?;
    Ok(())
}

struct TempDir {
    path: PathBuf,
}

impl TempDir {
    fn new() -> Result<Self> {
        let base = Path::new(crate::CACHE_DIR);
        fs::create_dir_all(base)?;
        let nanos = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or(0);
        for i in 0..100 {
            let p = base.join(format!("work-{}-{}-{}", std::process::id(), nanos, i));
            if fs::create_dir(&p).is_ok() {
                return Ok(TempDir { path: p });
            }
        }
        Err(YatError::Io(io::Error::new(
            io::ErrorKind::AlreadyExists,
            "no temp dir",
        )))
    }
    fn path(&self) -> &Path {
        &self.path
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.path);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn safe_join_blocks_parent() {
        let base = Path::new("/tmp/base");
        assert!(safe_join(base, Path::new("../etc/passwd")).is_err());
        assert!(safe_join(base, Path::new("/etc/passwd")).is_err());
        assert!(safe_join(base, Path::new("usr/bin/foo")).is_ok());
        assert!(safe_join(base, Path::new("./usr/./bin/foo")).is_ok());
    }

    #[test]
    fn version_order() {
        assert_eq!(version_cmp("0.10", "0.9"), Ordering::Greater);
        assert_eq!(version_cmp("1.2.3", "1.2.3"), Ordering::Equal);
        assert_eq!(version_cmp("1.02", "1.2"), Ordering::Equal);
        assert_eq!(version_cmp("0.3.31", "0.3.2"), Ordering::Greater);
    }

    /// `tar` может сохранить `./META` как hardlink на `META`; после
    /// нормализации путей это один и тот же файл, и распаковка не должна
    /// удалять его и падать.
    #[test]
    fn extract_self_referencing_hardlink() {
        use flate2::write::GzEncoder;
        use flate2::Compression;
        use tar::{Builder, EntryType, Header};

        let base = std::env::temp_dir().join(format!("yat-hardlink-{}", std::process::id()));
        let _ = fs::remove_dir_all(&base);
        fs::create_dir_all(&base).unwrap();
        let tarball = base.join("pkg.yat");

        {
            let file = File::create(&tarball).unwrap();
            let encoder = GzEncoder::new(file, Compression::default());
            let mut builder = Builder::new(encoder);

            let data = b"name: demo\nversion: 1.0\n";
            let mut header = Header::new_gnu();
            header.set_size(data.len() as u64);
            header.set_mode(0o644);
            header.set_entry_type(EntryType::Regular);
            header.set_cksum();
            builder.append_data(&mut header, "META", &data[..]).unwrap();

            let mut link = Header::new_gnu();
            link.set_size(0);
            link.set_mode(0o644);
            link.set_entry_type(EntryType::Link);
            link.set_link_name("META").unwrap();
            link.set_cksum();
            builder
                .append_data(&mut link, "./META", io::empty())
                .unwrap();

            builder.into_inner().unwrap().finish().unwrap();
        }

        let stage = base.join("stage");
        extract_stage(&tarball, &stage).unwrap();
        assert_eq!(
            fs::read_to_string(stage.join("META")).unwrap(),
            "name: demo\nversion: 1.0\n"
        );

        let _ = fs::remove_dir_all(&base);
    }
}
