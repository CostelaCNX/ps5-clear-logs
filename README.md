# PS5 Clear Logs Payload

Payload that frees space by removing crash dumps, core dumps and error report caches.

## How it works

On every run, with nothing to upload over FTP beforehand:

1. **Discovery:** scans `/data`, `/system_data` and `/system_data/priv` for directories named
   like `crash`, `core`, `dump`, `error`, `erpt` or `log`, and logs how many files and KiB each
   one holds. Read-only.
2. **Automatic clean:** empties the three directories in the built-in list
   (`validated_targets` in `main.c`), manually validated over FTP — see the section below.
   This always runs, with no external confirmation needed.
3. **Optional extra:** if `/data/clear_logs.paths` exists (one directory per line), those paths
   are also cleaned in this run, in addition to the built-in ones. Not required for normal
   operation — only useful to clean something found during discovery without recompiling.

Everything is logged to `/data/clear_logs.log` (append mode, timestamped per run), because not
every payload manager surfaces the process's standard output.

Safeguards: paths containing `..` or too shallow (e.g. `/data`) are rejected, symlinks are not
followed, and the listed root directory itself is kept (only its contents are removed).

**Autoload warning:** since cleaning now runs without confirmation on every launch, adding this
payload to the payload manager's `autoload.txt` means it will clean the three built-in
directories *every time the console boots*. That is the intended behavior, but if you want to
add a fourth directory, validate it manually over FTP first (delete a test file, confirm the
owning service recovers) before adding it to the built-in list or to `clear_logs.paths`.

## Requirements

- A PS5 with a working jailbreak on your firmware and an active ELF loader.
- [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) with `PS5_PAYLOAD_SDK` pointing
  to it.

## Build and deploy

```bash
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make
make test PS5_HOST=<ps5_ip>
```

`clear_logs.elf` and the payload manager `.json` are not version-controlled — the `.json`
checksum depends on the binary produced by each build. To generate the `.json` after building:

```bash
sha256sum clear_logs.elf
```

and fill in `clear_logs.elf.json` with that value, following the schema your payload manager
uses (`name`, `filename`, `checksum`, `version`, etc.).

## Directories confirmed over FTP (firmware 13.60)

- `/system_data/priv/error/history` — error history shown in the system UI (one `.json` per
  error).
- `/system_data/priv/vshlog` — plain-text system shell log.
- `/system_data/priv/system_logger2/nobackup/database` — internal telemetry (`sl2_log.db` +
  `sl2_log.db-journal`), an **active SQLite database** while the console is on.

## Note on active SQLite databases

`sl2_log.db` (or any `.db` with a `-journal`/`-wal` sibling) may be in use at clean time. The
payload deletes it anyway and prints a `[note]` warning when it detects this, but the owning
service may log a transient error until it recreates the file — that is expected, not
corruption. Prefer running the clean right after a reboot to reduce the odds of catching a
transaction mid-write.

**Tested on 13.60 (over FTP, no `-journal` present at the time):** deleting `sl2_log.db` and
`sl2_capture.db` was safe — `system_logger2` recreated `sl2_log.db` from scratch (32 KB) as
soon as a new event was logged, with no system hang. `sl2_capture.db` only reappears once the
service needs it again.

## General warning

The directories above were listed from manual FTP inspection; any other path surfaced by the
automatic scan should be reviewed before it goes into `clear_logs.paths`. Do not add save data,
game or system directories to the list.

## No warranty

This software is provided "as is", without warranty of any kind, express or implied. Use it at
your own risk. The author is not liable for any damage, data loss or console malfunction
resulting from its use.

## License

This payload is built with [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk), licensed
under GPL-3.0. This project is distributed under the same license; see `LICENSE`.
