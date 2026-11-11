# Hardware evidence directory

No RK3588 board, MIPI RAW Sensor, BSP or rkAIQ runtime was attached to this
environment, so there is no fabricated probe log here. On the target, save the
unmodified output of:

```bash
scripts/isp_probe.sh | tee docs/isp_migration/hardware/isp_probe_$(date +%Y%m%d_%H%M%S).log
```

Add board-specific `uname`, device-tree, media graph, V4L2 format, dmesg and
rkAIQ evidence only after it has been captured on the actual board.
