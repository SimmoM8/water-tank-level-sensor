#!/usr/bin/env python3
"""
Firmware build audit for ESP32 Arduino project.

What it does:
- Compiles firmware with arduino-cli using CI-equivalent board options.
- Captures linker map (if available), symbol table, and size reports.
- Reports flash/RAM usage.
- Flags heavy libc/libstdc++ pulls.
- Flags dynamic allocation indicators (linked symbols + source patterns).
- Flags risky format-string usage patterns.
- Estimates telemetry JSON payload size and compares against MQTT state buffer.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


CI_BOARD_OPTIONS = {
    "FlashMode": "qio",
    "FlashFreq": "80",
    "FlashSize": "4M",
    "PartitionScheme": "default",
    "PSRAM": "disabled",
}

SOURCE_EXTS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".ino"}


@dataclass
class Symbol:
    size: int
    name: str
    sym_type: str


@dataclass
class Finding:
    severity: str
    path: str
    line: int
    message: str


def run_cmd(cmd: Sequence[str], cwd: Path, capture: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(
        list(cmd),
        cwd=str(cwd),
        text=True,
        capture_output=capture,
        check=False,
    )


def fail(msg: str, code: int = 1) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    raise SystemExit(code)


def parse_sketch_fqbn(sketch_yaml: Path, profile: str) -> str:
    if not sketch_yaml.is_file():
        fail(f"Missing sketch manifest: {sketch_yaml}")

    in_profile = False
    fqbn = ""
    profile_header_re = re.compile(r"^\s{2}([^:\s][^:]*)\s*:\s*$")
    fqbn_re = re.compile(r"^\s{4}fqbn:\s*(\S.*?)\s*$")

    for raw in sketch_yaml.read_text(encoding="utf-8").splitlines():
        m_prof = profile_header_re.match(raw)
        if m_prof:
            in_profile = (m_prof.group(1) == profile)
            continue

        if in_profile:
            m_fqbn = fqbn_re.match(raw)
            if m_fqbn:
                fqbn = m_fqbn.group(1).strip()
                break

    if not fqbn:
        fail(f"Could not resolve fqbn for profile '{profile}' in {sketch_yaml}")
    return fqbn


def compose_ci_fqbn(base_fqbn: str) -> str:
    option_blob = ",".join(f"{k}={v}" for k, v in CI_BOARD_OPTIONS.items())
    if "=" in base_fqbn.split(":")[-1]:
        return f"{base_fqbn},{option_blob}"
    return f"{base_fqbn}:{option_blob}"


def parse_properties(raw: str) -> Dict[str, str]:
    props: Dict[str, str] = {}
    for line in raw.splitlines():
        if "=" not in line:
            continue
        k, v = line.split("=", 1)
        props[k.strip()] = v.strip()
    return props


def resolve_binutil(props: Dict[str, str], tool: str) -> str:
    base = props.get("runtime.tools.xtensa-esp32-elf-gcc.path", "")
    if base:
        candidate = Path(base) / "bin" / f"xtensa-esp32-elf-{tool}"
        if candidate.is_file():
            return str(candidate)
    # fallback to common names on PATH
    for cand in (f"xtensa-esp32-elf-{tool}", tool):
        if shutil_which(cand):
            return cand
    return tool


def shutil_which(name: str) -> Optional[str]:
    for p in os.environ.get("PATH", "").split(os.pathsep):
        fp = Path(p) / name
        if fp.is_file() and os.access(str(fp), os.X_OK):
            return str(fp)
    return None


def find_latest_file(root: Path, pattern: str) -> Optional[Path]:
    files = list(root.rglob(pattern))
    if not files:
        return None
    files.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return files[0]


def parse_compile_usage(log_text: str) -> Tuple[Optional[int], Optional[float], Optional[int], Optional[float]]:
    flash_b = flash_pct = ram_b = ram_pct = None
    m_flash = re.search(r"Sketch uses\s+([\d,]+)\s+bytes.*\(([\d.]+)%\)", log_text)
    if m_flash:
        flash_b = int(m_flash.group(1).replace(",", ""))
        flash_pct = float(m_flash.group(2))
    m_ram = re.search(r"Global variables use\s+([\d,]+)\s+bytes.*\(([\d.]+)%\)", log_text)
    if m_ram:
        ram_b = int(m_ram.group(1).replace(",", ""))
        ram_pct = float(m_ram.group(2))
    return flash_b, flash_pct, ram_b, ram_pct


def parse_nm_symbols(nm_text: str) -> List[Symbol]:
    out: List[Symbol] = []
    sym_re = re.compile(r"^\s*([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+([A-Za-z])\s+(.+?)\s*$")
    for line in nm_text.splitlines():
        m = sym_re.match(line)
        if not m:
            continue
        size = int(m.group(2), 16)
        name = m.group(4)
        out.append(Symbol(size=size, name=name, sym_type=m.group(3)))
    return out


def parse_map_lib_contrib(map_text: str) -> List[Tuple[int, str, str]]:
    rows: List[Tuple[int, str, str]] = []
    for line in map_text.splitlines():
        if not any(x in line for x in ("libc.a", "libstdc++.a", "libm.a", "libgcc.a")):
            continue
        parts = line.split()
        if len(parts) < 4:
            continue
        section = parts[0]
        addr = parts[1]
        size_hex = parts[2]
        src = parts[3]
        if not section.startswith(".") or not addr.startswith("0x") or not size_hex.startswith("0x"):
            continue
        try:
            size = int(size_hex, 16)
        except ValueError:
            continue
        rows.append((size, section, src))
    rows.sort(key=lambda x: x[0], reverse=True)
    return rows


def scan_source_files(root: Path) -> Iterable[Path]:
    for p in root.rglob("*"):
        if p.suffix.lower() in SOURCE_EXTS and p.is_file():
            yield p


def extract_calls(text: str, fn_names: Sequence[str]) -> List[Tuple[str, str, int]]:
    name_alt = "|".join(re.escape(n) for n in fn_names)
    start_re = re.compile(rf"\b({name_alt})\s*\(")
    results: List[Tuple[str, str, int]] = []

    for m in start_re.finditer(text):
        fn = m.group(1)
        i = m.end() - 1  # position at '('
        depth = 0
        in_str = False
        esc = False
        start = i + 1
        j = i
        while j < len(text):
            ch = text[j]
            if in_str:
                if esc:
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ch == "\"":
                    in_str = False
            else:
                if ch == "\"":
                    in_str = True
                elif ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                    if depth == 0:
                        body = text[start:j]
                        line_no = text.count("\n", 0, m.start()) + 1
                        results.append((fn, body, line_no))
                        break
            j += 1
    return results


def split_call_args(call_body: str) -> List[str]:
    args: List[str] = []
    cur: List[str] = []
    depth = 0
    in_str = False
    esc = False

    for ch in call_body:
        if in_str:
            cur.append(ch)
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == "\"":
                in_str = False
            continue

        if ch == "\"":
            in_str = True
            cur.append(ch)
            continue
        if ch == "(":
            depth += 1
            cur.append(ch)
            continue
        if ch == ")":
            depth = max(0, depth - 1)
            cur.append(ch)
            continue
        if ch == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
            continue
        cur.append(ch)

    tail = "".join(cur).strip()
    if tail:
        args.append(tail)
    return args


def is_literal_string(expr: str) -> bool:
    e = expr.strip()
    return len(e) >= 2 and e[0] == "\"" and e[-1] == "\""


def decode_c_string_literal(expr: str) -> str:
    s = expr.strip()
    if not is_literal_string(s):
        return ""
    s = s[1:-1]
    # best-effort decoding (enough for length checks)
    s = s.replace(r"\\", "\\").replace(r"\"", "\"").replace(r"\n", "\n").replace(r"\t", "\t")
    return s


def rough_format_worst_len(fmt: str) -> int:
    # Conservative rough estimator for fixed-width placeholders.
    total = 0
    i = 0
    while i < len(fmt):
        if fmt[i] != "%":
            total += 1
            i += 1
            continue
        if i + 1 < len(fmt) and fmt[i + 1] == "%":
            total += 1
            i += 2
            continue

        j = i + 1
        while j < len(fmt) and fmt[j] in "-+ #0":
            j += 1
        while j < len(fmt) and fmt[j].isdigit():
            j += 1
        if j < len(fmt) and fmt[j] == ".":
            j += 1
            while j < len(fmt) and fmt[j].isdigit():
                j += 1
        while j < len(fmt) and fmt[j] in "hlLzjt":
            j += 1
        if j >= len(fmt):
            break
        conv = fmt[j]
        if conv in ("d", "i"):
            total += 12
        elif conv in ("u", "x", "X", "o"):
            total += 12
        elif conv in ("f", "F", "e", "E", "g", "G"):
            total += 24
        elif conv == "c":
            total += 1
        elif conv == "s":
            total += 64  # unknown / unbounded-ish unless precision is specified
        elif conv == "p":
            total += 18
        else:
            total += 8
        i = j + 1
    return total


def parse_array_sizes(text: str) -> Dict[str, int]:
    sizes: Dict[str, int] = {}
    # char buf[128];
    re_decl = re.compile(r"\bchar\s+([A-Za-z_]\w*)\s*\[\s*(\d+)\s*\]")
    for m in re_decl.finditer(text):
        sizes[m.group(1)] = int(m.group(2))
    return sizes


def format_string_findings(src_root: Path) -> List[Finding]:
    findings: List[Finding] = []
    fn_names = ("sprintf", "snprintf", "vsprintf", "vsnprintf", "printf", "fprintf")

    for path in scan_source_files(src_root):
        txt = path.read_text(encoding="utf-8", errors="ignore")
        arr_sizes = parse_array_sizes(txt)
        for fn, body, line in extract_calls(txt, fn_names):
            args = split_call_args(body)

            if fn in ("sprintf", "vsprintf"):
                msg = f"{fn} used; prefer bounded snprintf to avoid overflow risk"
                findings.append(Finding("HIGH", str(path), line, msg))
                if len(args) >= 2 and is_literal_string(args[1]):
                    fmt = decode_c_string_literal(args[1])
                    if args and args[0] in arr_sizes:
                        est = rough_format_worst_len(fmt)
                        cap = arr_sizes[args[0]]
                        if est >= cap:
                            findings.append(
                                Finding(
                                    "HIGH",
                                    str(path),
                                    line,
                                    f"Estimated formatted output ({est}) can exceed {args[0]}[{cap}]",
                                )
                            )
                continue

            if fn in ("snprintf", "vsnprintf"):
                if len(args) < 3:
                    findings.append(Finding("MEDIUM", str(path), line, f"{fn} call has <3 args (parse ambiguity)"))
                    continue
                size_arg = args[1]
                fmt_arg = args[2]
                if "sizeof" not in size_arg and not re.fullmatch(r"\d+", size_arg.strip()):
                    findings.append(
                        Finding("MEDIUM", str(path), line, f"{fn} size argument is not constant/sizeof: {size_arg.strip()}")
                    )
                if not is_literal_string(fmt_arg):
                    findings.append(
                        Finding("MEDIUM", str(path), line, f"{fn} format string is non-literal (harder to prove safety)")
                    )
                else:
                    fmt = decode_c_string_literal(fmt_arg)
                    # %s without precision can still create truncation/safety uncertainty in audits
                    if re.search(r"%(?!%)(?:[-+ #0]*\d*(?:\.\d+)?[hlLzjt]*)s", fmt) and "%. " not in fmt:
                        if "%." not in fmt:
                            findings.append(
                                Finding("LOW", str(path), line, f"{fn} uses %s without explicit precision: potential truncation risk")
                            )
            elif fn in ("printf", "fprintf"):
                fmt_idx = 0 if fn == "printf" else 1
                if len(args) > fmt_idx and not is_literal_string(args[fmt_idx]):
                    findings.append(
                        Finding("MEDIUM", str(path), line, f"{fn} format string is non-literal")
                    )

        for i, raw in enumerate(txt.splitlines(), start=1):
            if re.search(r"\bstrcpy\s*\(", raw):
                findings.append(Finding("HIGH", str(path), i, "strcpy used; consider bounded copy"))
            if re.search(r"\bstrcat\s*\(", raw):
                findings.append(Finding("HIGH", str(path), i, "strcat used; consider bounded append"))

    return findings


def dynamic_allocation_findings(src_root: Path, symbols: List[Symbol]) -> Tuple[List[str], List[Finding]]:
    linked_hits: List[str] = []
    symbol_patterns = [
        r"\bmalloc\b",
        r"\bcalloc\b",
        r"\brealloc\b",
        r"\bfree\b",
        r"\boperator new\b",
        r"\boperator delete\b",
        r"\bstd::basic_string\b",
        r"\bString::",
        r"\bpvPortMalloc\b",
        r"\bheap_caps_malloc\b",
    ]
    sym_re = re.compile("|".join(symbol_patterns))
    for s in symbols:
        if sym_re.search(s.name):
            linked_hits.append(f"{s.name} ({s.size} bytes)")
    linked_hits = sorted(set(linked_hits))

    findings: List[Finding] = []
    src_patterns = [
        (r"\bnew\s+[^;]+", "MEDIUM", "operator new usage"),
        (r"\bdelete\s*(?:\[\])?", "MEDIUM", "operator delete usage"),
        (r"\bmalloc\s*\(", "HIGH", "malloc usage"),
        (r"\bcalloc\s*\(", "HIGH", "calloc usage"),
        (r"\brealloc\s*\(", "HIGH", "realloc usage"),
        (r"\bfree\s*\(", "MEDIUM", "free usage"),
        (r"\bString\s+[A-Za-z_]\w*", "LOW", "Arduino String object (heap-alloc prone)"),
    ]
    compiled = [(re.compile(p), sev, msg) for p, sev, msg in src_patterns]

    for p in scan_source_files(src_root):
        for i, raw in enumerate(p.read_text(encoding="utf-8", errors="ignore").splitlines(), start=1):
            for rex, sev, msg in compiled:
                if rex.search(raw):
                    findings.append(Finding(sev, str(p), i, msg))

    return linked_hits, findings


def parse_numeric_constants(path: Path) -> Dict[str, int]:
    values: Dict[str, int] = {}
    txt = path.read_text(encoding="utf-8", errors="ignore")
    # constexpr size_t X = 16;
    for m in re.finditer(r"\b(?:constexpr|static\s+constexpr)\s+[A-Za-z_]\w*\s+([A-Za-z_]\w*)\s*=\s*([0-9]+)u?\s*;", txt):
        values[m.group(1)] = int(m.group(2))
    # #define X 16u
    for m in re.finditer(r"^\s*#define\s+([A-Za-z_]\w*)\s+([0-9]+)u?\s*$", txt, flags=re.MULTILINE):
        values.setdefault(m.group(1), int(m.group(2)))
    return values


def parse_state_json_guards(path: Path) -> Dict[str, bool]:
    txt = path.read_text(encoding="utf-8", errors="ignore")
    return {
        "measure_json": "measureJson(doc)" in txt,
        "overflow_check": "doc.overflowed()" in txt,
        "fits_check": "fits = (required + 1) <= outSize" in txt or "(required + 1) <= outSize" in txt,
    }


def collect_telemetry_paths(telemetry_registry: Path) -> List[str]:
    txt = telemetry_registry.read_text(encoding="utf-8", errors="ignore")
    paths = set(re.findall(r'writeAtPath\s*\(\s*root\s*,\s*"([^"]+)"', txt))
    return sorted(paths)


def guess_string_len(path: str, consts: Dict[str, int], extras: Dict[str, int]) -> int:
    def c(name: str, default: int) -> int:
        return consts.get(name, extras.get(name, default))

    if path in ("ota_last_ts", "ota_last_success_ts"):
        return 20  # YYYY-MM-DDTHH:MM:SSZ
    if "sha256" in path:
        return max(1, c("OTA_SHA256_MAX", 65) - 1)
    if path.endswith(".url") or path.endswith("url"):
        return max(1, c("OTA_URL_MAX", 256) - 1)
    if "request_id" in path:
        return max(1, c("OTA_REQUEST_ID_MAX", 48) - 1)
    if path.endswith("ota_error"):
        return max(1, c("OTA_ERROR_MAX", 64) - 1)
    if path.endswith("ota_state"):
        return max(1, c("OTA_STATE_MAX", 16) - 1)
    if path.endswith("ota_target_version") or path.endswith("latest_version"):
        return max(1, c("OTA_TARGET_VERSION_MAX", 16) - 1)
    if path.endswith("fw_version") or path.endswith("installed_version") or path.endswith("device.fw"):
        return max(1, c("DEVICE_FW_VERSION_MAX", 16) - 1)
    if path.endswith("time.status"):
        return max(1, c("TIME_STATUS_MAX", 16) - 1)
    if path.endswith("ota.result.status"):
        return max(1, c("OTA_STATUS_MAX", 16) - 1)
    if path.endswith("ota.result.message"):
        return max(1, c("OTA_MESSAGE_MAX", 64) - 1)
    if path.endswith("last_cmd.request_id"):
        return extras.get("kMaxLastCmdId", 40)
    if path.endswith("last_cmd.type"):
        return extras.get("kMaxLastCmdType", 24)
    if path.endswith("last_cmd.message"):
        return extras.get("kMaxLastCmdMsg", 64)
    if path.endswith(".ip"):
        return extras.get("kMaxWifiIp", 16)
    return 32


def guess_value_for_path(path: str, consts: Dict[str, int], extras: Dict[str, int]):
    p = path
    bool_tokens = (
        "connected",
        "valid",
        "inverted",
        "update_available",
        "ota.force",
        "ota.reboot",
    )
    if p in ("ota_last_ts", "ota_last_success_ts"):
        return "2026-01-01T00:00:00Z"
    if any(tok in p for tok in bool_tokens):
        return True
    numeric_tokens = (
        "schema",
        "ts",
        "rssi",
        ".raw",
        ".dry",
        ".wet",
        "min_diff",
        ".percent",
        ".liters",
        ".centimeters",
        "tank_volume_l",
        "rod_length_cm",
        "simulation_mode",
        "ota_progress",
        "started_ts",
        "completed_ts",
        "last_attempt_s",
        "last_success_s",
        "next_retry_s",
    )
    if any(tok in p for tok in numeric_tokens):
        return 4294967295
    if p.endswith("last_cmd.status"):
        return "applied"
    return "X" * guess_string_len(path, consts, extras)


def set_nested(root: Dict[str, object], dotted: str, value: object) -> None:
    parts = dotted.split(".")
    node = root
    for key in parts[:-1]:
        cur = node.get(key)
        if not isinstance(cur, dict):
            cur = {}
            node[key] = cur
        node = cur
    node[parts[-1]] = value


def estimate_json_payload_size(
    telemetry_registry: Path,
    device_state_h: Path,
    state_json_cpp: Path,
    mqtt_transport_cpp: Path,
) -> Dict[str, object]:
    paths = collect_telemetry_paths(telemetry_registry)
    consts = parse_numeric_constants(device_state_h)
    extras = parse_numeric_constants(state_json_cpp)
    guards = parse_state_json_guards(state_json_cpp)

    payload: Dict[str, object] = {}
    for p in paths:
        set_nested(payload, p, guess_value_for_path(p, consts, extras))

    payload_json = json.dumps(payload, separators=(",", ":"), ensure_ascii=True)
    observed_size = len(payload_json.encode("utf-8"))

    mqtt_txt = mqtt_transport_cpp.read_text(encoding="utf-8", errors="ignore")
    buffer_sizes = [int(m.group(1)) for m in re.finditer(r"static\s+char\s+buf\[(\d+)\]", mqtt_txt)]
    mqtt_buf = max(buffer_sizes) if buffer_sizes else 0

    return {
        "observed_synthetic_bytes": observed_size,
        "mqtt_state_buffer_bytes": mqtt_buf,
        "headroom_bytes": (mqtt_buf - observed_size) if mqtt_buf else None,
        "guard_checks": guards,
    }


def heavy_symbol_report(symbols: List[Symbol]) -> List[Symbol]:
    heavy_re = re.compile(
        r"(?:\bprintf\b|\bsnprintf\b|\bvsnprintf\b|\bvfprintf\b|\bscanf\b|"
        r"\bstd::basic_string\b|\bString::\b|\b__cxa\b|\boperator new\b|\boperator delete\b|"
        r"\bmalloc\b|\bcalloc\b|\brealloc\b|\bfree\b)",
        re.IGNORECASE,
    )
    hits = [s for s in symbols if heavy_re.search(s.name)]
    hits.sort(key=lambda s: s.size, reverse=True)
    return hits


def top_symbols(symbols: List[Symbol], n: int = 20) -> List[Symbol]:
    ordered = sorted(symbols, key=lambda s: s.size, reverse=True)
    return ordered[:n]


def main() -> int:
    ap = argparse.ArgumentParser(description="Compile + firmware risk audit")
    ap.add_argument("--sketch-dir", default="esp32/level_sensor", help="Sketch directory")
    ap.add_argument("--profile", default="release", help="Sketch profile name in sketch.yaml")
    ap.add_argument("--build-dir", default="build/firmware_audit", help="Audit artifact directory")
    ap.add_argument("--fw-version", default="audit-local", help="FW_VERSION define used for audit compile")
    ap.add_argument("--top", type=int, default=20, help="Top symbol rows to print")
    ap.add_argument("--strict", action="store_true", help="Exit non-zero on warnings")
    args = ap.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    sketch_dir = (repo_root / args.sketch_dir).resolve()
    sketch_yaml = sketch_dir / "sketch.yaml"
    build_root = (repo_root / args.build_dir).resolve()
    build_path = build_root / "build-path"
    report_dir = build_root / "reports"
    report_dir.mkdir(parents=True, exist_ok=True)
    build_path.mkdir(parents=True, exist_ok=True)

    if not shutil_which("arduino-cli"):
        fail("arduino-cli is not installed or not on PATH")

    base_fqbn = parse_sketch_fqbn(sketch_yaml, args.profile)
    compile_fqbn = compose_ci_fqbn(base_fqbn)

    map_file = build_path / "firmware.map"
    compile_log = report_dir / "compile.log"

    # Collect build properties to locate toolchain binutils.
    show_props_cmd = [
        "arduino-cli",
        "compile",
        "--fqbn",
        compile_fqbn,
        "--show-properties",
        str(sketch_dir),
    ]
    props_cp = run_cmd(show_props_cmd, cwd=repo_root, capture=True)
    if props_cp.returncode != 0:
        fail(f"Failed --show-properties:\n{props_cp.stdout}\n{props_cp.stderr}")
    props = parse_properties(props_cp.stdout)

    nm_tool = resolve_binutil(props, "nm")
    size_tool = resolve_binutil(props, "size")
    objdump_tool = resolve_binutil(props, "objdump")

    compile_cmd = [
        "arduino-cli",
        "compile",
        "--fqbn",
        compile_fqbn,
        "--build-path",
        str(build_path),
        "--build-property",
        f'compiler.cpp.extra_flags=-DFW_VERSION=\\"{args.fw_version}\\"',
        "--build-property",
        f"compiler.c.elf.extra_flags=-Wl,-Map,{map_file}",
        str(sketch_dir),
    ]
    cp = run_cmd(compile_cmd, cwd=repo_root, capture=True)
    compile_log.write_text(cp.stdout + "\n" + cp.stderr, encoding="utf-8")
    if cp.returncode != 0:
        fail(f"Compile failed. See {compile_log}")

    elf = find_latest_file(build_path, "*.elf")
    if not elf:
        fail(f"No ELF artifact found under {build_path}")
    map_path = map_file if map_file.is_file() else find_latest_file(build_path, "*.map")

    nm_cp = run_cmd([nm_tool, "-C", "-S", "--size-sort", str(elf)], cwd=repo_root, capture=True)
    size_cp = run_cmd([size_tool, str(elf)], cwd=repo_root, capture=True)
    sizeA_cp = run_cmd([size_tool, "-A", "-d", str(elf)], cwd=repo_root, capture=True)
    objdump_cp = run_cmd([objdump_tool, "-t", str(elf)], cwd=repo_root, capture=True)

    (report_dir / "symbols_nm.txt").write_text(nm_cp.stdout + "\n" + nm_cp.stderr, encoding="utf-8")
    (report_dir / "size.txt").write_text(size_cp.stdout + "\n" + size_cp.stderr, encoding="utf-8")
    (report_dir / "size_sections.txt").write_text(sizeA_cp.stdout + "\n" + sizeA_cp.stderr, encoding="utf-8")
    (report_dir / "symbols_objdump.txt").write_text(objdump_cp.stdout + "\n" + objdump_cp.stderr, encoding="utf-8")

    symbols = parse_nm_symbols(nm_cp.stdout) if nm_cp.returncode == 0 else []
    heavy_syms = heavy_symbol_report(symbols)
    top_syms = top_symbols(symbols, n=args.top)

    map_rows: List[Tuple[int, str, str]] = []
    if map_path and map_path.is_file():
        map_rows = parse_map_lib_contrib(map_path.read_text(encoding="utf-8", errors="ignore"))
        (report_dir / "linker_map_path.txt").write_text(str(map_path) + "\n", encoding="utf-8")
    else:
        (report_dir / "linker_map_path.txt").write_text("missing\n", encoding="utf-8")

    flash_b, flash_pct, ram_b, ram_pct = parse_compile_usage(cp.stdout + "\n" + cp.stderr)
    linked_alloc_hits, src_alloc_findings = dynamic_allocation_findings(repo_root / "esp32", symbols)
    fmt_findings = format_string_findings(repo_root / "esp32")

    json_check = estimate_json_payload_size(
        telemetry_registry=repo_root / "esp32/level_sensor/telemetry_registry.cpp",
        device_state_h=repo_root / "esp32/level_sensor/device_state.h",
        state_json_cpp=repo_root / "esp32/level_sensor/state_json.cpp",
        mqtt_transport_cpp=repo_root / "esp32/level_sensor/mqtt_transport.cpp",
    )

    warnings: List[str] = []
    if not map_path:
        warnings.append("Linker map was not generated; map-based library attribution is unavailable.")
    if flash_b is None:
        warnings.append("Could not parse flash usage from compile output.")
    if ram_b is None:
        warnings.append("Could not parse RAM usage from compile output.")
    if json_check["mqtt_state_buffer_bytes"] and json_check["headroom_bytes"] is not None:
        if json_check["headroom_bytes"] < 0:
            warnings.append("Synthetic telemetry payload exceeds MQTT state buffer.")
        elif json_check["headroom_bytes"] < 128:
            warnings.append("Synthetic telemetry payload has low headroom (<128 bytes).")
    guard_checks = json_check["guard_checks"]
    if not all(guard_checks.values()):
        warnings.append("state_json guard checks are incomplete (measureJson/fits/overflow).")

    summary: List[str] = []
    summary.append("=== Firmware Build Audit ===")
    summary.append(f"Sketch: {sketch_dir}")
    summary.append(f"Profile: {args.profile}")
    summary.append(f"FQBN (CI-style): {compile_fqbn}")
    summary.append(f"Build path: {build_path}")
    summary.append(f"ELF: {elf}")
    summary.append(f"Linker map: {map_path if map_path else 'missing'}")
    summary.append(f"Reports dir: {report_dir}")
    summary.append("")
    summary.append("== Size Summary ==")
    if flash_b is not None:
        summary.append(f"Flash usage: {flash_b} bytes ({flash_pct:.2f}%)")
    else:
        summary.append("Flash usage: unavailable")
    if ram_b is not None:
        summary.append(f"RAM usage:   {ram_b} bytes ({ram_pct:.2f}%)")
    else:
        summary.append("RAM usage:   unavailable")
    summary.append("")
    summary.append(f"== Top {args.top} Symbols By Size ==")
    for s in top_syms:
        summary.append(f"- {s.size:8d}  {s.name}")
    if not top_syms:
        summary.append("- unavailable")
    summary.append("")
    summary.append("== Heavy libc/libstdc++ Related Symbols ==")
    for s in heavy_syms[: max(10, args.top)]:
        summary.append(f"- {s.size:8d}  {s.name}")
    if not heavy_syms:
        summary.append("- none detected")
    summary.append("")
    summary.append("== Map Contributions (libc/libstdc++/libm/libgcc) ==")
    for size, section, src in map_rows[: max(10, args.top)]:
        summary.append(f"- {size:8d}  {section:24s} {src}")
    if not map_rows:
        summary.append("- unavailable")
    summary.append("")
    summary.append("== Dynamic Allocation Indicators ==")
    if linked_alloc_hits:
        summary.append("Linked symbols:")
        for h in linked_alloc_hits[:50]:
            summary.append(f"- {h}")
    else:
        summary.append("Linked symbols: none detected")
    if src_alloc_findings:
        summary.append("Source patterns:")
        for f in src_alloc_findings[:80]:
            summary.append(f"- [{f.severity}] {f.path}:{f.line} {f.message}")
    else:
        summary.append("Source patterns: none detected")
    summary.append("")
    summary.append("== Format/String Risk Findings ==")
    if fmt_findings:
        for f in fmt_findings[:120]:
            summary.append(f"- [{f.severity}] {f.path}:{f.line} {f.message}")
    else:
        summary.append("- none detected")
    summary.append("")
    summary.append("== JSON Capacity Check ==")
    summary.append(f"Synthetic telemetry payload bytes: {json_check['observed_synthetic_bytes']}")
    summary.append(f"MQTT state buffer bytes:          {json_check['mqtt_state_buffer_bytes']}")
    summary.append(f"Headroom bytes:                   {json_check['headroom_bytes']}")
    summary.append(
        "state_json guards: "
        f"measureJson={guard_checks['measure_json']} "
        f"fits={guard_checks['fits_check']} "
        f"overflow={guard_checks['overflow_check']}"
    )
    summary.append("")
    summary.append("== Warnings ==")
    if warnings:
        for w in warnings:
            summary.append(f"- {w}")
    else:
        summary.append("- none")

    summary_text = "\n".join(summary) + "\n"
    print(summary_text)
    (report_dir / "audit_summary.txt").write_text(summary_text, encoding="utf-8")

    if args.strict and (warnings or fmt_findings):
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
