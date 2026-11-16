"""Dependency-free structural checks for the camera/ISP migration."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


required = [
    "CMakeLists.txt",
    "README.md",
    "docs/ARCHITECTURE_REFACTOR_REPORT.md",
    "CODEX_CAMERA_ISP_EXECUTION_PLAN.md",
    "include/app_options.h",
    "include/camera_frame.h",
    "include/bounded_frame_queue.h",
    "include/v4l2_isp_camera.h",
    "include/mipi_pipeline.h",
    "include/dual_camera_pipeline.h",
    "include/frame_pairer.h",
    "include/composite_processor.h",
    "include/encoded_packet_fanout.h",
    "include/media_graph_validator.h",
    "include/aiq_controller.h",
    "tools/isp_debug_tool.cpp",
    "lib/app_options.cpp",
    "lib/v4l2_isp_camera.cpp",
    "lib/mipi_pipeline.cpp",
    "lib/dual_camera_pipeline.cpp",
    "lib/frame_pairer.cpp",
    "lib/composite_processor.cpp",
    "lib/encoded_packet_fanout.cpp",
    "lib/media_graph_validator.cpp",
    "lib/aiq_controller.cpp",
    "tests/camera_lifecycle_test.cpp",
    "scripts/isp_probe.sh",
    "scripts/isp_quality_analyzer.py",
    "scripts/iq_manifest.py",
    "tests/test_isp_quality_analyzer.py",
    "docs/isp_migration/00_repository_audit.md",
    "docs/isp_migration/01_baseline.md",
    "docs/isp_migration/02_buffer_lifecycle.md",
    "docs/isp_migration/03_isp_bringup.md",
    "docs/isp_migration/04_test_report.md",
    "docs/isp_migration/hardware/README.md",
]
for relative in required:
    check((ROOT / relative).is_file(), f"missing required file: {relative}")

cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
check("BUILD_HOST_TESTS" in cmake and "BUILD_RK_DEMO" in cmake, "CMake options missing")
check("REQUIRE_RK_DEMO" in cmake, "strict board dependency option missing")
check("isp_host_tests" in cmake and "add_test" in cmake, "host test target missing")
check("isp_debug_tool" in cmake, "isp_debug_tool target missing")
check("file(GLOB DEMO_LIB_SOURCES" not in cmake,
      "board target must not reintroduce arbitrary legacy sources")
for removed in (
    "include/stream_loader.h", "lib/stream_loader.cpp",
    "include/mpp_decoder.h", "lib/mpp_decoder.cpp",
    "include/detection_fusion_manager.h", "lib/detection_fusion_manager.cpp",
    "include/streaming_manager.h", "lib/streaming_manager.cpp",
    "include/dma_alloc.hpp", "include/ThreadPool.hpp", "include/m_buffer.hpp",
):
    check(not (ROOT / removed).exists(), f"removed legacy file still present: {removed}")

main = (ROOT / "main.cpp").read_text(encoding="utf-8")
check("DualCameraPipeline" in main and "camera0" in main and "camera1" in main,
      "dual-camera pipeline is not wired")

options = (ROOT / "include/app_options.h").read_text(encoding="utf-8")
options_impl = (ROOT / "lib/app_options.cpp").read_text(encoding="utf-8")
check("enum class Source { Mipi };" in options,
      "app options must expose the single MIPI source")
check("--source only supports mipi in this build" in options_impl,
      "CLI must reject removed legacy input mode explicitly")
check("Usage: " in options_impl and "--source=mipi" in options_impl,
      "MIPI CLI help is missing")

v4l2 = (ROOT / "lib/v4l2_isp_camera.cpp").read_text(encoding="utf-8")
for token in ("VIDIOC_QUERYCAP", "VIDIOC_S_FMT", "VIDIOC_REQBUFS", "VIDIOC_QUERYBUF",
              "VIDIOC_EXPBUF", "VIDIOC_QBUF", "VIDIOC_DQBUF", "VIDIOC_STREAMON",
              "VIDIOC_STREAMOFF", "poll"):
    check(token in v4l2, f"V4L2 operation missing: {token}")

dual = (ROOT / "lib/dual_camera_pipeline.cpp").read_text(encoding="utf-8")
check("imcvtcolor" in dual and "EncodeNV12" in (ROOT / "lib/mpp_encoder.cpp").read_text(encoding="utf-8"),
      "dual path does not reuse RGA/MPP conversion")

postprocess = (ROOT / "lib/postprocess.cpp").read_text(encoding="utf-8")
check("labels[id]" in postprocess and "OBJ_NAME_MAX_SIZE - 1" in postprocess and ".prop = obj_conf" in postprocess,
      "event labels/confidence are not tied to model output safely")

frame = (ROOT / "include/camera_frame.h").read_text(encoding="utf-8")
check("stop_accepting" in frame and "wait_for_zero" in frame, "lease shutdown contract missing")

probe = (ROOT / "scripts/isp_probe.sh").read_text(encoding="utf-8")
check("VIDIOC" not in probe and "media-ctl" in probe and "v4l2-ctl" in probe,
      "probe should remain read-only and use board tools")
check("rm " not in probe and "tee /" not in probe, "probe contains a destructive/write command")

plan = (ROOT / "CODEX_CAMERA_ISP_EXECUTION_PLAN.md").read_text(encoding="utf-8")
check("当前状态：`IMPLEMENTED_AWAITING_HARDWARE`" in plan, "execution status not updated")

for index in range(1, 14):
    prefix = f"{index:02d}_"
    check(any(path.name.startswith(prefix) for path in (ROOT / "docs" / "isp").glob("*.md")),
          f"missing docs/isp document: {prefix}")
check((ROOT / "docs" / "isp" / "patches" / "imx415-rk3588-overlay-template.dts").is_file(),
      "DTS review template missing")

print(f"validate_project: PASS ({len(required)} required files)")
