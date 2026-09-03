#!/usr/bin/env python3
"""Unittest: no SheepShaver, no Qwen."""
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
    MARKER_STW,
    force_skip_68k_off,
    force_skip_hang_off,
    hang_off_millable,
    leftover_68k_pending,
    mill_kind,
    mill_moved,
    mill_worse,
    next_kind,
    next_leftover,
    next_skip_68k_off,
    next_skip_hang_off,
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
from qwen_lock import format_tokens, score_g3, usage_from_response, zero_usage
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
        self.assertEqual(next_step(st, "e25a61f1"), "score-log")

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
            "slot-26e90",
        )
        self.assertEqual(
            next_leftover(
                exhausted + ["leftover:slot-26e90"],
                [],
                hang_off=0x326510,
                saw_68k=True,
            ),
            "skip-3265a4",
        )
        done68 = exhausted + [
            "leftover:keep-68k",
            "leftover:read-noerr",
            "leftover:setfpos-noerr",
            "leftover:slot-26e90",
            "leftover:skip-3265a4",
            "leftover:spin-26e88",
            "leftover:skip-326458",
            "leftover:skip-hang:00326510",
        ]
        self.assertEqual(
            next_leftover(done68, [], hang_off=0x326510, saw_68k=True),
            "skip-68k",
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
            "cfm-aa5a",
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
        self.assertTrue(
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
            "NW-BOOT G3: 68k GetCCursor A97C pc=5005c86e\n"
            "NW-BOOT G3: 68k DialogDispatch AA68 sel=0304 fill\n"
        )
        mill = {"keep_log": str(log), "map_keep_log_only": True}
        off = next_skip_68k_off(mill, [], [])
        # GetCCursor 0x5c86e is UI path — do not mill skip; map NOP is next.
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
        for o in LOOK_AGAIN_SKIP_68K:
            self.assertFalse(skip_68k_millable(o), "look-again 0x%x" % o)
        self.assertTrue(skip_68k_ui_op(0xA97C))
        self.assertTrue(skip_68k_ui_op(0xAA68))
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
    def test_usage_from_response(self) -> None:
        u = usage_from_response(
            {
                "usage": {
                    "prompt_tokens": 10,
                    "completion_tokens": 2,
                    "total_tokens": 12,
                }
            }
        )
        self.assertEqual(u, {"in": 10, "out": 2, "total": 12})

    def test_usage_missing_total(self) -> None:
        u = usage_from_response({"usage": {"prompt_tokens": 10, "completion_tokens": 2}})
        self.assertEqual(u["total"], 12)

    def test_format_tokens(self) -> None:
        self.assertEqual(
            format_tokens("qwen", {"in": 1, "out": 2, "total": 3}, "lock"),
            "TOKENS qwen in=1 out=2 total=3 (lock)",
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
                    "qwen": {"in": 10, "out": 2, "total": 12},
                },
                {
                    "n": 7,
                    "kind": "skip-hang",
                    "hang_off": 0x326640,
                    "result": "REVERT",
                    "grok": {"in": 0, "out": 0, "total": 0},
                    "qwen": {"in": 11, "out": 3, "total": 14},
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
                    "qwen": {},
                }
            ]
        )
        self.assertIn("52.3s", table)
        self.assertIn("elapsed", table)


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
                        "qwen": {"in": 10, "out": 2, "total": 12},
                        "grok": {"in": 0, "out": 0, "total": 0},
                    }
                ],
                "tokens": {
                    "grok": {"in": 0, "out": 0, "total": 0},
                    "qwen": {"in": 10, "out": 2, "total": 12},
                },
            }
        }
        pack = pack_from_state(st)
        md = format_pack_md(pack)
        self.assertIn("3264fc", md)
        self.assertIn("skip-list 50325", md)
        self.assertIn("GetCCursor", md)
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
                        "qwen": {},
                        "grok": {},
                    },
                    {
                        "n": 7,
                        "kind": "skip-hang",
                        "hang_off": 0x326640,
                        "result": "REVERT",
                        "qwen": {},
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
        self.assertIn("GetCCursor", trap["text"])
        bra = disasm_68k_one(bytes(rom), 0x30)
        self.assertEqual(bra["kind"], "bra_star")
        self.assertEqual(ppc_one(0x900107D4), "stw")
        self.assertEqual(ppc_one(0x7C0604A6), "mfsr")
        self.assertEqual(A_LINE[0xAA68], "DialogDispatch")
        self.assertEqual(A_LINE[0xA88F], "InitCursor")
        self.assertEqual(A_LINE[0xA9C9], "GetResource")
        self.assertEqual(A_LINE[0xA01F], "GetEOF")
        self.assertEqual(A_LINE[0xA023], "GetFPos")
        c = classify_off(bytes(rom), 0x3264FC)
        self.assertTrue(c["hard"])
        self.assertFalse(c["millable"])
        from rom_disasm import format_report, region_tag

        self.assertEqual(region_tag(0x5C86E), "ui-dialog-path")
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
        self.assertNotIn("api.x.ai", joined)
        self.assertNotIn("/v1/chat", joined)
        self.assertIn("read_file,search_replace", joined)
        ptxt = prompt.read_text()
        self.assertIn("GetCCursor", ptxt)
        self.assertIn("0x5c86c-0x5c8c0", ptxt)

    def test_qwen_skips_http_unless_window_yes(self) -> None:
        r = score_g3({"g2_live": True, "parsed": {}}, window="unknown")
        self.assertTrue(r["skipped"])
        self.assertEqual(r["g3"], "no")
        r2 = score_g3({"g2_live": True, "parsed": {}}, window="no")
        self.assertTrue(r2["skipped"])
        self.assertEqual(r2["g3"], "no")

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


if __name__ == "__main__":
    unittest.main()
