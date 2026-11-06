import pathlib
import unittest

ROOT = pathlib.Path(__file__).parents[2]
COMPOSE = ROOT / "docker-compose.virtual-ef800.yml"


class DeploymentDefinitionTests(unittest.TestCase):
    def test_compose_keeps_simulator_private_and_web_loopback_bound(self):
        text = COMPOSE.read_text(encoding="utf-8")
        self.assertIn('"127.0.0.1:${VIRTUAL_EF800_WEB_PORT:-8080}:8080"', text)
        self.assertIn('expose:', text)
        self.assertNotIn('"8800:8800"', text)
        self.assertIn("/health", text)

    def test_images_are_non_root_and_manifest_based(self):
        simulator = (ROOT / "docker/virtual-ef800/Dockerfile").read_text(encoding="utf-8")
        web = (ROOT / "docker/virtual-ef800-web/Dockerfile").read_text(encoding="utf-8")
        entrypoint = (ROOT / "docker/virtual-ef800/entrypoint.sh").read_text(encoding="utf-8")
        self.assertIn("COPY build/ef800-main-native-sim/zephyr/zephyr.exe", simulator)
        self.assertNotIn("west update", simulator)
        self.assertIn("useradd --system", simulator)
        self.assertIn("USER virtualef800", web)
        self.assertIn("date -u +%Y-%m-%dT%H:%M:%SZ", entrypoint)
        self.assertIn("controlled restart (run=$run exit=42)", entrypoint)
        self.assertIn("exited unexpectedly (run=$run exit=$status)", entrypoint)


if __name__ == "__main__":
    unittest.main()
