# Hardware evidence directory

> **目录用途与证据边界；范围说明更新于 2026-11-16。**
>
> 本目录用于保存真实板端采集的原始日志。下面的采集说明不表示设备已经接入或链路已经验证；历史检查、主机测试和合成样本均不能替代 Sensor / media graph / rkAIQ 的板端证据。
>
> 现行依据：[当前架构](../../CURRENT_ARCHITECTURE.md) · [执行与验证状态](../../CODEX_EXECUTION_STATUS.md)。范围说明更新：2026-11-16（Asia/Shanghai）。

No RK3588 board, MIPI RAW Sensor, BSP or rkAIQ runtime was attached to this
environment, so there is no fabricated probe log here. On the target, save the
unmodified output of:

```bash
scripts/isp_probe.sh | tee docs/isp_migration/hardware/isp_probe_$(date +%Y%m%d_%H%M%S).log
```

Add board-specific `uname`, device-tree, media graph, V4L2 format, dmesg and
rkAIQ evidence only after it has been captured on the actual board.
