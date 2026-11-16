#include "encoded_packet_fanout.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

int main() {
    EncodedPacketDistributor distributor;
    const auto payload = std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{0, 0, 1, 0x65});
    int rtmp_count = 0;
    int record_count = 0;
    const std::vector<uint8_t> *rtmp_ptr = nullptr;
    const std::vector<uint8_t> *record_ptr = nullptr;
    assert(distributor.add_sink("rtmp", [&](const EncodedPacket &packet) {
        ++rtmp_count;
        rtmp_ptr = packet.bytes.get();
        return true;
    }));
    assert(distributor.add_sink("record", [&](const EncodedPacket &packet) {
        ++record_count;
        record_ptr = packet.bytes.get();
        return true;
    }));
    EncodedPacket packet{payload, 10, 10, true};
    assert(distributor.publish(packet));
    assert(rtmp_count == 1 && record_count == 1 && rtmp_ptr == record_ptr);
    EncodedPacket out_of_order{payload, 9, 9, false};
    assert(!distributor.publish(out_of_order));
    const auto stats = distributor.stats();
    assert(stats.published == 1 && stats.rejected == 1 && stats.sink_errors == 0);

    EncodedPacketDistributor isolated;
    int good = 0;
    assert(isolated.add_sink("bad", [](const EncodedPacket &) { return false; }));
    assert(isolated.add_sink("good", [&](const EncodedPacket &) { ++good; return true; }));
    assert(!isolated.publish(packet));
    assert(good == 1 && isolated.stats().sink_errors == 1);
    return 0;
}

