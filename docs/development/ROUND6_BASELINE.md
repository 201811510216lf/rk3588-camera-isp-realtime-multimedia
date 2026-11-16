# Round 6 Baseline

## Scope

The repository root for this round is `demo_multhread_decode_infer_mulmodel/`. The parent directory contains execution notes and is not a Git worktree. The Git worktree currently contains changes from the previous rounds; this baseline records the state before Round 6 additions.

## Repository state

- HEAD: `6085dab Converge project to single MIPI camera pipeline`
- Working tree: modified tracked files include `CMakeLists.txt`, `README.md`, camera lifecycle/CLI/V4L2 files and tests; many Round 5 dual camera, frame pairing, fanout, AIQ and documentation files are untracked.
- Existing top-level modules: `app_options`, `v4l2_isp_camera`, `camera_frame`, `dual_camera_pipeline`, `frame_pairer`, `composite_processor`, `encoded_packet_fanout`, `mpp_encoder`, `postprocess`, `aiq_controller`, and media graph tooling.
- Existing test targets: lifecycle/CLI, composite processor, encoded packet fanout, frame pairer, media graph validator, project structure validation, and Python ISP quality analyzer tests.

## Build environment observation

The current Windows shell has Python 3.12.7 and FFmpeg/ffprobe 7.0.2 available. `cmake`, `g++`, `clang++`, and `pkg-config` are not on PATH, and no host C++ build was run in this shell. The CMake project is C++17 and provides a host-only path (`BUILD_RK_DEMO=OFF`) that does not require RKNN/MPP/RGA/OpenCV. The RK demo target is conditionally skipped when sibling vendor SDK directories are missing.

## Existing architecture

The current production path is a V4L2 NV12/NV12M capture pipeline into a software `FramePairer`, two AI branches, CPU/RGA composite processing, one MPP H.264 encoder, and encoded packet fan-out to optional FFmpeg RTMP and segmented MP4 sinks. The code and documents correctly mark board-only claims as unverified. No VirtualSensor, RAW10 pack/unpack library, standalone Software ISP module, AE controller, AWB controller, IQ profile loader, or host-executable software ISP demo exists yet.

## Fifth-round review items

- NCHW/NHWC and model-specific tensor contracts remain coupled to existing RKNN/postprocess code and require explicit backend tests before claiming fixed.
- V4L2 layout metadata is now represented more explicitly in the camera/frame code, but actual single/multi-planar stride, offset, `bytesused`, and `sizeimage` behavior remains board-dependent and unmeasured.
- MPP timeout/completion ownership is documented as conservative/pending; no board evidence exists for asynchronous completion semantics.
- Fan-out validates packet PTS/DTS monotonicity and propagates sink errors, but independent queue/backpressure behavior still needs host simulation coverage.
- Stop/cancel and buffer lease tests exist for the current pipeline; the new software-first pipeline must preserve bounded shutdown and lease ownership.

## Round 6 implementation priorities

1. Add a dependency-light RAW10 utility and VirtualSensor with deterministic scenes and controls.
2. Add an independent, host-buildable Software ISP (BLC, DPC, LSC, demosaic, AWB, CCM, gamma, 2D/3D NR, sharpen, NV12 conversion) with focused tests.
3. Add software AE/AWB feedback loops and profile loading without pretending to be rkAIQ or RKISP.
4. Reuse the existing pairing/composite/fan-out contracts for a dual VirtualSensor demo and add explicit Demo/ONNX/RKNN backend boundaries.
5. Generate reports and documentation that distinguish source implementation, host measurements, and unverified hardware integration.

## Baseline limitations

No RK3588 board, IMX415 module, BSP, rkAIQ SDK, IQ file, RKNN model, or RTMP server is available in this environment. Any future report must keep these items in the `NOT_MEASURED`/`HARDWARE_BACKEND_NOT_AVAILABLE` category unless new evidence is collected.
