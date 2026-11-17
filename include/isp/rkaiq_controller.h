#pragma once

#include "aiq_controller.h"
#include <map>

namespace isp {
struct CameraSnapshot {
    IspStatus status;
    IspControlResult result;
};
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
    // Queries every registered camera independently; one failure does not hide the other snapshot.
    std::vector<CameraSnapshot> query_all() const;

  private:
    struct Entry {
        AiqCameraConfig config;
        std::unique_ptr<AiqController> controller;
    };
    std::map<IspCameraId, Entry> cameras_;
    bool initialized_ = false, stopping_ = false;
    bool before_started_ = false, after_started_ = false;
};
} // namespace isp
