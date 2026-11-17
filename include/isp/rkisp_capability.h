#pragma once

#include "isp/isp_config.h"
#include "isp/media_graph_inspector.h"

namespace isp {
// Validates active processed output separately from advertised formats and topology mapping.
IspControlResult validate_isp_output(const AiqCameraConfig &, const IspOutputNode &);
// Does not infer RKISP30 from a generic rkisp name or from configured expectations.
std::string reported_isp_generation(const IspDeviceInfo &);
} // namespace isp
