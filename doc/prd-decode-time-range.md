# PRD: 协议解码时间范围

## Problem Statement

dsview-cli 当前对离线/在线协议解码都是全量处理——从文件或采集的起始一直解码到末尾。对于长时间捕获的会话，实际需要分析的信号可能只占其中一小段。用户无法指定解码窗口，只能等待全量解码完成后再手动裁剪结果，浪费了 CPU 和等待时间。

## Solution

新增 `--decode-start TIME` 和 `--decode-end TIME` 两个命令行选项，让用户指定解码窗口。窗口外的采样不送入解码器，仅窗口内的注解被生成和导出。输出中时间列保持绝对时间（相对原始捕获起点），与全量解码时可对比。

第一阶段从离线解码开始。

## User Stories

1. 作为嵌入式工程师，我想对 `.dsl` 文件中的某个时间段做协议解码，这样长时间捕获中我只需关注发生通信的那一小段。
2. 作为嵌入式工程师，我想只指定 `--decode-start`（从此处到文件末尾解码），省去计算文件总时长的步骤。
3. 作为嵌入式工程师，我想只指定 `--decode-end`（从文件开头到此处解码），只关心前 N 秒内的事件。
4. 作为自动化脚本使用者，我希望通过 `--json FILE` 获取解码窗口的起止样本数和时间信息，以便确认我的窗口参数是否合理、确保结果可重现。
5. 作为嵌入式工程师，如果我指定了非法的窗口（start > end，或 start 超出文件范围），我应该收到明确的错误信息而非空输出。
6. 作为现有用户，我不传 `--decode-start` / `--decode-end` 时行为完全不变（全量解码）。

## Implementation Decisions

### CLI 标志

- `--decode-start TIME` — 解码窗口起点时间（如 `1.5s`、`200ms`、`10us`），使用 `sr_parse_timestring` 解析。
- `--decode-end TIME` — 解码窗口终点时间。
- 两个标志均可选：只给 start = 从起点到末尾，只给 end = 从开头到终点，两个都不给 = 全量解码。
- 两个标志的出现次数均限制一次（`set_single_option`）。

### 阶段划分

- **Stage 1（本次）**：离线解码模式支持。在线解码的 CLI 解析层预留扩展空间。
- **Stage 2（后续）**：在线解码模式支持。

### 命令模式约束

- 离线解码（`-i file.dsl -P ...`）：允许 `--decode-start` / `--decode-end`。
- 在线解码（`-d dev -P ...`）：CLI 解析层允许（预留），但执行层暂不处理——第二阶段实现。
- Scan、Show、Get、Set、离线导出、在线采集：均拒绝这两个选项。

### 窗口裁剪精度

当前 `feed.c` 以 64 样本为一组处理跨逻辑数据。窗口边界对齐到 64 样本组：
- `decode_start_sample` 向下对齐到最近 64 的倍数。
- `decode_end_sample` 向上对齐到最近 64 的倍数。
- 最大误差 63 样本。后续增加样本级精确裁剪。

### 时间→样本转换

- 采样率来自 `.dsl` 文件元数据，在 `begin_session` 时已就绪。
- 转换在 `begin_session` 中执行：`window_sample = time_ns × samplerate / SR_SEC(1)`。
- 转换后的 `decode_start_sample` / `decode_end_sample` 存入 `decode_runtime` 结构体。
- `consume_cross_logic` 只做纯整数样本数比较。

### 裁剪执行位置

裁剪发生在 `consume_cross_logic` 入口处：
- 完整组在 start 之前 → 跳过，不送入 `send_decode_logic_chunk`。
- 完整组在 end 之后 → 触发 `done`，停止解码。
- 加 TODO 标记未来样本级精确裁剪的需求。

### 窗口数据流

```
options.c → cli_option_state.decode_start / decode_end (原始字符串)
    → shape.c → cli_command_shape.decode_start / decode_end (校验后)
        → decode_runtime.begin_session → 时间→样本转换
            → decode_runtime.decode_start_sample / decode_end_sample
                → feed.c:consume_cross_logic → 窗口裁剪
```

### JSON sidecar 扩展

离线解码的 `--json FILE` 输出增加窗口信息：

```json
{
  "result": {
    "stacks": [...],
    "rows_written": 150,
    "annotations_emitted": 300,
    "decode_window_start_sample": 6400,
    "decode_window_end_sample": 12800,
    "decode_window_start_time_ns": 6400.0,
    "decode_window_end_time_ns": 12800.0
  }
}
```

窗口字段仅在指定了 `--decode-start` 或 `--decode-end` 时出现。

### 边界情况处理

| 条件 | 行为 |
|------|------|
| `start > end` | 报错退出，输出明确错误信息 |
| `start >= 文件总样本数` | 报错退出 |
| `end > 文件总样本数` | 报错退出 |
| 两个都没给 | 全量解码，行为与非窗口模式一致 |
| 两个都给了，均在文件范围内 | 正常窗口解码 |

### 涉及模块

| 模块 | 变更 |
|------|------|
| `option_state.h` | 新增 `decode_start` / `decode_end` 字段 |
| `options.c` | 解析 `--decode-start` / `--decode-end` |
| `options.c` | help 文本更新 |
| `shape.h` | `cli_command_shape` 新增 `decode_start` / `decode_end` 指针 |
| `shape.c` | `cli_command_shape_build` 传递字段；各 `unsupported_for_*` 函数更新 |
| `runtime_internal.h` | `decode_runtime` 新增 `decode_start_sample` / `decode_end_sample` |
| `runtime.c` | `begin_session` 中时间→样本转换 + 边界校验 |
| `feed.c` | `consume_cross_logic` 中窗口裁剪 + TODO |
| `summary.c` | JSON sidecar 增加窗口信息 |

## Testing Decisions

### 测试原则

测试外部行为而非实现细节：给定 CLI 参数，验证输出文件和 JSON sidecar 是否符合预期。

### 测试目标

1. **命令行形状测试**（参考 `tests/command_shape_test.c`）：验证 `--decode-start` / `--decode-end` 在各命令模式下的接受/拒绝行为。
2. **解码与导出测试**（参考 `tests/decode_export_test.c`）：验证窗口裁剪后的导出输出仅包含窗口内的注解，且 `Time[ns]` 为绝对时间。
3. **合约测试**（参考 `cli/tests/contract.py`）：离线解码 + 窗口 + JSON sidecar 的全路径集成测试。

### 已有类似测试

- `tests/command_shape_test.c` — 直接模块测试，使用 Tool Doubles。
- `tests/decode_export_test.c` — 导出格式化测试。
- `cli/tests/contract.py` — Python 合约测试，端到端验证 CLI 行为。

## Out of Scope

- 样本级精确裁剪（当前 64 样本组对齐，已标记 TODO）
- 在线解码（第二阶段）
- 离线导出模式（`-i file.dsl -o file.sr -O srzip`）的窗口支持
- 多解码窗口或分段解码
