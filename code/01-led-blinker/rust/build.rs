// Check this link for examples: https://doc.rust-lang.org/cargo/reference/build-script-examples.html
use std::env::var;
use std::process::{Command, ExitStatus};

fn main() {
    // List of cargo env var: https://doc.rust-lang.org/cargo/reference/environment-variables.html
    // The "RUSTC_LINKER" variable is defined in the ".cargo/config.toml" file
    let linker = var("RUSTC_LINKER").unwrap();
    let linker_args = vec!["-mcpu=cortex-m4", "-mthumb", "-c", "-o"];
    let out_dir = var("OUT_DIR").unwrap();

    let status: ExitStatus = Command::new(linker)
        .args(linker_args)
        .arg(format!("{}/crt0.o", out_dir))
        .arg("../common/crt0.s")
        .status()
        .unwrap();
    assert!(status.success(), "crt0.s assembly failed");
    println!("cargo::rustc-link-arg={}/crt0.o", out_dir);
}
