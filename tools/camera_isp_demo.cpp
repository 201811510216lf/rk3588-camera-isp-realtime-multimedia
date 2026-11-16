#include "ai/inference_backend.h"
#include "camera/virtual_sensor.h"
#include "composite_processor.h"
#include "frame_pairer.h"
#include "isp/ae_controller.h"
#include "isp/isp_pipeline.h"
#include "isp/isp_profile.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
    uint32_t camera_count = 1, width = 640, height = 360, fps = 30, duration = 3;
    bool enable_ae = false, enable_awb = true, enable_ai = false, enable_composite = false;
    std::string scene = "color_checker", profile, output, report = "reports/isp";
    bool show_help = false;
};

bool uint_arg(const std::string &s, const char *name, uint32_t *out) {
    const std::string p = std::string(name) + "="; if (s.rfind(p, 0) != 0) return false;
    try { *out = static_cast<uint32_t>(std::stoul(s.substr(p.size()))); return true; } catch (...) { return false; }
}
bool str_arg(const std::string &s, const char *name, std::string *out) { const std::string p = std::string(name) + "="; if (s.rfind(p, 0) != 0) return false; *out = s.substr(p.size()); return true; }
bool next_str(int argc, char **argv, int *index, const std::string &arg, const char *name, std::string *out) {
    if (arg != name) return false; if (*index + 1 >= argc) return false; *out = argv[++(*index)] ? argv[*index] : ""; return !out->empty();
}
bool next_uint(int argc, char **argv, int *index, const std::string &arg, const char *name, uint32_t *out) {
    if (arg != name || *index + 1 >= argc) return false; try { *out = static_cast<uint32_t>(std::stoul(argv[++(*index)])); return true; } catch (...) { return false; }
}

bool parse(int argc, char **argv, Options *o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i] ? argv[i] : "";
        if (a == "--enable-ae") o->enable_ae = true; else if (a == "--enable-awb") o->enable_awb = true; else if (a == "--disable-awb") o->enable_awb = false; else if (a == "--enable-ai") o->enable_ai = true; else if (a == "--enable-composite") o->enable_composite = true;
        else if (uint_arg(a, "--camera-count", &o->camera_count) || uint_arg(a, "--width", &o->width) || uint_arg(a, "--height", &o->height) || uint_arg(a, "--fps", &o->fps) || uint_arg(a, "--duration", &o->duration) || next_uint(argc, argv, &i, a, "--camera-count", &o->camera_count) || next_uint(argc, argv, &i, a, "--width", &o->width) || next_uint(argc, argv, &i, a, "--height", &o->height) || next_uint(argc, argv, &i, a, "--fps", &o->fps) || next_uint(argc, argv, &i, a, "--duration", &o->duration)) {}
        else if (str_arg(a, "--scene", &o->scene) || str_arg(a, "--isp-profile", &o->profile) || str_arg(a, "--output", &o->output) || str_arg(a, "--report", &o->report) || next_str(argc, argv, &i, a, "--scene", &o->scene) || next_str(argc, argv, &i, a, "--isp-profile", &o->profile) || next_str(argc, argv, &i, a, "--output", &o->output) || next_str(argc, argv, &i, a, "--report", &o->report)) {}
        else if (a == "--sensor" || a.rfind("--sensor=", 0) == 0) { std::string sensor; if (!(str_arg(a, "--sensor", &sensor) || next_str(argc, argv, &i, a, "--sensor", &sensor))) return false; if (sensor != "virtual") { std::cerr << "only --sensor virtual is supported in host demo\n"; return false; } }
        else if (a == "--format" || a.rfind("--format=", 0) == 0) { std::string format; if (!(str_arg(a, "--format", &format) || next_str(argc, argv, &i, a, "--format", &format))) return false; if (format != "raw10") { std::cerr << "only --format raw10 is supported in host demo\n"; return false; } }
        else if (a == "--help" || a == "-h") { o->show_help = true; return true; }
        else { std::cerr << "unknown argument: " << a << "\n"; return false; }
    }
    return o->camera_count >= 1 && o->camera_count <= 2 && o->width > 0 && o->height > 0 && o->fps > 0 && !o->report.empty();
}

camera::VirtualScene scene_from(const std::string &s) {
    if (s == "gray_chart") return camera::VirtualScene::GrayChart; if (s == "checkerboard") return camera::VirtualScene::Checkerboard; if (s == "slanted_edge") return camera::VirtualScene::SlantedEdge; if (s == "dark" || s == "dark-to-bright") return camera::VirtualScene::Dark; if (s == "bright" || s == "bright-to-dark") return camera::VirtualScene::Bright; if (s == "hdr") return camera::VirtualScene::HDR; if (s == "motion") return camera::VirtualScene::Motion; return camera::VirtualScene::ColorChecker;
}
isp::BayerPattern map_bayer(camera::BayerPattern b) { switch (b) { case camera::BayerPattern::RGGB: return isp::BayerPattern::RGGB; case camera::BayerPattern::GRBG: return isp::BayerPattern::GRBG; case camera::BayerPattern::GBRG: return isp::BayerPattern::GBRG; case camera::BayerPattern::BGGR: return isp::BayerPattern::BGGR; } return isp::BayerPattern::RGGB; }
camera::BayerPattern map_bayer(isp::BayerPattern b) { switch (b) { case isp::BayerPattern::RGGB: return camera::BayerPattern::RGGB; case isp::BayerPattern::GRBG: return camera::BayerPattern::GRBG; case isp::BayerPattern::GBRG: return camera::BayerPattern::GBRG; case isp::BayerPattern::BGGR: return camera::BayerPattern::BGGR; } return camera::BayerPattern::RGGB; }
void write_ppm(const std::filesystem::path &path, const isp::RgbImage &rgb) { std::ofstream out(path, std::ios::binary); out << "P6\n" << rgb.width << " " << rgb.height << "\n255\n"; for (uint32_t y = 0; y < rgb.height; ++y) out.write(reinterpret_cast<const char *>(rgb.data.data() + static_cast<size_t>(y) * rgb.stride), static_cast<std::streamsize>(rgb.width * 3U)); }
void write_json(const std::filesystem::path &path, const std::string &body) { std::ofstream out(path); out << body; }

} // namespace

int main(int argc, char **argv) {
    Options opt; if (!parse(argc, argv, &opt)) return 2;
    if (opt.show_help) { std::cout << "camera_isp_demo --camera-count=N --width=N --height=N --fps=N --duration=N --scene=dark|bright|motion|color_checker|daylight|tungsten|fluorescent|shadow|dark-to-bright|bright-to-dark --isp-profile=PATH --enable-ae --enable-awb --enable-ai --enable-composite --output=PATH --report=DIR\n"; return 0; }
    std::filesystem::create_directories(opt.report); if (!opt.output.empty()) { const auto parent = std::filesystem::path(opt.output).parent_path(); if (!parent.empty()) std::filesystem::create_directories(parent); }
    std::vector<std::unique_ptr<camera::VirtualSensor>> sensors; std::vector<std::unique_ptr<isp::SoftwareIsp>> pipelines; std::vector<std::unique_ptr<isp::AeController>> aes; std::vector<std::unique_ptr<ai::IInferenceBackend>> backends;
    isp::IspProfile loaded_profile; bool have_profile = false; std::string profile_error;
    if (!opt.profile.empty()) { if (!isp::IspProfileStore::load(opt.profile, &loaded_profile, &profile_error)) { std::cerr << profile_error << '\n'; return 1; } if (loaded_profile.config.width != opt.width || loaded_profile.config.height != opt.height) { std::cerr << "profile dimensions must match --width/--height\n"; return 1; } have_profile = true; }
    for (uint32_t i = 0; i < opt.camera_count; ++i) {
        camera::VirtualSensorConfig sc; sc.width = opt.width; sc.height = opt.height; sc.fps = opt.fps; sc.scene = scene_from(opt.scene); sc.bayer = have_profile ? map_bayer(loaded_profile.config.bayer) : camera::BayerPattern::RGGB; sc.seed += i * 17U; sc.realtime_pacing = false; if (sc.scene == camera::VirtualScene::Dark) sc.scene_brightness = 0.12F; if (sc.scene == camera::VirtualScene::Bright) sc.scene_brightness = 0.92F;
        if (opt.scene == "tungsten") { sc.illuminant_r = 1.45F; sc.illuminant_g = 1.0F; sc.illuminant_b = 0.55F; }
        else if (opt.scene == "fluorescent") { sc.illuminant_r = 0.88F; sc.illuminant_g = 1.1F; sc.illuminant_b = 1.25F; }
        else if (opt.scene == "shadow") { sc.illuminant_r = 0.8F; sc.illuminant_g = 0.95F; sc.illuminant_b = 1.35F; }
        auto sensor = std::make_unique<camera::VirtualSensor>(sc); std::string error; if (!sensor->start(&error)) { std::cerr << error << '\n'; return 1; }
        isp::IspConfig ic; ic.width = opt.width; ic.height = opt.height; ic.bayer = map_bayer(sc.bayer); ic.enable_awb = opt.enable_awb; ic.enable_denoise = true; ic.denoise_mode = isp::DenoiseMode::Gaussian; ic.denoise_strength = 0.25F; ic.enable_temporal_denoise = opt.camera_count > 1; ic.temporal_strength = 0.25F; ic.enable_sharpen = true; ic.sharpen_strength = 0.15F;
        auto pipeline = std::make_unique<isp::SoftwareIsp>(ic); if (have_profile) pipeline->set_config(loaded_profile.config);
        auto ae = std::make_unique<isp::AeController>(have_profile ? loaded_profile.ae : isp::AeConfig{}); ae->reset({10000.0F, 1.0F, 1.0F}); auto backend = ai::create_backend(ai::BackendKind::Demo); ai::InferenceConfig aic; if (!backend->initialize(aic) && opt.enable_ai) { std::cerr << "demo backend failed\n"; return 1; }
        sensors.push_back(std::move(sensor)); pipelines.push_back(std::move(pipeline)); aes.push_back(std::move(ae)); backends.push_back(std::move(backend));
    }
    FramePairer pairer({5'000'000, 50'000'000, 8, FramePairPolicy::DropOlder}); uint64_t total = static_cast<uint64_t>(std::max(1U, opt.duration)) * opt.fps; uint64_t pairs = 0; double sum_luma = 0.0; double sum_delta_ms = 0.0; uint64_t ai_count = 0; std::vector<isp::RgbImage> last_rgb(opt.camera_count); std::vector<isp::Nv12Image> last_nv12(opt.camera_count); std::string error;
    const auto begin = std::chrono::steady_clock::now();
    for (uint64_t n = 0; n < total; ++n) {
        if (n == total / 2U && (opt.scene == "dark-to-bright" || opt.scene == "bright-to-dark")) {
            const auto transition = opt.scene == "dark-to-bright" ? camera::VirtualScene::Bright : camera::VirtualScene::Dark;
            for (auto &sensor : sensors) sensor->set_scene(transition);
        }
        std::vector<camera::Raw10Frame> frames(opt.camera_count); std::vector<isp::IspStatistics> stats(opt.camera_count);
        for (uint32_t i = 0; i < opt.camera_count; ++i) {
            if (!sensors[i]->capture(&frames[i], &error)) { std::cerr << error << '\n'; return 1; }
            if (!pipelines[i]->process_raw10(frames[i].data.data(), frames[i].data.size(), static_cast<uint32_t>(frames[i].stride), &last_rgb[i], &last_nv12[i], &stats[i], &error)) { std::cerr << error << '\n'; return 1; }
            sum_luma += stats[i].mean_luma;
            if (opt.enable_ae) { isp::AeStatistics ae_stats; ae_stats.mean_luma = static_cast<float>(stats[i].mean_luma); ae_stats.median_luma = static_cast<float>(stats[i].median_luma); ae_stats.highlight_ratio = static_cast<float>(stats[i].highlight_ratio); const auto r = aes[i]->update(ae_stats); sensors[i]->set_exposure_us(r.control.exposure_us); sensors[i]->set_analog_gain(r.control.analog_gain); sensors[i]->set_digital_gain(r.control.digital_gain); }
            if (opt.enable_ai) { const auto r = backends[i]->infer(last_rgb[i], frames[i].sequence); if (r.ok) ++ai_count; }
            if (opt.camera_count == 2) { FrameSample sample; sample.camera_id = i; sample.sequence = frames[i].sequence; sample.timestamp_ns = frames[i].timestamp_ns; sample.normalized_timestamp_ns = frames[i].timestamp_ns; sample.clock_domain = FrameClockDomain::Monotonic; auto ready = pairer.push(std::move(sample)); for (const auto &p : ready) { ++pairs; sum_delta_ms += p.delta_ns / 1'000'000.0; } }
        }
        if (!opt.output.empty() && n == 0) {
            write_ppm(opt.output + ".cam0.ppm", last_rgb[0]);
            if (opt.camera_count == 2) {
                write_ppm(opt.output + ".cam1.ppm", last_rgb[1]);
                if (opt.enable_composite) {
                    CompositeLayout layout; std::vector<uint8_t> composite; std::string compose_error;
                    std::vector<uint8_t> packed0 = last_nv12[0].y; packed0.insert(packed0.end(), last_nv12[0].uv.begin(), last_nv12[0].uv.end());
                    std::vector<uint8_t> packed1 = last_nv12[1].y; packed1.insert(packed1.end(), last_nv12[1].uv.begin(), last_nv12[1].uv.end());
                    if (CompositeProcessor::make_side_by_side(opt.width, opt.height, opt.width * 2U, opt.height, &layout, &compose_error) &&
                        CompositeProcessor::compose_nv12(packed0.data(), packed1.data(), opt.width, opt.height, layout, &composite, &compose_error)) {
                        std::ofstream out(opt.output + ".composite.nv12", std::ios::binary); out.write(reinterpret_cast<const char *>(composite.data()), static_cast<std::streamsize>(composite.size()));
                    } else { std::cerr << "composite failed: " << compose_error << '\n'; }
                }
            }
        }
    }
    if (opt.camera_count == 2) { for (auto &p : pairer.tick(UINT64_MAX / 2U)) { if (!p.is_stale()) { ++pairs; sum_delta_ms += p.delta_ns / 1'000'000.0; } } }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count(); const double frame_count = static_cast<double>(total * opt.camera_count); const auto pair_stats = pairer.stats();
    std::string json = "{\n  \"simulated\": true,\n  \"camera_count\": " + std::to_string(opt.camera_count) + ",\n  \"frames_per_camera\": " + std::to_string(total) + ",\n  \"mean_luma\": " + std::to_string(sum_luma / std::max(1.0, frame_count)) + ",\n  \"pairs\": " + std::to_string(pairs) + ",\n  \"pair_mean_delta_ms\": " + std::to_string(pairs ? sum_delta_ms / pairs : 0.0) + ",\n  \"pair_p50_ms\": " + std::to_string(pair_stats.p50_ms()) + ",\n  \"pair_p95_ms\": " + std::to_string(pair_stats.p95_ms()) + ",\n  \"pair_p99_ms\": " + std::to_string(pair_stats.p99_ms()) + ",\n  \"pair_max_delta_ms\": " + std::to_string(pair_stats.max_delta_ns / 1'000'000.0) + ",\n  \"demo_ai_frames\": " + std::to_string(ai_count) + ",\n  \"wall_seconds\": " + std::to_string(elapsed) + ",\n  \"measured_fps\": " + std::to_string(total / std::max(0.001, elapsed)) + ",\n  \"hardware_status\": \"HARDWARE_BACKEND_NOT_AVAILABLE\"\n}\n";
    const auto report_path = std::filesystem::path(opt.report) / (opt.enable_ae ? "ae/convergence_report.json" : "overall/quality_report.json");
    std::filesystem::create_directories(report_path.parent_path()); write_json(report_path, json); std::cout << json; return 0;
}
