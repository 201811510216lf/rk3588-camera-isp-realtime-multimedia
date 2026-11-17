#include "isp/rkaiq_controller.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
using namespace isp;
struct Trace {
    std::vector<std::string> calls;
    std::string fail;
    bool initialized = false, prepared = false, running = false, mismatch = false, unsupported = false;
    bool bad_iq = false, info_throws = false, reported_ready = false;
    uint32_t status_queries = 0;
    ControlSnapshot values;
};
// Only compiled into this test executable. Never exposed as a production backend/input mode.
class Fake final : public IRkAiqBackend {
  public:
    explicit Fake(std::shared_ptr<Trace> t) : t_(std::move(t)) {}
    BackendInfo info() const override {
        if (t_->info_throws)
            throw std::runtime_error("injected info failure");
        BackendInfo i;
        i.name = "test-only-fake";
        i.evidence = EvidenceSource::TestBackend;
        return i;
    }
    IspControlResult initialize(const AiqCameraConfig &c) override {
        id_ = c.camera_id;
        auto r = step("init");
        if (r.ok())
            t_->initialized = true;
        return r;
    }
    IspControlResult prepare() override {
        auto r = step("prepare");
        if (r.ok())
            t_->prepared = true;
        return r;
    }
    IspControlResult start() override {
        auto r = step("start");
        if (r.ok())
            t_->running = true;
        return r;
    }
    IspControlResult stop() override {
        auto r = step("stop");
        if (r.ok())
            t_->running = false;
        return r;
    }
    IspControlResult deinitialize() override {
        assert(!t_->running);
        auto r = step("deinit");
        if (r.ok())
            t_->initialized = t_->prepared = false;
        return r;
    }
    IspControlResult query_capabilities(IspControlCapabilities *c) const override {
        *c = {};
        c->ae_mode = c->awb_mode = c->exposure_time = c->analog_gain = !t_->unsupported;
        c->readback = true;
        c->exposure_us = {1, 100000};
        c->gain = {1, 16};
        return success("caps");
    }
    IspControlResult query_status(IspStatus *s) const override {
        ++t_->status_queries;
        *s = {};
        s->camera_id = id_;
        s->state = t_->reported_ready
                       ? IspRuntimeState::Ready
                       : (t_->running ? IspRuntimeState::Running : IspRuntimeState::Discovered);
        s->backend = info();
        s->hardware_confirmed = true;
        s->iq.loaded = Confirmation::Confirmed;
        return success("status");
    }
    IspControlResult read_controls(ControlSnapshot *c) const override {
        *c = t_->values;
        if (t_->fail == "readback") {
            auto r = success("readback");
            r.status = ControlStatus::BackendError;
            r.message = "injected readback failure";
            return r;
        }
        if (t_->mismatch) {
            c->exposure_us = 123;
            c->analog_gain = 2;
            c->ae_mode = AeMode::Auto;
            c->awb_mode = AwbMode::Auto;
        }
        c->hardware_confirmed = true;
        return success("readback");
    }
    IspControlResult validate_iq(const AiqCameraConfig &, IqValidation *v) const override {
        v->sensor_match = t_->bad_iq ? Confirmation::Mismatch : Confirmation::Confirmed;
        v->sdk_compatible = Confirmation::Confirmed;
        return success("iq");
    }
    IspControlResult set_ae_mode(AeMode m) override {
        t_->values.ae_mode = m;
        return step("ae");
    }
    IspControlResult set_exposure_us(double v) override {
        t_->values.exposure_us = v;
        return step("exposure");
    }
    IspControlResult set_gain(double v) override {
        t_->values.analog_gain = v;
        return step("gain");
    }
    IspControlResult set_awb_mode(AwbMode m) override {
        t_->values.awb_mode = m;
        return step("awb");
    }

  private:
    IspControlResult success(const char *op) const {
        IspControlResult r;
        r.status = ControlStatus::Ok;
        r.operation = op;
        r.camera_id = id_;
        r.hardware_confirmed = true;
        return r;
    }
    IspControlResult step(const char *op) {
        t_->calls.push_back(op);
        auto r = success(op);
        if (t_->fail == op) {
            r.status = ControlStatus::BackendError;
            r.message = "injected failure";
        }
        return r;
    }
    std::shared_ptr<Trace> t_;
    IspCameraId id_ = 0;
};
OwnershipEvidence no_owner() {
    OwnershipEvidence o;
    o.scan_complete = true;
    o.source = EvidenceSource::TestBackend;
    return o;
}
AiqCameraConfig configuration(const std::filesystem::path &root, uint32_t id = 0) {
    AiqCameraConfig c;
    c.camera_id = id;
    c.media_device = "/dev/media-test";
    c.sensor_entity = "fixture-sensor" + std::to_string(id);
    c.video_node = "/dev/video-test" + std::to_string(id);
    c.isp_instance = "fixture-isp" + std::to_string(id);
    c.width = 640;
    c.height = 480;
    c.fps = 30;
    c.ownership = IspAiqOwnershipMode::InProcess;
    c.start_order = AiqStartOrder::BeforeStream;
    c.stop_order = AiqStopOrder::AfterStreamOff;
    c.iq_root = root.string();
    c.iq_file = "fixture.json";
    return c;
}
void normal(const AiqCameraConfig &config) {
    auto t = std::make_shared<Trace>();
    {
        AiqController c(config, std::make_unique<Fake>(t), no_owner());
        assert(c.initialize().ok());
        assert(!c.initialize().ok());
        assert(c.prepare().ok());
        assert(c.start().ok());
        assert(c.deinitialize().status == ControlStatus::InvalidState);
        auto r = c.set_ae_mode(AeMode::Manual);
        assert(r.ok() && r.request_accepted && !r.hardware_confirmed);
        assert(c.set_exposure_time_us(1000).ok());
        assert(c.set_analog_gain(3).ok());
        assert(c.set_awb_mode(AwbMode::Manual).ok());
        assert(c.set_analog_gain(std::numeric_limits<double>::infinity()).status ==
               ControlStatus::InvalidArgument);
        assert(c.set_exposure_time_us(100001).status == ControlStatus::InvalidArgument);
        t->mismatch = true;
        assert(c.set_exposure_time_us(1000).status == ControlStatus::ReadbackMismatch);
        assert(c.set_analog_gain(4).status == ControlStatus::ReadbackMismatch);
        assert(c.set_ae_mode(AeMode::Manual).status == ControlStatus::ReadbackMismatch);
        assert(c.set_awb_mode(AwbMode::Manual).status == ControlStatus::ReadbackMismatch);
        t->mismatch = false;
        t->fail = "readback";
        auto accepted = c.set_exposure_time_us(1000);
        assert(!accepted.ok() && accepted.request_accepted && !accepted.hardware_confirmed);
        t->fail.clear();
        t->unsupported = true;
        assert(c.set_analog_gain(4).status == ControlStatus::Unsupported);
        IspStatus s;
        assert(c.query(&s).ok());
        assert(!s.hardware_confirmed && s.state == IspRuntimeState::Discovered &&
               s.iq.loaded == Confirmation::NotMeasured);
    }
    assert(t->calls[t->calls.size() - 2] == "stop" && t->calls.back() == "deinit");
}
void failures(const AiqCameraConfig &config) {
    for (const auto &failure : {"init", "prepare", "start"}) {
        auto t = std::make_shared<Trace>();
        t->fail = failure;
        {
            AiqController c(config, std::make_unique<Fake>(t), no_owner());
            auto r = c.initialize();
            if (r.ok())
                r = c.prepare();
            if (r.ok())
                r = c.start();
            assert(!r.ok());
        }
        assert(t->calls.back() == "deinit" && !t->running && !t->initialized);
        if (std::string(failure) == "start")
            assert(t->calls[t->calls.size() - 2] == "stop");
    }
    auto stop_trace = std::make_shared<Trace>();
    {
        AiqController c(config, std::make_unique<Fake>(stop_trace), no_owner());
        assert(c.initialize().ok() && c.prepare().ok() && c.start().ok());
        stop_trace->fail = "stop";
        assert(!c.shutdown().ok());
        assert(stop_trace->calls.back() == "stop");
        assert(c.deinitialize().status == ControlStatus::InvalidState);
        stop_trace->fail.clear();
        assert(c.shutdown().ok());
    }
    assert(stop_trace->calls.back() == "deinit");
    auto info_trace = std::make_shared<Trace>();
    info_trace->info_throws = true;
    {
        AiqController broken(config, std::make_unique<Fake>(info_trace), no_owner());
        IspStatus status;
        assert(broken.query(&status).status == ControlStatus::BackendError);
        assert(!status.hardware_confirmed);
    }

    auto t = std::make_shared<Trace>();
    OwnershipEvidence owner = no_owner();
    owner.owners = {"possible-external-daemon"};
    AiqController conflict(config, std::make_unique<Fake>(t), owner);
    assert(conflict.initialize().status == ControlStatus::OwnershipConflict && t->calls.empty());
    owner = no_owner();
    owner.scan_complete = false;
    AiqController unknown(config, std::make_unique<Fake>(t), owner);
    assert(unknown.initialize().status == ControlStatus::OwnershipConflict);
    auto missing = config;
    missing.iq_file = "missing.json";
    AiqController absent(missing, std::make_unique<Fake>(t), no_owner());
    assert(!absent.initialize().ok());
    t->bad_iq = true;
    AiqController mismatch(config, std::make_unique<Fake>(t), no_owner());
    assert(mismatch.initialize().status == ControlStatus::ConfigMismatch);
    auto external = config;
    external.ownership = IspAiqOwnershipMode::ExternalService;
    AiqController service(external, std::make_unique<Fake>(t), no_owner());
    assert(service.initialize().status == ControlStatus::Unsupported);
    assert(service.shutdown().ok());
    assert(t->calls.empty());
}
void manager(const AiqCameraConfig &config) {
    auto a = std::make_shared<Trace>(), b = std::make_shared<Trace>();
    b->fail = "prepare";
    RkAiqManager m;
    assert(m.add_camera(config, std::make_unique<Fake>(a), no_owner()).ok());
    auto other = config;
    other.camera_id = 1;
    other.sensor_entity = "fixture-sensor1";
    other.isp_instance = "fixture-isp1";
    assert(m.add_camera(other, std::make_unique<Fake>(b), no_owner()).ok());
    assert(!m.initialize_all().ok());
    assert(!a->initialized && !b->initialized);
    assert(a->calls.back() == "deinit" && b->calls.back() == "deinit");
    b->fail.clear();
    assert(m.initialize_all().ok());
    assert(m.start_phase(AiqStartOrder::BeforeStream).ok());
    assert(m.shutdown_phase(AiqStopOrder::BeforeStreamOff).ok());
    assert(a->running && b->running);
    assert(m.shutdown_phase(AiqStopOrder::AfterStreamOff).ok());
    assert(!a->running && !b->running);
    assert(m.shutdown_all().ok());
    auto deferred_trace = std::make_shared<Trace>();
    deferred_trace->fail = "start";
    auto deferred_config = config;
    deferred_config.start_order = AiqStartOrder::AfterStream;
    deferred_config.stop_order = AiqStopOrder::AfterStreamOff;
    RkAiqManager deferred;
    assert(deferred.add_camera(deferred_config, std::make_unique<Fake>(deferred_trace), no_owner()).ok());
    assert(deferred.initialize_all().ok());
    assert(!deferred.start_phase(AiqStartOrder::AfterStream, true).ok());
    assert(deferred_trace->calls.back() == "start" && deferred_trace->initialized);
    assert(deferred.shutdown_phase(AiqStopOrder::BeforeStreamOff).ok());
    assert(deferred_trace->calls.back() == "start");
    assert(deferred.shutdown_phase(AiqStopOrder::AfterStreamOff).ok());
    assert(deferred_trace->calls[deferred_trace->calls.size() - 2] == "stop" &&
           deferred_trace->calls.back() == "deinit");
    RkAiqManager same;
    assert(same.add_camera(config, std::make_unique<Fake>(a), no_owner()).ok());
    other.isp_instance = config.isp_instance;
    assert(same.add_camera(other, std::make_unique<Fake>(b), no_owner()).status ==
           ControlStatus::OwnershipConflict);
}
} // namespace
int main() {
    const auto root = std::filesystem::current_path() / "rkaiq-lifecycle-fixture";
    std::filesystem::create_directories(root);
    {
        std::ofstream out(root / "fixture.json");
        out << "test fixture, not a real IQ file";
    }
    const auto c = configuration(root);
    normal(c);
    failures(c);
    manager(c);
    IqValidation iq;
    assert(validate_iq_file(c, &iq).ok() && iq.loaded == Confirmation::NotMeasured);
    auto escape = c;
    escape.iq_file = "../CMakeCache.txt";
    assert(!validate_iq_file(escape, &iq).ok());
    auto unavailable = create_unavailable_backend();
    assert(!unavailable->initialize(c).ok());
    auto disabled_config = c;
    disabled_config.camera_id = 1;
    disabled_config.ownership = IspAiqOwnershipMode::Unavailable;
    auto disabled_trace = std::make_shared<Trace>();
    disabled_trace->reported_ready = true;
    disabled_trace->info_throws = true;
    {
        AiqController disabled(disabled_config, std::make_unique<Fake>(disabled_trace), no_owner());
        IspStatus status;
        auto result = disabled.query(&status);
        assert(result.status == ControlStatus::BackendUnavailable);
        assert(status.camera_id == 1 && status.state == IspRuntimeState::Unavailable);
        assert(!status.hardware_confirmed && !status.capabilities.ae_mode && !status.capabilities.readback);
        assert(disabled_trace->status_queries == 0);
    }

    AiqControllerConfig legacy;
    AiqController bridge(legacy);
    assert(bridge.set_gain(std::numeric_limits<double>::infinity(), nullptr) == AiqResult::InvalidArgument);
    std::filesystem::remove(root / "fixture.json");
    std::filesystem::remove(root);
    return 0;
}
