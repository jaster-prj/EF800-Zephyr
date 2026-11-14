#!/usr/bin/env python3
"""Run target-aware clang-tidy and Cppcheck checks on EF800-owned C sources."""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
from pathlib import Path


CHECKS = (
    "-*,bugprone-sizeof-expression,bugprone-suspicious-memset-usage,"
    "readability-misleading-indentation"
)
GCC_ONLY_FLAGS = {
    "-fno-printf-return-value",
    "-fno-reorder-functions",
    "-mfp16-format=ieee",
    "-mtp=soft",
    "--param=min-pagesize=0",
    "-specs=picolibc.specs",
    "-fno-defer-pop",
    "-fno-freestanding",
}


def source_is_owned(source: str, roots: tuple[str, ...]) -> bool:
    normalized = os.path.realpath(source)
    for root in roots:
        root = os.path.realpath(root)
        if normalized.startswith(root + os.sep) and normalized.endswith(".c"):
            return True
    return False


def run_database(
    clang_tidy: str,
    database: Path,
    roots: tuple[str, ...],
    arm_target: bool,
    report_directory: Path,
    label: str,
) -> int:
    database_file = database / "compile_commands.json"
    if not database_file.is_file():
        print(f"ERROR {label}: missing compile database {database_file}", file=sys.stderr)
        return 1

    try:
        entries = json.loads(database_file.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"ERROR {label}: cannot read compile database: {error}", file=sys.stderr)
        return 1

    sources = sorted(
        {
            os.path.realpath(entry["file"] if os.path.isabs(entry["file"])
                             else os.path.join(entry["directory"], entry["file"]))
            for entry in entries
            if source_is_owned(
                entry["file"] if os.path.isabs(entry["file"])
                else os.path.join(entry["directory"], entry["file"]),
                roots,
            )
        }
    )
    if not sources:
        print(f"ERROR {label}: no EF800-owned C sources in {database_file}", file=sys.stderr)
        return 1

    adapted_entries = []
    for entry in entries:
        copied = dict(entry)
        args = copied.get("arguments")
        if args is None:
            args = shlex.split(copied["command"])
            command_key = "command"
        else:
            args = list(args)
            command_key = "arguments"
        args = [arg for arg in args if arg not in GCC_ONLY_FLAGS]
        if command_key == "command":
            copied[command_key] = shlex.join(args)
        else:
            copied[command_key] = args
        adapted_entries.append(copied)

    adapted_database = report_directory / "databases" / label
    adapted_database.mkdir(parents=True, exist_ok=True)
    (adapted_database / "compile_commands.json").write_text(
        json.dumps(adapted_entries), encoding="utf-8"
    )
    report_directory.mkdir(parents=True, exist_ok=True)
    report = report_directory / f"{label}.log"
    command = [
        clang_tidy,
        "-p",
        str(adapted_database),
        f"--checks={CHECKS}",
    ]
    if arm_target:
        command.append("--extra-arg-before=--target=arm-none-eabi")
    command.extend(sources)

    print(f"RUN {label}: {len(sources)} EF800-owned C sources")
    with report.open("w", encoding="utf-8") as output:
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, check=False)
    warnings = sum(1 for line in report.read_text(encoding="utf-8").splitlines()
                   if "warning:" in line)
    errors = sum(1 for line in report.read_text(encoding="utf-8").splitlines()
                 if "error:" in line)
    print(f"RESULT {label}: exit={result.returncode}, warnings={warnings}, errors={errors}")
    return result.returncode


def compiler_builtin_macros(entries: list[dict], output: Path, label: str) -> bool:
    """Capture compiler built-ins using a real compile command's target flags."""
    for entry in entries:
        command = entry.get("arguments")
        if command is None:
            command = shlex.split(entry["command"])
        else:
            command = list(command)
        if not command:
            continue

        compiler = command[0]
        if Path(compiler).name == "ccache" and len(command) > 1:
            compiler = command[1]
            command = command[1:]
        target_flags = []
        value_options = {
            "-D", "-U", "-I", "-isystem", "-iquote", "-idirafter",
            "-imacros", "-include",
        }
        index = 1
        while index < len(command):
            arg = command[index]
            if arg in value_options and index + 1 < len(command):
                target_flags.extend((arg, command[index + 1]))
                index += 2
                continue
            if arg.startswith(("-D", "-U", "-I", "-mcpu=", "-mabi=", "-mfpu=",
                               "-mfloat-abi=", "--sysroot=", "-std=")) or arg in (
                "-mthumb", "-marm", "-nostdinc"
            ):
                target_flags.append(arg)
            index += 1
        try:
            result = subprocess.run(
                [compiler, *target_flags, "-dM", "-E", "-x", "c", "/dev/null"],
                cwd=entry["directory"],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
        except OSError as error:
            print(f"ERROR {label}: cannot run target compiler {compiler}: {error}",
                  file=sys.stderr)
            return False
        if result.returncode != 0:
            print(f"ERROR {label}: compiler macro capture failed: {result.stderr}",
                  file=sys.stderr)
            return False
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(result.stdout, encoding="utf-8")
        return True
    print(f"ERROR {label}: no compile command available for compiler macros",
          file=sys.stderr)
    return False


def run_cppcheck_database(
    cppcheck: str,
    database: Path,
    roots: tuple[str, ...],
    report_directory: Path,
    label: str,
) -> int:
    """Check only EF800-owned translation units; ignore external-header findings."""
    database_file = database / "compile_commands.json"
    if not database_file.is_file():
        print(f"ERROR {label}: missing compile database {database_file}", file=sys.stderr)
        return 1
    try:
        entries = json.loads(database_file.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"ERROR {label}: cannot read compile database: {error}", file=sys.stderr)
        return 1

    owned_entries = []
    for entry in entries:
        source = entry["file"] if os.path.isabs(entry["file"]) else os.path.join(
            entry["directory"], entry["file"]
        )
        if source_is_owned(source, roots):
            owned_entries.append(entry)
    if not owned_entries:
        print(f"ERROR {label}: no EF800-owned C sources in {database_file}",
              file=sys.stderr)
        return 1

    adapted_database = report_directory / "cppcheck-databases" / label
    adapted_database.mkdir(parents=True, exist_ok=True)
    (adapted_database / "compile_commands.json").write_text(
        json.dumps(owned_entries), encoding="utf-8"
    )
    macros = report_directory / "cppcheck-macros" / f"{label}.h"
    if not compiler_builtin_macros(owned_entries, macros, label):
        return 1

    report_directory.mkdir(parents=True, exist_ok=True)
    report = report_directory / f"cppcheck-{label}.log"
    command = [
        cppcheck,
        "--enable=warning,style,performance,portability",
        "--inline-suppr",
        "--error-exitcode=2",
        f"--include={macros}",
        f"--project={adapted_database / 'compile_commands.json'}",
        "--template={file}:{line}:{severity}:{id}:{message}",
        "--quiet",
    ]
    print(f"RUN Cppcheck {label}: {len(owned_entries)} EF800-owned translation units")
    try:
        with report.open("w", encoding="utf-8") as output:
            result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT,
                                    check=False)
    except OSError as error:
        print(f"ERROR {label}: cannot run Cppcheck: {error}", file=sys.stderr)
        return 1

    owned_paths = tuple(os.path.realpath(root) + os.sep for root in roots)
    findings = []
    fatal_tool_errors = []
    for line in report.read_text(encoding="utf-8").splitlines():
        # Cppcheck diagnostics use file:line:severity:id:message. Only findings
        # located in EF800-owned files fail this check; included dependency
        # diagnostics remain visible in the saved raw report.
        fields = line.split(":", 4)
        if len(fields) >= 4 and fields[2] in {
            "error", "warning", "style", "performance", "portability"
        }:
            path = os.path.realpath(fields[0])
            if path.startswith(owned_paths):
                findings.append(line)
        elif line.startswith("cppcheck:") and "error:" in line:
            fatal_tool_errors.append(line)

    print(f"RESULT Cppcheck {label}: owned findings={len(findings)}, "
          f"tool errors={len(fatal_tool_errors)}, exit={result.returncode}")
    if findings or fatal_tool_errors:
        for line in findings + fatal_tool_errors:
            print(line, file=sys.stderr)
        return 1
    # Cppcheck may return its configured error code for external-header
    # findings. Those remain in the retained raw report but are outside the
    # EF800-owned finding set.
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--application", type=Path, required=True)
    parser.add_argument("--clang-tidy", default="clang-tidy-22")
    parser.add_argument("--cppcheck", default="cppcheck")
    args = parser.parse_args()

    workspace = args.workspace.resolve()
    application = args.application.resolve()
    reports = application / "build" / "c-quality-reports"
    reports.mkdir(parents=True, exist_ok=True)
    firmware_roots = (application / "application", application / "shared")

    failures = 0
    image_targets = (
        "ef800-application",
        "ef800-backup",
        "ef800-bootloader",
        "keyboard-application",
        "keyboard-bootloader",
        "keyboard-updater",
    )
    for target in image_targets:
        database = application / "build" / target
        failures += run_cppcheck_database(
            args.cppcheck,
            database,
            tuple(str(root) for root in firmware_roots),
            reports,
            target,
        ) != 0
        failures += run_database(
            args.clang_tidy,
            database,
            tuple(str(root) for root in firmware_roots),
            True,
            reports,
            target,
        ) != 0

    twister_root = workspace / "twister-out"
    test_roots = (application / "tests", application / "shared")
    test_databases = sorted(twister_root.rglob("compile_commands.json"))
    if not test_databases:
        print(f"ERROR native tests: no compile databases below {twister_root}", file=sys.stderr)
        failures += 1
    for index, database_file in enumerate(test_databases):
        database = database_file.parent
        label = f"native-test-{index:02d}-{database.name}"
        failures += run_database(
            args.clang_tidy,
            database,
            tuple(str(root) for root in test_roots),
            False,
            reports,
            label,
        ) != 0
        failures += run_cppcheck_database(
            args.cppcheck,
            database,
            tuple(str(root) for root in test_roots),
            reports,
            label,
        ) != 0

    plugin_database = (
        application / "application/plugins/measurement-plugin/build/ci-host"
    )
    failures += run_database(
        args.clang_tidy,
        plugin_database,
        (
            str(application / "application/plugins/measurement-plugin"),
            str(application / "shared/ef800_plugin"),
        ),
        False,
        reports,
        "plugin-host",
    ) != 0
    failures += run_cppcheck_database(
        args.cppcheck,
        plugin_database,
        (
            str(application / "application/plugins/measurement-plugin"),
            str(application / "shared/ef800_plugin"),
        ),
        reports,
        "plugin-host",
    ) != 0

    if failures:
        print(f"C quality checks failed: {failures} analysis group(s)", file=sys.stderr)
        return 1
    print("All configured focused C quality checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
