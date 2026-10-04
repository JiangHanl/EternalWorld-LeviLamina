"""Opt-in real BDS regression; never run in GitHub CI or against old servers."""
import argparse
import json
import queue
import subprocess
import threading
import time
from pathlib import Path


class Server:
    def __init__(self, directory, evidence):
        self.lines = []
        self.incoming = queue.Queue()
        self.evidence = evidence
        self.process = subprocess.Popen(
            [str(directory / "bedrock_server_mod.exe")], cwd=directory,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", bufsize=1,
            creationflags=subprocess.CREATE_NO_WINDOW if hasattr(subprocess, "CREATE_NO_WINDOW") else 0,
        )
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self):
        for line in self.process.stdout:
            self.lines.append(line.rstrip())
            self.incoming.put(line)

    def expect(self, pattern, timeout=40):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"BDS exited {self.process.returncode} while waiting for {pattern}")
            try:
                line = self.incoming.get(timeout=min(1, max(.01, deadline - time.monotonic())))
            except queue.Empty:
                continue
            if pattern in line:
                self.evidence.append(line.strip())
                print(f"PASS {pattern}", flush=True)
                return line
        raise RuntimeError(f"Timeout waiting for {pattern}")

    def command(self, command, expected):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        return self.expect(expected)

    def stop(self):
        start = len(self.lines)
        if self.process.poll() is None:
            self.process.stdin.write("stop\n")
            self.process.stdin.flush()
            try:
                self.process.wait(timeout=40)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
                raise RuntimeError("BDS did not stop gracefully")
        self.reader.join(timeout=2)
        if self.process.returncode != 0:
            raise RuntimeError(f"BDS exit code {self.process.returncode}")
        shutdown = self.lines[start:]
        if any(" ERR [Eternal]" in line or "Wrong Host thread" in line for line in shutdown):
            raise RuntimeError("Host shutdown logged an error despite process exit 0")
        cleanup = next((line for line in shutdown if "EternalHost stop cleanup PASS" in line), None)
        if cleanup is None:
            raise RuntimeError("Host terminal module cleanup was not confirmed")
        self.evidence.append(cleanup.strip())
        print("PASS Host terminal module cleanup", flush=True)
        print("PASS graceful stop exit 0", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=Path, default=Path(__file__).resolve().parents[1] / "server")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "artifacts" / "real-bds")
    args = parser.parse_args()
    server = args.server.resolve()
    # This helper is deliberately limited to the isolated native test checkout.
    if server.name != "server" or server.parent.name != "MC_BDS_Native":
        raise RuntimeError("Refusing a server outside the isolated MC_BDS_Native/server directory")
    if sorted(p.name for p in (server / "plugins").iterdir() if p.is_dir()) != ["Eternal", "LeviLamina"]:
        raise RuntimeError("Expected only Eternal and LeviLamina runtime directories")
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "result.json").write_text(json.dumps({"result": "RUNNING"}) + "\n", encoding="utf-8")
    evidence = []
    for iteration in (1, 2):
        instance = Server(server, evidence)
        try:
            instance.expect("Server started")
            instance.command("ecore status", "business features disabled")
            instance.command("ecore selfcheck", "selfcheck PASS")
            instance.command("eternal status", "core")
            instance.command("ll list", "There are 1 mods: Eternal")
            if iteration == 1:
                instance.command("ll disable Eternal", "Disable mod Eternal successfully")
                instance.command("ll enable Eternal", "Enable mod Eternal successfully")
                instance.command("ecore selfcheck", "selfcheck PASS")
        finally:
            try:
                instance.stop()
            finally:
                (args.output / f"run-{iteration}.log").write_text("\n".join(instance.lines), encoding="utf-8")
    # Selected diagnostic lines only; raw logs stay in ignored artifacts.
    (args.output / "selected-evidence.txt").write_text("\n".join(evidence) + "\n", encoding="utf-8")
    (args.output / "result.json").write_text(json.dumps({
        "result": "PASS", "environment": "REAL BDS", "restart_runs": 2,
        "host": "Eternal", "core": "internal DLL module", "asset_features": 0,
        "players_connected": False, "server_stopped": True,
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
