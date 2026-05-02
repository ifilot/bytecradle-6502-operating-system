import errno
import os
import pathlib
import pty
import select
import shutil
import signal
import struct
import subprocess
import termios
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
EMU = ROOT / "emulator" / "build" / "bc6502emu"
BCOS_ROM = ROOT / "src" / "bin" / "bcos.bin"
REQUIRED_TOOLS = ("cmake", "make", "cc65", "ca65", "ld65")
CMD_TIMEOUT_S = 120


def missing_tools() -> list[str]:
    return [tool for tool in REQUIRED_TOOLS if shutil.which(tool) is None]


def run_cmd(cmd: list[str], *, cwd: pathlib.Path = ROOT) -> None:
    subprocess.run(cmd, cwd=cwd, check=True, timeout=CMD_TIMEOUT_S)


def build_all_once() -> None:
    run_cmd(["cmake", "-S", "emulator/src", "-B", "emulator/build"])
    run_cmd(["cmake", "--build", "emulator/build", "-j"])
    run_cmd(["make", "-C", "src"])
    run_cmd(["make", "-C", "programs"])


def read_until(fd: int, expected: str, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    chunks: list[bytes] = []

    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        rlist, _, _ = select.select([fd], [], [], max(0.0, min(0.2, remaining)))
        if not rlist:
            continue

        try:
            data = os.read(fd, 4096)
        except OSError as exc:
            if exc.errno == errno.EIO:
                break
            raise

        if not data:
            break

        chunks.append(data)
        combined = b"".join(chunks).decode("utf-8", errors="replace")
        if expected in combined:
            return combined

    text = b"".join(chunks).decode("utf-8", errors="replace")
    raise AssertionError(f"Did not observe expected text {expected!r}. Output so far:\n{text}")


def spawn_emulator(argv: list[str]) -> tuple[subprocess.Popen[bytes], int]:
    master_fd, slave_fd = pty.openpty()

    # Emulator checks terminal dimensions at startup; ensure a valid PTY size.
    fcntl_winsz = struct.pack("HHHH", 25, 80, 0, 0)
    termios_ioctl = termios.TIOCSWINSZ
    import fcntl

    fcntl.ioctl(slave_fd, termios_ioctl, fcntl_winsz)

    proc = subprocess.Popen(
        argv,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        cwd=ROOT,
        preexec_fn=os.setsid,
    )
    os.close(slave_fd)
    return proc, master_fd


def stop_emulator(proc: subprocess.Popen[bytes], master_fd: int) -> None:
    if proc.poll() is None:
        os.killpg(os.getpgid(proc.pid), signal.SIGINT)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            proc.wait(timeout=5)
    os.close(master_fd)


def resolve_sd_image() -> pathlib.Path:
    candidate = os.environ.get("BC6502_SDCARD_IMAGE")
    if candidate:
        return pathlib.Path(candidate).expanduser().resolve()

    return ROOT / "emulator" / "script" / "sdcard.img"


def write_test_rom(path: pathlib.Path) -> None:
    """Create a 32KB ROM image mapped at $8000-$FFFF.

    Program behavior:
      - Emit "E2E OK\n" through ACIA data register ($7F04)
      - Halt in an infinite loop
    """
    rom = bytearray([0xEA] * 0x8000)

    # Program located at CPU address 0x8000 (offset 0x0000 in this ROM blob)
    code = bytes(
        [
            0xA2,
            0x00,  # LDX #$00
            0xBD,
            0x11,
            0x80,  # LDA $8011,X
            0xF0,
            0x07,  # BEQ done
            0x8D,
            0x04,
            0x7F,  # STA $7F04
            0xE8,  # INX
            0x4C,
            0x02,
            0x80,  # JMP loop
            0x4C,
            0x0E,
            0x80,  # done: JMP done
        ]
    )

    msg = b"E2E OK\n\x00"

    rom[0 : len(code)] = code
    rom[len(code) : len(code) + len(msg)] = msg

    # vectors at $FFFA-$FFFF (offset 0x7FFA)
    rom[0x7FFA:0x7FFC] = (0x00, 0x80)  # NMI
    rom[0x7FFC:0x7FFE] = (0x00, 0x80)  # RESET
    rom[0x7FFE:0x8000] = (0x00, 0x80)  # IRQ/BRK

    path.write_bytes(rom)
