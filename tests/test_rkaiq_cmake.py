"""Negative SDK discovery tests; generated headers/ELF bytes are test fixtures only."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class RkAiqCMakeTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.module = Path(__file__).resolve().parents[1] / "cmake" / "RkAiqAdapter.cmake"
        self.cmake = os.environ.get("RKISP_TEST_CMAKE", "cmake")

    def run_check(self, body, expected, success=False):
        script = self.root / "check.cmake"
        script.write_text(
            f'set(CMAKE_SYSTEM_NAME Linux)\nset(CMAKE_SYSTEM_PROCESSOR aarch64)\n'
            f'include("{self.module.as_posix()}")\n{body}\nconfigure_rkaiq_adapter(unused)\n',
            encoding="utf-8",
        )
        result = subprocess.run([self.cmake, "-P", str(script)], cwd=self.root,
                                capture_output=True, text=True, encoding="utf-8", errors="replace")
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        self.assertIn(expected, " ".join((result.stdout + result.stderr).split()))

    def sdk_fixture(self, machine):
        include = self.root / "include"
        include.mkdir()
        (include / "rk_aiq_user_api2_sysctl.h").write_text("// negative test fixture\n")
        data = bytearray(20)
        data[:4] = b"\x7fELF"
        data[4] = 2
        data[5] = 1
        data[18:20] = machine.to_bytes(2, "little")
        library = self.root / "libfixture.so"
        library.write_bytes(data)
        return f'set(ENABLE_RKAIQ ON)\nset(RKAIQ_INCLUDE_DIR "{include.as_posix()}" CACHE PATH "" FORCE)\nset(RKAIQ_LIBRARIES "{library.as_posix()}" CACHE STRING "" FORCE)'

    def test_sdk_off_needs_no_dependencies(self):
        self.run_check("set(ENABLE_RKAIQ OFF)", "adapter OFF", success=True)

    def test_missing_header_fails(self):
        self.run_check("set(ENABLE_RKAIQ ON)\nset(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)\n"
                       f'set(CMAKE_FIND_ROOT_PATH "{self.root.as_posix()}")', "missing actual")

    def test_wrong_architecture_fails_before_link(self):
        self.run_check(self.sdk_fixture(62), "architecture mismatch")

    def test_no_verified_adapter_fails(self):
        self.run_check(self.sdk_fixture(183), "no verified BSP API port")

    def test_unknown_target_and_elf_class_fail(self):
        body = self.sdk_fixture(183)
        self.run_check(body + '\nset(CMAKE_SYSTEM_PROCESSOR unverified)', "architecture is unknown")
        library = self.root / "libfixture.so"
        data = bytearray(library.read_bytes()); data[4] = 1; library.write_bytes(data)
        self.run_check(body, "64-bit ELF class")

    def test_uninspected_archives_fail(self):
        body = self.sdk_fixture(183)
        (self.root / "libfixture.so").write_bytes(b"!<arch>\n" + b" " * 40)
        self.run_check(body, "static archives/unknown formats are unsupported")

    def test_header_without_libraries_fails(self):
        body = self.sdk_fixture(183) + '\nset(RKAIQ_LIBRARIES "" CACHE STRING "" FORCE)'
        self.run_check(body, "set RKAIQ_LIBRARIES")

    def test_directories_cannot_masquerade_as_header_or_adapter(self):
        body = self.sdk_fixture(183)
        header = self.root / "include" / "rk_aiq_user_api2_sysctl.h"
        header.unlink(); header.mkdir()
        self.run_check(body, "missing actual")
        header.rmdir(); header.write_text("// negative fixture only\n")
        self.run_check(body + f'\nset(RKAIQ_ADAPTER_SOURCE "{self.root.as_posix()}" CACHE FILEPATH "" FORCE)',
                       "no verified BSP API port")


if __name__ == "__main__":
    unittest.main()
