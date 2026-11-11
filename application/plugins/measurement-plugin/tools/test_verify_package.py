#!/usr/bin/env python3
"""Unit tests for EF800 plugin section memory declarations."""
import unittest

from verify_package import validate_memory_bounds


class MemoryBoundsTests(unittest.TestCase):
    def setUp(self):
        self.sections = {
            ".text": (12000, 1024),
            ".rodata": (3000, 1024),
            ".data": (128, 1024),
            ".bss": (256, 1024),
        }

    def validate(self, sections=None, ram=2048, text=16384, rodata=16384,
                 data=1024, bss=1024, shared=4096):
        validate_memory_bounds(self.sections if sections is None else sections,
                               ram, text, rodata, data, bss, shared)

    def test_valid_declarations_fit(self):
        self.validate()

    def test_missing_text_or_rodata_bound_rejected(self):
        with self.assertRaises(ValueError):
            self.validate(text=0)
        with self.assertRaises(ValueError):
            self.validate(rodata=0)

    def test_oversized_bounds_rejected(self):
        with self.assertRaises(ValueError):
            self.validate(text=32769)
        with self.assertRaises(ValueError):
            self.validate(shared=4097)

    def test_actual_sections_must_fit_declarations(self):
        with self.assertRaises(ValueError):
            self.validate(text=11999)
        with self.assertRaises(ValueError):
            self.validate(data=127)

    def test_writable_bounds_must_fit_declared_ram(self):
        with self.assertRaises(ValueError):
            self.validate(ram=1024, data=1024, bss=1024)

    def test_missing_optional_sections_consume_no_budget(self):
        self.validate(sections={".text": (1000, 1024), ".rodata": (1000, 1024)})


if __name__ == "__main__":
    unittest.main()
