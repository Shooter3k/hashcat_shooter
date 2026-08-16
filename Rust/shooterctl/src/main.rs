use std::collections::{BTreeMap, HashSet, VecDeque};
use std::env;
use std::fmt;
use std::fs::{self, File, OpenOptions};
use std::io::{self, BufRead, BufReader, BufWriter, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::process::{self, Command, ExitCode, Stdio};
use std::sync::{Arc, Condvar, Mutex};
use std::thread;
use std::time::{Instant, SystemTime, UNIX_EPOCH};

type AppResult<T> = Result<T, String>;

// Rust's standard print macros panic on a closed downstream pipe. Command-line
// tools are routinely piped into head/Select-Object, so treat that condition as
// a successful early consumer exit and keep every other stdout error explicit.
fn write_stdout(arguments: fmt::Arguments<'_>, newline: bool) {
    let stdout = io::stdout();
    let mut output = stdout.lock();
    let result = output.write_fmt(arguments).and_then(|()| {
        if newline {
            output.write_all(b"\n")
        } else {
            Ok(())
        }
    });

    if let Err(error) = result {
        if error.kind() == io::ErrorKind::BrokenPipe {
            process::exit(0);
        }
        eprintln!("shooterctl: cannot write standard output: {error}");
        process::exit(1);
    }
}

macro_rules! print {
    ($($argument:tt)*) => {
        write_stdout(format_args!($($argument)*), false)
    };
}

macro_rules! println {
    () => {
        write_stdout(format_args!(""), true)
    };
    ($($argument:tt)*) => {
        write_stdout(format_args!($($argument)*), true)
    };
}

#[derive(Clone, Debug, Default)]
struct Manifest {
    mode: String,
    attack_mode: u32,
    hashes: String,
    wordlists: Vec<String>,
    rules: Vec<String>,
    masks: Vec<String>,
    output: Option<String>,
    potfile: Option<String>,
    total_work: Option<u64>,
    extra: Vec<String>,
}

#[derive(Clone, Debug)]
struct Chunk {
    start: u64,
    count: u64,
    attempts: u32,
}

#[derive(Debug, Default)]
struct FleetState {
    queue: VecDeque<Chunk>,
    active: usize,
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(message) => {
            eprintln!("shooterctl: {message}");
            ExitCode::from(1)
        }
    }
}

fn run() -> AppResult<()> {
    let args: Vec<String> = env::args().skip(1).collect();

    if args.is_empty() {
        print_help();
        return Ok(());
    }

    match args[0].as_str() {
        "--help" | "-h" | "help" => print_help(),
        "--version" | "-V" => println!("shooterctl {}", env!("CARGO_PKG_VERSION")),
        "doctor" | "--doctor" => cmd_doctor(&args[1..])?,
        "support-bundle" => cmd_support_bundle(&args[1..])?,
        "index" => cmd_index(&args[1..])?,
        "rule-report" | "--rule-report" => cmd_rule_report(&args[1..])?,
        "manifest" => cmd_manifest(&args[1..])?,
        "plan" => cmd_plan(&args[1..], false)?,
        "run" => cmd_plan(&args[1..], true)?,
        "stream" => cmd_stream(&args[1..])?,
        "pipeline" => cmd_pipeline(&args[1..])?,
        "fleet" => cmd_fleet(&args[1..])?,
        "mode" => cmd_mode(&args[1..])?,
        other => return Err(format!("unknown command '{other}'; run shooterctl --help")),
    }

    Ok(())
}

fn print_help() {
    println!(
        r#"Shooter Hashcat companion

Usage:
  shooterctl doctor|--doctor [--hashcat PATH] [--json] [--support-bundle DIR]
  shooterctl support-bundle [DIR] [--hashcat PATH]
  shooterctl index [--stride N] [--output FILE] FILE...
  shooterctl rule-report|--rule-report [--json] [--skip N] [--limit N] FILE...
  shooterctl manifest create FILE --mode MODE --hashes FILE [options]
  shooterctl manifest import-command FILE -- HASHCAT_ARGS
  shooterctl manifest show FILE
  shooterctl plan MANIFEST [--hashcat PATH]
  shooterctl run MANIFEST [--hashcat PATH]
  shooterctl stream [INPUT|-] --hashcat PATH [stream options] -- HASHCAT_ARGS
  shooterctl pipeline [--hashcat PATH] -- PRODUCER_ARGS ::: CONSUMER_ARGS
  shooterctl fleet MANIFEST --devices 1,2,... [fleet options]
  shooterctl mode search TEXT [--hashcat PATH]
  shooterctl mode explain MODE [--hashcat PATH]
  shooterctl mode identify HASH_OR_FILE [--hashcat PATH]

The manifest, streaming, and fleet commands print the exact Hashcat commands
they run. Support bundles deliberately omit command lines, hashes, candidates,
and environment values."#
    );
}

fn now_secs() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

fn json_escape(value: &str) -> String {
    let mut out = String::with_capacity(value.len() + 8);

    for ch in value.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c.is_control() => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }

    out
}

fn redact_path(value: &str) -> String {
    let home = env::var("USERPROFILE")
        .or_else(|_| env::var("HOME"))
        .unwrap_or_default();

    if !home.is_empty() {
        value.replace(&home, "<HOME>")
    } else {
        value.to_owned()
    }
}

fn parse_u64(value: &str, name: &str) -> AppResult<u64> {
    value
        .parse::<u64>()
        .map_err(|_| format!("{name} expects a non-negative integer, received '{value}'"))
}

fn value_after(args: &[String], name: &str) -> AppResult<Option<String>> {
    if let Some(pos) = args.iter().position(|arg| arg == name) {
        let value = args
            .get(pos + 1)
            .ok_or_else(|| format!("{name} requires a value"))?;
        Ok(Some(value.clone()))
    } else {
        Ok(None)
    }
}

fn flag_present(args: &[String], name: &str) -> bool {
    args.iter().any(|arg| arg == name)
}

fn positional_args(args: &[String], options_with_values: &[&str]) -> AppResult<Vec<String>> {
    let mut values = Vec::new();
    let mut pos = 0;

    while pos < args.len() {
        let arg = &args[pos];

        if options_with_values.iter().any(|name| arg == name) {
            if pos + 1 >= args.len() {
                return Err(format!("{arg} requires a value"));
            }
            pos += 2;
            continue;
        }
        if arg.starts_with('-') {
            pos += 1;
            continue;
        }

        values.push(arg.clone());
        pos += 1;
    }

    Ok(values)
}

fn default_hashcat() -> PathBuf {
    if let Ok(exe) = env::current_exe()
        && let Some(parent) = exe.parent()
    {
        let windows = parent.join("hashcat.exe");
        if windows.is_file() {
            return windows;
        }

        let unix = parent.join("hashcat");
        if unix.is_file() {
            return unix;
        }

        let bin = parent.join("hashcat.bin");
        if bin.is_file() {
            return bin;
        }
    }

    if cfg!(windows) {
        PathBuf::from("hashcat.exe")
    } else {
        PathBuf::from("hashcat")
    }
}

fn hashcat_from(args: &[String]) -> AppResult<PathBuf> {
    Ok(value_after(args, "--hashcat")?
        .map(PathBuf::from)
        .unwrap_or_else(default_hashcat))
}

fn capture(program: &Path, args: &[&str]) -> io::Result<std::process::Output> {
    Command::new(program).args(args).output()
}

fn count_extension(root: &Path, extension: &str) -> u64 {
    let Ok(entries) = fs::read_dir(root) else {
        return 0;
    };

    entries
        .filter_map(Result::ok)
        .filter(|entry| {
            entry
                .path()
                .extension()
                .is_some_and(|ext| ext.eq_ignore_ascii_case(extension))
        })
        .count() as u64
}

fn collect_doctor(hashcat: &Path) -> (Vec<(String, String, bool)>, String) {
    let mut checks = Vec::new();
    let mut backend = String::new();

    checks.push((
        "operating_system".to_owned(),
        env::consts::OS.to_owned(),
        true,
    ));
    checks.push((
        "architecture".to_owned(),
        env::consts::ARCH.to_owned(),
        true,
    ));
    checks.push((
        "working_directory".to_owned(),
        env::current_dir()
            .map(|p| redact_path(&p.display().to_string()))
            .unwrap_or_else(|_| "<unavailable>".to_owned()),
        true,
    ));
    checks.push((
        "hashcat_path".to_owned(),
        redact_path(&hashcat.display().to_string()),
        hashcat.is_file(),
    ));

    match capture(hashcat, &["--version"]) {
        Ok(output) if output.status.success() => checks.push((
            "hashcat_version".to_owned(),
            String::from_utf8_lossy(&output.stdout).trim().to_owned(),
            true,
        )),
        Ok(output) => checks.push((
            "hashcat_version".to_owned(),
            format!("failed with {}", output.status),
            false,
        )),
        Err(error) => checks.push(("hashcat_version".to_owned(), error.to_string(), false)),
    }

    if let Some(root) = hashcat.parent() {
        let ext = if cfg!(windows) { "dll" } else { "so" };
        checks.push((
            "module_count".to_owned(),
            count_extension(&root.join("modules"), ext).to_string(),
            count_extension(&root.join("modules"), ext) > 0,
        ));
        checks.push((
            "bridge_count".to_owned(),
            count_extension(&root.join("bridges"), ext).to_string(),
            count_extension(&root.join("bridges"), ext) > 0,
        ));
        checks.push((
            "feed_count".to_owned(),
            count_extension(&root.join("feeds"), ext).to_string(),
            count_extension(&root.join("feeds"), ext) > 0,
        ));
        checks.push((
            "opencl_directory".to_owned(),
            redact_path(&root.join("OpenCL").display().to_string()),
            root.join("OpenCL").is_dir(),
        ));
    }

    match capture(hashcat, &["--backend-info", "--quiet"]) {
        Ok(output) => {
            backend = String::from_utf8_lossy(&output.stdout).into_owned();
            backend.push_str(&String::from_utf8_lossy(&output.stderr));
            checks.push((
                "backend_probe".to_owned(),
                if output.status.success() {
                    "completed".to_owned()
                } else {
                    format!("completed with {}", output.status)
                },
                output.status.success(),
            ));
        }
        Err(error) => checks.push(("backend_probe".to_owned(), error.to_string(), false)),
    }

    checks.push((
        "zstd_command".to_owned(),
        if command_available("zstd") {
            "available".to_owned()
        } else {
            "not found (only needed for .zst streams)".to_owned()
        },
        true,
    ));

    (checks, backend)
}

fn doctor_json(checks: &[(String, String, bool)]) -> String {
    let body = checks
        .iter()
        .map(|(name, value, ok)| {
            format!(
                "    {{\"name\":\"{}\",\"ok\":{},\"value\":\"{}\"}}",
                json_escape(name),
                ok,
                json_escape(value)
            )
        })
        .collect::<Vec<_>>()
        .join(",\n");

    format!(
        "{{\n  \"schema\": \"shooter-doctor-v1\",\n  \"created_unix\": {},\n  \"checks\": [\n{}\n  ]\n}}\n",
        now_secs(),
        body
    )
}

fn write_support_bundle(
    destination: &Path,
    checks: &[(String, String, bool)],
    backend: &str,
) -> AppResult<()> {
    fs::create_dir_all(destination)
        .map_err(|e| format!("cannot create {}: {e}", destination.display()))?;
    fs::write(destination.join("doctor.json"), doctor_json(checks))
        .map_err(|e| format!("cannot write doctor.json: {e}"))?;
    fs::write(destination.join("backend-info.txt"), backend)
        .map_err(|e| format!("cannot write backend-info.txt: {e}"))?;
    fs::write(
        destination.join("README.txt"),
        "Shooter support bundle\n\nThis bundle omits command lines, hashes, candidates, potfile contents, and environment values. Review every file before sharing it.\n",
    )
    .map_err(|e| format!("cannot write support README: {e}"))?;

    Ok(())
}

fn cmd_doctor(args: &[String]) -> AppResult<()> {
    let hashcat = hashcat_from(args)?;
    let (checks, backend) = collect_doctor(&hashcat);

    if flag_present(args, "--json") {
        print!("{}", doctor_json(&checks));
    } else {
        println!("Shooter diagnostic check");
        for (name, value, ok) in &checks {
            println!(
                "{:4} {:22} {}",
                if *ok { "ok" } else { "FAIL" },
                name,
                value
            );
        }
    }

    if let Some(path) = value_after(args, "--support-bundle")? {
        write_support_bundle(Path::new(&path), &checks, &backend)?;
        println!("Support bundle: {}", Path::new(&path).display());
    }

    if checks.iter().any(|(_, _, ok)| !ok) {
        return Err("one or more diagnostic checks failed".to_owned());
    }

    Ok(())
}

fn cmd_support_bundle(args: &[String]) -> AppResult<()> {
    let hashcat = hashcat_from(args)?;
    let destination = positional_args(args, &["--hashcat"])?
        .first()
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(format!("shooter-support-{}", now_secs())));
    let (checks, backend) = collect_doctor(&hashcat);

    write_support_bundle(&destination, &checks, &backend)?;
    println!("Support bundle: {}", destination.display());

    Ok(())
}

fn command_available(name: &str) -> bool {
    Command::new(name)
        .arg("--version")
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .is_ok()
}

fn fnv_update(mut hash: u64, data: &[u8]) -> u64 {
    for byte in data {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(1_099_511_628_211);
    }
    hash
}

fn intended_path(path: &Path) -> AppResult<PathBuf> {
    if path.exists() {
        fs::canonicalize(path).map_err(|e| format!("cannot resolve {}: {e}", path.display()))
    } else {
        let parent = path
            .parent()
            .filter(|value| !value.as_os_str().is_empty())
            .unwrap_or_else(|| Path::new("."));
        let parent = fs::canonicalize(parent)
            .map_err(|e| format!("cannot resolve output directory {}: {e}", parent.display()))?;
        Ok(parent.join(
            path.file_name()
                .ok_or_else(|| format!("output {} has no file name", path.display()))?,
        ))
    }
}

fn create_index(source: &Path, output: &Path, stride: u64) -> AppResult<()> {
    if intended_path(source)? == intended_path(output)? {
        return Err("the index output must not overwrite its source file".to_owned());
    }

    let metadata = fs::metadata(source)
        .map_err(|e| format!("cannot read metadata for {}: {e}", source.display()))?;
    let modified = metadata
        .modified()
        .ok()
        .and_then(|time| time.duration_since(UNIX_EPOCH).ok())
        .map_or(0, |duration| duration.as_secs());
    let input = File::open(source).map_err(|e| format!("cannot open {}: {e}", source.display()))?;
    let mut reader = BufReader::with_capacity(4 * 1024 * 1024, input);
    let mut writer = BufWriter::new(
        File::create(output).map_err(|e| format!("cannot create {}: {e}", output.display()))?,
    );

    writeln!(writer, "SHOOTER-HCIDX 1").map_err(|e| e.to_string())?;
    writeln!(writer, "source_size {:020}", metadata.len()).map_err(|e| e.to_string())?;
    writeln!(writer, "modified {:020}", modified).map_err(|e| e.to_string())?;
    writeln!(writer, "stride {:020}", stride).map_err(|e| e.to_string())?;
    let line_pos = writer.stream_position().map_err(|e| e.to_string())? + 11;
    writeln!(writer, "line_count {:020}", 0).map_err(|e| e.to_string())?;
    let hash_pos = writer.stream_position().map_err(|e| e.to_string())? + 12;
    writeln!(writer, "fingerprint {:016x}", 0).map_err(|e| e.to_string())?;
    writeln!(writer, "offsets").map_err(|e| e.to_string())?;

    let mut buffer = Vec::with_capacity(4096);
    let mut offset = 0u64;
    let mut lines = 0u64;
    let mut fingerprint = 14_695_981_039_346_656_037u64;

    loop {
        buffer.clear();
        let read = reader
            .read_until(b'\n', &mut buffer)
            .map_err(|e| format!("cannot read {}: {e}", source.display()))?;
        if read == 0 {
            break;
        }

        if lines.is_multiple_of(stride) {
            writeln!(writer, "{offset}").map_err(|e| e.to_string())?;
        }

        fingerprint = fnv_update(fingerprint, &buffer);
        offset += read as u64;
        lines += 1;
    }

    writer.flush().map_err(|e| e.to_string())?;
    let mut file = writer
        .into_inner()
        .map_err(|e| format!("cannot finalize {}: {e}", output.display()))?;
    file.seek(SeekFrom::Start(line_pos))
        .map_err(|e| e.to_string())?;
    write!(file, "{lines:020}").map_err(|e| e.to_string())?;
    file.seek(SeekFrom::Start(hash_pos))
        .map_err(|e| e.to_string())?;
    write!(file, "{fingerprint:016x}").map_err(|e| e.to_string())?;
    file.sync_all().map_err(|e| e.to_string())?;

    println!(
        "Indexed {} lines from {} -> {} (stride {})",
        lines,
        source.display(),
        output.display(),
        stride
    );

    Ok(())
}

fn cmd_index(args: &[String]) -> AppResult<()> {
    let stride = value_after(args, "--stride")?
        .map(|value| parse_u64(&value, "--stride"))
        .transpose()?
        .unwrap_or(4096)
        .max(1);
    let explicit_output = value_after(args, "--output")?;
    let mut files = Vec::new();
    let mut skip_next = false;

    for arg in args {
        if skip_next {
            skip_next = false;
            continue;
        }
        if arg == "--stride" || arg == "--output" {
            skip_next = true;
        } else if !arg.starts_with('-') {
            files.push(arg.clone());
        }
    }

    if files.is_empty() {
        return Err("index requires at least one input file".to_owned());
    }
    if explicit_output.is_some() && files.len() != 1 {
        return Err("--output can only be used with one input file".to_owned());
    }

    for file in files {
        let source = PathBuf::from(&file);
        let output = explicit_output
            .as_ref()
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from(format!("{file}.hcidx")));
        create_index(&source, &output, stride)?;
    }

    Ok(())
}

fn bloom_test_and_set(bits: &mut [u64], hash: u64) -> bool {
    let bit_count = (bits.len() * 64) as u64;
    let first = hash % bit_count;
    let second = hash.rotate_left(29).wrapping_mul(0x9e37_79b9_7f4a_7c15) % bit_count;
    let first_mask = 1u64 << (first & 63);
    let second_mask = 1u64 << (second & 63);
    let first_idx = (first >> 6) as usize;
    let second_idx = (second >> 6) as usize;
    let present = bits[first_idx] & first_mask != 0 && bits[second_idx] & second_mask != 0;
    bits[first_idx] |= first_mask;
    bits[second_idx] |= second_mask;
    present
}

fn cmd_rule_report(args: &[String]) -> AppResult<()> {
    let skip = value_after(args, "--skip")?
        .map(|value| parse_u64(&value, "--skip"))
        .transpose()?
        .unwrap_or(0);
    let limit = value_after(args, "--limit")?
        .map(|value| parse_u64(&value, "--limit"))
        .transpose()?;
    let json = flag_present(args, "--json");
    let mut files = Vec::new();
    let mut skip_next = false;

    for arg in args {
        if skip_next {
            skip_next = false;
            continue;
        }
        if arg == "--skip" || arg == "--limit" {
            skip_next = true;
        } else if !arg.starts_with('-') {
            files.push(arg.clone());
        }
    }

    if files.is_empty() {
        return Err("rule-report requires at least one rule file".to_owned());
    }

    let mut lines_seen = 0u64;
    let mut lines_reported = 0u64;
    let mut empty = 0u64;
    let mut comments = 0u64;
    let mut multibyte = 0u64;
    let mut invalid_utf8 = 0u64;
    let mut bytes = 0u64;
    let mut max_length = 0usize;
    let mut possible_duplicates = 0u64;
    let mut bloom = vec![0u64; 1 << 20];
    let mut operations: BTreeMap<u8, u64> = BTreeMap::new();
    let mut per_file = Vec::new();

    'files: for name in &files {
        let file = File::open(name).map_err(|e| format!("cannot open {name}: {e}"))?;
        let mut reader = BufReader::with_capacity(4 * 1024 * 1024, file);
        let mut buffer = Vec::with_capacity(256);
        let mut file_lines = 0u64;

        loop {
            buffer.clear();
            let read = reader
                .read_until(b'\n', &mut buffer)
                .map_err(|e| format!("cannot read {name}: {e}"))?;
            if read == 0 {
                break;
            }

            lines_seen += 1;
            file_lines += 1;

            if lines_seen <= skip {
                continue;
            }
            if limit.is_some_and(|value| lines_reported >= value) {
                per_file.push((name.clone(), file_lines));
                break 'files;
            }

            while buffer
                .last()
                .is_some_and(|byte| *byte == b'\n' || *byte == b'\r')
            {
                buffer.pop();
            }

            lines_reported += 1;
            bytes += buffer.len() as u64;
            max_length = max_length.max(buffer.len());

            if buffer.is_empty() {
                empty += 1;
                continue;
            }
            if buffer[0] == b'#' {
                comments += 1;
                continue;
            }
            if buffer.iter().any(|byte| *byte >= 0x80) {
                multibyte += 1;
            }
            if std::str::from_utf8(&buffer).is_err() {
                invalid_utf8 += 1;
            }

            let hash = fnv_update(14_695_981_039_346_656_037, &buffer);
            if bloom_test_and_set(&mut bloom, hash) {
                possible_duplicates += 1;
            }

            for byte in &buffer {
                if byte.is_ascii_graphic() {
                    *operations.entry(*byte).or_default() += 1;
                }
            }
        }

        per_file.push((name.clone(), file_lines));
    }

    if json {
        let file_json = per_file
            .iter()
            .map(|(name, count)| {
                format!(
                    "{{\"path\":\"{}\",\"lines_seen\":{count}}}",
                    json_escape(name)
                )
            })
            .collect::<Vec<_>>()
            .join(",");
        println!(
            "{{\"schema\":\"shooter-rule-report-v1\",\"lines\":{lines_reported},\"bytes\":{bytes},\"empty\":{empty},\"comments\":{comments},\"multibyte\":{multibyte},\"invalid_utf8\":{invalid_utf8},\"max_length\":{max_length},\"possible_duplicates\":{possible_duplicates},\"files\":[{file_json}]}}"
        );
    } else {
        println!("Rule report");
        println!("  selected lines       {lines_reported}");
        println!("  bytes                {bytes}");
        println!("  empty lines          {empty}");
        println!("  comments             {comments}");
        println!("  multibyte rules      {multibyte}");
        println!("  invalid UTF-8        {invalid_utf8}");
        println!("  longest rule         {max_length} bytes");
        println!("  possible duplicates  {possible_duplicates} (fixed-memory estimate)");
        println!("  file order");
        for (name, count) in per_file {
            println!("    {count:12}  {name}");
        }
        println!("  most common rule bytes");
        let mut common: Vec<(u8, u64)> = operations.into_iter().collect();
        common.sort_by_key(|(_, count)| std::cmp::Reverse(*count));
        for (byte, count) in common.into_iter().take(16) {
            println!("    {:>3}  '{}'  {count}", byte, char::from(byte));
        }
    }

    Ok(())
}

fn manifest_json(manifest: &Manifest) -> String {
    fn array(values: &[String]) -> String {
        values
            .iter()
            .map(|value| format!("\"{}\"", json_escape(value)))
            .collect::<Vec<_>>()
            .join(", ")
    }
    fn optional(value: &Option<String>) -> String {
        value
            .as_ref()
            .map(|item| format!("\"{}\"", json_escape(item)))
            .unwrap_or_else(|| "null".to_owned())
    }

    format!(
        concat!(
            "{{\n",
            "  \"schema\": \"shooter-target-v1\",\n",
            "  \"mode\": \"{}\",\n",
            "  \"attack_mode\": {},\n",
            "  \"hashes\": \"{}\",\n",
            "  \"wordlists\": [{}],\n",
            "  \"rules\": [{}],\n",
            "  \"masks\": [{}],\n",
            "  \"output\": {},\n",
            "  \"potfile\": {},\n",
            "  \"total_work\": {},\n",
            "  \"extra\": [{}]\n",
            "}}\n"
        ),
        json_escape(&manifest.mode),
        manifest.attack_mode,
        json_escape(&manifest.hashes),
        array(&manifest.wordlists),
        array(&manifest.rules),
        array(&manifest.masks),
        optional(&manifest.output),
        optional(&manifest.potfile),
        manifest
            .total_work
            .map(|value| value.to_string())
            .unwrap_or_else(|| "null".to_owned()),
        array(&manifest.extra)
    )
}

fn json_value_start<'a>(text: &'a str, key: &str) -> AppResult<&'a str> {
    let needle = format!("\"{}\"", key);
    let pos = text
        .find(&needle)
        .ok_or_else(|| format!("manifest is missing '{key}'"))?;
    let after = &text[pos + needle.len()..];
    let colon = after
        .find(':')
        .ok_or_else(|| format!("manifest key '{key}' has no value"))?;
    Ok(after[colon + 1..].trim_start())
}

fn json_string(text: &str, key: &str) -> AppResult<String> {
    let value = json_value_start(text, key)?;
    if !value.starts_with('"') {
        return Err(format!("manifest key '{key}' must be a string"));
    }
    let mut escaped = false;
    let mut out = String::new();
    for ch in value[1..].chars() {
        if escaped {
            match ch {
                'n' => out.push('\n'),
                'r' => out.push('\r'),
                't' => out.push('\t'),
                other => out.push(other),
            }
            escaped = false;
        } else if ch == '\\' {
            escaped = true;
        } else if ch == '"' {
            return Ok(out);
        } else {
            out.push(ch);
        }
    }
    Err(format!("manifest key '{key}' has an unterminated string"))
}

fn json_optional_string(text: &str, key: &str) -> AppResult<Option<String>> {
    let value = json_value_start(text, key)?;
    if value.starts_with("null") {
        Ok(None)
    } else {
        json_string(text, key).map(Some)
    }
}

fn json_number(text: &str, key: &str) -> AppResult<Option<u64>> {
    let value = json_value_start(text, key)?;
    if value.starts_with("null") {
        return Ok(None);
    }
    let digits: String = value.chars().take_while(|ch| ch.is_ascii_digit()).collect();
    if digits.is_empty() {
        return Err(format!("manifest key '{key}' must be a number"));
    }
    parse_u64(&digits, key).map(Some)
}

fn json_array(text: &str, key: &str) -> AppResult<Vec<String>> {
    let value = json_value_start(text, key)?;
    if !value.starts_with('[') {
        return Err(format!("manifest key '{key}' must be an array"));
    }
    let mut values = Vec::new();
    let mut current = String::new();
    let mut quoted = false;
    let mut escaped = false;

    for ch in value[1..].chars() {
        if quoted {
            if escaped {
                current.push(match ch {
                    'n' => '\n',
                    'r' => '\r',
                    't' => '\t',
                    other => other,
                });
                escaped = false;
            } else if ch == '\\' {
                escaped = true;
            } else if ch == '"' {
                quoted = false;
                values.push(std::mem::take(&mut current));
            } else {
                current.push(ch);
            }
        } else if ch == '"' {
            quoted = true;
        } else if ch == ']' {
            return Ok(values);
        }
    }

    Err(format!("manifest key '{key}' has an unterminated array"))
}

fn load_manifest(path: &Path) -> AppResult<Manifest> {
    let text = fs::read_to_string(path)
        .map_err(|e| format!("cannot read manifest {}: {e}", path.display()))?;
    let schema = json_string(&text, "schema")?;
    if schema != "shooter-target-v1" {
        return Err(format!(
            "unsupported manifest schema '{schema}'; expected shooter-target-v1"
        ));
    }
    let attack_mode = json_number(&text, "attack_mode")?
        .ok_or_else(|| "attack_mode cannot be null".to_owned())?;
    let attack_mode = u32::try_from(attack_mode)
        .map_err(|_| "manifest attack_mode exceeds the supported range".to_owned())?;
    Ok(Manifest {
        mode: json_string(&text, "mode")?,
        attack_mode,
        hashes: json_string(&text, "hashes")?,
        wordlists: json_array(&text, "wordlists")?,
        rules: json_array(&text, "rules")?,
        masks: json_array(&text, "masks")?,
        output: json_optional_string(&text, "output")?,
        potfile: json_optional_string(&text, "potfile")?,
        total_work: json_number(&text, "total_work")?,
        extra: json_array(&text, "extra")?,
    })
}

fn cmd_manifest(args: &[String]) -> AppResult<()> {
    match args.first().map(String::as_str) {
        Some("show") => {
            let path = args.get(1).ok_or("manifest show requires a file")?;
            let manifest = load_manifest(Path::new(path))?;
            print!("{}", manifest_json(&manifest));
        }
        Some("create") => {
            let path = args
                .get(1)
                .ok_or("manifest create requires an output file")?;
            let mode = value_after(args, "--mode")?.ok_or("--mode is required")?;
            let hashes = value_after(args, "--hashes")?.ok_or("--hashes is required")?;
            let attack_mode = value_after(args, "--attack-mode")?
                .map(|value| parse_u64(&value, "--attack-mode"))
                .transpose()?
                .unwrap_or(0);
            let attack_mode = u32::try_from(attack_mode)
                .map_err(|_| "--attack-mode exceeds the supported range".to_owned())?;
            let mut manifest = Manifest {
                mode,
                attack_mode,
                hashes,
                output: value_after(args, "--output")?,
                potfile: value_after(args, "--potfile")?,
                total_work: value_after(args, "--total-work")?
                    .map(|value| parse_u64(&value, "--total-work"))
                    .transpose()?,
                ..Manifest::default()
            };

            let mut pos = 2;
            while pos < args.len() {
                match args[pos].as_str() {
                    "--wordlist" => {
                        pos += 1;
                        manifest
                            .wordlists
                            .push(args.get(pos).ok_or("--wordlist requires a value")?.clone());
                    }
                    "--rule" => {
                        pos += 1;
                        manifest
                            .rules
                            .push(args.get(pos).ok_or("--rule requires a value")?.clone());
                    }
                    "--mask" => {
                        pos += 1;
                        manifest
                            .masks
                            .push(args.get(pos).ok_or("--mask requires a value")?.clone());
                    }
                    "--extra" => {
                        pos += 1;
                        manifest
                            .extra
                            .push(args.get(pos).ok_or("--extra requires a value")?.clone());
                    }
                    "--mode" | "--hashes" | "--attack-mode" | "--output" | "--potfile"
                    | "--total-work" => pos += 1,
                    _ => {}
                }
                pos += 1;
            }

            fs::write(path, manifest_json(&manifest))
                .map_err(|e| format!("cannot write manifest {path}: {e}"))?;
            println!("Manifest created: {path}");
        }
        Some("import-command") => {
            let path = args
                .get(1)
                .ok_or("manifest import-command requires an output file")?;
            let split = args
                .iter()
                .position(|arg| arg == "--")
                .ok_or("manifest import-command requires -- before the Hashcat command")?;
            let mut command = args[split + 1..].to_vec();
            if command
                .first()
                .is_some_and(|value| value.to_ascii_lowercase().contains("hashcat"))
            {
                command.remove(0);
            }
            let manifest = import_hashcat_command(&command)?;
            fs::write(path, manifest_json(&manifest))
                .map_err(|e| format!("cannot write manifest {path}: {e}"))?;
            println!("Manifest imported: {path}");
        }
        _ => return Err("usage: shooterctl manifest create|import-command|show ...".to_owned()),
    }

    Ok(())
}

fn option_takes_value(arg: &str) -> bool {
    matches!(
        arg,
        "-m" | "--hash-type"
            | "-a"
            | "--attack-mode"
            | "-r"
            | "--rules-file"
            | "-o"
            | "--outfile"
            | "--potfile-path"
            | "-w"
            | "--workload-profile"
            | "-d"
            | "--backend-devices"
            | "-D"
            | "--opencl-device-types"
            | "-n"
            | "--kernel-accel"
            | "-u"
            | "--kernel-loops"
            | "-T"
            | "--kernel-threads"
            | "-s"
            | "--skip"
            | "-l"
            | "--limit"
            | "-j"
            | "--rule-left"
            | "-k"
            | "--rule-right"
            | "-1"
            | "--custom-charset1"
            | "-2"
            | "--custom-charset2"
            | "-3"
            | "--custom-charset3"
            | "-4"
            | "--custom-charset4"
            | "-5"
            | "--custom-charset5"
            | "-6"
            | "--custom-charset6"
            | "-7"
            | "--custom-charset7"
            | "-8"
            | "--custom-charset8"
            | "--session"
            | "--runtime"
            | "--status-timer"
            | "--outfile-check-dir"
            | "--outfile-check-timer"
            | "--debug-mode"
            | "--debug-file"
            | "--bitmap-min"
            | "--bitmap-max"
            | "--encoding-from"
            | "--encoding-to"
            | "--separator"
            | "-p"
    )
}

fn import_hashcat_command(command: &[String]) -> AppResult<Manifest> {
    let mut manifest = Manifest::default();
    let mut positional = Vec::new();
    let mut pos = 0;

    while pos < command.len() {
        let arg = &command[pos];
        if arg == "-m" || arg == "--hash-type" {
            pos += 1;
            manifest.mode = command
                .get(pos)
                .ok_or("-m/--hash-type requires a value")?
                .clone();
        } else if arg == "-a" || arg == "--attack-mode" {
            pos += 1;
            let attack_mode = parse_u64(
                command
                    .get(pos)
                    .ok_or("-a/--attack-mode requires a value")?,
                "--attack-mode",
            )?;
            manifest.attack_mode = u32::try_from(attack_mode)
                .map_err(|_| "--attack-mode exceeds the supported range".to_owned())?;
        } else if arg == "-r" || arg == "--rules-file" {
            pos += 1;
            manifest.rules.push(
                command
                    .get(pos)
                    .ok_or("-r/--rules-file requires a value")?
                    .clone(),
            );
        } else if arg == "-o" || arg == "--outfile" {
            pos += 1;
            manifest.output = Some(
                command
                    .get(pos)
                    .ok_or("-o/--outfile requires a value")?
                    .clone(),
            );
        } else if arg == "--potfile-path" {
            pos += 1;
            manifest.potfile = Some(
                command
                    .get(pos)
                    .ok_or("--potfile-path requires a value")?
                    .clone(),
            );
        } else if let Some((name, value)) = arg.split_once('=') {
            match name {
                "--hash-type" => manifest.mode = value.to_owned(),
                "--attack-mode" => {
                    manifest.attack_mode = u32::try_from(parse_u64(value, name)?)
                        .map_err(|_| "--attack-mode exceeds the supported range".to_owned())?
                }
                "--rules-file" => manifest.rules.push(value.to_owned()),
                "--outfile" => manifest.output = Some(value.to_owned()),
                "--potfile-path" => manifest.potfile = Some(value.to_owned()),
                _ => manifest.extra.push(arg.clone()),
            }
        } else if option_takes_value(arg) {
            let value = command
                .get(pos + 1)
                .ok_or_else(|| format!("{arg} requires a value"))?;
            manifest.extra.push(arg.clone());
            manifest.extra.push(value.clone());
            pos += 1;
        } else if arg.starts_with('-') {
            manifest.extra.push(arg.clone());
        } else {
            positional.push(arg.clone());
        }
        pos += 1;
    }

    if manifest.mode.is_empty() {
        return Err("imported commands must specify -m/--hash-type".to_owned());
    }
    manifest.hashes = positional
        .first()
        .ok_or("the imported command has no hash or hash file")?
        .clone();
    let inputs = &positional[1..];
    match manifest.attack_mode {
        3 => manifest.masks.extend_from_slice(inputs),
        7 | 12 => {
            if let Some(mask) = inputs.first() {
                manifest.masks.push(mask.clone());
            }
            manifest
                .wordlists
                .extend_from_slice(inputs.get(1..).unwrap_or_default());
        }
        6 => {
            if let Some(wordlist) = inputs.first() {
                manifest.wordlists.push(wordlist.clone());
            }
            manifest
                .masks
                .extend_from_slice(inputs.get(1..).unwrap_or_default());
        }
        _ => manifest.wordlists.extend_from_slice(inputs),
    }

    Ok(manifest)
}

fn append_attack_inputs(args: &mut Vec<String>, manifest: &Manifest) {
    if matches!(manifest.attack_mode, 7 | 12) {
        args.extend(manifest.masks.iter().cloned());
        args.extend(manifest.wordlists.iter().cloned());
    } else {
        args.extend(manifest.wordlists.iter().cloned());
        args.extend(manifest.masks.iter().cloned());
    }
}

fn manifest_commands(manifest: &Manifest) -> Vec<Vec<String>> {
    let rules: Vec<Option<&String>> = if manifest.rules.is_empty() {
        vec![None]
    } else {
        manifest.rules.iter().map(Some).collect()
    };
    let mut commands = Vec::new();

    for rule in rules {
        let mut args = vec![
            "-m".to_owned(),
            manifest.mode.clone(),
            "-a".to_owned(),
            manifest.attack_mode.to_string(),
            manifest.hashes.clone(),
        ];
        append_attack_inputs(&mut args, manifest);
        if let Some(rule) = rule {
            args.push("-r".to_owned());
            args.push(rule.clone());
        }
        if let Some(output) = &manifest.output {
            args.push("-o".to_owned());
            args.push(output.clone());
        }
        if let Some(potfile) = &manifest.potfile {
            args.push("--potfile-path".to_owned());
            args.push(potfile.clone());
        }
        args.extend(manifest.extra.iter().cloned());
        commands.push(args);
    }

    commands
}

fn quote_arg(arg: &str) -> String {
    if arg.is_empty() || arg.chars().any(char::is_whitespace) {
        format!("\"{}\"", arg.replace('"', "\\\""))
    } else {
        arg.to_owned()
    }
}

fn print_command(program: &Path, args: &[String]) {
    let rendered = std::iter::once(quote_arg(&program.display().to_string()))
        .chain(args.iter().map(|arg| quote_arg(arg)))
        .collect::<Vec<_>>()
        .join(" ");
    println!("{rendered}");
}

fn cmd_plan(args: &[String], execute: bool) -> AppResult<()> {
    let manifest_path = args.first().ok_or("plan/run requires a manifest file")?;
    let manifest = load_manifest(Path::new(manifest_path))?;
    let hashcat = hashcat_from(args)?;
    let commands = manifest_commands(&manifest);

    for command_args in commands {
        print_command(&hashcat, &command_args);
        if execute {
            let status = Command::new(&hashcat)
                .args(&command_args)
                .status()
                .map_err(|e| format!("cannot start {}: {e}", hashcat.display()))?;
            if !status.success() {
                return Err(format!("Hashcat exited with {status}"));
            }
        }
    }

    Ok(())
}

fn read_checkpoint(path: Option<&str>) -> AppResult<u64> {
    let Some(path) = path else {
        return Ok(0);
    };
    if !Path::new(path).exists() {
        return Ok(0);
    }
    let text =
        fs::read_to_string(path).map_err(|e| format!("cannot read checkpoint {path}: {e}"))?;
    parse_u64(text.trim(), "checkpoint")
}

fn indexed_seek(file: &mut File, source: &Path, line: u64) -> AppResult<u64> {
    if line == 0 {
        return Ok(0);
    }
    let index_path = PathBuf::from(format!("{}.hcidx", source.display()));
    if !index_path.is_file() {
        return Ok(0);
    }
    let metadata = fs::metadata(source)
        .map_err(|e| format!("cannot validate index for {}: {e}", source.display()))?;
    let modified = metadata
        .modified()
        .ok()
        .and_then(|time| time.duration_since(UNIX_EPOCH).ok())
        .map_or(0, |duration| duration.as_secs());
    let reader = BufReader::new(
        File::open(&index_path)
            .map_err(|e| format!("cannot open {}: {e}", index_path.display()))?,
    );
    let mut stride = None;
    let mut indexed_size = None;
    let mut indexed_modified = None;
    let mut offsets = false;
    let mut wanted_sample = 0u64;
    let mut sample = 0u64;

    for result in reader.lines() {
        let entry = result.map_err(|e| format!("cannot read {}: {e}", index_path.display()))?;
        if let Some(value) = entry.strip_prefix("source_size ") {
            indexed_size = value.trim().parse::<u64>().ok();
        } else if let Some(value) = entry.strip_prefix("modified ") {
            indexed_modified = value.trim().parse::<u64>().ok();
        } else if let Some(value) = entry.strip_prefix("stride ") {
            stride = value.trim().parse::<u64>().ok();
            wanted_sample = line / stride.unwrap_or(1).max(1);
        } else if entry == "offsets" {
            offsets = true;
        } else if offsets {
            if sample == wanted_sample {
                if indexed_size != Some(metadata.len()) || indexed_modified != Some(modified) {
                    eprintln!("Ignoring stale index {}", index_path.display());
                    return Ok(0);
                }
                let offset = parse_u64(entry.trim(), "index offset")?;
                file.seek(SeekFrom::Start(offset))
                    .map_err(|e| format!("cannot seek {}: {e}", source.display()))?;
                let base = wanted_sample * stride.unwrap_or(1).max(1);
                println!("Index resume: line {base} at byte {offset}");
                return Ok(base);
            }
            sample += 1;
        }
    }

    Ok(0)
}

fn cmd_stream(args: &[String]) -> AppResult<()> {
    let split = args.iter().position(|arg| arg == "--");
    let hash_args: Vec<String> = split
        .map(|pos| args[pos + 1..].to_vec())
        .unwrap_or_default();
    if hash_args.is_empty() {
        return Err("stream requires Hashcat arguments after --".to_owned());
    }
    let option_args = split.map_or(args, |pos| &args[..pos]);
    let hashcat = hashcat_from(option_args)?;
    let checkpoint = value_after(option_args, "--checkpoint")?;
    let input_name = positional_args(
        option_args,
        &[
            "--hashcat",
            "--checkpoint",
            "--skip-lines",
            "--limit-lines",
            "--total-candidates",
        ],
    )?
    .first()
    .cloned()
    .unwrap_or_else(|| "-".to_owned());

    if input_name != "-"
        && let Some(path) = &checkpoint
        && intended_path(Path::new(&input_name))? == intended_path(Path::new(path))?
    {
        return Err("the checkpoint must not overwrite its stream input".to_owned());
    }

    let resume = read_checkpoint(checkpoint.as_deref())?;
    let skip_lines = value_after(option_args, "--skip-lines")?
        .map(|value| parse_u64(&value, "--skip-lines"))
        .transpose()?
        .unwrap_or(0)
        .max(resume);
    let limit = value_after(option_args, "--limit-lines")?
        .map(|value| parse_u64(&value, "--limit-lines"))
        .transpose()?;
    let total = value_after(option_args, "--total-candidates")?
        .map(|value| parse_u64(&value, "--total-candidates"))
        .transpose()?;
    let zstd = flag_present(option_args, "--zstd") || input_name.ends_with(".zst");

    let mut zstd_child = None;
    let mut initial_seen = 0u64;
    let reader: Box<dyn Read> = if zstd {
        let mut command = Command::new("zstd");
        command.arg("-dc");
        if input_name != "-" {
            command.arg(&input_name);
        }
        command.stdout(Stdio::piped());
        let mut child = command
            .spawn()
            .map_err(|e| format!("cannot start zstd: {e}; install zstd or decompress first"))?;
        let stdout = child.stdout.take().ok_or("zstd stdout is unavailable")?;
        zstd_child = Some(child);
        Box::new(stdout)
    } else if input_name == "-" {
        Box::new(io::stdin())
    } else {
        let source = Path::new(&input_name);
        let mut file = File::open(source)
            .map_err(|e| format!("cannot open stream input {input_name}: {e}"))?;
        initial_seen = indexed_seek(&mut file, source, skip_lines)?;
        Box::new(file)
    };

    let effective_args = hash_args;
    print_command(&hashcat, &effective_args);

    let mut child = Command::new(&hashcat)
        .args(&effective_args)
        .stdin(Stdio::piped())
        .spawn()
        .map_err(|e| format!("cannot start {}: {e}", hashcat.display()))?;
    let mut child_stdin = child.stdin.take().ok_or("Hashcat stdin is unavailable")?;
    let mut reader = BufReader::with_capacity(4 * 1024 * 1024, reader);
    let mut buffer = Vec::with_capacity(256);
    let mut seen = initial_seen;
    let mut sent = 0u64;

    loop {
        buffer.clear();
        let read = reader
            .read_until(b'\n', &mut buffer)
            .map_err(|e| format!("stream read failed: {e}"))?;
        if read == 0 {
            break;
        }
        if seen < skip_lines {
            seen += 1;
            continue;
        }
        if limit.is_some_and(|value| sent >= value) {
            break;
        }

        child_stdin
            .write_all(&buffer)
            .map_err(|e| format!("Hashcat closed its input: {e}"))?;
        seen += 1;
        sent += 1;

        if sent.is_multiple_of(10_000)
            && let Some(path) = &checkpoint
        {
            fs::write(path, seen.to_string())
                .map_err(|e| format!("cannot update checkpoint {path}: {e}"))?;
        }
        if sent.is_multiple_of(1_000_000)
            && let Some(total) = total
        {
            eprintln!(
                "Stream progress: {sent}/{total} ({:.2}%)",
                100.0 * sent as f64 / total.max(1) as f64
            );
        }
    }

    drop(child_stdin);
    let status = child
        .wait()
        .map_err(|e| format!("cannot wait for Hashcat: {e}"))?;
    if let Some(mut process) = zstd_child {
        let _ = process.wait();
    }
    if let Some(path) = checkpoint {
        fs::write(&path, seen.to_string())
            .map_err(|e| format!("cannot finalize checkpoint {path}: {e}"))?;
    }
    if let Some(total) = total {
        println!("Streamed {sent}/{total} declared candidates (source position {seen})");
    } else {
        println!("Streamed {sent} candidates (source position {seen})");
    }

    if !status.success() {
        return Err(format!("Hashcat exited with {status}"));
    }

    Ok(())
}

fn cmd_pipeline(args: &[String]) -> AppResult<()> {
    let split = args
        .iter()
        .position(|arg| arg == "--")
        .ok_or("pipeline requires -- before the producer arguments")?;
    let divider = args
        .iter()
        .position(|arg| arg == ":::")
        .ok_or("pipeline requires ::: between producer and consumer arguments")?;
    if divider <= split + 1 || divider + 1 >= args.len() {
        return Err("pipeline requires nonempty producer and consumer arguments".to_owned());
    }
    let hashcat = hashcat_from(&args[..split])?;
    let mut producer_args = args[split + 1..divider].to_vec();
    let consumer_args = args[divider + 1..].to_vec();
    if !producer_args.iter().any(|arg| arg == "--stdout") {
        producer_args.push("--stdout".to_owned());
    }

    println!("Producer:");
    print_command(&hashcat, &producer_args);
    println!("Consumer:");
    print_command(&hashcat, &consumer_args);

    let mut producer = Command::new(&hashcat)
        .args(&producer_args)
        .stdout(Stdio::piped())
        .spawn()
        .map_err(|e| format!("cannot start producer {}: {e}", hashcat.display()))?;
    let producer_stdout = producer
        .stdout
        .take()
        .ok_or("producer stdout is unavailable")?;
    let mut consumer = match Command::new(&hashcat)
        .args(&consumer_args)
        .stdin(Stdio::from(producer_stdout))
        .spawn()
    {
        Ok(process) => process,
        Err(error) => {
            let _ = producer.kill();
            let _ = producer.wait();
            return Err(format!(
                "cannot start consumer {}: {error}",
                hashcat.display()
            ));
        }
    };

    let consumer_status = consumer
        .wait()
        .map_err(|e| format!("cannot wait for consumer: {e}"))?;
    if !consumer_status.success() {
        let _ = producer.kill();
    }
    let producer_status = producer
        .wait()
        .map_err(|e| format!("cannot wait for producer: {e}"))?;

    if !consumer_status.success() {
        return Err(format!("consumer exited with {consumer_status}"));
    }
    if !producer_status.success() {
        return Err(format!("producer exited with {producer_status}"));
    }

    Ok(())
}

fn render_template(value: &str, device: &str, start: u64) -> String {
    value
        .replace("{device}", device)
        .replace("{start}", &start.to_string())
}

fn telemetry_write(file: &Arc<Mutex<File>>, line: &str) {
    if let Ok(mut locked) = file.lock() {
        let _ = writeln!(locked, "{line}");
        let _ = locked.flush();
    }
}

fn cmd_fleet(args: &[String]) -> AppResult<()> {
    let manifest_path = args.first().ok_or("fleet requires a manifest")?;
    let manifest = load_manifest(Path::new(manifest_path))?;
    let total = manifest
        .total_work
        .ok_or("fleet manifests require total_work")?;
    if manifest.rules.len() > 1 {
        return Err(
            "fleet currently accepts zero or one rule; use plan/run for a rule series".to_owned(),
        );
    }
    let devices_text = value_after(args, "--devices")?.ok_or("--devices is required")?;
    let devices: Vec<String> = devices_text
        .split(',')
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(str::to_owned)
        .collect();
    if devices.is_empty() {
        return Err("--devices did not contain a device id".to_owned());
    }
    let hashcat = hashcat_from(args)?;
    let chunk_size = value_after(args, "--chunk-size")?
        .map(|value| parse_u64(&value, "--chunk-size"))
        .transpose()?
        .unwrap_or_else(|| (total / (devices.len() as u64 * 16)).max(1));
    let retries = value_after(args, "--retries")?
        .map(|value| parse_u64(&value, "--retries"))
        .transpose()?
        .unwrap_or(2) as u32;
    let telemetry_path = value_after(args, "--telemetry")?
        .unwrap_or_else(|| format!("shooter-fleet-{}.jsonl", now_secs()));
    let telemetry = Arc::new(Mutex::new(
        OpenOptions::new()
            .create(true)
            .append(true)
            .open(&telemetry_path)
            .map_err(|e| format!("cannot create telemetry file: {e}"))?,
    ));
    let mut queue_data = VecDeque::new();
    let mut start = 0u64;
    while start < total {
        let count = chunk_size.min(total - start);
        queue_data.push_back(Chunk {
            start,
            count,
            attempts: 0,
        });
        start += count;
    }
    let state = Arc::new((
        Mutex::new(FleetState {
            queue: queue_data,
            active: 0,
        }),
        Condvar::new(),
    ));
    let failed_chunks = Arc::new(Mutex::new(Vec::<Chunk>::new()));
    let quarantined = Arc::new(Mutex::new(HashSet::<String>::new()));
    let base_args = manifest_commands(&manifest)
        .into_iter()
        .next()
        .ok_or("manifest produced no command")?;
    let mut workers = Vec::new();

    for device in devices.clone() {
        let state = Arc::clone(&state);
        let failed_chunks = Arc::clone(&failed_chunks);
        let quarantined = Arc::clone(&quarantined);
        let telemetry = Arc::clone(&telemetry);
        let hashcat = hashcat.clone();
        let base_args = base_args.clone();
        let output = manifest.output.clone();
        let potfile = manifest.potfile.clone();

        workers.push(thread::spawn(move || {
            let mut failures = 0u32;
            loop {
                if quarantined
                    .lock()
                    .is_ok_and(|set| set.contains(&device))
                {
                    break;
                }
                let mut chunk = {
                    let (state_lock, wake) = &*state;
                    let mut fleet = match state_lock.lock() {
                        Ok(value) => value,
                        Err(_) => break,
                    };
                    loop {
                        if let Some(chunk) = fleet.queue.pop_front() {
                            fleet.active += 1;
                            break chunk;
                        }
                        if fleet.active == 0 {
                            wake.notify_all();
                            return;
                        }
                        fleet = match wake.wait(fleet) {
                            Ok(value) => value,
                            Err(_) => return,
                        };
                    }
                };
                let mut command_args = base_args.clone();
                command_args.push("-d".to_owned());
                command_args.push(device.clone());
                command_args.push("--skip".to_owned());
                command_args.push(chunk.start.to_string());
                command_args.push("--limit".to_owned());
                command_args.push(chunk.count.to_string());
                command_args.push("--session".to_owned());
                command_args.push(format!("fleet-{}-{}", device, chunk.start));
                if let Some(value) = &output
                    && let Some(pos) = command_args.iter().position(|arg| arg == "-o")
                {
                    command_args[pos + 1] = render_template(value, &device, chunk.start);
                }
                if let Some(value) = &potfile
                    && let Some(pos) = command_args
                        .iter()
                        .position(|arg| arg == "--potfile-path")
                {
                    command_args[pos + 1] = format!(
                        "{}.device-{}",
                        render_template(value, &device, chunk.start),
                        device
                    );
                }

                telemetry_write(
                    &telemetry,
                    &format!(
                        "{{\"event\":\"start\",\"time\":{},\"device\":\"{}\",\"start\":{},\"count\":{}}}",
                        now_secs(),
                        json_escape(&device),
                        chunk.start,
                        chunk.count
                    ),
                );
                let chunk_started = Instant::now();
                print_command(&hashcat, &command_args);
                let status = Command::new(&hashcat).args(&command_args).status();
                let duration_ms = chunk_started.elapsed().as_millis() as u64;
                let work_per_second = if duration_ms == 0 {
                    0.0
                } else {
                    chunk.count as f64 / (duration_ms as f64 / 1000.0)
                };
                let success = status.as_ref().is_ok_and(|value| value.success());
                telemetry_write(
                    &telemetry,
                    &format!(
                        "{{\"event\":\"finish\",\"time\":{},\"device\":\"{}\",\"start\":{},\"count\":{},\"duration_ms\":{},\"work_per_second\":{:.3},\"ok\":{}}}",
                        now_secs(),
                        json_escape(&device),
                        chunk.start,
                        chunk.count,
                        duration_ms,
                        work_per_second,
                        success
                    ),
                );

                if success {
                    failures = 0;
                } else {
                    failures += 1;
                    chunk.attempts += 1;
                }

                let quarantine = !success && failures > retries;
                {
                    let (state_lock, wake) = &*state;
                    let mut fleet = match state_lock.lock() {
                        Ok(value) => value,
                        Err(_) => return,
                    };
                    fleet.active = fleet.active.saturating_sub(1);
                    if !success {
                        if chunk.attempts <= retries {
                            fleet.queue.push_back(chunk.clone());
                        } else if let Ok(mut failed) = failed_chunks.lock() {
                            failed.push(chunk.clone());
                        }
                    }
                    wake.notify_all();
                }

                if quarantine {
                        if let Ok(mut set) = quarantined.lock() {
                            set.insert(device.clone());
                        }
                        telemetry_write(
                            &telemetry,
                            &format!(
                                "{{\"event\":\"quarantine\",\"time\":{},\"device\":\"{}\",\"failures\":{failures}}}",
                                now_secs(),
                                json_escape(&device)
                            ),
                        );
                        break;
                }
            }
        }));
    }

    for worker in workers {
        worker.join().map_err(|_| "a fleet worker panicked")?;
    }
    let remaining = {
        let (state_lock, _) = &*state;
        state_lock
            .lock()
            .map_err(|_| "fleet queue lock failed")?
            .queue
            .len()
    } + failed_chunks
        .lock()
        .map_err(|_| "fleet failed-chunk lock failed")?
        .len();
    let quarantined = quarantined
        .lock()
        .map_err(|_| "fleet quarantine lock failed")?
        .clone();
    println!("Fleet telemetry: {telemetry_path}");
    if !quarantined.is_empty() {
        println!("Quarantined devices: {:?}", quarantined);
    }
    if remaining != 0 {
        return Err(format!(
            "{remaining} chunks remain after every available worker stopped"
        ));
    }

    Ok(())
}

fn cmd_mode(args: &[String]) -> AppResult<()> {
    let action = args.first().map(String::as_str).unwrap_or("");
    let query = args
        .get(1)
        .ok_or("mode command requires a search term, mode, or hash")?;
    let hashcat = hashcat_from(args)?;

    match action {
        "explain" => {
            let output = Command::new(&hashcat)
                .args(["-m", query, "--hash-info"])
                .output()
                .map_err(|e| format!("cannot start {}: {e}", hashcat.display()))?;
            print!("{}", String::from_utf8_lossy(&output.stdout));
            eprint!("{}", String::from_utf8_lossy(&output.stderr));
            if !output.status.success() {
                return Err(format!("Hashcat exited with {}", output.status));
            }
        }
        "identify" => {
            let status = Command::new(&hashcat)
                .args(["--identify", query])
                .status()
                .map_err(|e| format!("cannot start {}: {e}", hashcat.display()))?;
            if !status.success() {
                return Err(format!("Hashcat exited with {status}"));
            }
        }
        "search" => {
            let output = Command::new(&hashcat)
                .args(["--hash-info", "--quiet"])
                .output()
                .map_err(|e| format!("cannot start {}: {e}", hashcat.display()))?;
            let text = String::from_utf8_lossy(&output.stdout);
            let needle = query.to_lowercase();
            let mut matches = 0u64;
            let mut block = Vec::new();
            for line in text.lines() {
                if line.starts_with("Hash mode #") && !block.is_empty() {
                    let joined = block.join("\n");
                    if joined.to_lowercase().contains(&needle) {
                        println!("{joined}\n");
                        matches += 1;
                    }
                    block.clear();
                }
                block.push(line);
            }
            let joined = block.join("\n");
            if joined.to_lowercase().contains(&needle) {
                println!("{joined}");
                matches += 1;
            }
            println!("Matched {matches} modes");
        }
        _ => return Err("usage: shooterctl mode search|explain|identify VALUE".to_owned()),
    }

    Ok(())
}
