"""uds-flash — command-line UDS flash programming tool."""

from __future__ import annotations

import sys
from pathlib import Path

import click
from rich.console import Console
from rich.progress import BarColumn, Progress, TaskProgressColumn, TextColumn, TimeElapsedColumn

# Allow running from repo root without installation
sys.path.insert(0, str(Path(__file__).resolve().parent))

from uds import UdsClient, UdsFlashService, UdsNrcError, UdsSecurity, UdsSession, UdsTimeoutError  # noqa: E402

console = Console()


# ── CLI entry point ────────────────────────────────────────────────────────


@click.group()
@click.option("--interface", default="vcan0", show_default=True, help="CAN interface.")
@click.option("--ecu-id", default=1, show_default=True, type=int, help="ECU ID.")
@click.option(
    "--address",
    default="0x0000",
    show_default=True,
    help="Base address in hex (e.g. 0x08000000).",
)
@click.pass_context
def cli(ctx: click.Context, interface: str, ecu_id: int, address: str) -> None:
    """UDS flash programming tool."""
    ctx.ensure_object(dict)
    ctx.obj["interface"] = interface
    ctx.obj["ecu_id"] = ecu_id
    ctx.obj["address"] = int(address, 0)


# ── download ───────────────────────────────────────────────────────────────


@cli.command()
@click.argument("file", type=click.Path(exists=True, readable=True, path_type=Path))
@click.option("--addr-len", default=4, show_default=True, type=int, help="Address field length.")
@click.option("--size-len", default=2, show_default=True, type=int, help="Size field length.")
@click.option("--timeout", default=10.0, show_default=True, type=float, help="Timeout (s).")
@click.option(
    "--unlock-level",
    default=1,
    show_default=True,
    type=int,
    help="Security level to unlock before flashing.",
)
@click.pass_context
def download(
    ctx: click.Context,
    file: Path,
    addr_len: int,
    size_len: int,
    timeout: float,
    unlock_level: int,
) -> None:
    """Download binary FILE to ECU flash memory."""
    opts = ctx.obj
    data = file.read_bytes()
    console.print(f"[bold]Downloading[/bold] {file.name} ({len(data):,} bytes) "
                  f"→ 0x{opts['address']:08X}")

    with Progress(
        TextColumn("[progress.description]{task.description}"),
        BarColumn(),
        TaskProgressColumn(),
        TimeElapsedColumn(),
        console=console,
    ) as progress:
        task = progress.add_task("Flashing…", total=len(data))

        def on_progress(sent: int, total: int) -> None:
            progress.update(task, completed=sent)

        with UdsClient(opts["interface"], opts["ecu_id"], timeout) as client:
            try:
                # Switch to programming session and unlock
                session_svc = UdsSession(client)
                session_svc.change_session(0x02)
                UdsSecurity(client).unlock(unlock_level)

                UdsFlashService(client).download(
                    opts["address"],
                    data,
                    addr_len=addr_len,
                    size_len=size_len,
                    on_progress=on_progress,
                )
            except (UdsNrcError, UdsTimeoutError) as exc:
                console.print(f"\n[red]Error:[/red] {exc}")
                sys.exit(1)

    console.print("[green]Download complete.[/green]")


# ── upload ─────────────────────────────────────────────────────────────────


@cli.command()
@click.argument("file", type=click.Path(writable=True, path_type=Path))
@click.option("--length", required=True, type=int, help="Number of bytes to upload.")
@click.option("--addr-len", default=4, show_default=True, type=int, help="Address field length.")
@click.option("--size-len", default=2, show_default=True, type=int, help="Size field length.")
@click.option("--timeout", default=10.0, show_default=True, type=float, help="Timeout (s).")
@click.pass_context
def upload(
    ctx: click.Context,
    file: Path,
    length: int,
    addr_len: int,
    size_len: int,
    timeout: float,
) -> None:
    """Upload ECU flash memory region to FILE."""
    opts = ctx.obj
    console.print(f"[bold]Uploading[/bold] 0x{opts['address']:08X} ({length:,} bytes)"
                  f" → {file.name}")

    with Progress(
        TextColumn("[progress.description]{task.description}"),
        BarColumn(),
        TaskProgressColumn(),
        TimeElapsedColumn(),
        console=console,
    ) as progress:
        task = progress.add_task("Reading…", total=length)

        def on_progress(received: int, total: int) -> None:
            progress.update(task, completed=received)

        with UdsClient(opts["interface"], opts["ecu_id"], timeout) as client:
            try:
                data = UdsFlashService(client).upload(
                    opts["address"],
                    length,
                    addr_len=addr_len,
                    size_len=size_len,
                    on_progress=on_progress,
                )
            except (UdsNrcError, UdsTimeoutError) as exc:
                console.print(f"\n[red]Error:[/red] {exc}")
                sys.exit(1)

    file.write_bytes(data)
    console.print(f"[green]Upload complete — {len(data):,} bytes written to {file}.[/green]")


# ── entry ──────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    cli()
