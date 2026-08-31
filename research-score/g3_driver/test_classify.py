#!/usr/bin/env python3
"""Unittest: no SheepShaver, no Qwen."""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from classify import (
    bc_disp,
    classify_pair,
    classify_text,
    format_classify,
    primary,
    xo,
)
from mill_escalate import write_escalate
from parse_log import parse_log
from g3_driver import next_step

FIXTURE = HERE / "fixtures" / "ss-pr10-2d295270.tail.txt"
G2_SNIP = HERE / "fixtures" / "ss-pr10-g2-hit.snippet.txt"

SYNTH_STW = """NW-BOOT G3: DEC leave 50326 cmp pc=503256f4 op=2c9e0000 nxt=3bc00000 r30=00020001
NW-BOOT G3: DEC leave 50326 cmp pc=50326674 op=2c08ffff nxt=40820008 r8=00000000
NW-BOOT G3: DEC leave 50326 cmp pc=50326564 op=900107d4 nxt=7c0604a6
NW-BOOT heartbeat pc=50326564 msr=00003010 same=0
"""


class DecodeTests(unittest.TestCase):
    def test_primary_xo(self) -> None:
        self.assertEqual(primary(0x2C9E0000), 11)
        self.assertEqual(primary(0x7C001800), 31)
        self.assertEqual(xo(0x7C001800), 0)
        self.assertEqual(primary(0x40820008), 16)
        self.assertEqual(primary(0x3BC00000), 14)
        self.assertEqual(primary(0x900107D4), 36)
        self.assertEqual(primary(0x7C0604A6), 31)
        self.assertEqual(xo(0x7C0604A6), 595)

    def test_bc_disp(self) -> None:
        self.assertEqual(bc_disp(0x40820008), 8)
        self.assertEqual(bc_disp(0x4082FFF0), -16)
        self.assertEqual(bc_disp(0x4082000C), 12)


class PairTests(unittest.TestCase):
    def test_false_stw_spr(self) -> None:
        self.assertEqual(classify_pair(0x900107D4, 0x7C0604A6), "false-stw-spr")

    def test_false_cmp_li(self) -> None:
        self.assertEqual(classify_pair(0x2C9E0000, 0x3BC00000), "false-cmp-li")

    def test_false_back_bc(self) -> None:
        self.assertEqual(classify_pair(0x4082FFF0, None), "false-back-bc")

    def test_wait_cmp_fwd(self) -> None:
        self.assertEqual(classify_pair(0x2C08FFFF, 0x40820008), "wait-cmp-fwd-bc")

    def test_bclr_is_new(self) -> None:
        # bclr XL-form primary 19
        self.assertEqual(classify_pair(0x4E800020, None), "NEW")


class FixtureTests(unittest.TestCase):
    def test_2d295270_tail(self) -> None:
        text = FIXTURE.read_text()
        report = classify_text(text)
        self.assertEqual(report["LAST_HB_CLASS"], "unknown-hb")
        self.assertEqual(report["LIVE_CLASS"], "wait-cmp-fwd-bc")
        self.assertEqual(report["STILL_CLASS"], "wait-cmp-fwd-bc")
        self.assertFalse(report["NEW"])
        line = format_classify(report)
        self.assertIn("LAST_HB_CLASS=unknown-hb", line)
        self.assertIn("LIVE_CLASS=wait-cmp-fwd-bc", line)
        self.assertIn("STILL_CLASS=wait-cmp-fwd-bc", line)
        self.assertIn("NEW=no", line)
        # pic-idle must not steal LIVE_CLASS
        self.assertTrue(report["pic_idle"])

    def test_g2_pin(self) -> None:
        text = G2_SNIP.read_text() + "\n" + FIXTURE.read_text()
        parsed = parse_log(text)
        self.assertTrue(parsed["g2_live"])
        self.assertIsNotNone(parsed["g2_hit_line"])

    def test_g2_snippet_alone(self) -> None:
        parsed = parse_log(G2_SNIP.read_text())
        self.assertTrue(parsed["g2_live"])

    def test_tail_classify_no_g2_required(self) -> None:
        report = classify_text(FIXTURE.read_text())
        self.assertEqual(report["LIVE_CLASS"], "wait-cmp-fwd-bc")

    def test_synthetic_false_stw_spr(self) -> None:
        report = classify_text(SYNTH_STW)
        self.assertEqual(report["LIVE_CLASS"], "false-stw-spr")
        self.assertFalse(report["NEW"])
        self.assertNotEqual(report["LIVE_CLASS"], "wait-cmp-fwd-bc")

    def test_escalate_wait_names_ppc_cpu_and_refuse(self) -> None:
        report = classify_text(FIXTURE.read_text())
        td = Path(tempfile.mkdtemp())
        path = write_escalate("2d295270", report, dest=td / "escalate-2d295270.md")
        body = path.read_text()
        self.assertIn("ppc-cpu.cpp", body)
        self.assertIn("stw+mfsr at 50326564", body)
        self.assertIn("900107d4", body)
        self.assertIn("g3", body)
        self.assertIn("wait-cmp-fwd-bc", body)
        self.assertIn("ppc-cpu.cpp", body)

    def test_synthetic_does_not_escalate_wait(self) -> None:
        report = classify_text(SYNTH_STW)
        td = Path(tempfile.mkdtemp())
        path = write_escalate("e298371e", report, dest=td / "escalate-e298371e.md")
        body = path.read_text()
        self.assertIn("false-stw-spr", body)
        self.assertIn("Do not treat 50326564 900107d4/7c0604a6 as a wait", body)
        # LIVE_CLASS is refuse, not a wait mill
        self.assertEqual(report["LIVE_CLASS"], "false-stw-spr")


class NextStepTests(unittest.TestCase):
    def test_new_sha_is_process(self) -> None:
        self.assertEqual(next_step({"tips": {}}, "e25a61f1"), "process")

    def test_escalate_ready_waits(self) -> None:
        st = {"tips": {"e25a61f1": {"state": "escalate_ready", "class": "wait-cmp-fwd-bc"}}}
        self.assertEqual(next_step(st, "e25a61f1"), "wait")

    def test_new_tip_after_wait_is_process(self) -> None:
        st = {"tips": {"e25a61f1": {"state": "escalate_ready"}}}
        self.assertEqual(next_step(st, "aaaaaaaa"), "process")

    def test_g3_lock_done(self) -> None:
        st = {"run": {"g3": "yes"}, "tips": {}}
        self.assertEqual(next_step(st, "e25a61f1"), "g3-done")

    def test_skip_e298(self) -> None:
        self.assertEqual(next_step({"tips": {}}, "e298371e"), "skip-e298")


if __name__ == "__main__":
    unittest.main()
