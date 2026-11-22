use std::fmt;
use std::io;

#[derive(Debug)]
pub enum YatError {
    Io(io::Error),
    Http(String),
    Crypto(String),
    Meta(String),
    Pkg(String),
    NotFound(String),
    Invalid(String),
    AlreadyInstalled(String),
    MissingDependency(String),
    SignatureInvalid,
    Archive(String),
}

pub type Result<T> = std::result::Result<T, YatError>;

impl fmt::Display for YatError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            YatError::Io(e) => write!(f, "io: {}", e),
            YatError::Http(s) => write!(f, "http: {}", s),
            YatError::Crypto(s) => write!(f, "crypto: {}", s),
            YatError::Meta(s) => write!(f, "meta: {}", s),
            YatError::Pkg(s) => write!(f, "pkg: {}", s),
            YatError::NotFound(s) => write!(f, "not found: {}", s),
            YatError::Invalid(s) => write!(f, "invalid: {}", s),
            YatError::AlreadyInstalled(s) => write!(f, "already installed: {}", s),
            YatError::MissingDependency(s) => write!(f, "missing dependency: {}", s),
            YatError::SignatureInvalid => write!(f, "signature invalid"),
            YatError::Archive(s) => write!(f, "archive: {}", s),
        }
    }
}

impl std::error::Error for YatError {}

impl From<io::Error> for YatError {
    fn from(e: io::Error) -> Self {
        YatError::Io(e)
    }
}

impl From<reqwest::Error> for YatError {
    fn from(e: reqwest::Error) -> Self {
        YatError::Http(e.to_string())
    }
}
