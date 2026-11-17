"""Validate the current product boundary and required build inputs."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
required = ["CMakeLists.txt", "README.md", "CODEX_AUTOPILOT_PLAN.md",
            "src/main.cpp", "src/dual_camera_pipeline.cpp", "include/camera_frame.h",
            "include/bounded_frame_queue.h", "src/v4l2_isp_camera.cpp", "src/frame_pairer.cpp",
            "tools/camera_isp_demo.cpp", "src/camera/virtual_sensor.cpp", "src/isp/isp_pipeline.cpp"]
for name in required:
    assert (ROOT / name).is_file(), f"missing {name}"
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
for name in ["Threads::Threads", "CMAKE_EXPORT_COMPILE_COMMANDS", "ENABLE_V4L2",
             "ENABLE_ROCKCHIP_RGA", "ENABLE_ROCKCHIP_MPP", "ENABLE_RKNN", "ENABLE_RKAIQ"]:
    assert name in cmake, f"missing build contract {name}"
assert not (ROOT / "include/rknn_api.h").exists(), "bundled header must not shadow BSP SDK"
assert "enum class Source { Mipi };" in (ROOT / "include/app_options.h").read_text(encoding="utf-8")
for name in ["include/stream_loader.h", "include/mipi_pipeline.h", "src/mipi_pipeline.cpp"]:
    assert not (ROOT / name).exists(), f"obsolete input present: {name}"
for line in (ROOT / "CMakeLists.txt").read_text(encoding="utf-8").splitlines():
    name = line.strip().rstrip(")")
    if name.startswith(("src/", "tests/", "tools/")) and name.endswith(".cpp"):
        assert (ROOT / name).is_file(), f"CMake references missing implementation: {name}"
print(f"validate_project: PASS ({len(required)} required files)")
