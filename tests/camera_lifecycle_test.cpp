#include "app_options.h"
#include "bounded_frame_queue.h"
#include "camera_frame.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>

namespace {

void test_lease_requeues_only_after_last_copy() {
    int requeues = 0;
    auto state = std::make_shared<camera::BufferLeaseState>(
        [&](uint32_t index) {
            assert(index == 3);
            ++requeues;
        });

    assert(state->acquire(3));
    {
        auto lease = std::make_shared<camera::FrameLease>(state, 3);
        camera::CameraFrame first;
        first.desc.index = 3;
        first.desc.data = reinterpret_cast<void *>(0x1);
        first.lease = lease;
        camera::CameraFrame second = first;
        lease.reset();
        first.lease.reset();
        assert(requeues == 0);
        assert(state->in_flight() == 1);
        second.lease.reset();
    }
    assert(requeues == 1);
    assert(state->in_flight() == 0);
}

void test_stop_prevents_qbuf_after_streamoff() {
    int requeues = 0;
    auto state = std::make_shared<camera::BufferLeaseState>(
        [&](uint32_t) { ++requeues; });
    assert(state->acquire(1));
    state->stop_accepting();
    auto lease = std::make_shared<camera::FrameLease>(state, 1);
    lease.reset();
    assert(requeues == 0);
    assert(state->wait_for_zero(std::chrono::milliseconds(10)));
}

void test_drop_oldest_is_countable() {
    BoundedFrameQueue<int> queue(2);
    bool dropped = false;
    assert(queue.push_drop_oldest(1, &dropped) && !dropped);
    assert(queue.push_drop_oldest(2, &dropped) && !dropped);
    assert(queue.push_drop_oldest(3, &dropped) && dropped);
    int value = 0;
    assert(queue.pop(value) && value == 2);
    assert(queue.pop(value) && value == 3);
    queue.close();
    assert(!queue.pop(value));
}

void test_cli_validation() {
    const char *args[] = {"myDemo", "--source=mipi", "--device", "/dev/video7",
                          "--width=1280", "--height", "720", "--ai-enable=false",
                          "--ai-fps", "5"};
    AppOptions options;
    bool help = false;
    std::string error;
    assert(parse_app_options(static_cast<int>(sizeof(args) / sizeof(args[0])),
                             const_cast<char **>(args), &options, &help, &error));
    assert(!help);
    assert(options.source == AppOptions::Source::Mipi);
    assert(options.device == "/dev/video7");
    assert(options.width == 1280 && options.height == 720);
    assert(!options.ai_enable);

    const char *bad_args[] = {"myDemo", "--source=bad"};
    assert(!parse_app_options(2, const_cast<char **>(bad_args), &options, &help, &error));
    assert(error.find("legacy or mipi") != std::string::npos);

    const char *raw_mp4_args[] = {"myDemo", "--source=mipi", "--output=x.mp4"};
    assert(!parse_app_options(3, const_cast<char **>(raw_mp4_args), &options, &help, &error));
    assert(error.find("raw H.264") != std::string::npos);
}

} // namespace

int main() {
    test_lease_requeues_only_after_last_copy();
    test_stop_prevents_qbuf_after_streamoff();
    test_drop_oldest_is_countable();
    test_cli_validation();
    std::cout << "camera_lifecycle_test: PASS\n";
    return 0;
}
