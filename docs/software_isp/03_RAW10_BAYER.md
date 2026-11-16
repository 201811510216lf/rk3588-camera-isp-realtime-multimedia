# RAW10 and Bayer

## Principle

Four pixels occupy five bytes: four low bytes followed by packed two-bit MSBs; four Bayer patterns are supported.

## Source and flow

`camera/raw10.h; lib/camera/raw10.cpp`

## Parameters and I/O

Inputs are configured through `VirtualSensorConfig`, `IspConfig`, `AeConfig` and the demo command line. Outputs are typed RGB888/NV12 frames, statistics and JSON reports. Bounds and buffer sizes are checked at module boundaries.

## Test method and current result

Run `cmake -S . -B build-host -DBUILD_RK_DEMO=OFF`, `cmake --build build-host`, then `ctest --test-dir build-host --output-on-failure`. The checked-in unit tests cover RAW10, Bayer processing, ISP output, AE/profile behavior, pairing and the demo AI backend. This Windows shell currently has no CMake or C++ compiler, so local build status is `NOT_MEASURED`.

## Known limits and interview prompts

The implementation is a host Software ISP and does not claim RKISP, rkAIQ, RGA, MPP, hardware synchronized exposure or RKNN model accuracy. Explain why each hardware boundary needs board evidence, how RAW10 MSBs are packed, how AE avoids oscillation, and how leases/queues bound shutdown.
