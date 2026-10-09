import pathlib
import unittest

ROOT = pathlib.Path(__file__).parent


class WebAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.html = (ROOT / "web/index.html").read_text(encoding="utf-8")
        cls.js = (ROOT / "web/app.js").read_text(encoding="utf-8")

    def test_front_panel_assets_are_wired(self):
        self.assertIn("/ws", self.js)
        self.assertIn("canvas", self.html)
        self.assertIn("Acquire control", self.html)
        self.assertIn("beforeunload", self.js)

    def test_renderer_rejects_partial_frames(self):
        self.assertIn("hex.length !== FRAME_SIZE * 2", self.js)
        self.assertIn("putImageData", self.js)

    def test_keypad_has_all_semantic_keys(self):
        for key in ("up", "down", "left", "right", "enter", "escape"):
            self.assertIn(f'data-key="{key}"', self.html)

    def test_keypad_has_complete_canonical_inventory_without_aliases(self):
        import re
        keys = re.findall(r'data-key="([^"]+)"', self.html)
        self.assertEqual(len(keys), 59)
        self.assertEqual(len(set(keys)), 59)
        self.assertNotIn("esc", keys)
        self.assertNotIn("return", keys)
        self.assertNotIn("back", keys)
        self.assertNotIn("control", keys)

    def test_keypad_has_held_state_and_accessible_pressed_state(self):
        self.assertIn('aria-pressed="false"', self.html)
        self.assertIn("setKeyHeld", self.js)
        self.assertIn("pendingDown", self.js)
        self.assertIn("release-all", self.js)
        self.assertIn("aria-pressed=\"true\"", (ROOT / "web/style.css").read_text(encoding="utf-8"))

    def test_diagnostics_panel_is_bounded_and_labeled(self):
        self.assertIn('native-sim behavioral evidence', self.html)
        for element in ('logs', 'logs-pause', 'logs-clear', 'logs-severity', 'logs-autoscroll'):
            self.assertIn(element, self.html)
        self.assertIn('MAX_LOG_RECORDS = 64', self.js)
        self.assertIn('message.type === \'log\'', self.js)
        self.assertIn('diagnostic gap', self.js)
        self.assertIn('logsPaused', self.js)
        self.assertIn('logsSeverity', self.js)
        self.assertIn('logsAutoscroll', self.js)


if __name__ == "__main__":
    unittest.main()
