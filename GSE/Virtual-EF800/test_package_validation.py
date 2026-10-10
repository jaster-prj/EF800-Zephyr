import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).parents[2]
TOOLS = ROOT / "application/plugins/measurement-plugin/tools"
sys.path.insert(0, str(TOOLS))
import verify_package  # noqa: E402


NATIVE = ROOT / "application/plugins/measurement-plugin/build/native-sim-deploy-renamed"
ARM = ROOT / "application/plugins/measurement-plugin/build/arm2/elta3_plugin.pmp"


class PackageValidationTests(unittest.TestCase):
    def test_native_packages_validate(self):
        for package in (NATIVE / "elta3_native_plugin.pmp",
                        NATIVE / "fts3xx_native_plugin.pmp"):
            if package.exists():
                verify_package.verify(package, None)

    def test_malformed_header_is_rejected(self):
        package = NATIVE / "fts3xx_native_plugin.pmp"
        if not package.exists():
            self.skipTest("native package has not been built")
        data = bytearray(package.read_bytes())
        data[0:4] = b"BAD!"
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory) / "malformed.pmp"
            temporary.write_bytes(data)
            with self.assertRaises(ValueError):
                verify_package.verify(temporary, None)

    def test_arm_package_is_rejected_by_native_target_check(self):
        if not ARM.exists():
            self.skipTest("ARM package has not been built")
        with self.assertRaises(ValueError):
            verify_package.verify(ARM, 3)


if __name__ == "__main__":
    unittest.main()
