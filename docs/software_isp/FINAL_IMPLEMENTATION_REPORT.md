# Round 6 Final Implementation Report

## 1. Scope and evidence

The repository root for this round is `demo_multhread_decode_infer_mulmodel/`. The implementation is software-first and uses `IMX415_SIM` as a clearly labelled virtual source. No IMX415 module, RK3588 board, BSP, rkAIQ SDK, IQ file, RKNN model or RTMP server was available in the current environment.

The Windows shell has Python 3.12 and FFmpeg 7.0.2, but no `cmake`, `g++`, `clang++` or `pkg-config` on `PATH`. Python structural and ISP analyzer tests were run. C++ configure/build/ctest and board tests are `NOT_MEASURED`, not claimed as passing.

## 2. New files

- `include/camera/raw10.h`, `lib/camera/raw10.cpp`: standard RAW10 pack/unpack with stride, odd-width and length checks.
- `include/camera/virtual_sensor.h`, `include/camera/virtual_sensor_config.h`, `lib/camera/virtual_sensor.cpp`, `lib/camera/virtual_sensor_config.cpp`: deterministic eight-scene VirtualSensor with four Bayer patterns, controls, sequence and timestamps.
- `include/isp/isp_pipeline.h`, `lib/isp/isp_pipeline.cpp`: BLC, DPC, radial LSC, bilinear and edge-aware demosaic, Gray World/White Patch/Manual AWB, CCM, gamma LUT, Gaussian/median/bilateral 2DNR, motion-aware temporal 3DNR, sharpen, statistics and BT.601/BT.709 NV12 output.
- `include/isp/ae_controller.h`, `lib/isp/ae_controller.cpp`: smoothed shutter-first AE with exposure/gain limits, clipping protection and anti-flicker quantization.
- `include/isp/isp_profile.h`, `lib/isp/isp_profile.cpp`, `configs/isp/imx415_1080p.yaml`: dependency-free software profile loader/saver, hot apply and rollback. This is separate from Rockchip IQ.
- `include/ai/inference_backend.h`, `lib/ai/inference_backend.cpp`: Demo, optional ONNX and RKNN backend boundary. Demo detections are marked `SIMULATED`.
- `tools/camera_isp_demo.cpp`: single- and dual-sensor host demonstration, AE/AWB feedback, FramePairer, optional AI and NV12 composite output.
- `scripts/encode_nv12_with_ffmpeg.py`: explicit host FFmpeg H.264/MP4 utility and optional RTMP attempt with preserved error status.
- `tests/raw_virtual_sensor_test.cpp`, `tests/software_isp_test.cpp`, `tests/ae_profile_test.cpp`, `tests/ai_backend_test.cpp`.
- `docs/software_isp/01_...md` through `17_...md` and `docs/development/ROUND6_BASELINE.md`.
- CMake adds `software_isp`, `virtual_camera`, `demo_ai`, `camera_isp_demo`, focused host tests and optional `ENABLE_ASAN_UBSAN` flags.

## 3. Existing P1 fixes

`lib/postprocess.cpp` now uses the source index for both NMS class checks, keeps the sorted confidence position paired with the sorted score, bounds label loading and validates null inputs, dimensions, scale and quantization vectors. Existing V4L2 plane/lease, fan-out and stop/cancel contracts remain in place; board-specific completion semantics remain unverified.

## 4. Software ISP and 3A status

The complete host pipeline is implemented as typed C++ stages: RAW10 → unpack → BLC/DPC/LSC → Bayer demosaic → AWB → CCM → gamma → 2DNR/3DNR → sharpen → RGB888/NV12. AE consumes image statistics and writes exposure, analogue gain and digital gain back to VirtualSensor; AWB gains change the RGB pixels and are smoothed between frames. Statistics include brightness, clipping, noise variance, Laplacian sharpness, edge strength, temporal stability and a no-reference color-balance error.

The unit tests cover RAW10 boundaries, all Bayer patterns, NV12 validity, ISP statistics, AE limit response and profile loading. They could not be compiled in this shell because the C++ toolchain is absent.

## 5. Dual path and AI status

Two independent VirtualSensor and SoftwareIsp instances can be run by `camera_isp_demo`. FramePairer uses monotonic timestamps, tolerance, queue limits and timeout accounting. This is software timestamp pairing and is not hardware synchronized exposure. CPU `CompositeProcessor` can create a side-by-side NV12 frame. The Demo backend is deterministic and explicitly labelled; no model accuracy is claimed. RKNN/ONNX paths return an unavailable status when their SDK/runtime is absent.

## 6. Encoding and streaming status

The existing MPP/RGA/V4L2 production interfaces were retained. The new host demo emits NV12 and the FFmpeg helper was smoke-tested with a tiny NV12 fixture; the C++ pipeline itself was not built here. No RTMP server was configured, so RTMP is `NOT_MEASURED`; a failed connection is propagated by the helper rather than reported as success. MPP, RGA, RKISP and rkAIQ are not represented by the software implementations.

## 7. Validation performed

- `python tests/validate_project.py`: PASS.
- Python ISP quality analyzer tests: PASS (6 tests).
- `python scripts/run_round6_reports.py`: generated labelled auxiliary SIMULATED AE/AWB/quality plots and JSON under `reports/isp/`.
- `git diff --check`: PASS for the Round 6 edits.
- Environment survey: Python 3.12 and FFmpeg 7.0.2 available; CMake/C++ compiler/pkg-config unavailable.
- The dependency-free FFmpeg helper was smoke-tested with a 16×16 NV12 frame: FFmpeg 7.0.2 produced an H.264-in-MP4 file and `ffprobe` reported `codec_name=h264`, `16x16`, `yuv420p`. This validates the helper and host encoder only; the C++ pipeline and RK MPP remain `NOT_MEASURED`.
- C++ build, sanitizer runs, 4K performance and RTMP connection: `NOT_MEASURED`.

## 8. Hardware gap and résumé-safe claims

Still requiring board evidence are IMX415 I2C probe, sensor driver, DTS, MIPI CSI/DPHY/RKCIF/RKISP, real IQ tuning, rkAIQ, RKNN model inference, RGA import/fence behavior, MPP completion, hardware dual-camera synchronization and RTMP service availability. Safe résumé claims are: implemented a dependency-light Software ISP and RAW10 simulator; implemented tested AE/AWB feedback and image-quality metrics; designed explicit RKNN/RGA/MPP/V4L2 backend boundaries; and retained board-only paths without presenting simulations as hardware validation.
