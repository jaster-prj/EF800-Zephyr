import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).parent


class WebAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.html = (ROOT / "web/index.html").read_text(encoding="utf-8")
        cls.js = (ROOT / "web/app.js").read_text(encoding="utf-8")
        cls.css = (ROOT / "web/style.css").read_text(encoding="utf-8")

    def test_front_panel_assets_are_wired(self):
        self.assertIn("/ws", self.js)
        self.assertIn("canvas", self.html)
        self.assertIn("Acquire control", self.html)
        self.assertIn("beforeunload", self.js)

    def test_renderer_rejects_partial_frames(self):
        self.assertIn("hex.length !== FRAME_SIZE * 2", self.js)
        self.assertIn("putImageData", self.js)

    def test_lcd_panel_has_no_semantic_text_overlay(self):
        self.assertNotIn('id="text"', self.html)
        self.assertNotIn("text.textContent", self.js)
        self.assertNotIn("renderText", self.js)

    def test_renderer_applies_contrast_without_changing_lcd_pixels(self):
        self.assertIn("function lcdInkColor()", self.js)
        self.assertIn("lcdContrastDuty <= 78", self.js)
        self.assertIn("lcdContrastDuty / 78", self.js)
        self.assertIn("(lcdContrastDuty - 78) / 22", self.js)
        self.assertIn("const LCD_BACKGROUND", self.js)
        self.assertIn("color = on ? ink : LCD_BACKGROUND", self.js)
        self.assertIn("message.type === 'lcd-contrast'", self.js)
        self.assertIn("renderPixels(lastLcdPixels)", self.js)

    def test_logs_window_has_catalog_backed_composition_controls(self):
        self.assertIn("id=\"composition-groups\"", self.html)
        self.assertIn("id=\"composition-status\"", self.html)
        self.assertIn("message.type === 'composition'", self.js)
        self.assertIn("command: 'composition-set'", self.js)
        self.assertIn("Active now:", self.js)
        self.assertIn("Restart the simulator to activate", self.js)

    def test_virtual_battery_soc_control_is_bounded_and_source_aware(self):
        self.assertIn('id="battery-soc" type="number" min="0" max="100" step="1"', self.html)
        self.assertIn('id="battery-soc-apply"', self.html)
        self.assertIn('aria-live="polite"', self.html)
        self.assertIn("command: 'battery-soc-set'", self.js)
        self.assertIn("batterySocSource === 'BATSOC0'", self.js)
        self.assertIn("message.type === 'battery-soc'", self.js)
        self.assertIn("Number.isInteger(socPercent)", self.js)
        self.assertIn("socPercent < 0 || socPercent > 100", self.js)
        self.assertNotIn("simulator:8800", self.js)

    def test_keypad_has_all_semantic_keys(self):
        for key in ("up", "down", "left", "right", "enter", "escape"):
            self.assertIn(f'data-key="{key}"', self.html)

    def test_keypad_has_complete_canonical_inventory_without_aliases(self):
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
        self.assertIn('native-sim behavioral diagnostics', self.html)
        for element in ('logs', 'logs-pause', 'logs-clear', 'logs-severity', 'logs-autoscroll'):
            self.assertIn(element, self.html)
        self.assertIn('MAX_LOG_RECORDS = 64', self.js)
        self.assertIn('message.type === \'log\'', self.js)
        self.assertIn('diagnostic gap', self.js)
        self.assertNotIn('diagnostic records were dropped or evicted', self.js)
        self.assertIn('logsPaused', self.js)
        self.assertIn('logsSeverity', self.js)
        self.assertIn('logsAutoscroll', self.js)
        self.assertIn('Array.isArray(record.segments)', self.js)
        self.assertIn('span.style.color', self.js)
        self.assertIn("span.textContent = segment.text", self.js)

    def test_main_mtp_file_view_uses_gateway_and_excludes_private_storage(self):
        for element in ("mtp-storage", "mtp-list", "mtp-file", "mtp-upload",
                        "mtp-dropzone", "mtp-progress", "mtp-cancel"):
            self.assertIn(f'id="{element}"', self.html)
        self.assertIn("Data", self.html)
        self.assertIn("Plugins", self.html)
        self.assertIn("FWU", self.html)
        self.assertIn("Configuration and raw storage are not exposed", self.html)
        self.assertIn("/api/mtp/list", self.js)
        self.assertIn("/api/mtp/download", self.js)
        self.assertIn("/api/mtp/upload", self.js)
        self.assertIn("event.dataTransfer.files[0]", self.js)
        self.assertIn("AbortController", self.js)
        self.assertIn("mtp-cancel", self.js)
        self.assertIn("mtp-progress", self.js)
        self.assertNotIn("simulator:8800", self.js)
        self.assertNotIn('summary>Native-sim measurement plugins', self.html)
        self.assertNotIn('id="plugin-file"', self.html)

    def test_notebook_layout_matches_field_hardware_orientation(self):
        lcd = self.html.index('id="lcd"')
        keys = self.html.index('id="keypad"')
        connectors = self.html.index('class="connector-edge"')
        self.assertLess(lcd, keys)
        self.assertLess(keys, connectors)
        for marker in ('class="key-bank key-bank-left"',
                       'class="bottom-key-matrix alpha-board"',
                       'class="key-bank key-bank-right"'):
            self.assertIn(marker, self.html)
        self.assertEqual(self.html.count("D-SUB 25 · FEMALE"), 2)
        self.assertIn("D-SUB 25 · MALE", self.html)
        self.assertIn("USB-C", self.html)
        self.assertIn("BLUETOOTH", self.html)
        self.assertIn("INTERNAL CONNECTOR", self.html)

    def test_key_groups_follow_the_photographed_hardware_layout(self):
        left_start = self.html.index('aria-label="left-side modifier keys"')
        left_end = self.html.index('</aside>', left_start)
        left = self.html[left_start:left_end]
        left_keys = re.findall(r'data-key="([^"]+)"', left)
        self.assertEqual(left_keys, ["on", "off", "escape", "insert", "alt",
                                     "caret", "tab", "less", "caps", "shift", "ctrl"])
        self.assertEqual(left.count('class="key-empty"'), 1)

        function_start = self.html.index('class="function-pad"')
        number_start = self.html.index('class="number-pad"')
        alpha_start = self.html.index('class="bottom-key-matrix alpha-board"')
        function_keys = re.findall(r'data-key="([^"]+)"',
                                   self.html[function_start:number_start])
        self.assertEqual(function_keys, ["delete", "up", "backspace", "left", "down", "right"])
        number_keys = re.findall(r'data-key="([^"]+)"',
                                 self.html[number_start:alpha_start])
        self.assertEqual(number_keys, ["7", "8", "9", "4", "5", "6", "1", "2", "3",
                                       "minus", "0", "period", "space", "plus"])
        self.assertIn('grid-template-areas: "left lcd right" "left alpha right"', self.css)
        self.assertIn("minmax(84px, .7fr) minmax(0, 4.4fr) minmax(130px, 1.1fr)", self.css)
        self.assertIn(".alpha-home { grid-column: 1 / 10; grid-row: 2", self.css)
        self.assertIn(".alpha-bottom { grid-column: 1 / 10; grid-row: 3", self.css)
        self.assertIn(".key-bank-left .modifier-wide { grid-column: 1 / -1; }", self.css)
        self.assertIn("grid-row: 2 / 4", self.css)
        self.assertIn(".number-pad .number-space { grid-column: 1 / 3;", self.css)
        self.assertIn(".number-pad .number-plus { grid-column: 3;", self.css)


if __name__ == "__main__":
    unittest.main()
