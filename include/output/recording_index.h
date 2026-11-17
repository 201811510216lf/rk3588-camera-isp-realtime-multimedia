#pragma once

#include "encoded_packet_fanout.h"

#include <optional>
#include <string>

namespace output {

// Overflow-checked rational conversion; negative timestamps are invalid here.
std::optional<uint64_t> packet_time_ns(int64_t ticks, uint32_t num, uint32_t den);
std::string segment_name(uint64_t wallclock_ns, uint64_t segment_number, int camera_id);

// A mapping is written only for an accepted encoded packet. Finalize records
// separately state whether the corresponding MP4 was actually finalized.
std::string packet_index_json(const std::string &segment_id, const EncodedPacket &packet,
                              uint64_t segment_first_pts_ns);
std::string finalized_index_json(const std::string &segment_id, bool finalized);

} // namespace output
