"""Opt-in Docker Compose lifecycle acceptance checks.

Run with EF800_COMPOSE_INTEGRATION=1 after building the images. The default
unit-test collection remains usable on hosts without a Docker daemon.
"""
import os
import pathlib
import subprocess
import time
import unittest

ROOT = pathlib.Path(__file__).parents[2]
COMPOSE = ROOT / "docker-compose.virtual-ef800.yml"


@unittest.skipUnless(os.getenv("EF800_COMPOSE_INTEGRATION") == "1", "opt-in Docker Compose integration test")
class ComposeIntegrationTests(unittest.TestCase):
    def compose(self, *args):
        return subprocess.run(["docker", "compose", "-f", str(COMPOSE), *args],
                              cwd=ROOT, text=True, capture_output=True, timeout=180,
                              check=True)

    def tearDown(self):
        subprocess.run(["docker", "compose", "-f", str(COMPOSE), "down"],
                       cwd=ROOT, capture_output=True, timeout=60)

    def test_start_health_and_loopback_configuration(self):
        self.compose("up", "-d", "--build")
        deadline = time.monotonic() + 30
        health = None
        while time.monotonic() < deadline:
            result = subprocess.run(["curl", "--fail", "http://127.0.0.1:8080/health"],
                                    cwd=ROOT, text=True, capture_output=True, timeout=5)
            if result.returncode == 0:
                health = result
                break
            time.sleep(1)
        self.assertIsNotNone(health, "web gateway did not become healthy")
        self.assertIn('"web": "ready"', health.stdout)
        panel = subprocess.run(["curl", "--fail", "http://127.0.0.1:8080/"],
                               cwd=ROOT, text=True, capture_output=True, timeout=30,
                               check=True)
        self.assertIn("EF800Application logs", panel.stdout)
        config = self.compose("config")
        self.assertIn("127.0.0.1", config.stdout)
        self.assertNotIn('published: "8800"', config.stdout)


if __name__ == "__main__":
    unittest.main()
