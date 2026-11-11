"""Dependency-free structural checks for the camera/ISP migration."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


required = [
    "CMakeLists.txt",
    "README.md",
    "CODEX_CAMERA_ISP_EXECUTION_PLAN.md",
    "include/app_options.h",
    "include/camera_frame.h",
    "include/bounded_frame_queue.h",
    "include/v4l2_isp_camera.h",
    "include/mipi_pipeline.h",
    "lib/app_options.cpp",
    "lib/v4l2_isp_camera.cpp",
    "lib/mipi_pipeline.cpp",
    "tests/camera_lifecycle_test.cpp",
    "scripts/isp_probe.sh",
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

main = (ROOT / "main.cpp").read_text(encoding="utf-8")
check("MipiPipeline" in main and "run_legacy" in main, "both input modes are not wired")

v4l2 = (ROOT / "lib/v4l2_isp_camera.cpp").read_text(encoding="utf-8")
for token in ("VIDIOC_QUERYCAP", "VIDIOC_S_FMT", "VIDIOC_REQBUFS", "VIDIOC_QUERYBUF",
              "VIDIOC_EXPBUF", "VIDIOC_QBUF", "VIDIOC_DQBUF", "VIDIOC_STREAMON",
              "VIDIOC_STREAMOFF", "poll"):
    check(token in v4l2, f"V4L2 operation missing: {token}")

mipi = (ROOT / "lib/mipi_pipeline.cpp").read_text(encoding="utf-8")
check("imcvtcolor" in mipi and "EncodeNV12" in (ROOT / "lib/mpp_encoder.cpp").read_text(encoding="utf-8"),
      "mipi path does not reuse RGA/MPP conversion")

streaming = (ROOT / "lib/streaming_manager.cpp").read_text(encoding="utf-8")
check("RTSP output is not implemented" in streaming, "unsupported RTSP output is still a fake success")

postprocess = (ROOT / "lib/postprocess.cpp").read_text(encoding="utf-8")
check("labels[id]" in postprocess and "OBJ_NAME_MAX_SIZE - 1" in postprocess,
      "event labels are not tied to the model class ID safely")

frame = (ROOT / "include/camera_frame.h").read_text(encoding="utf-8")
check("stop_accepting" in frame and "wait_for_zero" in frame, "lease shutdown contract missing")

probe = (ROOT / "scripts/isp_probe.sh").read_text(encoding="utf-8")
check("VIDIOC" not in probe and "media-ctl" in probe and "v4l2-ctl" in probe,
      "probe should remain read-only and use board tools")
check("rm " not in probe and "tee /" not in probe, "probe contains a destructive/write command")

plan = (ROOT / "CODEX_CAMERA_ISP_EXECUTION_PLAN.md").read_text(encoding="utf-8")
check("当前状态：`IMPLEMENTED_AWAITING_HARDWARE`" in plan, "execution status not updated")

print(f"validate_project: PASS ({len(required)} required files)")
