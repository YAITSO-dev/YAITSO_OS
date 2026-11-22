pub mod crypto;
pub mod error;
pub mod http;
pub mod meta;
pub mod pkg;

pub const VERSION: &str = "0.3.31";

pub const REPO_DIR: &str = "/var/lib/yat/repo";
pub const INSTALLED_DIR: &str = "/var/lib/yat/installed";
pub const CACHE_DIR: &str = "/var/cache/yat";
pub const DB_DIR: &str = "/var/lib/yat";
pub const GLIBC_APPS: &str = "/var/lib/yat/glibc-apps.list";
pub const TRUSTED_KEYS: &str = "/etc/yat/trusted.keys";
pub const REPOS_CONF: &str = "/etc/yat/repos.conf";
pub const HISTORY_LOG: &str = "/var/log/yat/history.log";
