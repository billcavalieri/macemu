#!/usr/bin/env python3
"""Unittest: no SheepShaver, no local LLM."""
from __future__ import annotations

import os
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
from mill_apply import (
    MARKER_HANG_SKIP,
    MARKER_INITFONTS,
    MARKER_STW,
    force_skip_68k_off,
    force_skip_hang_off,
    hang_off_millable,
    is_applied,
    leftover_68k_pending,
    mill_kind,
    mill_moved,
    mill_worse,
    next_kind,
    next_leftover,
    next_skip_68k_off,
    next_skip_hang_off,
    patch_cpu_initfonts,
    patch_cpu_remove_skip,
    patch_cpu_skip_hang,
    patch_cpu_text,
    skip_68k_key,
    skip_hang_key,
    tested_keys,
)
from mill_escalate import write_escalate
from mill_pack import append_pack_log, format_pack_md, format_pack_slim_md, pack_from_state
from parse_log import hangcap_early_fail, hangcap_g0_stuck, hangcap_keep_stable, parse_log
from g3_lock import format_tokens, score_g3, zero_usage
from g3_driver import _fmt_sec, format_attempts_table, next_step
from debug_run import hangcap_sec

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

    def test_escalate_ready_mills(self) -> None:
        st = {
            "tips": {"e25a61f1": {"state": "escalate_ready", "class": "false-stw-spr"}},
            "mill": {"live_class": "false-stw-spr"},
        }
        step = next_step(st, "e25a61f1")
        self.assertIn(step, ("mill", "hangcap"))
        self.assertNotEqual(step, "wait")

    def test_pending_hangcap(self) -> None:
        st = {"mill": {"pending_hangcap": True, "live_class": "false-stw-spr"}, "tips": {}}
        self.assertEqual(next_step(st, "e25a61f1"), "hangcap")

    def test_new_tip_after_classify_is_mill(self) -> None:
        st = {"tips": {"aaaaaaaa": {"state": "escalate_ready", "class": "false-stw-spr"}}}
        step = next_step(st, "aaaaaaaa")
        self.assertIn(step, ("mill", "hangcap"))

    def test_g3_lock_done(self) -> None:
        st = {"run": {"g3": "yes"}, "tips": {}}
        self.assertEqual(next_step(st, "e25a61f1"), "g3-done")

    def test_skip_e298(self) -> None:
        self.assertEqual(next_step({"tips": {}}, "e298371e"), "skip-e298")

    def test_score_ignored_hangcap_log(self) -> None:
        st = {
            "mill": {
                "live_class": "false-stw-spr",
                "n": 1,
                "last_fail": "perl_exit",
                "tested": ["false-stw-spr"],
            }
        }
        log_path = Path("/tmp/ss-g3-mill-1.log")
        log_path.write_text("hang-cap ignored\n", encoding="utf-8")
        try:
            self.assertEqual(next_step(st, "e25a61f1"), "score-log")
        finally:
            log_path.unlink(missing_ok=True)

    def test_skip_tested_mills_next_kind(self) -> None:
        st = {
            "mill": {
                "live_class": "false-stw-spr",
                "tested": ["false-stw-spr:skip-pair"],
            }
        }
        step = next_step(st, "e25a61f1")
        self.assertIn(step, ("mill", "hangcap"))
        self.assertEqual(next_kind("false-stw-spr", st["mill"]["tested"]), "execute-pair")

    def test_all_stw_kinds_exhausted(self) -> None:
        tested = [
            "false-stw-spr:skip-pair",
            "false-stw-spr:execute-pair",
            "false-stw-spr:skip-mfsr",
        ]
        self.assertIsNone(next_kind("false-stw-spr", tested))
        self.assertEqual(next_leftover(tested, []), "poison-skip")
        self.assertEqual(
            next_leftover(tested + ["leftover:poison-skip"], []),
            "unstick-stw",
        )
        self.assertIsNone(
            next_leftover(
                tested + ["leftover:poison-skip", "leftover:unstick-stw"],
                [],
            )
        )
        exhausted = tested + ["leftover:poison-skip", "leftover:unstick-stw"]
        self.assertEqual(
            next_leftover(exhausted, [], hang_off=0x326510),
            "skip-hang",
        )
        self.assertEqual(
            next_leftover(exhausted, [], hang_off=0x3264FC),
            "skip-hang",
        )
        self.assertIsNone(next_leftover(exhausted, [], hang_off=0x3259E0))
        self.assertEqual(
            next_leftover(exhausted, [], hang_off=0x366084),
            "keep-68k",
        )
        self.assertEqual(
            next_leftover(
                exhausted + ["leftover:keep-68k"],
                [],
                hang_off=0x366084,
            ),
            "read-noerr",
        )
        self.assertEqual(
            next_leftover(exhausted, [], hang_off=0x326510, saw_68k=True),
            "stay-code66",
        )
        self.assertEqual(
            next_leftover(
                exhausted + ["leftover:stay-code66"],
                [],
                hang_off=0x326510,
                saw_68k=True,
            ),
            "launch-upgrader",
        )
        self.assertEqual(
            next_leftover(
                exhausted + ["leftover:stay-code66", "leftover:slot-26e90"],
                [],
                hang_off=0x326510,
                saw_68k=True,
            ),
            "launch-upgrader",
        )
        done68 = exhausted + [
            "leftover:keep-68k",
            "leftover:read-noerr",
            "leftover:setfpos-noerr",
            "leftover:slot-26e90",
            "leftover:skip-3265a4",
            "leftover:spin-26e88",
            "leftover:skip-326458",
            "leftover:fixmul-a868",
            "leftover:disposeptr-a01f",
            "leftover:initfonts-a8fe",
            "leftover:skip-hang:00326510",
        ]
        self.assertEqual(
            next_leftover(done68, [], hang_off=0x326510, saw_68k=True),
            "stay-code66",
        )
        self.assertEqual(
            next_leftover(
                done68 + ["leftover:stay-code66"],
                [],
                hang_off=0x326510,
                saw_68k=True,
            ),
            "launch-upgrader",
        )
        self.assertNotEqual(
            next_leftover(done68, [], hang_off=0x326510, saw_68k=True),
            "skip-hang",
        )
        self.assertEqual(
            next_leftover(done68, [], hang_off=0x326510),
            "skip-hang",
        )
        self.assertEqual(
            next_skip_hang_off(0x326510, done68, []),
            0x326514,
        )
        self.assertIsNotNone(force_skip_hang_off(None, done68, []))
        self.assertEqual(
            force_skip_hang_off(0x326510, done68, []),
            0x326514,
        )
        self.assertTrue(leftover_68k_pending(done68, [], saw_68k=True))
        self.assertFalse(leftover_68k_pending(done68, [], saw_68k=False))
        empty_map = {"map_keep_log_only": True}
        self.assertIsNone(force_skip_68k_off(empty_map, done68, ["leftover:spin-26e88"]))
        self.assertEqual(
            next_leftover(done68, [], hang_off=0x326510, saw_68k=True, mill=empty_map),
            "stay-code66",
        )
        self.assertEqual(
            next_leftover(
                done68 + ["leftover:stay-code66"],
                [],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "launch-upgrader",
        )
        self.assertEqual(
            next_leftover(
                done68 + ["leftover:stay-code66", "leftover:launch-upgrader"],
                [],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "splash-510",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                ],
                [],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "splash-510-even",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                    "leftover:splash-510-even",
                ],
                [],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pict-1000",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                    "leftover:splash-510-even",
                    "leftover:pict-1000",
                ],
                [],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-upgrader",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                    "leftover:splash-510-even",
                    "leftover:pict-1000",
                    "leftover:pef-upgrader",
                ],
                [],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-enter",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                    "leftover:splash-510-even",
                    "leftover:pict-1000",
                    "leftover:pef-upgrader",
                    "leftover:pef-enter",
                ],
                [],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-imports",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                    "leftover:splash-510-even",
                    "leftover:pict-1000",
                    "leftover:pef-upgrader",
                    "leftover:pef-enter",
                    "leftover:pef-imports",
                ],
                [],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-sysenv",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                    "leftover:splash-510-even",
                    "leftover:pict-1000",
                    "leftover:pef-upgrader",
                    "leftover:pef-enter",
                    "leftover:pef-imports",
                    "leftover:pef-sysenv",
                ],
                [],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-vol",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                    "leftover:splash-510-even",
                    "leftover:pict-1000",
                    "leftover:pef-upgrader",
                    "leftover:pef-enter",
                    "leftover:pef-imports",
                    "leftover:pef-sysenv",
                    "leftover:pef-vol",
                ],
                [],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-dce",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                    "leftover:splash-510-even",
                    "leftover:pict-1000",
                    "leftover:pef-upgrader",
                    "leftover:pef-enter",
                    "leftover:pef-imports",
                    "leftover:pef-sysenv",
                    "leftover:pef-vol",
                    "leftover:pef-dce",
                ],
                [],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-wait",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                    "leftover:splash-510-even",
                    "leftover:pict-1000",
                    "leftover:pef-upgrader",
                    "leftover:pef-enter",
                    "leftover:pef-imports",
                    "leftover:pef-sysenv",
                    "leftover:pef-vol",
                    "leftover:pef-dce",
                    "leftover:pef-wait",
                ],
                [],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-te",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                    "leftover:splash-510-even",
                    "leftover:pict-1000",
                    "leftover:pef-upgrader",
                    "leftover:pef-enter",
                    "leftover:pef-imports",
                    "leftover:pef-sysenv",
                    "leftover:pef-vol",
                    "leftover:pef-dce",
                    "leftover:pef-wait",
                    "leftover:pef-te",
                ],
                ["leftover:pef-te"],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-terec",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                    "leftover:splash-510-even",
                    "leftover:pict-1000",
                    "leftover:pef-upgrader",
                    "leftover:pef-enter",
                    "leftover:pef-imports",
                    "leftover:pef-sysenv",
                    "leftover:pef-vol",
                    "leftover:pef-dce",
                    "leftover:pef-wait",
                    "leftover:pef-te",
                    "leftover:pef-terec",
                ],
                ["leftover:pef-te", "leftover:pef-terec"],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-skipte",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                    "leftover:splash-510-even",
                    "leftover:pict-1000",
                    "leftover:pef-upgrader",
                    "leftover:pef-enter",
                    "leftover:pef-imports",
                    "leftover:pef-sysenv",
                    "leftover:pef-vol",
                    "leftover:pef-dce",
                    "leftover:pef-wait",
                    "leftover:pef-te",
                    "leftover:pef-terec",
                    "leftover:pef-skipte",
                ],
                ["leftover:pef-te", "leftover:pef-terec"],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-skipdi",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                    "leftover:splash-510-even",
                    "leftover:pict-1000",
                    "leftover:pef-upgrader",
                    "leftover:pef-enter",
                    "leftover:pef-imports",
                    "leftover:pef-sysenv",
                    "leftover:pef-vol",
                    "leftover:pef-dce",
                    "leftover:pef-wait",
                    "leftover:pef-te",
                    "leftover:pef-terec",
                    "leftover:pef-skipte",
                    "leftover:pef-skipdi",
                ],
                ["leftover:pef-te", "leftover:pef-terec"],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-idx",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                    "leftover:splash-510-even",
                    "leftover:pict-1000",
                    "leftover:pef-upgrader",
                    "leftover:pef-enter",
                    "leftover:pef-imports",
                    "leftover:pef-sysenv",
                    "leftover:pef-vol",
                    "leftover:pef-dce",
                    "leftover:pef-wait",
                    "leftover:pef-te",
                    "leftover:pef-terec",
                    "leftover:pef-skipte",
                    "leftover:pef-skipdi",
                    "leftover:pef-idx",
                ],
                ["leftover:pef-te", "leftover:pef-terec"],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-gnd",
        )
        self.assertEqual(
            next_leftover(
                done68
                + [
                    "leftover:stay-code66",
                    "leftover:launch-upgrader",
                    "leftover:splash-510",
                    "leftover:splash-510-even",
                    "leftover:pict-1000",
                    "leftover:pef-upgrader",
                    "leftover:pef-enter",
                    "leftover:pef-imports",
                    "leftover:pef-sysenv",
                    "leftover:pef-vol",
                    "leftover:pef-dce",
                    "leftover:pef-wait",
                    "leftover:pef-te",
                    "leftover:pef-terec",
                    "leftover:pef-skipte",
                    "leftover:pef-skipdi",
                    "leftover:pef-idx",
                    "leftover:pef-gnd",
                ],
                ["leftover:pef-te", "leftover:pef-terec"],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-gndid",
        )
        pef_done = done68 + [
            "leftover:stay-code66",
            "leftover:launch-upgrader",
            "leftover:splash-510",
            "leftover:splash-510-even",
            "leftover:pict-1000",
            "leftover:pef-upgrader",
            "leftover:pef-enter",
            "leftover:pef-imports",
            "leftover:pef-sysenv",
            "leftover:pef-vol",
            "leftover:pef-dce",
            "leftover:pef-wait",
            "leftover:pef-te",
            "leftover:pef-terec",
            "leftover:pef-skipte",
            "leftover:pef-skipdi",
            "leftover:pef-idx",
            "leftover:pef-gnd",
            "leftover:pef-gndid",
            "leftover:pef-d519",
            "leftover:pef-modal",
            "leftover:pef-no519",
            "leftover:pef-show",
            "leftover:pef-forcesplash",
        ]
        pef_rev = ["leftover:pef-te", "leftover:pef-terec"]
        self.assertEqual(
            next_leftover(
                pef_done,
                pef_rev,
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-skipwait",
        )
        self.assertEqual(
            next_leftover(
                pef_done + ["leftover:pef-skipwait"],
                pef_rev,
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-callsplash",
        )
        self.assertEqual(
            next_leftover(
                pef_done + ["leftover:pef-skipwait"],
                pef_rev + ["leftover:pef-callsplash"],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-skipalert",
        )
        self.assertEqual(
            next_leftover(
                pef_done + ["leftover:pef-skipwait", "leftover:pef-skipalert"],
                pef_rev + ["leftover:pef-callsplash"],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-nimp",
        )
        self.assertEqual(
            next_leftover(
                pef_done + ["leftover:pef-skipwait", "leftover:pef-skipalert"],
                pef_rev + ["leftover:pef-callsplash", "leftover:pef-nimp"],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-jumpsplash",
        )
        self.assertEqual(
            next_leftover(
                pef_done + ["leftover:pef-skipwait", "leftover:pef-skipalert"],
                pef_rev
                + [
                    "leftover:pef-callsplash",
                    "leftover:pef-nimp",
                    "leftover:pef-jumpsplash",
                ],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-plantsplash",
        )
        self.assertEqual(
            next_leftover(
                pef_done
                + [
                    "leftover:pef-skipwait",
                    "leftover:pef-skipalert",
                    "leftover:pef-plantsplash",
                ],
                pef_rev
                + [
                    "leftover:pef-callsplash",
                    "leftover:pef-nimp",
                    "leftover:pef-jumpsplash",
                ],
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-forceblit",
        )
        from mill_apply import LEFTOVER
        from mill_pef_splash import PEF_SPLASH_KINDS

        before_splash = list(done68)
        for k in LEFTOVER:
            if k in PEF_SPLASH_KINDS:
                break
            if k.startswith("pef-") or k in (
                "stay-code66",
                "launch-upgrader",
                "splash-510",
                "splash-510-even",
                "pict-1000",
            ):
                key = "leftover:%s" % k
                if key not in before_splash:
                    before_splash.append(key)
        self.assertEqual(
            next_leftover(
                before_splash,
                pef_rev,
                hang_off=0x326510,
                saw_68k=True,
                mill=empty_map,
            ),
            "pef-frontwin",
        )


class MillApplyTests(unittest.TestCase):
    def test_false_stw_is_skip_not_wait(self) -> None:
        self.assertEqual(mill_kind("false-stw-spr"), "skip-pair")
        self.assertNotEqual(mill_kind("false-stw-spr"), "wait-cmp-fwd-bc")
        self.assertEqual(next_kind("false-stw-spr", ["false-stw-spr:skip-pair"]), "execute-pair")
        self.assertEqual(next_kind("false-stw-spr", [
            "false-stw-spr:skip-pair",
            "false-stw-spr:execute-pair",
        ]), "skip-mfsr")
        self.assertIn("false-stw-spr:skip-pair", tested_keys(["false-stw-spr"], "false-stw-spr"))

    def test_qd_fb_stamp_in_cpu(self) -> None:
        cpu = (
            HERE.parents[1]
            / "SheepShaver"
            / "src"
            / "kpx_cpu"
            / "src"
            / "cpu"
            / "ppc"
            / "ppc-cpu.cpp"
        ).read_text()
        self.assertIn("G3: 68k QD fb 0x800000", cpu)
        self.assertIn("g3_qd_fb", cpu)
        self.assertNotIn("g3_qd_fill32", cpu)
        self.assertIn("G3: 68k Launch A9F2 enter CODE0", cpu)
        self.assertIn("G3: 68k Launch A9F2 CFM Upgrader", cpu)
        self.assertIn("G3: 68k GetNewDialog A97C Splash 510 even", cpu)
        self.assertIn("G3: 68k DrawPicture A8F6 PICT 1000", cpu)
        self.assertIn("G3: 68k Launch A9F2 CFM Upgrader PEF", cpu)
        self.assertIn("G3: 68k Launch A9F2 CFM Upgrader PEF enter", cpu)
        self.assertIn("g3_df_off = 74182144u", cpu)
        self.assertIn("g3_rf_off = 74305024u", cpu)
        self.assertNotIn("g3_rf_off = 112182784u", cpu)
        self.assertIn("G3: 68k LoadSeg A9F0 enter", cpu)

    def test_pef_gncw_patch(self) -> None:
        from mill_apply import MARKER_PEF_GNCW, patch_cpu_pef_gncw

        cpu_path = (
            HERE.parents[1]
            / "SheepShaver"
            / "src"
            / "kpx_cpu"
            / "src"
            / "cpu"
            / "ppc"
            / "ppc-cpu.cpp"
        )
        raw = cpu_path.read_text()
        if MARKER_PEF_GNCW in raw:
            text = raw
        else:
            text = patch_cpu_pef_gncw(raw)
        self.assertIn(MARKER_PEF_GNCW, text)
        self.assertIn("g3_plant_wind", text)
        self.assertIn("0x57494E44u", text)
        self.assertIn("0xa861006au", text)
        self.assertIn("ent + 0x6410u", text)
        self.assertIn("PEF gncwCall", text)
        self.assertNotEqual(text.count("GetNewCWindow id="), 0)
        self.assertEqual(patch_cpu_pef_gncw(text), text)

    def test_pef_call798c_patch(self) -> None:
        from mill_apply import MARKER_PEF_CALL798C, patch_cpu_pef_call798c, patch_cpu_pef_gncw

        cpu_path = (
            HERE.parents[1]
            / "SheepShaver"
            / "src"
            / "kpx_cpu"
            / "src"
            / "cpu"
            / "ppc"
            / "ppc-cpu.cpp"
        )
        raw = cpu_path.read_text()
        if MARKER_PEF_CALL798C in raw:
            text = raw
        else:
            if "PEF gncwCall" not in raw:
                raw = patch_cpu_pef_gncw(raw)
            text = patch_cpu_pef_call798c(raw)
        self.assertIn(MARKER_PEF_CALL798C, text)
        self.assertIn("0x48006559u", text)
        self.assertIn("ent + 0x64u", text)
        self.assertEqual(patch_cpu_pef_call798c(text), text)

    def test_patch_cpu_initfonts_marker(self) -> None:
        cpu = (
            HERE.parents[1]
            / "SheepShaver"
            / "src"
            / "kpx_cpu"
            / "src"
            / "cpu"
            / "ppc"
            / "ppc-cpu.cpp"
        ).read_text()
        self.assertIn(MARKER_INITFONTS, cpu)
        self.assertEqual(patch_cpu_initfonts(cpu), cpu)
        self.assertTrue(is_applied("leftover", kind="initfonts-a8fe"))

    def test_patch_cpu_inserts_skip_marker(self) -> None:
        import subprocess

        root = HERE.parents[1]
        raw = subprocess.check_output(
            [
                "git",
                "show",
                "HEAD:SheepShaver/src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp",
            ],
            cwd=str(root),
        )
        text = raw.decode()
        self.assertNotIn(MARKER_STW, text)
        out = patch_cpu_text(text)
        self.assertIn(MARKER_STW, out)
        self.assertIn("pc() += 8u", out)
        self.assertNotIn(
            "return nw_dec_leave_50326564_off(rom_off);",
            out.split("nw_dec_leave_50326564_wait", 1)[1][:400],
        )
        gone = patch_cpu_remove_skip(out)
        self.assertNotIn(MARKER_STW, gone)
        self.assertIn("return 0;", gone.split("nw_dec_leave_50326564_wait", 1)[1][:200])

    def test_miss_is_not_dsi_on_store_class(self) -> None:
        text = (
            "NW-BOOT G2: xlatehow=miss ea=68fff0dc msr=00003010\n"
            "NW-BOOT G2: first DSI SRR0=50325a14 DAR=10010002 DRhit=1\n"
            "NW-BOOT heartbeat pc=50326474 msr=00007472 same=0\n"
        )
        report = classify_text(text)
        self.assertFalse(report["parsed"]["dsi_on_store"])
        self.assertNotEqual(report["LIVE_CLASS"], "dsi-on-store")

    def test_mill_worse_68k_loss(self) -> None:
        before = {
            "parsed": {"g2_live": True, "mill_max": 0, "hang_04cecd36": False},
            "g2_live": True,
            "hang_04cecd36": False,
            "last_hb": {"pc": 0x50366084},
        }
        after = {
            "parsed": {"g2_live": True, "mill_max": 0, "hang_04cecd36": False},
            "g2_live": True,
            "hang_04cecd36": False,
            "last_hb": {"pc": 0x503265F4},
        }
        self.assertTrue(mill_worse(before, after))
        self.assertFalse(mill_worse(before, before))

    def test_mill_worse_keep_pc_never_reached_68k(self) -> None:
        before = {
            "parsed": {"g2_live": True, "mill_max": 0, "hang_04cecd36": False},
            "g2_live": True,
            "hang_04cecd36": False,
            "last_hb": {"pc": 0x50326554},
        }
        after = {
            "parsed": {
                "g2_live": True,
                "mill_max": 0,
                "hang_04cecd36": False,
                "reached_68k": False,
            },
            "g2_live": True,
            "hang_04cecd36": False,
            "last_hb": {"pc": 0x50326554},
            "reached_68k": False,
        }
        self.assertTrue(
            mill_worse(before, after, keep_pc=0x50366084, saw_68k=True)
        )
        keep = dict(after)
        keep["last_hb"] = {"pc": 0x50366084}
        keep["reached_68k"] = True
        keep["parsed"] = dict(after["parsed"], reached_68k=True)
        self.assertFalse(
            mill_worse(before, keep, keep_pc=0x50366084, saw_68k=True)
        )

    def test_mill_worse_short_run(self) -> None:
        before = {
            "parsed": {"g2_live": True, "mill_max": 0, "hang_04cecd36": False},
            "g2_live": True,
            "last_hb": {"pc": 0x50366084},
        }
        after = {
            "parsed": {
                "g2_live": True,
                "mill_max": 0,
                "hang_04cecd36": False,
                "reached_68k": True,
            },
            "g2_live": True,
            "last_hb": {"pc": 0x50366084},
            "reached_68k": True,
        }
        # keep_stable at 50366084 can finish in <8s; not worse.
        self.assertFalse(
            mill_worse(
                before, after, keep_pc=0x50366084, saw_68k=True, ss_alive_sec=1.6
            )
        )
        self.assertFalse(
            mill_worse(
                before,
                after,
                keep_pc=0x50366084,
                saw_68k=True,
                ss_alive_sec=1.6,
                window="yes",
            )
        )
        self.assertFalse(
            mill_worse(
                before, after, keep_pc=0x50366084, saw_68k=True, ss_alive_sec=12.0
            )
        )
        died = {
            "parsed": {
                "g2_live": True,
                "mill_max": 0,
                "hang_04cecd36": False,
                "reached_68k": False,
            },
            "g2_live": True,
            "last_hb": None,
            "reached_68k": False,
        }
        self.assertTrue(
            mill_worse(
                before, died, keep_pc=0x50366084, saw_68k=True, ss_alive_sec=1.6
            )
        )

    def test_mill_worse_g2_loss(self) -> None:
        before = {"parsed": {"g2_live": True, "mill_max": 0}, "g2_live": True}
        after = {
            "parsed": {"g2_live": False, "mill_max": 0, "hang_04cecd36": False},
            "g2_live": False,
            "hang_04cecd36": False,
        }
        self.assertTrue(mill_worse(before, after))

    def test_mill_moved_class(self) -> None:
        before = {"LIVE_CLASS": "false-stw-spr", "last_hb": {"pc": 0x50326564}}
        after = {"LIVE_CLASS": "NEW", "last_hb": {"pc": 0x50326600}}
        self.assertTrue(mill_moved(before, after))
        self.assertFalse(mill_moved(before, before))

    def test_skip_hang_off_hard(self) -> None:
        self.assertTrue(hang_off_millable(0x326510))
        self.assertFalse(hang_off_millable(0x3264FC))
        self.assertFalse(hang_off_millable(0x326564))
        self.assertFalse(hang_off_millable(0x3259E0))
        self.assertEqual(next_skip_hang_off(0x326510, [], []), 0x326510)
        self.assertEqual(next_skip_hang_off(0x3264FC, [], []), 0x326500)
        self.assertIsNone(next_skip_hang_off(0x3259E0, [], []))
        self.assertEqual(
            next_skip_hang_off(0x326510, [skip_hang_key(0x326510)], []),
            0x326514,
        )

    def test_patch_cpu_skip_hang(self) -> None:
        from mill_apply import cpu_path

        text = cpu_path().read_text()
        out = patch_cpu_skip_hang(text, 0x326510)
        self.assertIn(MARKER_HANG_SKIP, out)
        self.assertIn("if (hang_off == 0x326510u)", out)
        self.assertIn("pc() += 4u", out)
        out2 = patch_cpu_skip_hang(out, 0x32651C)
        self.assertIn("if (hang_off == 0x32651cu)", out2)
        self.assertNotIn("if (hang_off == 0x326510u)", out2)

    def test_pack_68k_mills_patch(self) -> None:
        from mill_apply import (
            MARKER_68K_KEEP,
            MARKER_READ_NOERR,
            MARKER_SETFPOS_NOERR,
            cpu_path,
            patch_cpu_keep_68k,
            patch_cpu_read_noerr,
            patch_cpu_setfpos_noerr,
        )

        text = cpu_path().read_text()
        k = patch_cpu_keep_68k(text)
        self.assertIn(MARKER_68K_KEEP, k)
        r = patch_cpu_read_noerr(text)
        self.assertIn(MARKER_READ_NOERR, r)
        s = patch_cpu_setfpos_noerr(text)
        self.assertIn(MARKER_SETFPOS_NOERR, s)

    def test_patch_cpu_slot_26e90(self) -> None:
        from mill_apply import MARKER_SLOT_26E90, cpu_path, patch_cpu_slot_26e90

        text = cpu_path().read_text()
        out = patch_cpu_slot_26e90(text)
        self.assertIn(MARKER_SLOT_26E90, out)
        self.assertNotIn("0x26de0u", out)
        self.assertIn("0x26e90u", out)

    def test_patch_cpu_spin_26e88(self) -> None:
        from mill_apply import (
            MARKER_68K_R24,
            MARKER_SPIN_26E88,
            cpu_path,
            patch_cpu_spin_26e88,
        )

        text = cpu_path().read_text()
        if MARKER_68K_R24 in text or MARKER_SPIN_26E88 in text:
            return
        out = patch_cpu_spin_26e88(text)
        self.assertIn(MARKER_SPIN_26E88, out)
        self.assertIn("r24 - 2u == ROMBase + 0x26e88u", out)

    def test_skip_68k_never_idle(self) -> None:
        from mill_apply import (
            MARKER_68K_R24,
            cpu_path,
            patch_cpu_skip_68k,
            skip_68k_millable,
        )

        self.assertFalse(skip_68k_millable(0x366084))
        self.assertFalse(skip_68k_millable(0x3265A4))
        self.assertFalse(skip_68k_millable(0x3264FC))
        self.assertTrue(skip_68k_millable(0x26E8A))
        mill = {"map_keep_log_only": True}
        rev = ["leftover:spin-26e88"]
        off = next_skip_68k_off(mill, [], rev)
        self.assertIsNone(off)
        self.assertIsNone(force_skip_68k_off(mill, [], rev))
        from mill_apply import mill_68k_walk_ok

        oldw = os.environ.get("G3_68K_WALK")
        try:
            os.environ.pop("G3_68K_WALK", None)
            self.assertFalse(mill_68k_walk_ok())
        finally:
            if oldw is None:
                os.environ.pop("G3_68K_WALK", None)
            else:
                os.environ["G3_68K_WALK"] = oldw
        old = os.environ.get("G3_68K_WALK")
        try:
            os.environ["G3_68K_WALK"] = "1"
            from mill_apply import mill_68k_walk_ok as walk_ok

            self.assertTrue(walk_ok())
            off = next_skip_68k_off(mill, [], rev)
            self.assertIsNotNone(off)
            self.assertNotEqual(off, 0x26E88)
            self.assertTrue(skip_68k_millable(off))
            self.assertEqual(off, force_skip_68k_off(mill, [], rev))
            tested = [skip_68k_key(o) for o in range(0x26000, 0x28000, 2)]
            wrapped = force_skip_68k_off(mill, tested, rev)
            self.assertIsNotNone(wrapped)
            self.assertTrue(skip_68k_millable(wrapped))
            self.assertNotEqual(wrapped, 0x26E88)
        finally:
            if old is None:
                os.environ.pop("G3_68K_WALK", None)
            else:
                os.environ["G3_68K_WALK"] = old
        text = cpu_path().read_text()
        out = patch_cpu_skip_68k(text, 0x26E8A)
        self.assertIn(MARKER_68K_R24, out)
        from mill_apply import mill_stamp_68k

        self.assertIn(mill_stamp_68k(0x26E8A), out)
        self.assertIn("skip68 = 0x26e8au", out)
        out2 = patch_cpu_skip_68k(out, 0x26E8C)
        self.assertIn("skip68 = 0x26e8cu", out2)
        self.assertIn(mill_stamp_68k(0x26E8C), out2)
        self.assertNotIn(mill_stamp_68k(0x26E8A), out2)
        self.assertNotIn("skip68 = 0x26e8au", out2)

    def test_skip_68k_map_before_walk(self) -> None:
        from mill_apply import (
            mill_68k_walk_ok,
            next_skip_68k_off,
            skip_68k_loop_op,
        )

        self.assertTrue(skip_68k_loop_op(0x60FF))
        self.assertTrue(skip_68k_loop_op(0x4E75))
        self.assertFalse(skip_68k_loop_op(0x1ADC))
        td = Path(tempfile.mkdtemp())
        log = td / "keep.log"
        log.write_text(
            "NW-BOOT G3: 68k map r24=50028a26 op=4e75\n"
            "NW-BOOT G3: 68k map r24=5001f9ae op=266c\n"
            "NW-BOOT G3: 68k spin r24=50008556 op=072a\n"
        )
        mill = {"keep_log": str(log), "map_keep_log_only": True}
        rev = ["leftover:spin-26e88"]
        off = next_skip_68k_off(mill, [], rev)
        self.assertEqual(off, 0x1F9AE)
        off2 = next_skip_68k_off(
            mill, ["leftover:skip-68k:0001f9ae"], rev
        )
        self.assertEqual(off2, 0x8556)
        self.assertFalse(mill_68k_walk_ok())
        off3 = next_skip_68k_off(
            mill,
            ["leftover:skip-68k:0001f9ae", "leftover:skip-68k:00008556"],
            rev,
        )
        self.assertIsNone(off3)

    def test_skip_68k_prefers_trap_pc(self) -> None:
        td = Path(tempfile.mkdtemp())
        log = td / "keep.log"
        log.write_text(
            "NW-BOOT G3: 68k map r24=500170e6 op=4e71\n"
            "NW-BOOT G3: 68k GetNewDialog A97C pc=5005c86e\n"
            "NW-BOOT G3: 68k DialogDispatch AA68 sel=0304 fill\n"
        )
        mill = {"keep_log": str(log), "map_keep_log_only": True}
        off = next_skip_68k_off(mill, [], [])
        # GetNewDialog overlay 0x5c86e is UI path — do not mill skip; map NOP is next.
        self.assertEqual(off, 0x170E6)
        log2 = td / "keep2.log"
        log2.write_text(
            "NW-BOOT G3: 68k map r24=500170e6 op=4e71\n"
            "NW-BOOT G3: 68k InitCPort ABE8 pc=50012346\n"
        )
        mill2 = {"keep_log": str(log2), "map_keep_log_only": True}
        self.assertEqual(next_skip_68k_off(mill2, [], []), 0x12346)

    def test_skip_68k_rom_findings_block_ui_fs(self) -> None:
        from mill_apply import (
            LOOK_AGAIN_SKIP_68K,
            leftover_map_remaining,
            skip_68k_millable,
            skip_68k_ui_op,
        )

        self.assertFalse(skip_68k_millable(0x5C86C))
        self.assertFalse(skip_68k_millable(0x5C86E))
        self.assertFalse(skip_68k_millable(0x5C888))
        self.assertFalse(skip_68k_millable(0x5C8AA))
        self.assertFalse(skip_68k_millable(0x16DE8))
        self.assertFalse(skip_68k_millable(0x9440))
        self.assertFalse(skip_68k_millable(0x94C2))
        self.assertFalse(skip_68k_millable(0x94CE))
        for o in LOOK_AGAIN_SKIP_68K:
            self.assertFalse(skip_68k_millable(o), "look-again 0x%x" % o)
        self.assertTrue(skip_68k_ui_op(0xA97C))
        self.assertTrue(skip_68k_ui_op(0xA97D))
        self.assertTrue(skip_68k_ui_op(0xAA1B))
        self.assertTrue(skip_68k_ui_op(0xAA68))
        self.assertTrue(skip_68k_ui_op(0xA9C9))
        self.assertTrue(skip_68k_ui_op(0xA06E))
        self.assertFalse(skip_68k_ui_op(0x4E71))
        td = Path(tempfile.mkdtemp())
        log = td / "keep.log"
        log.write_text(
            "NW-BOOT G3: 68k map r24=500170e6 op=a97c\n"
            "NW-BOOT G3: 68k map r24=50017100 op=4e71\n"
            "NW-BOOT G3: 68k map r24=50016fc2 op=a06e\n"
        )
        mill = {"keep_log": str(log), "map_keep_log_only": True}
        off = next_skip_68k_off(mill, [], [])
        self.assertEqual(off, 0x17100)
        remain, _p, n = leftover_map_remaining(mill, [], [], limit=10)
        self.assertEqual(remain, [0x17100])
        self.assertEqual(n, 1)


class TokenTests(unittest.TestCase):
    def test_format_tokens(self) -> None:
        self.assertEqual(
            format_tokens("grok", {"in": 1, "out": 2, "total": 3}, "build"),
            "TOKENS grok in=1 out=2 total=3 (build)",
        )
        self.assertEqual(
            format_tokens("grok", zero_usage(), "mill canned"),
            "TOKENS grok in=0 out=0 total=0 (mill canned)",
        )

    def test_attempts_table_sums(self) -> None:
        table = format_attempts_table(
            [
                {
                    "n": 6,
                    "kind": "skip-hang",
                    "hang_off": 0x326510,
                    "result": "KEEP",
                    "grok": {"in": 0, "out": 0, "total": 0},
                },
                {
                    "n": 7,
                    "kind": "skip-hang",
                    "hang_off": 0x326640,
                    "result": "REVERT",
                    "grok": {"in": 10, "out": 2, "total": 12},
                },
            ]
        )
        self.assertIn("326510", table)
        self.assertIn("KEEP", table)
        self.assertIn("REVERT", table)
        self.assertIn("SUM", table)
        self.assertIn("26", table)

    def test_fmt_sec_and_elapsed_column(self) -> None:
        self.assertEqual(_fmt_sec(12.34), "12.3s")
        self.assertEqual(_fmt_sec(75), "1m15s")
        table = format_attempts_table(
            [
                {
                    "n": 40,
                    "kind": "slot-26e90",
                    "hang_off": 0x366084,
                    "result": "KEEP",
                    "elapsed_sec": 52.3,
                    "grok": {},
                }
            ]
        )
        self.assertIn("52.3s", table)
        self.assertIn("elapsed", table)


class G3LockTests(unittest.TestCase):
    def test_g3_lock_requires_window_yes_and_g2(self) -> None:
        r = score_g3({"g2_live": True, "parsed": {}}, window="unknown")
        self.assertTrue(r["skipped"])
        self.assertEqual(r["g3"], "no")
        r2 = score_g3({"g2_live": True, "parsed": {}}, window="no")
        self.assertTrue(r2["skipped"])
        self.assertEqual(r2["g3"], "no")
        r3 = score_g3({"g2_live": True, "parsed": {}}, window="yes")
        self.assertFalse(r3["skipped"])
        self.assertEqual(r3["g3"], "yes")
        r4 = score_g3({"g2_live": False, "parsed": {}}, window="yes")
        self.assertEqual(r4["g3"], "no")


class PackTests(unittest.TestCase):
    def test_pack_is_bulk_not_full_cpu(self) -> None:
        st = {
            "mill": {
                "keep_log": str(G2_SNIP),
                "keep_pc": 0x50326510,
                "tested": ["leftover:skip-hang:00326510"],
                "reverted_kinds": ["leftover:unstick-stw"],
                "attempts": [
                    {
                        "n": 6,
                        "kind": "skip-hang",
                        "hang_off": 0x326510,
                        "result": "KEEP",
                        "g3": "no",
                        "grok": {"in": 0, "out": 0, "total": 0},
                    }
                ],
                "tokens": {
                    "grok": {"in": 0, "out": 0, "total": 0},
                },
            }
        }
        pack = pack_from_state(st)
        md = format_pack_md(pack)
        self.assertIn("3264fc", md)
        self.assertIn("skip-list 50325", md)
        self.assertIn("GetNewDialog", md)
        self.assertIn("0x5c86c-0x5c8c0", md)
        self.assertIn("MILL 1", md)
        self.assertIn("Job (new session: do this)", md)
        self.assertIn("mill_apply.py", md)
        self.assertIn("Every mill", md)
        self.assertIn("skip-68k", md)
        self.assertNotIn("powerpc_cpu::execute", md)
        self.assertIn("KEEP", md)

    def test_pack_keeps_every_mill(self) -> None:
        st = {
            "mill": {
                "attempts": [
                    {
                        "n": 6,
                        "kind": "skip-hang",
                        "hang_off": 0x326510,
                        "result": "KEEP",
                        "grok": {},
                    },
                    {
                        "n": 7,
                        "kind": "skip-hang",
                        "hang_off": 0x326640,
                        "result": "REVERT",
                        "grok": {},
                    },
                ]
            }
        }
        md = format_pack_md(pack_from_state(st))
        self.assertIn("### mill-6 KEEP", md)
        self.assertIn("### mill-7 REVERT", md)
        log = Path(tempfile.mkdtemp()) / "pack-log.md"
        pack = pack_from_state(st)
        append_pack_log(pack, log)
        append_pack_log(pack, log)
        text = log.read_text()
        self.assertEqual(text.count("### mill-6 "), 1)
        self.assertEqual(text.count("### mill-7 "), 1)

    def test_night_pack_fills_logs_not_in_attempts(self) -> None:
        import mill_pack

        td = Path(tempfile.mkdtemp())
        log = td / "ss-g3-mill-4.log"
        log.write_text(
            "NW-BOOT G2: first DSI SRR0=PC DR on HIT no second DSI\n"
            "NW-BOOT G3: KEEP hang skip pc=50326510 off=00326510\n"
            "NW-BOOT heartbeat pc=50326510 msr=00003010 same=0\n"
        )
        old = mill_pack._log_for_n

        def fake_log(n, mill):
            if n == 4:
                return log
            return old(n, mill)

        mill_pack._log_for_n = fake_log
        try:
            st = {"mill": {"n": 4, "attempts": []}}
            pack = pack_from_state(st)
            ns = [a["n"] for a in pack["attempts"]]
            self.assertIn(4, ns)
            md = format_pack_md(pack)
            self.assertIn("Pack:", md)
            self.assertIn("### mill-4", md)
        finally:
            mill_pack._log_for_n = old

    def test_hangcap_early_fail_is_hang04_not_walk(self) -> None:
        self.assertEqual(
            hangcap_early_fail("NW-BOOT G3: hang 04cecd36 pc=04cecd36\n"),
            "hang_04cecd36",
        )
        self.assertIsNone(
            hangcap_early_fail(
                "NW-BOOT G2: picspin mill=0\n"
                "NW-BOOT heartbeat pc=50326510 msr=00003010 same=0\n"
            )
        )
        self.assertEqual(
            hangcap_early_fail("G2: picspin n=1 mill=1\n"),
            "mill",
        )
        # KEEP mill-1116: 50326 walk then 68k. Do not abort on 50326 before 68k.
        pre68 = (
            "NW-BOOT G2: first DSI SRR0=50325a14 DAR=10010002 DRhit=1\n"
            "NW-BOOT heartbeat pc=50326510 msr=00003010 same=0\n"
        )
        self.assertIsNone(hangcap_early_fail(pre68, saw_68k=True))
        lost = (
            pre68
            + "NW-BOOT heartbeat pc=50366084 msr=00001010 same=1\n"
            + "NW-BOOT heartbeat pc=50326550 msr=00003010 same=0\n"
        )
        self.assertEqual(hangcap_early_fail(lost, saw_68k=True), "68k_loss")
        keep68 = (
            pre68
            + "NW-BOOT heartbeat pc=50366084 msr=00001010 same=6\n"
        )
        self.assertIsNone(hangcap_early_fail(keep68, saw_68k=True))
        self.assertFalse(hangcap_keep_stable(keep68, n=8))
        stable = pre68 + (
            "NW-BOOT heartbeat pc=50366084 msr=00001010 same=0\n" * 8
        )
        self.assertTrue(hangcap_keep_stable(stable, n=8))
        self.assertIsNone(hangcap_early_fail(stable, saw_68k=True))
        self.assertFalse(
            hangcap_keep_stable(
                pre68
                + "NW-BOOT heartbeat pc=50366084 msr=00001010 same=0\n" * 7
                + "NW-BOOT heartbeat pc=50326510 msr=00003010 same=0\n",
                n=8,
            )
        )

    def test_hangcap_g0_stuck(self) -> None:
        g0 = (
            "SDL_Init done\n"
            "NW-BOOT G0: DecodeROM 4 MiB NewWorld +0x30d064 NK +0x310000\n"
        )
        self.assertFalse(hangcap_g0_stuck(g0, 5.0))
        self.assertTrue(hangcap_g0_stuck(g0, 15.0))
        self.assertFalse(
            hangcap_g0_stuck(g0 + "NW-BOOT G1: HardwareInit handoff NK\n", 20.0)
        )

    def test_rom_disasm_68k_and_ppc_no_rom_file(self) -> None:
        from rom_disasm import (
            A_LINE,
            classify_off,
            decode_rom_image,
            disasm_68k_one,
            ppc_one,
        )

        rom = bytearray(0x400000)
        rom[0x10] = 0x4E
        rom[0x11] = 0x75
        rom[0x20] = 0xA9
        rom[0x21] = 0x7C
        rom[0x30] = 0x60
        rom[0x31] = 0xFE
        rts = disasm_68k_one(bytes(rom), 0x10)
        self.assertEqual(rts["kind"], "rts")
        self.assertEqual(rts["text"], "RTS")
        trap = disasm_68k_one(bytes(rom), 0x20)
        self.assertEqual(trap["kind"], "aline")
        self.assertIn("GetNewDialog", trap["text"])
        bra = disasm_68k_one(bytes(rom), 0x30)
        self.assertEqual(bra["kind"], "bra_star")
        self.assertEqual(ppc_one(0x900107D4), "stw")
        self.assertEqual(ppc_one(0x7C0604A6), "mfsr")
        self.assertEqual(A_LINE[0xAA68], "DialogDispatch")
        self.assertEqual(A_LINE[0xA88F], "InitCursor")
        self.assertEqual(A_LINE[0xA9C9], "SysError")
        self.assertEqual(A_LINE[0xA9A0], "GetResource")
        self.assertEqual(A_LINE[0xAA1B], "GetCCursor")
        self.assertEqual(A_LINE[0xA01F], "GetEOF")
        self.assertEqual(A_LINE[0xA023], "GetFPos")
        c = classify_off(bytes(rom), 0x3264FC)
        self.assertTrue(c["hard"])
        self.assertFalse(c["millable"])
        from rom_disasm import format_report, region_tag

        self.assertEqual(region_tag(0x5C86E), "ui-dialog-path")
        self.assertEqual(region_tag(0x9440), "code66-helper")
        c_help = classify_off(bytes(rom), 0x9440)
        self.assertFalse(c_help["millable"])
        self.assertIn("code66-helper", c_help["note"])
        self.assertEqual(region_tag(0x16DE8), "a190-data-table")
        self.assertEqual(region_tag(0x16FC2), "look-again-keep")
        c_ui = classify_off(bytes(rom), 0x5C86E)
        self.assertFalse(c_ui["millable"])
        self.assertIn("ui-dialog-path", c_ui["note"])
        c_look = classify_off(bytes(rom), 0x16FC2)
        self.assertFalse(c_look["millable"])
        self.assertIn("look-again-keep", c_look["note"])
        md = format_report(bytes(rom), [0x5C86E], count=2, nk=False)
        self.assertIn("Do not skip-68k", md)
        self.assertIn("look-again KEEP", md)
        self.assertIn("0x5c86c", md)
        self.assertEqual(decode_rom_image(b"\x00" * 0x400000).__class__, bytes)

    def test_classify_68k_hang_last_hb(self) -> None:
        text = (
            "NW-BOOT G2: first DSI SRR0=50325a14 DAR=10010002 DRhit=1\n"
            "NW-BOOT G2: first DSI SRR0=PC DR on HIT no second DSI\n"
            "NW-BOOT heartbeat pc=50366084 msr=00003010 same=8\n"
        )
        report = classify_text(text)
        self.assertEqual(report["LIVE_CLASS"], "68k-hang")
        self.assertTrue(report["parsed"]["reached_68k"])

    def test_mill_binary_match_stamp(self) -> None:
        from debug_run import binary_has_stamp, mill_binary_match
        from mill_apply import mill_stamp_68k

        td = Path(tempfile.mkdtemp())
        app = td / "SheepShaver"
        app.write_bytes(b"hdr " + mill_stamp_68k(0x27614).encode() + b" tail")
        self.assertTrue(binary_has_stamp(app, mill_stamp_68k(0x27614)))
        self.assertTrue(mill_binary_match(app, kind="skip-68k", hang_off=0x27614))
        self.assertFalse(mill_binary_match(app, kind="skip-68k", hang_off=0x27616))
        rt = td / "SheepShaver-rt"
        rt.write_bytes(b"G3: 68k map r24= only")
        self.assertFalse(
            mill_binary_match(rt, kind="skip-68k", hang_off=0x27614, runtime=True)
        )
        rt.write_bytes(
            b"G3: 68k map r24= G3: FB dump packed xRGB "
            b"G3: 68k GetNewDialog A97C"
        )
        self.assertTrue(
            mill_binary_match(rt, kind="skip-68k", hang_off=0x27614, runtime=True)
        )
        self.assertFalse(mill_binary_match(rt, kind="grok-escalate"))
        rt.write_bytes(b"G3: 68k LoadSeg A9F0 enter")
        self.assertTrue(mill_binary_match(rt, kind="grok-escalate"))
        self.assertFalse(mill_binary_match(rt, kind="getresource-a9a0"))
        rt.write_bytes(b"G3: 68k GetResource A9A0 toast")
        self.assertTrue(mill_binary_match(rt, kind="getresource-a9a0"))
        self.assertFalse(mill_binary_match(rt, kind="getnewdialog-dlog"))
        rt.write_bytes(b"G3: 68k GetNewDialog A97C toast")
        self.assertTrue(mill_binary_match(rt, kind="getnewdialog-dlog"))
        self.assertFalse(mill_binary_match(rt, kind="code66-syserr99"))
        rt.write_bytes(b"G3: 68k CODE 66 SysError 99 continue")
        self.assertTrue(mill_binary_match(rt, kind="code66-syserr99"))
        self.assertFalse(mill_binary_match(rt, kind="code66-resume"))
        rt.write_bytes(b"G3: 68k LoadSeg A9F0 CODE 66 resume")
        self.assertTrue(mill_binary_match(rt, kind="code66-resume"))
        self.assertFalse(mill_binary_match(rt, kind="code66-allow-9440"))
        rt.write_bytes(b"G3: 68k CODE 66 allow 0x9440")
        self.assertTrue(mill_binary_match(rt, kind="code66-allow-9440"))
        self.assertFalse(mill_binary_match(rt, kind="launch-upgrader"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader")
        self.assertTrue(mill_binary_match(rt, kind="launch-upgrader"))
        self.assertFalse(mill_binary_match(rt, kind="splash-510"))
        rt.write_bytes(b"G3: 68k GetNewDialog A97C Splash 510 dlg=10005000")
        self.assertTrue(mill_binary_match(rt, kind="splash-510"))
        self.assertFalse(mill_binary_match(rt, kind="splash-510-even"))
        rt.write_bytes(b"G3: 68k GetNewDialog A97C Splash 510 even dlg=1005130c")
        self.assertTrue(mill_binary_match(rt, kind="splash-510-even"))
        self.assertFalse(mill_binary_match(rt, kind="pict-1000"))
        rt.write_bytes(b"G3: 68k DrawPicture A8F6 PICT 1000")
        self.assertTrue(mill_binary_match(rt, kind="pict-1000"))
        self.assertFalse(mill_binary_match(rt, kind="pef-upgrader"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF p=10100000")
        self.assertTrue(mill_binary_match(rt, kind="pef-upgrader"))
        self.assertFalse(mill_binary_match(rt, kind="pef-enter"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF enter pc=101013d0")
        self.assertTrue(mill_binary_match(rt, kind="pef-enter"))
        self.assertFalse(mill_binary_match(rt, kind="pef-imports"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF import idx=1 r3=10115c5e")
        self.assertTrue(mill_binary_match(rt, kind="pef-imports"))
        self.assertFalse(mill_binary_match(rt, kind="pef-sysenv"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF SysEnvirons ver=2 rec=10115c00")
        self.assertTrue(mill_binary_match(rt, kind="pef-sysenv"))
        self.assertFalse(mill_binary_match(rt, kind="pef-vol"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF vol idx=70 r3=00000000")
        self.assertTrue(mill_binary_match(rt, kind="pef-vol"))
        self.assertFalse(mill_binary_match(rt, kind="pef-dce"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF dce h=10180040")
        self.assertTrue(mill_binary_match(rt, kind="pef-dce"))
        self.assertFalse(mill_binary_match(rt, kind="pef-wait"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF WaitNextEvent idx=149 r3=00000001")
        self.assertTrue(mill_binary_match(rt, kind="pef-wait"))
        self.assertFalse(mill_binary_match(rt, kind="pef-te"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF TENew idx=263 r3=10180080")
        self.assertTrue(mill_binary_match(rt, kind="pef-te"))
        self.assertFalse(mill_binary_match(rt, kind="pef-terec"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF TERec h=10180080")
        self.assertTrue(mill_binary_match(rt, kind="pef-terec"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skipte"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skipTE pc=1010b404")
        self.assertTrue(mill_binary_match(rt, kind="pef-skipte"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skipdi"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skipDI pc=1010b3dc")
        self.assertTrue(mill_binary_match(rt, kind="pef-skipdi"))
        self.assertFalse(mill_binary_match(rt, kind="pef-idx"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF idx n=295")
        self.assertTrue(mill_binary_match(rt, kind="pef-idx"))
        self.assertFalse(mill_binary_match(rt, kind="pef-gnd"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF GetNewDialog 510 dlg=1005130c")
        self.assertTrue(mill_binary_match(rt, kind="pef-gnd"))
        self.assertFalse(mill_binary_match(rt, kind="pef-gndid"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF GetNewDialog id=150 dlg=00000000")
        self.assertTrue(mill_binary_match(rt, kind="pef-gndid"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skipwait"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skipWait")
        self.assertTrue(mill_binary_match(rt, kind="pef-skipwait"))
        self.assertFalse(mill_binary_match(rt, kind="pef-callsplash"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF callSplash")
        self.assertTrue(mill_binary_match(rt, kind="pef-callsplash"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skipalert"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skipAlert")
        self.assertTrue(mill_binary_match(rt, kind="pef-skipalert"))
        self.assertFalse(mill_binary_match(rt, kind="pef-nimp"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF nimp")
        self.assertTrue(mill_binary_match(rt, kind="pef-nimp"))
        self.assertFalse(mill_binary_match(rt, kind="pef-jumpsplash"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF jumpSplash")
        self.assertTrue(mill_binary_match(rt, kind="pef-jumpsplash"))
        self.assertFalse(mill_binary_match(rt, kind="pef-plantsplash"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF plantSplash")
        self.assertTrue(mill_binary_match(rt, kind="pef-plantsplash"))
        self.assertFalse(mill_binary_match(rt, kind="pef-forceblit"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF forceBlit")
        self.assertTrue(mill_binary_match(rt, kind="pef-forceblit"))
        self.assertFalse(mill_binary_match(rt, kind="pef-blitoff"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF blitOff")
        self.assertTrue(mill_binary_match(rt, kind="pef-blitoff"))
        self.assertFalse(mill_binary_match(rt, kind="pef-callgnd"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF callGnd")
        self.assertTrue(mill_binary_match(rt, kind="pef-callgnd"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skipae"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skipAE")
        self.assertTrue(mill_binary_match(rt, kind="pef-skipae"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skipheap"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skipHeap")
        self.assertTrue(mill_binary_match(rt, kind="pef-skipheap"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skipb7"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skipB7")
        self.assertTrue(mill_binary_match(rt, kind="pef-skipb7"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skip20ec"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skip20ec")
        self.assertTrue(mill_binary_match(rt, kind="pef-skip20ec"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skip21bc"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skip21bc")
        self.assertTrue(mill_binary_match(rt, kind="pef-skip21bc"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skip2fb8"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skip2fb8")
        self.assertTrue(mill_binary_match(rt, kind="pef-skip2fb8"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skipglue"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skipGlue")
        self.assertTrue(mill_binary_match(rt, kind="pef-skipglue"))
        self.assertFalse(mill_binary_match(rt, kind="pef-alert"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF Alert")
        self.assertTrue(mill_binary_match(rt, kind="pef-alert"))
        self.assertFalse(mill_binary_match(rt, kind="pef-glue0"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF glue idx=125")
        self.assertTrue(mill_binary_match(rt, kind="pef-glue0"))
        self.assertFalse(mill_binary_match(rt, kind="pef-tocpict"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF tocPict")
        self.assertTrue(mill_binary_match(rt, kind="pef-tocpict"))
        self.assertFalse(mill_binary_match(rt, kind="pef-maindev"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF MainDevice")
        self.assertTrue(mill_binary_match(rt, kind="pef-maindev"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skipgmd"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skipGmd")
        self.assertTrue(mill_binary_match(rt, kind="pef-skipgmd"))
        self.assertFalse(mill_binary_match(rt, kind="pef-newptrc"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF NewPtrClear")
        self.assertTrue(mill_binary_match(rt, kind="pef-newptrc"))
        self.assertFalse(mill_binary_match(rt, kind="pef-nrd"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF NewRoutineDescriptor")
        self.assertTrue(mill_binary_match(rt, kind="pef-nrd"))
        self.assertFalse(mill_binary_match(rt, kind="pef-nourf"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF noUrF")
        self.assertTrue(mill_binary_match(rt, kind="pef-nourf"))
        self.assertFalse(mill_binary_match(rt, kind="pef-cup"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF CallUniversalProc")
        self.assertTrue(mill_binary_match(rt, kind="pef-cup"))
        self.assertFalse(mill_binary_match(rt, kind="pef-tick"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF TickCount")
        self.assertTrue(mill_binary_match(rt, kind="pef-tick"))
        self.assertFalse(mill_binary_match(rt, kind="pef-drawdlg"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF DrawDialog")
        self.assertTrue(mill_binary_match(rt, kind="pef-drawdlg"))
        self.assertFalse(mill_binary_match(rt, kind="pef-sizewin"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF SizeWindow")
        self.assertTrue(mill_binary_match(rt, kind="pef-sizewin"))
        self.assertFalse(mill_binary_match(rt, kind="pef-setditm"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF SetDialogItem")
        self.assertTrue(mill_binary_match(rt, kind="pef-setditm"))
        self.assertFalse(mill_binary_match(rt, kind="pef-nrdblr"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF nrdBlr p=10180010")
        self.assertTrue(mill_binary_match(rt, kind="pef-nrdblr"))
        self.assertFalse(mill_binary_match(rt, kind="pef-i042"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF void idx=%u i042")
        self.assertTrue(mill_binary_match(rt, kind="pef-i042"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF void idx=%u i042 i050")
        self.assertTrue(mill_binary_match(rt, kind="pef-i050"))
        self.assertFalse(mill_binary_match(rt, kind="pef-frontwin"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF FrontWindow w=1005130c")
        self.assertTrue(mill_binary_match(rt, kind="pef-frontwin"))
        self.assertFalse(mill_binary_match(rt, kind="pef-getport"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF GetPort p=1005130c")
        self.assertTrue(mill_binary_match(rt, kind="pef-getport"))
        self.assertFalse(mill_binary_match(rt, kind="pef-stublr"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF stubLR lr=10116000 to=101014b4")
        self.assertTrue(mill_binary_match(rt, kind="pef-stublr"))
        self.assertFalse(mill_binary_match(rt, kind="pef-textfont"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF TextFont")
        self.assertTrue(mill_binary_match(rt, kind="pef-textfont"))
        self.assertFalse(mill_binary_match(rt, kind="pef-hostlr"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF hostLR idx=117 lr=10116000 r0=00000000 s8=10102a00")
        self.assertTrue(mill_binary_match(rt, kind="pef-hostlr"))
        self.assertFalse(mill_binary_match(rt, kind="pef-hostlr2"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF hostLR2 idx=117 lr=10102a00 r0=10116000 s8=10101d68")
        self.assertTrue(mill_binary_match(rt, kind="pef-hostlr2"))
        self.assertFalse(mill_binary_match(rt, kind="pef-navrun"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF NavServicesCanRun")
        self.assertTrue(mill_binary_match(rt, kind="pef-navrun"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skipunld"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skipUnld")
        self.assertTrue(mill_binary_match(rt, kind="pef-skipunld"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skipnav"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skipNav")
        self.assertTrue(mill_binary_match(rt, kind="pef-skipnav"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skipdisp"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skipDisp")
        self.assertTrue(mill_binary_match(rt, kind="pef-skipdisp"))
        self.assertFalse(mill_binary_match(rt, kind="pef-skipurf"))
        rt.write_bytes(b"G3: 68k Launch A9F2 CFM Upgrader PEF skipUrf")
        self.assertTrue(mill_binary_match(rt, kind="pef-skipurf"))

    def test_grok_build_cmd_is_headless_not_http(self) -> None:
        from grok_build import grok_build_enabled, grok_cmd, write_grok_prompt

        self.assertTrue(grok_build_enabled())
        slim = Path(tempfile.mkdtemp()) / "pack-slim.md"
        slim.write_text("# slim\n")
        prompt = write_grok_prompt(slim, dest=slim.parent / "prompt.md")
        cmd = grok_cmd(slim, prompt)
        joined = " ".join(cmd)
        self.assertIn("--prompt-file", joined)
        self.assertIn("--permission-mode", joined)
        self.assertIn("bypassPermissions", joined)
        self.assertIn("--leader-socket", joined)
        self.assertIn("/tmp/ss-g3-grok-leader.sock", joined)
        self.assertNotIn("api.x.ai", joined)
        self.assertNotIn("/v1/chat", joined)
        self.assertIn("read_file,search_replace", joined)
        ptxt = prompt.read_text()
        self.assertIn("GetCCursor", ptxt)
        self.assertIn("0x5c86c-0x5c8c0", ptxt)

    def test_pack_slim_is_short_and_has_reply(self) -> None:
        st = {
            "mill": {
                "n": 2728,
                "keep_pc": 0x50366084,
                "keep_count": 394,
                "revert_count": 2347,
                "keep_log": None,
                "attempts": [
                    {
                        "n": 2728,
                        "kind": "skip-68k",
                        "hang_off": 0x51724,
                        "result": "KEEP",
                        "g3": "no",
                    }
                ],
                "tested": [],
                "reverted_kinds": [],
            }
        }
        md = format_pack_slim_md(st)
        self.assertIn("Grok Build", md)
        self.assertIn("MILL 1:", md)
        self.assertIn("Map remaining", md)
        self.assertIn("16384 map lines/hang-cap", md)
        self.assertNotIn("4096 unique cap", md)
        self.assertNotIn("## Every mill", md)
        self.assertLess(len(md), 20000)

    def test_patch_cpu_cfm_aa5a_and_trap_68k(self) -> None:
        from mill_apply import (
            MARKER_CFM_AA5A,
            MARKER_TRAP_68K,
            cpu_path,
            patch_cpu_cfm_aa5a,
            patch_cpu_trap_68k,
        )

        text = cpu_path().read_text()
        if MARKER_CFM_AA5A not in text:
            out = patch_cpu_cfm_aa5a(text)
            self.assertIn(MARKER_CFM_AA5A, out)
            self.assertIn("sel == 0xfffcu", out)
            text = out
        if MARKER_TRAP_68K not in text:
            out = patch_cpu_trap_68k(text)
            self.assertIn(MARKER_TRAP_68K, out)

    def test_hangcap_media_paths_and_copy(self) -> None:
        from debug_run import copy_hangcap_media, hangcap_media_paths

        log = Path("/tmp/ss-g3-mill-42.log")
        p = hangcap_media_paths(log)
        self.assertEqual(p["log"], log)
        self.assertEqual(p["log_gz"], Path("/tmp/ss-g3-mill-42.log.gz"))
        self.assertEqual(p["png"], Path("/tmp/ss-g3-mill-42.png"))
        self.assertEqual(p["plant_png"], Path("/tmp/ss-g3-mill-42-fb-plant.png"))
        self.assertEqual(p["fb"], Path("/tmp/ss-g3-mill-42-fb.pgm"))
        self.assertEqual(p["plant"], Path("/tmp/ss-g3-mill-42-fb-plant.pgm"))
        gz_in = hangcap_media_paths(Path("/tmp/ss-g3-mill-42.log.gz"))
        self.assertEqual(gz_in["png"], Path("/tmp/ss-g3-mill-42.png"))
        td = Path(tempfile.mkdtemp())
        src = td / "ss-g3-mill-7.log"
        src.write_text("log\n")
        (td / "ss-g3-mill-7.png").write_bytes(b"png")
        dest = td / "out"
        copy_hangcap_media(src, dest)
        self.assertFalse((dest / "ss-g3-mill-7.log").exists())
        self.assertTrue((dest / "ss-g3-mill-7.log.gz").is_file())
        self.assertTrue((dest / "ss-g3-mill-7.png").is_file())
        self.assertFalse((dest / "ss-g3-mill-7-fb.pgm").exists())
        from mill_log import read_log

        self.assertEqual(read_log(dest / "ss-g3-mill-7.log.gz"), "log\n")
        self.assertTrue(src.is_file())

    def test_pgm_to_png_guest_fb(self) -> None:
        from debug_run import pgm_to_png, png_is_valid, read_pgm

        td = Path(tempfile.mkdtemp())
        pgm = td / "fb.pgm"
        png = td / "fb.png"
        pixels = bytes([0, 255, 128, 64])
        pgm.write_bytes(b"P5\n2 2\n255\n" + pixels)
        self.assertEqual(read_pgm(pgm), (2, 2, pixels))
        self.assertTrue(pgm_to_png(pgm, png))
        self.assertTrue(png_is_valid(png))
        self.assertGreater(png.stat().st_size, 32)
        self.assertFalse(pgm_to_png(td / "missing.pgm", td / "no.png"))
        ppm = td / "fb.ppm"
        rgb_png = td / "fb-rgb.png"
        ppm.write_bytes(b"P6\n2 1\n255\n" + bytes([255, 0, 0, 0, 255, 0]))
        self.assertEqual(read_pgm(ppm)[0:2], (2, 1))
        self.assertTrue(pgm_to_png(ppm, rgb_png))
        self.assertTrue(png_is_valid(rgb_png))

    def test_notify_skip68k_empty_glass(self) -> None:
        from unittest.mock import patch
        from debug_run import notify_skip68k_empty

        with patch("debug_run.subprocess.call") as call:
            with patch.dict("os.environ", {"G3_FB_NOTIFY": "0"}):
                self.assertFalse(notify_skip68k_empty({"n": 16123}))
                call.assert_not_called()
            with patch.dict("os.environ", {"G3_FB_NOTIFY": "1"}):
                self.assertTrue(notify_skip68k_empty({"n": 16123}))
                self.assertTrue(call.called)
                joined = " ".join(str(c) for c in call.call_args_list)
                self.assertIn("osascript", joined)

    def test_notify_fb_glass_only_when_fb_yes(self) -> None:
        from unittest.mock import patch
        from debug_run import notify_fb_glass

        log = Path("/tmp/ss-g3-mill-9.log")
        with patch("debug_run.subprocess.call") as call:
            self.assertFalse(notify_fb_glass(log, {"fb": "no", "plant": "yes"}))
            call.assert_not_called()
            self.assertFalse(notify_fb_glass(log, {"fb": "unknown"}))
            call.assert_not_called()
            with patch.dict("os.environ", {"G3_FB_NOTIFY": "0"}):
                self.assertFalse(notify_fb_glass(log, {"fb": "yes"}))
                call.assert_not_called()
            with patch.dict("os.environ", {"G3_FB_NOTIFY": "1"}):
                self.assertTrue(notify_fb_glass(log, {"fb": "yes", "plant": "no"}))
                self.assertTrue(call.called)
                joined = " ".join(str(c) for c in call.call_args_list)
                self.assertIn("osascript", joined)

    def test_screenshot_sheepshaver_window_id(self) -> None:
        from unittest.mock import patch
        from debug_run import screenshot_sheepshaver

        td = Path(tempfile.mkdtemp())
        dest = td / "shot.png"

        class R:
            returncode = 0
            stdout = "12345\n"

        def fake_call(cmd, **kwargs):
            from debug_run import PNG_MAGIC

            dest.write_bytes(PNG_MAGIC + b"\x00" * 16)
            self.assertIn("screencapture", cmd[0])
            self.assertIn("-t", cmd)
            self.assertIn("png", cmd)
            self.assertIn("-l", cmd)
            self.assertIn("12345", cmd)
            return 0

        with patch("debug_run.trip_screen_recording", return_value={"ok": True}):
            with patch("debug_run.subprocess.run", return_value=R()):
                with patch("debug_run.subprocess.call", side_effect=fake_call):
                    self.assertTrue(screenshot_sheepshaver(dest))

    def test_trip_screen_recording_screencapture_probe(self) -> None:
        from unittest.mock import patch
        import debug_run

        debug_run._SCREEN_RECORDING_TRIPPED = False
        calls = []

        def fake_call(cmd, **kwargs):
            from debug_run import PNG_MAGIC

            calls.append(cmd)
            self.assertIn("-t", cmd)
            self.assertIn("png", cmd)
            Path(cmd[-1]).write_bytes(PNG_MAGIC + b"\x00" * 16)
            return 0

        with patch("debug_run.ctypes.util.find_library", return_value=None):
            with patch("debug_run.subprocess.call", side_effect=fake_call):
                r = debug_run.trip_screen_recording()
        self.assertTrue(r["screencapture"])
        self.assertTrue(r["ok"])
        self.assertEqual(calls[0][0], "screencapture")
        self.assertIn("-R", calls[0])
        self.assertTrue(debug_run._SCREEN_RECORDING_TRIPPED)

    def test_png_is_valid_rejects_tcc_stub(self) -> None:
        from debug_run import PNG_MAGIC, png_is_valid, unlink_invalid_png

        td = Path(tempfile.mkdtemp())
        stub = td / "stub.png"
        stub.write_bytes(b"PNG")
        self.assertFalse(png_is_valid(stub))
        unlink_invalid_png(stub)
        self.assertFalse(stub.exists())
        real = td / "real.png"
        real.write_bytes(PNG_MAGIC + b"\x00" * 16)
        self.assertTrue(png_is_valid(real))
        unlink_invalid_png(real)
        self.assertTrue(real.exists())

    def test_mill_log_gzip_stream_roundtrip(self) -> None:
        from mill_apply import _68k_pairs_from_log
        from mill_log import gzip_log, gzip_mill_logs, read_log, resolve_log

        td = Path(tempfile.mkdtemp())
        plain = td / "ss-g3-mill-3.log"
        body = (
            "NW-BOOT heartbeat pc=50366084 msr=00003010 same=8\n"
            "NW-BOOT G3: 68k map r24=500264d4 op=4e75\n"
        )
        plain.write_text(body)
        gz = gzip_log(plain, unlink_src=True)
        self.assertIsNotNone(gz)
        self.assertTrue(gz.is_file())
        self.assertFalse(plain.exists())
        self.assertEqual(read_log(plain), body)
        self.assertEqual(read_log(gz), body)
        self.assertEqual(resolve_log(plain), gz)
        pairs = _68k_pairs_from_log(plain)
        self.assertEqual(pairs[0][0], 0x264D4)
        other = td / "ss-g3-mill-4.log"
        other.write_text(body)
        r = gzip_mill_logs([td])
        self.assertEqual(r["ok"], 1)
        self.assertFalse(other.exists())
        self.assertEqual(read_log(td / "ss-g3-mill-4.log"), body)

    def test_compare_keep_fb_hash(self) -> None:
        from debug_run import compare_keep_fb, file_sha256, format_fb_changed

        td = Path(tempfile.mkdtemp())
        keep_log = td / "ss-g3-mill-1.log"
        mill_log = td / "ss-g3-mill-2.log"
        keep_log.write_text("k\n")
        mill_log.write_text("m\n")
        keep_fb = td / "ss-g3-mill-1-fb.pgm"
        mill_fb = td / "ss-g3-mill-2-fb.pgm"
        keep_plant = td / "ss-g3-mill-1-fb-plant.pgm"
        mill_plant = td / "ss-g3-mill-2-fb-plant.pgm"
        keep_fb.write_bytes(b"P5\n1 1\n255\nA")
        mill_fb.write_bytes(b"P5\n1 1\n255\nA")
        keep_plant.write_bytes(b"P5\n1 1\n255\nB")
        mill_plant.write_bytes(b"P5\n1 1\n255\nB")
        same = compare_keep_fb(keep_log, mill_log)
        self.assertEqual(same["fb_changed"], "no")
        self.assertEqual(same["fb"], "no")
        self.assertEqual(same["plant"], "no")
        self.assertEqual(same["keep_fb"], file_sha256(keep_fb))
        mill_fb.write_bytes(b"P5\n1 1\n255\nC")
        changed = compare_keep_fb(keep_log, mill_log)
        self.assertEqual(changed["fb_changed"], "yes")
        self.assertEqual(changed["fb"], "yes")
        self.assertEqual(changed["plant"], "no")
        self.assertGreaterEqual(changed["fb_diff"], 0.10)
        (td / "ss-g3-mill-1.png").write_bytes(b"keep-png")
        (td / "ss-g3-mill-2.png").write_bytes(b"mill-png-different")
        png_ignored = compare_keep_fb(keep_log, mill_log)
        self.assertEqual(png_ignored["fb_changed"], "yes")
        quiet = td / "ss-g3-mill-3.log"
        quiet.write_text("q\n")
        keep10 = td / "ss-g3-mill-1-fb.pgm"
        mill10 = td / "ss-g3-mill-3-fb.pgm"
        (td / "ss-g3-mill-3-fb-plant.pgm").write_bytes(b"P5\n1 1\n255\nB")
        pix = bytearray(b"\x00" * 100)
        keep10.write_bytes(b"P5\n10 10\n255\n" + bytes(pix))
        pix[0] = 1
        mill10.write_bytes(b"P5\n10 10\n255\n" + bytes(pix))
        tiny = compare_keep_fb(keep_log, quiet)
        self.assertEqual(tiny["fb"], "no")
        self.assertLess(tiny["fb_diff"], 0.10)
        mill10.write_bytes(b"P5\n10 10\n255\n" + (b"\xff" * 100))
        big = compare_keep_fb(keep_log, quiet)
        self.assertEqual(big["fb"], "yes")
        trunc = td / "ss-g3-mill-4.log"
        trunc.write_text("t\n")
        (td / "ss-g3-mill-4-fb.pgm").write_bytes(b"P5\n2560 480\n255\n" + b"\x00" * 100)
        (td / "ss-g3-mill-4-fb-plant.pgm").write_bytes(b"P5\n1 1\n255\nB")
        bad = compare_keep_fb(keep_log, trunc)
        self.assertEqual(bad["fb"], "unknown")
        line = format_fb_changed(changed)
        self.assertIn("fb_changed=yes", line)
        self.assertIn("fb=yes", line)
        missing = compare_keep_fb(None, mill_log)
        self.assertEqual(missing["fb_changed"], "unknown")
        self.assertEqual(missing["reason"], "no-keep-log")
        empty = Path(tempfile.mkdtemp()) / "ss-g3-mill-9.log"
        empty.write_text("x\n")
        no_pgm = compare_keep_fb(keep_log, empty)
        self.assertEqual(no_pgm["fb_changed"], "unknown")

    def test_fb_vision_prompt_json_and_socket(self) -> None:
        import json as json_mod
        from fb_vision import (
            VISION_SOCK,
            acp_prompt_json,
            grok_vision_cmd,
            image_block,
            parse_window,
            resolve_pngs,
        )

        td = Path(tempfile.mkdtemp())
        png = td / "shot.png"
        png.write_bytes(b"\x89PNG\r\n\x1a\n" + b"x" * 16)
        block = image_block(png)
        self.assertEqual(block["type"], "image")
        self.assertEqual(block["mimeType"], "image/png")
        self.assertTrue(block["data"])
        payload = acp_prompt_json([png])
        data = json_mod.loads(payload)
        self.assertEqual(data["type"], "acp")
        types = [b["type"] for b in data["content"]]
        self.assertIn("text", types)
        self.assertIn("image", types)
        cmd = grok_vision_cmd(payload)
        joined = " ".join(cmd)
        self.assertIn("--prompt-json", joined)
        self.assertIn(VISION_SOCK, joined)
        self.assertNotIn("/tmp/ss-g3-grok-leader.sock", joined)
        self.assertIn("bypassPermissions", joined)
        self.assertIn("--max-turns", joined)
        self.assertNotIn("search_replace", joined)
        self.assertNotIn("api.x.ai", joined)
        self.assertEqual(parse_window("WINDOW=yes\n"), "yes")
        self.assertEqual(parse_window("blah WINDOW=no"), "no")
        self.assertEqual(parse_window('{"text":"WINDOW=yes"}'), "yes")
        self.assertEqual(parse_window("no window line"), "unknown")
        picked = resolve_pngs(n=7)
        self.assertEqual(picked["pngs"], [Path("/tmp/ss-g3-mill-7.png")])

    def test_hangcap_sec_default_and_floor(self) -> None:
        old = os.environ.get("G3_HANGCAP_SEC")
        try:
            os.environ.pop("G3_HANGCAP_SEC", None)
            self.assertEqual(hangcap_sec(), 45)
            os.environ["G3_HANGCAP_SEC"] = "5"
            self.assertEqual(hangcap_sec(), 15)
            os.environ["G3_HANGCAP_SEC"] = "100"
            self.assertEqual(hangcap_sec(), 100)
        finally:
            if old is None:
                os.environ.pop("G3_HANGCAP_SEC", None)
            else:
                os.environ["G3_HANGCAP_SEC"] = old


class TestMillAnnotations(unittest.TestCase):
    def test_skip_candidate_priority_and_protected_block(self) -> None:
        from mill_annotations import clear_annotations, load_annotations
        from mill_apply import next_skip_68k_off, skip_68k_blocked

        doc = {
            "format": "NewWorldView-mill-annotations",
            "version": 1,
            "romKey": "fixture",
            "entries": [
                {
                    "id": "a",
                    "address": "68K:00026E90",
                    "romOffset": "0x26E90",
                    "action": "skipCandidate",
                    "kind": "millableSpin",
                    "symbol": "SlotHelperSpin",
                    "evidence": ["logHits:412"],
                    "reasoning": "Spin",
                    "approvedAt": "2026-09-04T00:00:00Z",
                },
                {
                    "id": "b",
                    "address": "68K:0005C86C",
                    "romOffset": "0x5C86C",
                    "action": "revert",
                    "kind": "protected",
                    "symbol": None,
                    "evidence": ["tag:get-ccursor-proc"],
                    "reasoning": "UI path",
                    "approvedAt": "2026-09-04T00:00:00Z",
                },
            ],
        }
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
            import json

            json.dump(doc, fh)
            path = Path(fh.name)
        try:
            clear_annotations()
            ann = load_annotations(path)
            self.assertTrue(skip_68k_blocked(0x5C86C))
            self.assertFalse(skip_68k_blocked(0x26E90))
            mill = {"map_keep_log_only": True}
            off = next_skip_68k_off(mill, [], [])
            self.assertEqual(off, 0x26E90)
        finally:
            path.unlink(missing_ok=True)
            clear_annotations()

    def test_format_pack_section(self) -> None:
        from mill_annotations import MillAnnotations

        ann = MillAnnotations(
            Path("fixture.json"),
            {
                "format": "NewWorldView-mill-annotations",
                "version": 1,
                "romKey": "fixture",
                "entries": [
                    {
                        "romOffset": "0x26E90",
                        "action": "skipCandidate",
                        "kind": "millableSpin",
                    }
                ],
            },
        )
        section = ann.format_pack_section()
        self.assertIn("NewWorldView annotations", section)
        self.assertIn("0x26e90", section)

    def test_token_usage_from_document_and_entries(self) -> None:
        from mill_annotations import MillAnnotations, parse_usage

        ann = MillAnnotations(
            Path("fixture.json"),
            {
                "format": "NewWorldView-mill-annotations",
                "version": 1,
                "romKey": "fixture",
                "tokenUsage": {"in": 500, "out": 20, "total": 520},
                "entries": [
                    {
                        "romOffset": "0x26E90",
                        "action": "skipCandidate",
                        "kind": "millableSpin",
                        "tokenUsage": {"in": 100, "out": 4, "total": 104},
                    }
                ],
            },
        )
        self.assertEqual(parse_usage({"in": 1, "out": 2}), {"in": 1, "out": 2, "total": 3})
        self.assertEqual(ann.token_usage()["total"], 520)
        self.assertIn("TOKENS apple_fm in=500 out=20 total=520", ann.format_token_line())

    def test_apple_fm_mill_usage_for_skip_entry(self) -> None:
        import json

        import g3_driver as gd
        from mill_annotations import load_annotations

        doc = {
            "format": "NewWorldView-mill-annotations",
            "version": 1,
            "romKey": "fixture",
            "tokenUsage": {"in": 500, "out": 20, "total": 520},
            "entries": [
                {
                    "romOffset": "0x1ddd4",
                    "action": "skipCandidate",
                    "kind": "millableSpin",
                    "tokenUsage": {"in": 100, "out": 4, "total": 104},
                }
            ],
        }
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
            json.dump(doc, fh)
            path = Path(fh.name)
        try:
            load_annotations(path)
            usage, role = gd._apple_fm_mill_usage(0x1DDD4, "skip-68k")
            self.assertEqual(usage["total"], 104)
            self.assertIn("skip-68k", role)
            st: dict = {}
            total = gd._apple_fm_total_usage(st)
            self.assertEqual(total["total"], 520)
            self.assertEqual(st["mill"]["tokens"]["apple_fm"]["total"], 520)
        finally:
            path.unlink(missing_ok=True)
            from mill_annotations import clear_annotations

            clear_annotations()


class TestMillHistogram(unittest.TestCase):
    def test_ranked_offs_and_skip_priority(self) -> None:
        from mill_histogram import clear_histogram, load_histogram
        from mill_apply import next_skip_68k_off

        doc = {
            "format": "NewWorldView-mill-histogram",
            "version": 1,
            "romKey": "fixture",
            "scannedLogs": 42,
            "entries": [
                {"romOffset": "0x1ddd4", "count": 120, "space": "m68kToolbox"},
                {"romOffset": "0x26e90", "count": 88, "space": "m68kToolbox"},
            ],
        }
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
            import json

            json.dump(doc, fh)
            path = Path(fh.name)
        try:
            clear_histogram()
            hist = load_histogram(path)
            self.assertEqual(hist.ranked_offs(), [0x1DDD4, 0x26E90])
            mill = {"map_keep_log_only": True}
            off = next_skip_68k_off(mill, [], [])
            self.assertEqual(off, 0x1DDD4)
        finally:
            path.unlink(missing_ok=True)
            clear_histogram()

    def test_histogram_after_annotations(self) -> None:
        from mill_annotations import clear_annotations, load_annotations
        from mill_histogram import clear_histogram, load_histogram
        from mill_apply import next_skip_68k_off

        ann_doc = {
            "format": "NewWorldView-mill-annotations",
            "version": 1,
            "romKey": "fixture",
            "entries": [
                {
                    "romOffset": "0x26E90",
                    "action": "skipCandidate",
                    "kind": "millableSpin",
                }
            ],
        }
        hist_doc = {
            "format": "NewWorldView-mill-histogram",
            "version": 1,
            "romKey": "fixture",
            "entries": [
                {"romOffset": "0x1ddd4", "count": 120},
                {"romOffset": "0x26e90", "count": 88},
            ],
        }
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
            import json

            json.dump(ann_doc, fh)
            ann_path = Path(fh.name)
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
            import json

            json.dump(hist_doc, fh)
            hist_path = Path(fh.name)
        try:
            clear_annotations()
            clear_histogram()
            load_annotations(ann_path)
            load_histogram(hist_path)
            mill = {"map_keep_log_only": True}
            off = next_skip_68k_off(mill, [], [])
            self.assertEqual(off, 0x26E90)
        finally:
            ann_path.unlink(missing_ok=True)
            hist_path.unlink(missing_ok=True)
            clear_annotations()
            clear_histogram()

    def test_format_pack_section(self) -> None:
        from mill_histogram import MillHistogram

        hist = MillHistogram(
            Path("fixture.json"),
            {
                "format": "NewWorldView-mill-histogram",
                "version": 1,
                "romKey": "fixture",
                "scannedLogs": 10,
                "entries": [{"romOffset": "0x1ddd4", "count": 5}],
            },
        )
        section = hist.format_pack_section()
        self.assertIn("NewWorldView histogram", section)
        self.assertIn("0x1ddd4", section)


if __name__ == "__main__":
    unittest.main()
