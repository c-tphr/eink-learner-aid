"""Command line: python -m piccoflash <command> [options].

There is deliberately no --force, --offset or --no-verify option: what may be
written is decided by guard.py alone.
"""

from __future__ import annotations

import argparse
import os
import platform
import sys
import traceback
from typing import Callable, List, Optional

from . import PINNED_ESPTOOL_VERSION, __version__, backup, flash, guard, identify, monitor, plan as planmod, ports
from . import restore, workdir
from .common import ALREADY_CONVERTED, NOT_REACHED, OK, STOP, Context, EXIT_NAMES, PortProblem, not_reached_help
from .device import Device, DeviceError, DeviceNotReached
from .ui import UI

COMMANDS = ("identify", "backup", "plan", "flash", "monitor", "restore", "make-plan", "ports")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="python -m piccoflash", description=(
        "Guarded identify, backup, flash, monitor and restore for the BOOX Picco. "
        "Exit codes: 0 OK, 1 STOP/refused (nothing written), 2 usage, 3 already converted, "
        "4 device not reached (nothing written), 5 flash failed part-way (run restore), 6 restore failed."))
    p.add_argument("--version", action="version", version="piccoflash %s" % __version__)
    sub = p.add_subparsers(dest="command", metavar="command")
    sub.required = True

    def common(sp, plan=True):
        sp.add_argument("--dry-run", action="store_true",
                        help="print every step and esptool call; never open the serial port")
        sp.add_argument("--port", default=None, help="serial port, e.g. COM5 (default: the only Espressif port)")
        sp.add_argument("--work-dir", default="work", help="folder for records, backups and logs (default: work)")
        if plan:
            sp.add_argument("--plan", default=os.path.join("kit", planmod.PLAN_FILE_NAME),
                            help="flash-plan.json or the folder holding it (default: kit/flash-plan.json)")
        sp.add_argument("--launcher", default="python -m piccoflash", help=argparse.SUPPRESS)

    common(sub.add_parser("identify", help="read-only: chip, eFuses, bootloader header, partition table"), plan=False)
    common(sub.add_parser("backup", help="read-only: read all 16 MB twice and keep it if both match"), plan=False)
    common(sub.add_parser("plan", help="show and verify the flash plan (no device)"))
    common(sub.add_parser("flash", help="write the four planned images (needs identify and a verified backup)"))
    mon = sub.add_parser("monitor", help="record USB serial output until Ctrl+C")
    common(mon, plan=False)
    mon.add_argument("--reset", action="store_true", help="pulse the reset line once at the start")
    rst = sub.add_parser("restore", help="write a verified 16 MB backup back at 0x0")
    common(rst, plan=False)
    rst.add_argument("backup_file", help="the picco-backup-<mac>-<time>.bin file")
    mk = sub.add_parser("make-plan", help="copy four images into a kit folder and write a verified flash-plan.json")
    mk.add_argument("--bootloader", required=True)
    mk.add_argument("--partitions", required=True)
    mk.add_argument("--boot-app0", required=True)
    mk.add_argument("--app", required=True)
    mk.add_argument("--version", dest="fw_version", required=True, help="firmware version string")
    mk.add_argument("--build-id", default="unknown", help="build identifier, e.g. the git commit")
    mk.add_argument("--expect-offset", action="append", default=[], metavar="ROLE=0xHEX",
                    help="cross-check: the offset the build system uses for ROLE; refused if it differs")
    mk.add_argument("-o", "--out", required=True, help="kit folder to create (must not exist or be empty)")
    mk.add_argument("--dry-run", action="store_true", help="check the inputs only; write nothing")
    common(sub.add_parser("ports", help="list serial ports (opens none)"), plan=False)
    return p


def main(argv: Optional[List[str]] = None, device_factory: Optional[Callable[[str, UI], Device]] = None,
         port_resolver: Optional[Callable[[Optional[str], UI], str]] = None, ui: Optional[UI] = None,
         monitor_port_factory=None) -> int:
    args = build_parser().parse_args(argv)
    ui = ui or UI()
    if device_factory is None:
        from . import esptool_device

        device_factory = esptool_device.factory
    port_resolver = port_resolver or ports.resolver

    if args.command == "make-plan":
        return _make_plan(args, ui)
    ctx = Context(ui=ui, work=args.work_dir, plan_path=getattr(args, "plan", ""), port=args.port,
                  dry_run=args.dry_run, device_factory=device_factory, port_resolver=port_resolver,
                  launcher=args.launcher)
    if args.command in ("identify", "backup", "flash", "restore", "monitor"):
        ui.open_log(os.path.join(args.work_dir, "logs", "%s%s-%s.log" % (
            args.command, "-dryrun" if args.dry_run else "", workdir.timestamp())))
    ui.say("piccoflash %s (Python %s, esptool %s pinned)%s" % (
        __version__, platform.python_version(), PINNED_ESPTOOL_VERSION, "  DRY RUN" if args.dry_run else ""))
    try:
        if args.command == "identify":
            code = _identify(ctx)
        elif args.command == "backup":
            code = backup.run(ctx)
        elif args.command == "plan":
            code = _plan(ctx)
        elif args.command == "flash":
            code = flash.run(ctx)
        elif args.command == "restore":
            code = restore.run(ctx, args.backup_file)
        elif args.command == "monitor":
            code = monitor.run(ctx, args.reset, port_factory=monitor_port_factory)
        elif args.command == "ports":
            code = ports.print_ports(ui)
        else:  # pragma: no cover
            code = STOP
    except KeyboardInterrupt:
        ui.say("Stopped by Ctrl+C.")
        code = STOP
    except Exception as exc:  # a bug, or an unexpected environment problem
        ui.say("INTERNAL ERROR: %s: %s" % (type(exc).__name__, exc))
        ui.log_only(traceback.format_exc())
        ui.say("Check the output above for what had already happened.")
        code = STOP
    ui.say("[exit %d: %s]" % (code, EXIT_NAMES.get(code, "?")))
    if ui.log_path:
        ui.say("Log: %s" % os.path.abspath(ui.log_path))
    ui.close_log()
    return code


def _identify(ctx: Context) -> int:
    ui = ctx.ui
    ui.heading("Identify (read-only)")
    if ctx.dry_run:
        port = backup._dry_port(ctx)
        from . import esptool_calls

        ui.say("DRY RUN: the device is not contacted. A real run would:")
        for call in esptool_calls.connect(port) + esptool_calls.chip_info(port) + esptool_calls.security_info(port):
            ui.say("  %s" % call.api())
        for off, length in (identify.BOOTLOADER_READ, identify.TABLE_READ, identify.OTADATA_READ):
            call = esptool_calls.read(port, off, length, "identify-<time>-0x%X.bin" % off)
            ui.say("  %s" % call.api())
        ui.say("  %s" % esptool_calls.hard_reset(port).api())
        ui.say("then save identify-<time>.json and the three blobs in %s, and print the verdict." % ctx.work)
        return OK
    try:
        port = ctx.port_resolver(ctx.port, ui)
    except PortProblem as exc:
        ui.say(str(exc))
        return NOT_REACHED
    dev = ctx.device_factory(port, ui)
    try:
        dev.connect()
    except DeviceNotReached as exc:
        not_reached_help(ui, str(exc))
        return NOT_REACHED
    try:
        try:
            res = identify.read_and_evaluate(dev, ui)
        except DeviceError as exc:
            ui.say("The connection failed while reading: %s. Nothing was written." % exc)
            return NOT_REACHED
        path = identify.save_record(res, ctx.work, port)
        identify.print_summary(res, ui)
        ui.say("Saved: %s" % os.path.abspath(path))
        try:
            dev.hard_reset()
        except DeviceError as exc:
            ui.say("(Automatic reset failed: %s. Nothing was changed; restart the Picco by hand.)" % exc)
    finally:
        dev.close()
    return {identify.PASS: OK, identify.ALREADY_CONVERTED: ALREADY_CONVERTED}.get(res.verdict, STOP)


def _plan(ctx: Context) -> int:
    ui = ctx.ui
    try:
        plan = planmod.load(ctx.plan_path)
    except planmod.PlanError as exc:
        ui.say("REFUSED: the flash plan cannot be used:")
        for r in exc.reasons:
            ui.say("  - %s" % r)
        return STOP
    for line in planmod.format_plan_header(plan) + [""] + planmod.format_table(plan):
        ui.say(line)
    errs = planmod.verify(plan)
    ui.say("")
    if errs:
        ui.say("REFUSED by the guard:")
        for e in errs:
            ui.say("  - %s" % e)
        return STOP
    ui.say("Plan verified: exactly the four allowed images, at their only allowed offsets, every file's")
    ui.say("SHA-256 equals the plan, and every image passes the content checks.")
    return OK


def _make_plan(args, ui: UI) -> int:
    sources = {"bootloader": args.bootloader, "partition_table": args.partitions, "boot_app0": args.boot_app0,
               "app": args.app}
    try:
        expect = dict(planmod.parse_expect_offset(t) for t in args.expect_offset)
        missing = [s for s in sources.values() if not os.path.isfile(s)]
        if missing:
            raise planmod.PlanError(["no such file: %s" % m for m in missing])
        if args.dry_run:
            for role, src in sources.items():
                with open(src, "rb") as fp:
                    data = fp.read()
                region = guard.REGION_BY_ROLE[role]
                errs = guard.check_extent(role, expect.get(role, region.offset), len(data))
                errs += guard.check_image_content(role, data)
                ui.say("%-16s %s: %s" % (role, src, "ok" if not errs else "; ".join(errs)))
            return OK
        plan = planmod.make_plan(sources, args.out, args.fw_version, args.build_id, expect)
    except planmod.PlanError as exc:
        ui.say("REFUSED: no plan was written.")
        for r in exc.reasons:
            ui.say("  - %s" % r)
        return STOP
    except guard.GuardError as exc:
        ui.say("REFUSED: no plan was written.")
        for r in exc.reasons:
            ui.say("  - %s" % r)
        return STOP
    except OSError as exc:
        ui.say("REFUSED: no plan was written: %s" % exc)
        return STOP
    for line in planmod.format_plan_header(plan) + [""] + planmod.format_table(plan):
        ui.say(line)
    ui.say("")
    ui.say("Plan written and verified: %s" % os.path.abspath(plan.path))
    return OK


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
