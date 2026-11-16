# DTS patch policy

This directory deliberately contains a reviewable overlay template, not a
claim of a board-ready patch. The repository has no selected RK3588 board,
DTS, schematic, lane map, GPIO polarity, regulator names, or I2C bus. Applying
this file without replacing the `__BOARD_SPECIFIC__` tokens would be unsafe.

Once the board is identified:

1. copy the vendor DTS and record its git commit;
2. replace tokens from the schematic and existing BSP sensor example;
3. add the reciprocal CSI endpoint and verify full/split DPHY mode;
4. compile with `dtc -I dts -O dtb` and review `git diff --check`;
5. attach `media-ctl -p`, `v4l2-ctl --all`, and dmesg evidence.
