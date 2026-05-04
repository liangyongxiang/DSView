# dsview-cli

`dsview-cli` is a headless command-line bridge for DreamSourceLab devices built
on top of `libsigrok4DSL`. The current stage keeps the command surface close to
`sigrok-cli`, while retaining a few DSView-specific extensions needed by this
fork.

## Build

From the DSView repository root:

```bash
cmake --build <build-dir> --target dsview-cli
```

The executable is emitted next to the GUI binary:

- `build.dir/dsview-cli`

Standalone cli-only configure/build:

```bash
cmake -S external/DSView/cli -B external/DSView/build-cli-only-local -G Ninja
cmake --build external/DSView/build-cli-only-local
cmake --install external/DSView/build-cli-only-local --prefix <install-prefix>
```

The standalone entry keeps the GUI CMake path out of the configure step. It
still stages or installs the runtime assets that `dsview-cli` actually needs:

- `DSView/res`
- `DSView/demo`
- `libsigrokdecode4DSL/decoders`
- Windows runtime DLLs and embedded Python files

## Testing

The repo now carries a Python contract-test harness for the CLI surface:

```bash
python external/DSView/cli/tests/contract.py
```

There is also a direct `Command Shape` module test target:

```bash
cmake --build <build-dir> --target dsview-cli-command-shape-test
./build.dir/dsview-cli-command-shape-test
```

By default it:

- rebuilds `dsview-cli` from `external/DSView/build.dir`
- exercises `--scan`, `--show`, `--get`, `--set`
- captures a self-contained `virtual-demo` `.dsl` fixture
- verifies `.dsl -> srzip -> dsl`
- verifies offline and live protocol table export
- verifies parallel `-P` with one `--decode-output` per stack
- checks the generated JSON envelopes and key artifacts

Useful switches:

- `--skip-build`
  Reuse the existing binary instead of rebuilding first.
- `--run-standalone-build`
  Also configure and build the standalone `cli-only` CMake entry.
- `--binary-path <path>`
  Test a specific `dsview-cli.exe`.
- `--output-root <dir>`
  Write logs and generated artifacts to a custom directory.

## Stage-1 command surface

Supported actions:

- `--scan`
- `--show`
- `--get`
- `--set`
- capture with `-d`, `-c`, `--samples`, `--time`, `-C`, `-o`, `-O`
- offline waveform export with `-i`, `-o`, `-O`
- protocol table export with `-i` or live capture plus `-P` and
  `--decode-output`

Supported output formats:

- `-O srzip`
- `-O dsl`

Current DSView-specific extensions:

- `--meta FILE`
- `--json FILE`
- `--trig-pos PCT`
- `--decode-output FILE`

Examples:

```bash
./build.dir/dsview-cli --scan
./build.dir/dsview-cli --show -d DSLogic
./build.dir/dsview-cli --get samplerate -d DSLogic
./build.dir/dsview-cli --set -d DSLogic -c samplerate=10M
./build.dir/dsview-cli -d DSLogic -c samplerate=10M --samples 1M -C 0=CLK,1-3 -O srzip -o capture.sr
./build.dir/dsview-cli -d DSLogic -c samplerate=10M --time 2s -C 0-3 -O dsl -o capture.dsl --meta capture.meta.json --json capture.json
./build.dir/dsview-cli -i capture.dsl -o capture.sr -O srzip --json export.json
./build.dir/dsview-cli -i capture.sr -o capture.dsl -O dsl --json export.json
./build.dir/dsview-cli -i capture.dsl -P uart:rx=CH2:baudrate=921600 --decode-output uart.csv --json decode.json
./build.dir/dsview-cli -d DSLogic -c samplerate=10M --time 6s -C 2=CH2 -P uart:rx=CH2:baudrate=921600 --decode-output uart.csv --json decode.json
```

## Stage-1 offline waveform export

Current supported shapes:

```bash
./build.dir/dsview-cli \
  -i capture.dsl \
  -o capture.sr \
  -O srzip \
  --json export.json
```

```bash
./build.dir/dsview-cli \
  -i capture.sr \
  -o capture.dsl \
  -O dsl \
  --json export.json
```

Current boundaries:

- Input must be a local `.dsl` or `.sr` session file.
- Stage-1 waveform export currently supports logic session files only.
- `-o/--output-file` is required.
- Current validated export shapes are:
  - `.dsl -> srzip`
  - `.sr -> dsl`
- `-O/--output-format` currently supports `srzip` and `dsl` on this path, but
  not every input/output combination is part of the stage-1 contract yet.
- This path is separate from protocol table export:
  - no `-P`
  - no `--decode-output`
- This path is separate from live capture:
  - no `-d`
  - no `-c`
  - no `--samples`
  - no `--time`
  - no `--meta`
- `--json FILE` writes a thin result envelope for the export command.

## Stage-1 protocol table export

Current supported shape:

```bash
./build.dir/dsview-cli \
  -i capture.dsl \
  -P uart:rx=CH2:baudrate=921600 \
  --decode-output uart.csv
```

```bash
./build.dir/dsview-cli \
  -d DSLogic \
  -c samplerate=10M \
  --time 6s \
  -C 2=CH2 \
  -P uart:rx=CH2:baudrate=921600 \
  --decode-output uart.csv
```

```bash
./build.dir/dsview-cli \
  -i capture.dsl \
  -P uart:rx=CH2:baudrate=921600 \
  --decode-output uart.csv \
  -P spi:clk=CH0:mosi=CH1:miso=CH3:cs=CH4 \
  --decode-output spi.txt \
  --json decode.json
```

Current boundaries:

- Exactly one decode mode is selected at a time:
  - offline decode from a local `.dsl` session file via `-i`
  - live decode from a logic capture device via `-d ...`
- The input session or live capture source must contain logic channels.
- One or more `-P` values are accepted.
- A single `-P` may contain a stacked decoder chain such as `uart,midi`.
- Multiple parallel decoder stacks are supported as a DSView CLI extension:
  - each `-P` must have one matching `--decode-output`
  - pairing is positional by command-line order
  - the number of `-P` and `--decode-output` arguments must match exactly
- Each `--decode-output` currently writes `csv` or `txt`, inferred from its own
  filename extension.
- The exported table follows DSView GUI protocol-table semantics:
  - header starts with `Id,Time[ns],...`
  - only the top-level row of each decoder stack is exported
  - each cell uses the first display string of the annotation
- `txt` and `csv` currently share the same table content semantics.
- `--json FILE` writes one thin result envelope for the whole command and does
  not duplicate the table contents.
- Empty decode results are treated as success with a header-only table.
- Protocol table export is separate from waveform output modules such as
  `-O csv`; it is not part of the normal sample-output path.
- Live decode is a one-shot export path:
  - it does not also write `-o/-O` waveform output
  - it does not use `--meta`
  - it may be combined with `--json FILE`

## Source layout

The CLI is split to follow the `sigrok-cli` file boundaries as closely as
practical for this fork:

The directories mirror the stable seams: `command/`, `runtime/`, `device/`,
`source/`, `waveform/`, `decode/`, and `support/`.

Cross-group exported function seams follow `cli_<group>_<module>_<action>`.
Command-local adapter entry points keep short verb names in `command/`
because the directory and header already supply the context. Data structs keep
concept-first names such as `cli_logic_source` and `cli_selected_device`.

- `main.c`
  Process entry point that owns Option State lifecycle, help/error handling,
  Command Shape construction, and top-level command dispatch.
- `options.c`
  Command-line option parsing plus Option State lifecycle helpers for parsed
  CLI arguments.
- `shape.c`
  Command Shape construction, command-mode validation, and default-value
  normalization between parse time and execution time.
- `parse.c`
  String parsing helpers such as driver specs and generic key/value args.
- `runtime_layout.c`
  Runtime Layout for executable-relative resource layout, user-data layout,
  decoder script lookup, and embedded Python home discovery.
- `device_selected.c`
  Selected Device for Runtime Layout-based sigrok bring-up, runtime-owned
  live-device selection, virtual-input activation, and current-device
  lifecycle.
- `device_config.c`
  Device Configuration for stage-1 config schema, config request parsing,
  current-value snapshots, requested-value lookup, `--config` application,
  and live-capture limit/capture-ratio policy over Selected Device.
- `logic_source.c`
  Logic Source for live-device and offline-session bring-up, logic-only
  validation, samplerate lookup, and source-specific Channel Selection State
  preparation.
- `scan_run.c`
  Thin `--scan` command adapter.
- `show_run.c`
  Thin `--show` command adapter.
- `get_run.c`
  Thin `--get` command adapter.
- `set_run.c`
  Thin `--set` command adapter.
- `capture_run.c`
  Thin live command adapter that hands capture or live protocol decode to
  Waveform Session Execution.
- `export_run.c`
  Thin offline waveform-export adapter that hands `.dsl -> srzip` / `.dsl ->
  dsl` replay and `.sr -> dsl` conversion orchestration to Waveform Session
  Execution.
- `srzip_session_conversion.c`
  SRZip Session Conversion for `.sr` metadata parsing, probe layout
  normalization, logic chunk replay, sample repacking, and `.dsl` archive
  writeout.
- `session_metadata.c`
  Session Conversion Metadata such as `.dsl` `header`, `.dsl` `session`,
  and waveform capture metadata value trees.
- `waveform_session.c`
  Waveform Session Execution, including live/offline command orchestration,
  callback bridging, timeout/stop policy, capture byte limiting,
  cross-data repacking, and Capture Runtime State.
- `waveform_archive_output.c`
  Waveform Archive Output factory plus shared metadata, shadow-channel, and
  `.dsl` archive helpers.
- `waveform_archive_output_none.c`, `waveform_archive_output_srzip.c`,
  `waveform_archive_output_dsl.c`
  Concrete Waveform Archive Output adapters for `NONE`, `SRZIP`, and `DSL`.
- `channel_selection_state.c`
  Channel Selection State for channel parsing, request defaulting, source-slot
  mapping, device layout application, trigger application, selected-channel
  metadata, and probe-derived data.
- `decode_run.c`
  Thin offline Protocol Decode command adapter over Decode Runtime.
- `runtime.c`
  Protocol Decode runtime public seam for live/offline runners, offline decode
  command execution, internal Runtime Layout-based decode bring-up, and
  Decode Summary finalization.
- `stack_plan.c`
  Decoder Stack Plan for `-P` parsing, decoder selection, stack
  compatibility, option coercion, root-channel binding, and default export
  row planning before runtime side effects begin.
- `stack.c`
  Internal execution of Decoder Stack Plan into `libsigrokdecode` sessions,
  callback registration, samplerate injection, and stack startup.
- `feed.c`
  Internal logic replay/feed adapters that pack input waveforms for
  libsigrokdecode.
- `export.c`
  Internal Protocol Table Export formatting, file output, and annotation text
  shaping.
- `summary.c`
  Internal Decode Summary serialization for protocol-decode `--json FILE`
  sidecars through JSON value trees.
- `command_result_json.c`
  Command Result serialization for `--json FILE` sidecars such as
  `scan/show/get/set/capture/export` through JSON value trees.
- `json.c`
  JSON value-tree construction helpers plus shared render/write support for
  envelopes and metadata sidecars.
- `command_internal.h`, `parse.h`, `json.h`,
  `runtime.h`, `runtime_internal.h`, `stack_plan.h`,
  `export.h`, `summary.h`, `stack_runtime.h`,
  `dsl_layout.h`, `waveform_archive_output_internal.h`
  Internal helper headers that keep command adapters, support code, and decode
  planning/export/runtime hooks from sharing one umbrella seam.
- `tests/command_shape_test.c`, `tests/decode_stack_plan_test.c`,
  `tests/device_config_test.c`,
  `tests/waveform_archive_output_test.c`,
  `tests/srzip_session_conversion_test.c`, `tests/decode_export_test.c`,
  `tests/decode_summary_test.c`
  Direct module tests for Command Shape, Device Configuration, Decoder Stack
  Plan, Waveform Archive Output, SRZip Session Conversion, Protocol Table
  Export, and Decode Summary using small Tool Doubles instead of the full
  CLI/runtime bring-up path.

## Notes

- Stage-1 capture currently focuses on logic capture.
- Stage-1 offline waveform export currently supports:
  - `.dsl -> srzip`
  - `.sr -> dsl`
  - verified `dsl -> srzip -> dsl` roundtrip for logic session files.
- Stage-1 protocol decode currently focuses on `.dsl` and live-capture
  protocol table export for logic captures.
- `--json FILE` writes a machine-readable result envelope without changing the
  normal `stdout` / `stderr` text output.
- `--meta FILE` writes a capture metadata sidecar independent of the main
  output format.
