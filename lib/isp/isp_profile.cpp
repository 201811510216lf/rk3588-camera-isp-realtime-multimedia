#include "isp/isp_profile.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace isp {
namespace {

std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}
std::string strip_comment(std::string s) {
    const auto p = s.find('#');
    if (p != std::string::npos) s.resize(p);
    return trim(s);
}
bool parse_bool(const std::string &v, bool *out) {
    if (v == "true" || v == "1" || v == "on") { *out = true; return true; }
    if (v == "false" || v == "0" || v == "off") { *out = false; return true; }
    return false;
}
bool parse_float(const std::string &v, float *out) { try { size_t n = 0; *out = std::stof(v, &n); return n == v.size(); } catch (...) { return false; } }
bool parse_uint(const std::string &v, uint32_t *out) { try { size_t n = 0; const auto x = std::stoul(v, &n); if (n != v.size() || x > 0xffffffffUL) return false; *out = static_cast<uint32_t>(x); return true; } catch (...) { return false; } }
std::string unquote(std::string v) { if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) return v.substr(1, v.size()-2); return v; }
void set_error(std::string *e, const std::string &s) { if (e) *e = s; }

} // namespace

bool IspProfileStore::load(const std::string &path, IspProfile *profile, std::string *error) {
    if (!profile) { set_error(error, "profile output is null"); return false; }
    std::ifstream in(path);
    if (!in) { set_error(error, "cannot open profile: " + path); return false; }
    IspProfile p;
    std::string line, section;
    uint32_t ccm_index = 0;
    while (std::getline(in, line)) {
        line = strip_comment(line); if (line.empty()) continue;
        const auto colon = line.find(':'); if (colon == std::string::npos) continue;
        std::string key = trim(line.substr(0, colon)); std::string value = unquote(trim(line.substr(colon + 1)));
        if (value.empty()) { section = key; continue; }
        if (key == "sensor_profile") { p.sensor_profile = value; continue; }
        if (key == "version") { if (!parse_uint(value, &p.version)) { set_error(error, "invalid profile version"); return false; } continue; }
        if (section == "input" && key == "width") { if (!parse_uint(value, &p.config.width)) { set_error(error, "invalid profile input width"); return false; } continue; }
        if (section == "input" && key == "height") { if (!parse_uint(value, &p.config.height)) { set_error(error, "invalid profile input height"); return false; } continue; }
        if (section == "input" && key == "bayer") { if (value == "RGGB") p.config.bayer = BayerPattern::RGGB; else if (value == "GRBG") p.config.bayer = BayerPattern::GRBG; else if (value == "GBRG") p.config.bayer = BayerPattern::GBRG; else if (value == "BGGR") p.config.bayer = BayerPattern::BGGR; else { set_error(error, "unsupported bayer pattern"); return false; } continue; }
        bool b = false; float f = 0.0F; uint32_t u = 0;
        if (section == "blc" && key == "enabled" && parse_bool(value, &b)) p.config.enable_blc = b;
        else if (section == "blc" && key == "r" && parse_uint(value, &u)) p.config.black_level_r = static_cast<uint16_t>(std::min(u, 1023U));
        else if (section == "blc" && key == "gr" && parse_uint(value, &u)) p.config.black_level_gr = static_cast<uint16_t>(std::min(u, 1023U));
        else if (section == "blc" && key == "gb" && parse_uint(value, &u)) p.config.black_level_gb = static_cast<uint16_t>(std::min(u, 1023U));
        else if (section == "blc" && key == "b" && parse_uint(value, &u)) p.config.black_level_b = static_cast<uint16_t>(std::min(u, 1023U));
        else if (section == "awb" && key == "enabled" && parse_bool(value, &b)) p.config.enable_awb = b;
        else if (section == "awb" && key == "algorithm") { if (value == "white_patch") p.config.awb_algorithm = AwbAlgorithm::WhitePatch; else if (value == "manual") p.config.awb_algorithm = AwbAlgorithm::Manual; else if (value == "gray_world") p.config.awb_algorithm = AwbAlgorithm::GrayWorld; else { set_error(error, "unsupported AWB algorithm"); return false; } }
        else if (section == "ae" && key == "target_luma" && parse_float(value, &f)) p.ae.target_luma = std::clamp(f, 1.0F, 254.0F);
        else if (section == "denoise" && key == "spatial_strength" && parse_float(value, &f)) { p.config.denoise_strength = std::clamp(f, 0.0F, 1.0F); p.config.enable_denoise = p.config.denoise_strength > 0.0F; }
        else if (section == "denoise" && key == "temporal_strength" && parse_float(value, &f)) { p.config.temporal_strength = std::clamp(f, 0.0F, 1.0F); p.config.enable_temporal_denoise = p.config.temporal_strength > 0.0F; }
        else if (section == "sharpen" && key == "strength" && parse_float(value, &f)) { p.config.sharpen_strength = std::clamp(f, 0.0F, 1.0F); p.config.enable_sharpen = p.config.sharpen_strength > 0.0F; }
        else if (section == "gamma" && key == "value" && parse_float(value, &f)) p.config.gamma = std::clamp(f, 0.1F, 8.0F);
        else if (section == "ccm" && key.rfind("m", 0) == 0 && parse_float(value, &f) && ccm_index < 9) p.config.ccm[ccm_index++] = f;
    }
    if (p.config.width == 0 || p.config.height == 0) { set_error(error, "profile input width/height are required"); return false; }
    *profile = p; if (error) error->clear(); return true;
}

bool IspProfileStore::save(const std::string &path, const IspProfile &p, std::string *error) {
    std::ofstream out(path); if (!out) { set_error(error, "cannot write profile: " + path); return false; }
    const char *bayer = p.config.bayer == BayerPattern::GRBG ? "GRBG" : p.config.bayer == BayerPattern::GBRG ? "GBRG" : p.config.bayer == BayerPattern::BGGR ? "BGGR" : "RGGB";
    const char *awb = p.config.awb_algorithm == AwbAlgorithm::WhitePatch ? "white_patch" : p.config.awb_algorithm == AwbAlgorithm::Manual ? "manual" : "gray_world";
    out << "version: " << p.version << "\nsensor_profile: " << p.sensor_profile << "\ninput:\n  width: " << p.config.width << "\n  height: " << p.config.height << "\n  format: RAW10\n  bayer: " << bayer << "\nblc:\n  enabled: " << (p.config.enable_blc ? "true" : "false") << "\n  r: " << p.config.black_level_r << "\n  gr: " << p.config.black_level_gr << "\n  gb: " << p.config.black_level_gb << "\n  b: " << p.config.black_level_b << "\nawb:\n  enabled: " << (p.config.enable_awb ? "true" : "false") << "\n  algorithm: " << awb << "\nae:\n  target_luma: " << p.ae.target_luma << "\ndenoise:\n  spatial_strength: " << p.config.denoise_strength << "\n  temporal_strength: " << p.config.temporal_strength << "\nsharpen:\n  strength: " << p.config.sharpen_strength << "\ngamma:\n  value: " << p.config.gamma << "\n";
    if (!out) { set_error(error, "profile write failed"); return false; } if (error) error->clear(); return true;
}

bool IspProfileStore::apply(const IspProfile &p, SoftwareIsp *pipeline, std::string *error) {
    if (!pipeline || p.config.width == 0 || p.config.height == 0) { set_error(error, "invalid profile or pipeline"); return false; }
    if (have_active_) { previous_ = active_; have_previous_ = true; }
    active_ = p; have_active_ = true; pipeline->set_config(p.config); if (error) error->clear(); return true;
}

bool IspProfileStore::apply(const IspProfile &p, SoftwareIsp *pipeline, AeController *ae, std::string *error) {
    if (!apply(p, pipeline, error)) return false;
    if (ae) ae->set_config(p.ae);
    return true;
}

bool IspProfileStore::rollback(SoftwareIsp *pipeline, std::string *error) {
    if (!pipeline || !have_previous_) { set_error(error, "no previous ISP profile to rollback"); return false; }
    const IspProfile old = previous_; previous_ = active_; active_ = old; pipeline->set_config(active_.config); if (error) error->clear(); return true;
}

bool IspProfileStore::rollback(SoftwareIsp *pipeline, AeController *ae, std::string *error) {
    if (!have_previous_) { set_error(error, "no previous ISP profile to rollback"); return false; }
    if (!rollback(pipeline, error)) return false;
    if (ae) ae->set_config(active_.ae);
    return true;
}

} // namespace isp
