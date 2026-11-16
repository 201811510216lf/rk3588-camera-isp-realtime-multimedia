# Dual camera sync

## Principle

Two independent VirtualSensor instances use monotonic timestamps and FramePairer tolerance/timeout accounting; this is software timestamp pairing, not synchronized exposure.

## Source and flow

`frame_pairer.h; tools/camera_isp_demo.cpp`

## Parameters and I/O

Inputs are configured through `VirtualSensorConfig`, `IspConfig`, `AeConfig` and the demo command line. Outputs are typed RGB888/NV12 frames, statistics and JSON reports. Bounds and buffer sizes are checked at module boundaries.

## Test method and current result

Run `cmake -S . -B build-host -DBUILD_RK_DEMO=OFF`, `cmake --build build-host`, then `ctest --test-dir build-host --output-on-failure`. The checked-in unit tests cover RAW10, Bayer processing, ISP output, AE/profile behavior, pairing and the demo AI backend. The original Round 6 shell did not expose CMake/C++ tools on PATH. Current host build, review and verification evidence is tracked in [CODEX_EXECUTION_STATUS.md](../CODEX_EXECUTION_STATUS.md); do not infer current test status from this historical environment note.

## Known limits and interview prompts

The implementation is a host Software ISP and does not claim RKISP, rkAIQ, RGA, MPP, hardware synchronized exposure or RKNN model accuracy. Explain why each hardware boundary needs board evidence, how RAW10 MSBs are packed, how AE avoids oscillation, and how leases/queues bound shutdown.
