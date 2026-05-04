#!/usr/bin/env python3

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


def assert_true(condition, message):
    if not condition:
        raise AssertionError(message)


def assert_equal(expected, actual, message):
    if expected != actual:
        raise AssertionError(f"{message} (expected: {expected}, actual: {actual})")


def assert_file_exists(path: Path):
    assert_true(path.is_file(), f"missing file: {path}")


def load_json(path: Path):
    assert_file_exists(path)
    return json.loads(path.read_text(encoding="utf-8"))


def get_text(path: Path):
    assert_file_exists(path)
    return path.read_text(encoding="utf-8")


def find_default_build_dir(dsview_root: Path):
    for candidate in (
        dsview_root / "build-mingw-static-local",
        dsview_root / "build.dir",
    ):
        if (candidate / "CMakeCache.txt").is_file():
            return candidate
    return dsview_root / "build-mingw-static-local"


def path_to_msys(path: Path):
    text = path.resolve().as_posix()
    if re.match(r"^[A-Za-z]:/", text):
        return f"/{text[0].lower()}{text[2:]}"
    return text


class ContractHarness:
    def __init__(self, args):
        self.args = args
        self.script_dir = Path(__file__).resolve().parent
        self.cli_dir = self.script_dir.parent
        self.dsview_root = self.cli_dir.parent
        self.repo_root = self.dsview_root.parent.parent

        exe_name = "dsview-cli.exe" if os.name == "nt" else "dsview-cli"
        self.binary_path = Path(args.binary_path).resolve() if args.binary_path else (self.dsview_root / "build.dir" / exe_name).resolve()
        self.build_dir = Path(args.build_dir).resolve() if args.build_dir else find_default_build_dir(self.dsview_root).resolve()
        self.output_root = Path(args.output_root).resolve() if args.output_root else (self.repo_root / ".tmp" / "cli-contract").resolve()
        self.standalone_build_dir = Path(args.standalone_build_dir).resolve() if args.standalone_build_dir else (self.dsview_root / "build-cli-only-local").resolve()

        if self.output_root.exists():
            shutil.rmtree(self.output_root)
        self.output_root.mkdir(parents=True, exist_ok=True)

        self.logs_dir = self.output_root / "logs"
        self.cases_dir = self.output_root / "cases"
        self.logs_dir.mkdir(parents=True, exist_ok=True)
        self.cases_dir.mkdir(parents=True, exist_ok=True)
        self.case_results = []

    def run_process(self, cmd, *, cwd=None, timeout=None, check=False, capture=False):
        result = subprocess.run(
            cmd,
            cwd=str(cwd) if cwd else None,
            timeout=timeout,
            check=check,
            capture_output=capture,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        return result

    def invoke_ninja_build_fallback(self, build_dir_path: Path, target_name: str):
        bash_path = Path(r"C:\Users\yongxiang\scoop\apps\msys2\current\usr\bin\bash.exe")
        mingw_bin = "/c/Users/yongxiang/scoop/apps/msys2/current/mingw64/bin"

        if not bash_path.is_file():
            raise RuntimeError("root dsview-cli build failed and MSYS2 bash fallback is unavailable")
        if not (build_dir_path / "build.ninja").is_file():
            raise RuntimeError("root dsview-cli build failed and no ninja fallback is available")

        print("[build] fallback via MSYS2 bash + ninja")
        build_dir_unix = path_to_msys(build_dir_path)
        command = f"export PATH={mingw_bin}:$PATH; cd '{build_dir_unix}'; ninja {target_name} -j1"
        result = self.run_process([str(bash_path), "-lc", command], cwd=self.repo_root)
        if result.returncode != 0:
            raise RuntimeError("fallback ninja build failed")

    def build_root_cli(self):
        print(f"[build] cmake --build {self.build_dir} --target dsview-cli")
        result = self.run_process(["cmake", "--build", str(self.build_dir), "--target", "dsview-cli"], cwd=self.repo_root)
        if result.returncode != 0:
            self.invoke_ninja_build_fallback(self.build_dir, "dsview-cli")

    def build_standalone_cli(self):
        print("[build] standalone cli-only configure/build")
        result = self.run_process(["cmake", "-S", str(self.cli_dir), "-B", str(self.standalone_build_dir), "-G", "Ninja"], cwd=self.repo_root)
        if result.returncode != 0:
            raise RuntimeError("standalone cli-only configure failed")
        result = self.run_process(["cmake", "--build", str(self.standalone_build_dir)], cwd=self.repo_root)
        if result.returncode != 0:
            raise RuntimeError("standalone cli-only build failed")

    def invoke_cli_case(self, name, arguments, timeout_sec=120):
        stdout_path = self.logs_dir / f"{name}.stdout.txt"
        stderr_path = self.logs_dir / f"{name}.stderr.txt"

        try:
            result = self.run_process(
                [str(self.binary_path), *arguments],
                cwd=self.repo_root,
                timeout=timeout_sec,
                capture=True,
            )
        except subprocess.TimeoutExpired as exc:
            stdout_text = exc.stdout if isinstance(exc.stdout, str) else (exc.stdout or b"").decode("utf-8", errors="replace")
            stderr_text = exc.stderr if isinstance(exc.stderr, str) else (exc.stderr or b"").decode("utf-8", errors="replace")
            stdout_path.write_text(stdout_text, encoding="utf-8")
            stderr_path.write_text(stderr_text, encoding="utf-8")
            raise RuntimeError(f"case '{name}' timed out after {timeout_sec}s") from exc

        stdout_path.write_text(result.stdout or "", encoding="utf-8")
        stderr_path.write_text(result.stderr or "", encoding="utf-8")
        return {
            "name": name,
            "exit_code": result.returncode,
            "stdout_path": stdout_path,
            "stderr_path": stderr_path,
            "arguments": arguments,
        }

    def record_pass(self, name):
        self.case_results.append({"name": name, "status": "PASS"})
        print(f"[pass] {name}")

    def print_summary(self):
        print("\nContract test summary\n")
        width = max(len("Name"), *(len(item["name"]) for item in self.case_results))
        print(f'{"Name".ljust(width)}  Status')
        print(f'{"-" * width}  ------')
        for item in self.case_results:
            print(f'{item["name"].ljust(width)}  {item["status"]}')
        print(f"\nArtifacts: {self.output_root}")

    def run(self):
        if not self.args.skip_build:
            self.build_root_cli()
            if self.args.run_standalone_build:
                self.build_standalone_cli()

        assert_file_exists(self.binary_path)

        scan_json = self.cases_dir / "scan.json"
        scan = self.invoke_cli_case("scan", ["--scan", "--json", str(scan_json)])
        assert_equal(0, scan["exit_code"], "scan should succeed")
        scan_obj = load_json(scan_json)
        assert_true(scan_obj["success"], "scan json should report success")
        assert_equal("scan", scan_obj["command"], "scan command mismatch")
        assert_true(len(scan_obj["result"]) >= 1, "scan should return at least one device")
        scan_drivers = [item["driver"] for item in scan_obj["result"] if "driver" in item]
        assert_true("virtual-demo" in scan_drivers, "scan should include virtual-demo")
        self.record_pass("scan")

        show_json = self.cases_dir / "show.json"
        show = self.invoke_cli_case("show", ["--show", "-d", "virtual-demo", "--json", str(show_json)])
        assert_equal(0, show["exit_code"], "show should succeed")
        show_obj = load_json(show_json)
        assert_true(show_obj["success"], "show json should report success")
        assert_equal("show", show_obj["command"], "show command mismatch")
        assert_equal("virtual-demo", show_obj["result"]["driver"], "show driver mismatch")
        self.record_pass("show")

        show_missing_json = self.cases_dir / "show_missing.json"
        show_missing = self.invoke_cli_case("show_unknown_driver", ["--show", "-d", "missing-driver", "--json", str(show_missing_json)])
        assert_true(show_missing["exit_code"] != 0, "show unknown driver should fail")
        show_missing_obj = load_json(show_missing_json)
        assert_true(not show_missing_obj["success"], "show unknown driver json should report failure")
        assert_equal("show", show_missing_obj["command"], "show unknown driver command mismatch")
        assert_true("failed to open selected device" in show_missing_obj["error"], "show unknown driver error text mismatch")
        self.record_pass("show_unknown_driver")

        get_json = self.cases_dir / "get.json"
        get_case = self.invoke_cli_case("get", ["--get", "samplerate", "-d", "virtual-demo", "--json", str(get_json)])
        assert_equal(0, get_case["exit_code"], "get should succeed")
        get_obj = load_json(get_json)
        assert_true(get_obj["success"], "get json should report success")
        assert_equal("get", get_obj["command"], "get command mismatch")
        assert_equal("1000000", get_obj["result"]["values"]["samplerate"], "virtual-demo samplerate mismatch")
        self.record_pass("get")

        get_unknown_json = self.cases_dir / "get_unknown.json"
        get_unknown = self.invoke_cli_case("get_unknown_option", ["--get", "missing_option", "-d", "virtual-demo", "--json", str(get_unknown_json)])
        assert_true(get_unknown["exit_code"] != 0, "get unknown option should fail")
        get_unknown_obj = load_json(get_unknown_json)
        assert_true(not get_unknown_obj["success"], "get unknown option json should report failure")
        assert_equal("get", get_unknown_obj["command"], "get unknown option command mismatch")
        assert_true("unknown device option" in get_unknown_obj["error"], "get unknown option error text mismatch")
        self.record_pass("get_unknown_option")

        set_json = self.cases_dir / "set.json"
        set_case = self.invoke_cli_case("set", ["--set", "-d", "virtual-demo", "-c", "samplerate=1M", "--json", str(set_json)])
        assert_equal(0, set_case["exit_code"], "set should succeed")
        set_obj = load_json(set_json)
        assert_true(set_obj["success"], "set json should report success")
        assert_equal("set", set_obj["command"], "set command mismatch")
        assert_equal("1M", set_obj["result"]["values"]["samplerate"], "set samplerate echo mismatch")
        self.record_pass("set")

        fixture_dsl = self.cases_dir / "fixture.dsl"
        capture_meta = self.cases_dir / "capture.meta.json"
        capture_json = self.cases_dir / "capture.json"
        capture = self.invoke_cli_case(
            "capture_dsl",
            [
                "-d", "virtual-demo",
                "-c", "samplerate=1M",
                "--samples", "65536",
                "-C", "0=CH0,1=CH1,2=CH2,3=CH3",
                "-O", "dsl",
                "-o", str(fixture_dsl),
                "--meta", str(capture_meta),
                "--json", str(capture_json),
            ],
        )
        assert_equal(0, capture["exit_code"], "dsl capture should succeed")
        assert_file_exists(fixture_dsl)
        assert_file_exists(capture_meta)
        capture_obj = load_json(capture_json)
        capture_meta_obj = load_json(capture_meta)
        assert_true(capture_obj["success"], "capture json should report success")
        assert_equal("capture", capture_obj["command"], "capture command mismatch")
        assert_equal("dsl", capture_obj["result"]["output_format"], "capture output format mismatch")
        assert_equal(str(capture_meta), capture_obj["result"]["meta"], "capture meta path mismatch")
        assert_true(int(capture_obj["result"]["samples"]) > 0, "capture should produce samples")
        assert_equal(1000000, int(capture_meta_obj["samplerate"]), "capture meta samplerate mismatch")
        assert_equal("logic", capture_meta_obj["mode"], "capture meta mode mismatch")
        assert_equal(1, int(capture_meta_obj["unitsize"]), "capture meta unitsize mismatch")
        capture_samples = int(capture_obj["result"]["samples"])
        self.record_pass("capture_dsl")

        fixture_sr = self.cases_dir / "fixture.sr"
        export_sr_json = self.cases_dir / "export_sr.json"
        export_sr = self.invoke_cli_case(
            "export_srzip",
            ["-i", str(fixture_dsl), "-o", str(fixture_sr), "-O", "srzip", "--json", str(export_sr_json)],
        )
        assert_equal(0, export_sr["exit_code"], "dsl -> srzip export should succeed")
        assert_file_exists(fixture_sr)
        export_sr_obj = load_json(export_sr_json)
        assert_true(export_sr_obj["success"], "dsl -> srzip json should report success")
        assert_equal("export", export_sr_obj["command"], "export command mismatch")
        assert_equal("srzip", export_sr_obj["result"]["output_format"], "srzip export format mismatch")
        assert_equal(capture_samples, int(export_sr_obj["result"]["samples"]), "dsl -> srzip sample count drift")
        self.record_pass("export_srzip")

        roundtrip_dsl = self.cases_dir / "roundtrip.dsl"
        export_dsl_json = self.cases_dir / "export_dsl.json"
        export_dsl = self.invoke_cli_case(
            "export_dsl",
            ["-i", str(fixture_sr), "-o", str(roundtrip_dsl), "-O", "dsl", "--json", str(export_dsl_json)],
        )
        assert_equal(0, export_dsl["exit_code"], "srzip -> dsl export should succeed")
        assert_file_exists(roundtrip_dsl)
        export_dsl_obj = load_json(export_dsl_json)
        assert_true(export_dsl_obj["success"], "srzip -> dsl json should report success")
        assert_equal("dsl", export_dsl_obj["result"]["output_format"], "dsl export format mismatch")
        assert_equal(capture_samples, int(export_dsl_obj["result"]["samples"]), "srzip -> dsl sample count drift")
        self.record_pass("export_dsl")

        export_dsl_missing_json = self.cases_dir / "export_dsl_missing_channel.json"
        export_dsl_missing = self.invoke_cli_case(
            "export_dsl_missing_channel",
            ["-i", str(fixture_sr), "-o", str(self.cases_dir / "missing_channel.dsl"), "-O", "dsl", "-C", "7", "--json", str(export_dsl_missing_json)],
        )
        assert_true(export_dsl_missing["exit_code"] != 0, "srzip -> dsl missing channel should fail")
        export_dsl_missing_obj = load_json(export_dsl_missing_json)
        assert_true(not export_dsl_missing_obj["success"], "srzip missing channel json should report failure")
        assert_equal("export", export_dsl_missing_obj["command"], "srzip missing channel command mismatch")
        assert_true("selected logic channel not found in srzip input" in export_dsl_missing_obj["error"], "srzip missing channel error text mismatch")
        self.record_pass("export_dsl_missing_channel")

        decode_csv = self.cases_dir / "offline_uart.csv"
        decode_json = self.cases_dir / "offline_decode.json"
        offline_decode = self.invoke_cli_case(
            "offline_decode",
            [
                "-i", str(fixture_dsl),
                "-P", "uart:rx=CH2:baudrate=115200",
                "--decode-output", str(decode_csv),
                "--json", str(decode_json),
            ],
        )
        assert_equal(0, offline_decode["exit_code"], "offline decode should succeed")
        assert_file_exists(decode_csv)
        offline_decode_obj = load_json(decode_json)
        assert_true(offline_decode_obj["success"], "offline decode json should report success")
        assert_equal("decode", offline_decode_obj["command"], "offline decode command mismatch")
        assert_equal(1, int(offline_decode_obj["result"]["stack_count"]), "offline decode stack count mismatch")
        assert_true(int(offline_decode_obj["result"]["rows_written"]) > 0, "offline decode should emit rows")
        assert_equal(1, len(offline_decode_obj["result"]["stacks"]), "offline decode should report one stack")
        assert_true(offline_decode_obj["result"]["stacks"][0]["success"], "offline decode stack should succeed")
        assert_equal("csv", offline_decode_obj["result"]["stacks"][0]["output_format"], "offline decode output format mismatch")
        first_line = decode_csv.read_text(encoding="utf-8").splitlines()[0]
        assert_true(re.match(r"^Id,Time\[ns\],", first_line) is not None, "offline decode header mismatch")
        self.record_pass("offline_decode")

        decode_bad_channel_csv = self.cases_dir / "offline_bad_channel.csv"
        decode_bad_channel_json = self.cases_dir / "offline_bad_channel.json"
        decode_bad_channel = self.invoke_cli_case(
            "offline_decode_bad_channel",
            [
                "-i", str(fixture_dsl),
                "-P", "uart:rx=CH99:baudrate=115200",
                "--decode-output", str(decode_bad_channel_csv),
                "--json", str(decode_bad_channel_json),
            ],
        )
        assert_true(decode_bad_channel["exit_code"] != 0, "offline decode bad channel should fail")
        decode_bad_channel_obj = load_json(decode_bad_channel_json)
        assert_true(not decode_bad_channel_obj["success"], "offline decode bad channel json should report failure")
        assert_equal("decode", decode_bad_channel_obj["command"], "offline decode bad channel command mismatch")
        assert_true("root decoder channel binding does not match the input logic channels" in decode_bad_channel_obj["error"], "offline decode bad channel error text mismatch")
        self.record_pass("offline_decode_bad_channel")

        parallel_csv = self.cases_dir / "parallel_uart_a.csv"
        parallel_txt = self.cases_dir / "parallel_uart_b.txt"
        parallel_json = self.cases_dir / "parallel_decode.json"
        parallel_decode = self.invoke_cli_case(
            "offline_parallel_decode",
            [
                "-i", str(fixture_dsl),
                "-P", "uart:rx=CH2:baudrate=115200",
                "--decode-output", str(parallel_csv),
                "-P", "uart:rx=CH2:baudrate=115200",
                "--decode-output", str(parallel_txt),
                "--json", str(parallel_json),
            ],
        )
        assert_equal(0, parallel_decode["exit_code"], "parallel offline decode should succeed")
        assert_file_exists(parallel_csv)
        assert_file_exists(parallel_txt)
        parallel_obj = load_json(parallel_json)
        assert_true(parallel_obj["success"], "parallel decode json should report success")
        assert_equal(2, int(parallel_obj["result"]["stack_count"]), "parallel decode stack count mismatch")
        assert_equal(2, len(parallel_obj["result"]["stacks"]), "parallel decode stacks array mismatch")
        assert_true(parallel_obj["result"]["stacks"][0]["success"] and parallel_obj["result"]["stacks"][1]["success"], "parallel decode stacks should succeed")
        assert_true(int(parallel_obj["result"]["stacks"][0]["rows_written"]) > 0, "parallel decode stack #1 should emit rows")
        assert_equal(get_text(parallel_csv), get_text(parallel_txt), "parallel decode outputs should match for identical stacks")
        self.record_pass("offline_parallel_decode")

        live_decode_csv = self.cases_dir / "live_uart.csv"
        live_decode_json = self.cases_dir / "live_decode.json"
        live_decode = self.invoke_cli_case(
            "live_decode",
            [
                "-d", "virtual-demo",
                "-c", "samplerate=1M",
                "--samples", "65536",
                "-C", "0=CH0,1=CH1,2=CH2,3=CH3",
                "-P", "uart:rx=CH2:baudrate=115200",
                "--decode-output", str(live_decode_csv),
                "--json", str(live_decode_json),
            ],
        )
        assert_equal(0, live_decode["exit_code"], "live decode should succeed")
        assert_file_exists(live_decode_csv)
        live_decode_obj = load_json(live_decode_json)
        assert_true(live_decode_obj["success"], "live decode json should report success")
        assert_equal("live", live_decode_obj["result"]["input_source"], "live decode source mismatch")
        assert_equal(1, int(live_decode_obj["result"]["stack_count"]), "live decode stack count mismatch")
        assert_true(int(live_decode_obj["result"]["rows_written"]) > 0, "live decode should emit rows")
        self.record_pass("live_decode")

        live_parallel_csv = self.cases_dir / "live_parallel_uart_a.csv"
        live_parallel_txt = self.cases_dir / "live_parallel_uart_b.txt"
        live_parallel_json = self.cases_dir / "live_parallel_decode.json"
        live_parallel = self.invoke_cli_case(
            "live_parallel_decode",
            [
                "-d", "virtual-demo",
                "-c", "samplerate=1M",
                "--samples", "65536",
                "-C", "0=CH0,1=CH1,2=CH2,3=CH3",
                "-P", "uart:rx=CH2:baudrate=115200",
                "--decode-output", str(live_parallel_csv),
                "-P", "uart:rx=CH2:baudrate=115200",
                "--decode-output", str(live_parallel_txt),
                "--json", str(live_parallel_json),
            ],
        )
        assert_equal(0, live_parallel["exit_code"], "live parallel decode should succeed")
        assert_file_exists(live_parallel_csv)
        assert_file_exists(live_parallel_txt)
        live_parallel_obj = load_json(live_parallel_json)
        assert_true(live_parallel_obj["success"], "live parallel decode json should report success")
        assert_equal(2, int(live_parallel_obj["result"]["stack_count"]), "live parallel decode stack count mismatch")
        assert_equal(get_text(live_parallel_csv), get_text(live_parallel_txt), "live parallel decode outputs should match for identical stacks")
        self.record_pass("live_parallel_decode")

        mismatch_json = self.cases_dir / "mismatch_decode.json"
        mismatch = self.invoke_cli_case(
            "decode_output_mismatch",
            [
                "-i", str(fixture_dsl),
                "-P", "uart:rx=CH2:baudrate=115200",
                "-P", "uart:rx=CH2:baudrate=115200",
                "--decode-output", str(self.cases_dir / "only_one.csv"),
                "--json", str(mismatch_json),
            ],
        )
        assert_true(mismatch["exit_code"] != 0, "decode-output mismatch should fail")
        mismatch_obj = load_json(mismatch_json)
        assert_true(not mismatch_obj["success"], "decode-output mismatch json should report failure")
        assert_equal("decode", mismatch_obj["command"], "mismatch command mismatch")
        assert_true("one --decode-output for each -P" in mismatch_obj["error"], "mismatch error text mismatch")
        self.record_pass("decode_output_mismatch")

        selector_conflict_json = self.cases_dir / "selector_conflict.json"
        selector_conflict = self.invoke_cli_case(
            "selector_conflict",
            [
                "--scan",
                "--show",
                "--json", str(selector_conflict_json),
            ],
        )
        assert_true(selector_conflict["exit_code"] != 0, "selector conflict should fail")
        selector_conflict_obj = load_json(selector_conflict_json)
        assert_true(not selector_conflict_obj["success"], "selector conflict json should report failure")
        assert_equal("command", selector_conflict_obj["command"], "selector conflict command mismatch")
        assert_true("top-level command selectors cannot be combined" in selector_conflict_obj["error"], "selector conflict error text mismatch")
        self.record_pass("selector_conflict")

        scan_mix_json = self.cases_dir / "scan_mix.json"
        scan_mix = self.invoke_cli_case(
            "scan_with_capture_option",
            [
                "--scan",
                "-o", str(self.cases_dir / "unexpected.sr"),
                "--json", str(scan_mix_json),
            ],
        )
        assert_true(scan_mix["exit_code"] != 0, "scan/capture mix should fail")
        scan_mix_obj = load_json(scan_mix_json)
        assert_true(not scan_mix_obj["success"], "scan/capture mix json should report failure")
        assert_equal("scan", scan_mix_obj["command"], "scan/capture mix command mismatch")
        assert_true("scan mode cannot be mixed" in scan_mix_obj["error"], "scan/capture mix error text mismatch")
        self.record_pass("scan_with_capture_option")

        self.print_summary()


def parse_args():
    parser = argparse.ArgumentParser(description="Contract-test harness for dsview-cli.")
    parser.add_argument("--binary-path")
    parser.add_argument("--build-dir")
    parser.add_argument("--output-root")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--run-standalone-build", action="store_true")
    parser.add_argument("--standalone-build-dir")
    return parser.parse_args()


def main():
    args = parse_args()
    harness = ContractHarness(args)
    harness.run()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"[fail] {exc}", file=sys.stderr)
        sys.exit(1)
