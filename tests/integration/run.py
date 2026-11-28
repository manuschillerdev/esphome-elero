#!/usr/bin/env python3
"""Build the checkout and run HA integration tests in a disposable smolvm."""

import argparse
import hashlib
import json
import platform
import shlex
import subprocess
import sys
import tarfile
import time
import urllib.request
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def emulator_binary():
    """Keep a checksum-verified test tool in the repository's ignored cache."""
    arch = {"x86_64": "x86_64", "aarch64": "aarch64", "arm64": "aarch64"}[platform.machine()]
    checksum = {
        "x86_64": "0911981012ed40e1936b3554271348bf4a9a2ddfd106ce619cd2ad1841232290",
        "aarch64": "2c6dc4b572a1e5a2377a995f4a0bc7a73c915df3a3a60080fad705435aa26eec",
    }[arch]
    filename = f"esp-emu-1.0.0-rc2-{arch}-unknown-linux-gnu.tar.gz"
    cache = ROOT / ".tmp" / "tools"
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / filename
    if not archive.exists():
        urllib.request.urlretrieve(
            f"https://github.com/espressif/esp-emulator/releases/download/v1.0.0-rc2/{filename}", archive
        )
    if hashlib.sha256(archive.read_bytes()).hexdigest() != checksum:
        raise RuntimeError(f"Checksum mismatch: {archive}")
    with tarfile.open(archive) as bundle:
        member = bundle.getmember(filename.removesuffix(".tar.gz") + "/esp-emu")
        target = cache / f"esp-emu-1.0.0-rc2-{arch}"
        content = bundle.extractfile(member)
        assert content is not None
        target.write_bytes(content.read())
        target.chmod(0o755)
        return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=["mqtt", "native", "all"], default="all")
    parser.add_argument("--keep", action="store_true", help="keep VM after tests")
    parser.add_argument("--name", default="elero-e2e-" + uuid.uuid4().hex[:8])
    parser.add_argument("--reuse", action="store_true", help="explicitly reset and reuse a retained TEST VM")
    parser.add_argument("--skip-build", action="store_true", help="use existing local firmware builds")
    parser.add_argument("--test", help="run matching scenario names")
    args = parser.parse_args()
    if not args.name.startswith("elero-e2e-"):
        parser.error("VM name must start with elero-e2e-")
    artifacts = ROOT / ".tmp" / "integration" / (time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:6])
    artifacts.mkdir(parents=True)
    print(f"VM: {args.name}\nEvidence: {artifacts}", flush=True)

    run_started = time.monotonic()
    timings = []

    def record_time(phase, started, **details):
        timings.append({"phase": phase, "seconds": round(time.monotonic() - started, 3), **details})
        (artifacts / "timing.json").write_text(
            json.dumps({"elapsed_seconds": round(time.monotonic() - run_started, 3), "steps": timings}, indent=2) + "\n"
        )

    def run(command, *, check=True, timeout=600):
        started = time.monotonic()
        print("$ " + shlex.join(map(str, command)), flush=True)
        with (artifacts / "commands.log").open("a") as log:
            log.write("$ " + shlex.join(map(str, command)) + "\n")
            result = subprocess.run(
                command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout
            )
            log.write(result.stdout)
        record_time("command", started, command=list(map(str, command)), exit_code=result.returncode)
        if check and result.returncode:
            raise RuntimeError(result.stdout[-12000:])
        return result

    def vm(*command, **kwargs):
        return run(["smolvm", "machine", "exec", "--name", args.name, "--", *command], **kwargs)

    def copy(source, target):
        run(["smolvm", "machine", "cp", str(source), f"{args.name}:{target}"])

    modes = ["mqtt", "native"] if args.mode == "all" else [args.mode]
    created = False
    try:
        if not args.skip_build:
            for mode in modes:
                run(["uv", "run", "--locked", "esphome", "compile", str(HERE / f"{mode}.yaml")], timeout=1800)
        sources = run(
            [
                "git",
                "ls-files",
                "--cached",
                "--others",
                "--exclude-standard",
                "components",
                "tests/integration",
                "pyproject.toml",
                "uv.lock",
            ]
        ).stdout.splitlines()
        manifest = {
            "git_head": run(["git", "rev-parse", "HEAD"]).stdout.strip(),
            "skip_build": args.skip_build,
            "mode": args.mode,
            "test": args.test,
            "source_sha256": {
                name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                for name in sources
                if (ROOT / name).is_file()
            },
            "firmware_sha256": {
                mode: hashlib.sha256(
                    (HERE / ".esphome" / "build" / mode / "build" / "firmware.factory.bin").read_bytes()
                ).hexdigest()
                for mode in modes
            },
        }
        (artifacts / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        machines = json.loads(run(["smolvm", "machine", "ls", "--json"]).stdout)
        exists = any(machine["name"] == args.name for machine in machines)
        if exists and not args.reuse:
            raise RuntimeError("Test VM exists. Select a fresh name or explicitly use --reuse.")
        if not exists:
            run(
                [
                    "smolvm",
                    "machine",
                    "create",
                    "--name",
                    args.name,
                    "--net-backend",
                    "virtio-net",
                    "--smolfile",
                    str(HERE / "smolvm.smolfile"),
                ]
            )
        created = True
        if not exists or next(m for m in machines if m["name"] == args.name)["state"] != "running":
            run(["smolvm", "machine", "start", "--name", args.name])
        if vm("ip", "link", "show", "eth0", check=False).returncode:
            raise RuntimeError("This test requires virtio-net. Create a fresh test VM; TSI breaks this workload.")
        # A stopped VM retains the images on disk but loses its bind mounts.
        vm("sh", "-ec", "mountpoint -q /var/lib/docker || mount --bind /storage/docker /var/lib/docker")
        vm(
            "sh",
            "-ec",
            "docker info >/dev/null 2>&1 || nohup dockerd --storage-driver=overlay2 >/workspace/docker.log 2>&1 &",
        )
        deadline = time.monotonic() + 45
        while vm("docker", "info", check=False).returncode:
            if time.monotonic() > deadline:
                raise RuntimeError("Docker did not start")
            time.sleep(0.5)
        # The smolfile installs this. Only older retained test VMs need setup.
        if vm("python3", "-c", "import aiohttp", check=False).returncode:
            vm("apk", "add", "--no-cache", "py3-aiohttp", "-q")
        vm("rm", "-rf", "/workspace/evidence")
        vm("mkdir", "-p", "/workspace/tests", "/workspace/evidence")
        for filename in ("scenarios.py", "protocol.py", "espemu_radio.py"):
            copy(HERE / filename, f"/workspace/tests/{filename}")
        copy(emulator_binary(), "/workspace/esp-emu")
        vm("chmod", "+x", "/workspace/esp-emu")
        for mode in modes:
            binary = HERE / ".esphome" / "build" / mode / "build" / "firmware.factory.bin"
            copy(binary, f"/workspace/tests/{mode}.bin")
        for image in ("ghcr.io/home-assistant/home-assistant:2026.11.0", "eclipse-mosquitto:2.0.22"):
            if vm("docker", "image", "inspect", "--format", "{{.Id}}", image, check=False).returncode:
                vm("docker", "pull", image)
        command = ["python3", "-u", "/workspace/tests/scenarios.py", "--mode", args.mode]
        if args.test:
            command += ["--test", args.test]
        # Stream scenario progress; firmware build and image logs stay in artifacts.
        scenario_started = time.monotonic()
        with (artifacts / "scenarios.log").open("w") as log:
            process = subprocess.Popen(
                ["smolvm", "machine", "exec", "--stream", "--timeout", "45m", "--name", args.name, "--", *command],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            try:
                assert process.stdout is not None
                for line in process.stdout:
                    print(line, end="", flush=True)
                    log.write(line)
                return process.wait()
            finally:
                record_time("scenarios", scenario_started, mode=args.mode)
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=20)
    finally:
        if created:
            for container in ("ha", "broker"):
                result = vm("docker", "logs", "--tail", "400", container, check=False, timeout=30)
                (artifacts / f"{container}.log").write_text(result.stdout)
            vm("tar", "czf", "/workspace/evidence.tar.gz", "-C", "/workspace", "evidence", check=False)
            run(
                [
                    "smolvm",
                    "machine",
                    "cp",
                    f"{args.name}:/workspace/evidence.tar.gz",
                    str(artifacts / "evidence.tar.gz"),
                ],
                check=False,
            )
            archive = artifacts / "evidence.tar.gz"
            if archive.exists():
                with tarfile.open(archive) as bundle:
                    bundle.extractall(artifacts, filter="data")
            if not args.keep:
                run(["smolvm", "machine", "delete", "--name", args.name, "-f"], check=False)
            else:
                print(f"Retained test VM: {args.name}")
        record_time("total", run_started)


if __name__ == "__main__":
    sys.exit(main())
