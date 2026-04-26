"""uds-diag — command-line UDS diagnostic tool."""

from __future__ import annotations

import sys
from pathlib import Path

import click
from rich.console import Console
from rich.table import Table

# Allow running from repo root without installation
sys.path.insert(0, str(Path(__file__).resolve().parent))

from uds import (  # noqa: E402
    UdsClient,
    UdsDtcService,
    UdsDidService,
    UdsNrc,
    UdsNrcError,
    UdsSecurity,
    UdsSession,
    UdsTimeoutError,
)

console = Console()


# ── CLI entry point ────────────────────────────────────────────────────────


@click.group()
@click.option("--interface", default="vcan0", show_default=True, help="CAN interface.")
@click.option("--ecu-id", default=1, show_default=True, type=int, help="ECU ID.")
@click.option("--timeout", default=2.0, show_default=True, type=float, help="Timeout (s).")
@click.pass_context
def cli(ctx: click.Context, interface: str, ecu_id: int, timeout: float) -> None:
    """UDS diagnostic command-line tool."""
    ctx.ensure_object(dict)
    ctx.obj["interface"] = interface
    ctx.obj["ecu_id"] = ecu_id
    ctx.obj["timeout"] = timeout


# ── session ────────────────────────────────────────────────────────────────


@cli.command()
@click.argument("session_type", type=int, default=1)
@click.pass_context
def session(ctx: click.Context, session_type: int) -> None:
    """Change diagnostic session (0x10). SESSION_TYPE: 1=default, 2=programming, 3=extended."""
    opts = ctx.obj
    with UdsClient(opts["interface"], opts["ecu_id"], opts["timeout"]) as client:
        try:
            svc = UdsSession(client)
            result = svc.change_session(session_type)
            table = Table(title="Session Changed")
            table.add_column("Parameter")
            table.add_column("Value")
            table.add_row("Session Type", hex(result["session_type"]))
            table.add_row("P2 (ms)", str(result["p2_ms"]))
            table.add_row("P2* (ms)", str(result["p2_star_ms"]))
            console.print(table)
        except (UdsNrcError, UdsTimeoutError) as exc:
            console.print(f"[red]Error:[/red] {exc}")
            sys.exit(1)


# ── tester-present ─────────────────────────────────────────────────────────


@cli.command("tester-present")
@click.option("--suppress", is_flag=True, default=False, help="Suppress positive response.")
@click.pass_context
def tester_present(ctx: click.Context, suppress: bool) -> None:
    """Send Tester Present (0x3E) to keep the session alive."""
    opts = ctx.obj
    with UdsClient(opts["interface"], opts["ecu_id"], opts["timeout"]) as client:
        try:
            UdsSession(client).tester_present(suppress=suppress)
            console.print("[green]Tester Present sent successfully.[/green]")
        except (UdsNrcError, UdsTimeoutError) as exc:
            console.print(f"[red]Error:[/red] {exc}")
            sys.exit(1)


# ── read-did ───────────────────────────────────────────────────────────────


@cli.command("read-did")
@click.argument("did_ids", nargs=-1, required=True)
@click.pass_context
def read_did(ctx: click.Context, did_ids: tuple[str, ...]) -> None:
    """Read Data by Identifier (0x22). DID_IDS: one or more DID values (hex or int)."""
    opts = ctx.obj
    parsed = [int(d, 0) for d in did_ids]
    with UdsClient(opts["interface"], opts["ecu_id"], opts["timeout"]) as client:
        try:
            result = UdsDidService(client).read(parsed)
            table = Table(title="Read DID")
            table.add_column("DID")
            table.add_column("Data (hex)")
            for did, data in result.items():
                table.add_row(f"0x{did:04X}", data.hex())
            console.print(table)
        except (UdsNrcError, UdsTimeoutError) as exc:
            console.print(f"[red]Error:[/red] {exc}")
            sys.exit(1)


# ── write-did ──────────────────────────────────────────────────────────────


@cli.command("write-did")
@click.argument("did_id")
@click.argument("data_hex")
@click.pass_context
def write_did(ctx: click.Context, did_id: str, data_hex: str) -> None:
    """Write Data by Identifier (0x2E). DID_ID: DID value. DATA_HEX: hex-encoded data."""
    opts = ctx.obj
    did = int(did_id, 0)
    data = bytes.fromhex(data_hex)
    with UdsClient(opts["interface"], opts["ecu_id"], opts["timeout"]) as client:
        try:
            UdsDidService(client).write(did, data)
            console.print(f"[green]DID 0x{did:04X} written successfully.[/green]")
        except (UdsNrcError, UdsTimeoutError) as exc:
            console.print(f"[red]Error:[/red] {exc}")
            sys.exit(1)


# ── read-dtc ───────────────────────────────────────────────────────────────


@cli.command("read-dtc")
@click.option("--status-mask", default=0xFF, show_default=True, type=int, help="Status mask.")
@click.pass_context
def read_dtc(ctx: click.Context, status_mask: int) -> None:
    """Read DTC information (0x19 sub-function 0x02)."""
    opts = ctx.obj
    with UdsClient(opts["interface"], opts["ecu_id"], opts["timeout"]) as client:
        try:
            dtcs = UdsDtcService(client).read_by_status(status_mask)
            if not dtcs:
                console.print("No DTCs found.")
                return
            table = Table(title=f"DTCs (status_mask=0x{status_mask:02X})")
            table.add_column("DTC Code (hex)")
            table.add_column("Status (hex)")
            for dtc in dtcs:
                table.add_row(f"0x{dtc['code']:06X}", f"0x{dtc['status']:02X}")
            console.print(table)
        except (UdsNrcError, UdsTimeoutError) as exc:
            console.print(f"[red]Error:[/red] {exc}")
            sys.exit(1)


# ── clear-dtc ──────────────────────────────────────────────────────────────


@cli.command("clear-dtc")
@click.option("--group", default=0xFFFFFF, show_default=True, type=int, help="DTC group.")
@click.pass_context
def clear_dtc(ctx: click.Context, group: int) -> None:
    """Clear diagnostic information (0x14)."""
    opts = ctx.obj
    with UdsClient(opts["interface"], opts["ecu_id"], opts["timeout"]) as client:
        try:
            UdsDtcService(client).clear(group)
            console.print("[green]DTCs cleared successfully.[/green]")
        except (UdsNrcError, UdsTimeoutError) as exc:
            console.print(f"[red]Error:[/red] {exc}")
            sys.exit(1)


# ── reset ──────────────────────────────────────────────────────────────────


@cli.command()
@click.option("--reset-type", default=1, show_default=True, type=int,
              help="1=hard, 2=keyOffOn, 3=soft.")
@click.pass_context
def reset(ctx: click.Context, reset_type: int) -> None:
    """Send ECU Reset (0x11)."""
    opts = ctx.obj
    with UdsClient(opts["interface"], opts["ecu_id"], opts["timeout"]) as client:
        try:
            UdsDidService(client).ecu_reset(reset_type)
            console.print("[green]ECU reset command sent.[/green]")
        except (UdsNrcError, UdsTimeoutError) as exc:
            console.print(f"[red]Error:[/red] {exc}")
            sys.exit(1)


# ── unlock ─────────────────────────────────────────────────────────────────


@cli.command()
@click.option("--level", default=1, show_default=True, type=int, help="Security level.")
@click.pass_context
def unlock(ctx: click.Context, level: int) -> None:
    """Security Access unlock (0x27)."""
    opts = ctx.obj
    with UdsClient(opts["interface"], opts["ecu_id"], opts["timeout"]) as client:
        try:
            unlocked = UdsSecurity(client).unlock(level)
            if unlocked:
                console.print(f"[green]Security level {level} unlocked.[/green]")
        except (UdsNrcError, UdsTimeoutError) as exc:
            console.print(f"[red]Error:[/red] {exc}")
            sys.exit(1)


# ── entry ──────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    cli()
