#include "aiq_controller.h"
#include "isp/sensor_registry.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
std::string take(int argc, char **argv, int *index, const std::string &arg) {
    const auto equal = arg.find('=');
    std::string value;
    if (equal != std::string::npos)
        value = arg.substr(equal + 1);
    else if (*index + 1 < argc)
        value = argv[++*index];
    if (value.empty() || value.rfind("--", 0) == 0)
        throw std::invalid_argument("missing value for " + arg);
    return value;
}
uint32_t camera_number(const std::string &text) {
    if (text != "0" && text != "1")
        throw std::invalid_argument("camera-id must be 0 or 1 (logical ID)");
    return static_cast<uint32_t>(text[0] - '0');
}
double positive(const std::string &text) {
    size_t consumed = 0;
    const double number = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(number) || number <= 0)
        throw std::invalid_argument("control value must be a positive finite number");
    return number;
}
std::string quoted(const std::string &text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c >= 32)
            out += static_cast<char>(c);
        else {
            const char *hex = "0123456789abcdef";
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out + '"';
}
void help(const char *name) {
    std::cout
        << "Usage: " << name << " [--isp-config PATH] [--camera-id 0|1] [--json] [operation]\n"
        << "  --isp-status --list-cameras --list-sensors --list-controls\n"
        << "  --iq-root PATH --iq-file PATH --dump-isp-state PATH\n"
        << "  --ae-auto | --ae-manual | --exposure-us VALUE | --gain VALUE | --awb-auto | --awb-manual\n"
        << "One control per invocation. Discovery is not readiness; no implicit AIQ initialization.\n";
}
int exit_result(const isp::IspControlResult &result, bool json) {
    if (json)
        std::cout << isp::result_json(result) << '\n';
    else
        std::cout << isp::to_string(result.status) << " operation=" << result.operation
                  << " camera_id=" << result.camera_id << " hardware_confirmed=" << result.hardware_confirmed
                  << " " << result.message << '\n';
    if (result.ok())
        return 0;
    return result.status == isp::ControlStatus::Unsupported ||
                   result.status == isp::ControlStatus::BackendUnavailable
               ? 3
               : 1;
}
} // namespace
int main(int argc, char **argv) {
    bool json = false, list_cameras = false, list_sensors = false, list_controls = false;
    uint32_t camera_id = 0;
    std::string config_path, iq_root, iq_file, dump, operation;
    double number = 0;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto key = arg.substr(0, arg.find('='));
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return 0;
            }
            if (arg == "--json") {
                json = true;
                continue;
            }
            if (arg == "--isp-status")
                continue;
            if (arg == "--list-cameras") {
                list_cameras = true;
                continue;
            }
            if (arg == "--list-sensors") {
                list_sensors = true;
                continue;
            }
            if (arg == "--list-controls") {
                list_controls = true;
                continue;
            }
            if (key == "--camera-id") {
                camera_id = camera_number(take(argc, argv, &i, arg));
                continue;
            }
            if (key == "--isp-config") {
                config_path = take(argc, argv, &i, arg);
                continue;
            }
            if (key == "--iq-root") {
                iq_root = take(argc, argv, &i, arg);
                continue;
            }
            if (key == "--iq-file") {
                iq_file = take(argc, argv, &i, arg);
                continue;
            }
            if (key == "--dump-isp-state") {
                dump = take(argc, argv, &i, arg);
                continue;
            }
            if (arg == "--ae-auto" || arg == "--ae-manual" || arg == "--awb-auto" || arg == "--awb-manual" ||
                key == "--exposure-us" || key == "--gain") {
                if (!operation.empty())
                    throw std::invalid_argument("only one control operation is allowed per invocation");
                operation = key;
                if (key == "--exposure-us" || key == "--gain")
                    number = positive(take(argc, argv, &i, arg));
                continue;
            }
            throw std::invalid_argument("unknown option: " + arg);
        }
        if ((!operation.empty() || !dump.empty()) && (list_cameras || list_sensors || list_controls))
            throw std::invalid_argument("listing and control/dump operations cannot be combined");
        if (!operation.empty() && !dump.empty())
            throw std::invalid_argument("control and dump must use separate invocations");
        isp::IspConfig config;
        if (!config_path.empty()) {
            const auto rc = isp::load_isp_config(config_path, &config);
            if (!rc.ok())
                return exit_result(rc, json);
        }
        if (list_cameras || list_sensors) {
            if (json)
                std::cout << (list_sensors ? "{\"sensors\":[" : "{\"cameras\":[");
            bool first = true;
            isp::ReadOnlyMediaGraphInspector inspector;
            for (const auto &camera : config.cameras) {
                const auto evidence = isp::inspect_isp_camera(camera, inspector);
                if (json) {
                    if (!first)
                        std::cout << ',';
                    std::cout << "{\"camera_id\":" << camera.camera_id
                              << ",\"sensor_entity\":" << quoted(camera.sensor_entity)
                              << ",\"video_node\":" << quoted(camera.video_node)
                              << ",\"mapping_valid\":" << (evidence.mapping_valid ? "true" : "false")
                              << ",\"live_evidence\":" << (evidence.live_evidence ? "true" : "false") << '}';
                } else
                    std::cout << "camera_id=" << camera.camera_id << " sensor=" << camera.sensor_entity
                              << " node=" << camera.video_node << " mapping_valid=" << evidence.mapping_valid
                              << " live_evidence=" << evidence.live_evidence << '\n';
                first = false;
            }
            if (json)
                std::cout << "]}\n";
            else if (config.cameras.empty())
                std::cout << "BACKEND_UNAVAILABLE: no configured Sensor identities; use --isp-config\n";
            return config.cameras.empty() ? 3 : 0;
        }
        isp::AiqCameraConfig selected;
        selected.camera_id = camera_id;
        bool selected_found = config.cameras.empty();
        for (const auto &camera : config.cameras)
            if (camera.camera_id == camera_id) {
                selected = camera;
                selected_found = true;
            }
        if (!selected_found)
            throw std::invalid_argument("camera-id absent from ISP configuration");
        if (!iq_root.empty())
            selected.iq_root = iq_root;
        if (!iq_file.empty())
            selected.iq_file = iq_file;
        const auto owner = isp::discover_aiq_ownership();
        auto backend = selected.ownership == isp::IspAiqOwnershipMode::ExternalService
                           ? isp::create_external_service_backend(selected, owner)
                           : (selected.ownership == isp::IspAiqOwnershipMode::InProcess
                                  ? isp::create_rockchip_backend()
                                  : isp::create_unavailable_backend());
        AiqController controller(selected, std::move(backend), owner);
        if (!operation.empty()) {
            isp::IspControlResult rc;
            if (operation == "--ae-auto" || operation == "--ae-manual")
                rc = controller.set_ae_mode(operation == "--ae-auto" ? isp::AeMode::Auto
                                                                     : isp::AeMode::Manual);
            else if (operation == "--awb-auto" || operation == "--awb-manual")
                rc = controller.set_awb_mode(operation == "--awb-auto" ? isp::AwbMode::Auto
                                                                       : isp::AwbMode::Manual);
            else if (operation == "--exposure-us")
                rc = controller.set_exposure_time_us(number);
            else
                rc = controller.set_analog_gain(number);
            return exit_result(rc, json);
        }
        isp::IspStatus status;
        const auto queried = controller.query(&status);
        if (!dump.empty()) {
            if (std::filesystem::exists(dump))
                throw std::invalid_argument("refusing to overwrite an existing state dump");
            std::ofstream output(dump, std::ios::out | std::ios::binary);
            output << isp::status_json(status) << '\n';
            output.flush();
            if (!output.good())
                throw std::runtime_error("state dump write failed");
        }
        if (json)
            std::cout << isp::status_json(status) << '\n';
        else {
            std::cout << "camera_id=" << status.camera_id << "\nstate=" << isp::to_string(status.state)
                      << "\nownership=" << isp::to_string(status.ownership)
                      << "\nbackend=" << status.backend.name << "\nsdk_version=" << status.backend.sdk_version
                      << "\niq_match=" << isp::to_string(status.iq.sensor_match)
                      << "\niq_loaded=" << isp::to_string(status.iq.loaded)
                      << "\nhardware_confirmed=" << status.hardware_confirmed << '\n';
            if (list_controls)
                std::cout << "ae_mode=" << status.capabilities.ae_mode
                          << "\nexposure_us=" << status.capabilities.exposure_time
                          << "\ngain=" << status.capabilities.analog_gain
                          << "\nawb_mode=" << status.capabilities.awb_mode
                          << "\nreadback=" << status.capabilities.readback << '\n';
            std::cout << status.message << '\n';
        }
        // Discovery can succeed while reporting UNAVAILABLE; it is never promoted to READY.
        return queried.ok() ? 0 : 3;
    } catch (const std::exception &error) {
        if (json) {
            isp::IspControlResult invalid;
            invalid.status = isp::ControlStatus::InvalidArgument;
            invalid.operation = operation.empty() ? "parse_cli" : operation;
            invalid.camera_id = camera_id;
            invalid.backend_name = "isp-control-cli";
            invalid.message = error.what();
            std::cout << isp::result_json(invalid) << '\n';
        } else
            std::cerr << "INVALID_ARGUMENT: " << error.what() << '\n';
        return 2;
    }
}
