use crate::error::{Result, YatError};
use reqwest::blocking::Client;
use std::time::Duration;

fn client(timeout_secs: u64) -> Result<Client> {
    Client::builder()
        .timeout(Duration::from_secs(timeout_secs))
        .connect_timeout(Duration::from_secs(10))
        .redirect(reqwest::redirect::Policy::limited(5))
        .user_agent(format!("yat/{}", crate::VERSION))
        .build()
        .map_err(|e| YatError::Http(e.to_string()))
}

pub fn fetch(url: &str) -> Result<Vec<u8>> {
    let c = client(120)?;
    let resp = c.get(url).send()?;
    let status = resp.status();
    if !status.is_success() {
        return Err(YatError::Http(format!("HTTP {}: {}", status, url)));
    }
    let body = resp.bytes()?;
    Ok(body.to_vec())
}

pub fn fetch_text(url: &str) -> Result<String> {
    let bytes = fetch(url)?;
    String::from_utf8(bytes).map_err(|e| YatError::Http(format!("utf8: {}", e)))
}

pub fn head(url: &str) -> Result<u16> {
    let c = client(20)?;
    let resp = c.head(url).send()?;
    Ok(resp.status().as_u16())
}
