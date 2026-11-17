#include "isp/rkaiq_backend.h"

namespace isp {
namespace {
class UnavailableBackend final : public IRkAiqBackend {
  public:
    BackendInfo info() const override {
        BackendInfo i;
        i.name = "rockchip-rkaiq-unavailable";
        return i;
    }
    IspControlResult initialize(const AiqCameraConfig &c) override {
        id_ = c.camera_id;
        return no("initialize");
    }
    IspControlResult prepare() override { return no("prepare"); }
    IspControlResult start() override { return no("start"); }
    IspControlResult stop() override { return no("stop"); }
    IspControlResult deinitialize() override { return no("deinitialize"); }
    IspControlResult query_capabilities(IspControlCapabilities *c) const override {
        if (c)
            *c = {};
        return no("query_capabilities");
    }
    IspControlResult query_status(IspStatus *s) const override {
        if (s) {
            *s = {};
            s->camera_id = id_;
            s->backend = info();
            s->state = IspRuntimeState::Unavailable;
            s->message = "BLOCKED_HARDWARE: no verified BSP SDK adapter";
        }
        return no("query_status");
    }
    IspControlResult read_controls(ControlSnapshot *c) const override {
        if (c)
            *c = {};
        return no("read_controls");
    }
    IspControlResult validate_iq(const AiqCameraConfig &, IqValidation *) const override {
        return no("validate_iq");
    }
    IspControlResult set_ae_mode(AeMode) override { return no("set_ae_mode"); }
    IspControlResult set_exposure_us(double) override { return no("set_exposure_us"); }
    IspControlResult set_gain(double) override { return no("set_gain"); }
    IspControlResult set_awb_mode(AwbMode) override { return no("set_awb_mode"); }

  private:
    IspControlResult no(const char *op) const {
        IspControlResult r;
        r.status = ControlStatus::BackendUnavailable;
        r.operation = op;
        r.camera_id = id_;
        r.backend_name = info().name;
        r.message = "BLOCKED_HARDWARE: real BSP headers, libraries and a reviewed SDK adapter are required; "
                    "no RKAIQ API was called";
        return r;
    }
    IspCameraId id_ = 0;
};
} // namespace
std::unique_ptr<IRkAiqBackend> create_rockchip_backend() {
#ifdef RKAIQ_SDK_ADAPTER_ENABLED
    auto backend = create_sdk_backend();
    if (backend)
        return backend;
#endif
    return create_unavailable_backend();
}
std::unique_ptr<IRkAiqBackend> create_unavailable_backend() { return std::make_unique<UnavailableBackend>(); }
} // namespace isp
