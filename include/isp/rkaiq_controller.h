#pragma once

#include "aiq_controller.h"
#include <map>

namespace isp {
// Keeps the existing AiqController as the one controller implementation.
class RkAiqManager {
  public:
    ~RkAiqManager();
    IspControlResult add_camera(AiqCameraConfig config, std::unique_ptr<IRkAiqBackend> backend,
                                OwnershipEvidence ownership);
    IspControlResult initialize_all();
    IspControlResult start_phase(AiqStartOrder phase, bool stream_active = false);
    IspControlResult start_all();
    IspControlResult shutdown_all();
    IspControlResult shutdown_phase(AiqStopOrder phase);
    AiqController *controller(IspCameraId id);
    const AiqController *controller(IspCameraId id) const;
    std::vector<IspCameraId> camera_ids() const;

  private:
    struct Entry {
        AiqCameraConfig config;
        std::unique_ptr<AiqController> controller;
    };
    std::map<IspCameraId, Entry> cameras_;
    bool initialized_ = false;
};
} // namespace isp
