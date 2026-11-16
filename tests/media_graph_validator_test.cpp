#include "media_graph_validator.h"

#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
    const auto root = std::filesystem::temp_directory_path() / "rk3588_media_graph_validator_test";
    std::filesystem::create_directories(root);
    const auto graph = root / "graph.txt";
    const auto cam0 = root / "cam0.txt";
    const auto cam1 = root / "cam1.txt";
    {
        std::ofstream(graph) << "- entity 1: sensor0\n  -> /dev/video10 [ENABLED]\n";
        std::ofstream(cam0) << "Format Video Capture: NV12 1920x1080\n帧率 (30/1)\n";
        std::ofstream(cam1) << "Format Video Capture: NV12M 1920x1080\n帧率 (30/1)\n";
    }
    MediaGraphValidationConfig config;
    config.camera0_node = "/dev/video10";
    config.camera1_node = "/dev/video11";
    config.graph_file = graph.string();
    config.camera0_info_file = cam0.string();
    config.camera1_info_file = cam1.string();
    config.expected_width = 1920;
    config.expected_height = 1080;
    config.expected_fps = 30;
    const auto ok = validate_media_graph(config);
    assert(ok.ok());
    config.camera1_node = config.camera0_node;
    const auto duplicate = validate_media_graph(config);
    assert(!duplicate.ok());
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    return 0;
}

