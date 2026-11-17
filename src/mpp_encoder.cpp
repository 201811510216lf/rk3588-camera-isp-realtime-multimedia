/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#include "mpp_encoder.h"
#include "im2d.h"
#include <chrono>
#include <errno.h>
#include <limits.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>

namespace {

bool set_encoder_cfg(MppEncCfg cfg, const char *key, int value) {
    return cfg && mpp_enc_cfg_set_s32(cfg, key, value) == MPP_OK;
}

} // namespace

MppEncoder::MppEncoder()
    : mpp_ctx_(NULL), mpp_mpi_(NULL), enc_cfg_(NULL), frame_(NULL), packet_(NULL), frm_grp_(NULL),
      pkt_grp_(NULL), width_(0), height_(0), horizontal_stride_(0), vertical_stride_(0), fps_(0), bitrate_(0),
      mpp_type_(MPP_VIDEO_CodingAVC), initialized_(false), faulted_(false), pending_frame_(NULL),
      pending_buffer_(NULL), yuv_buffer_(NULL), yuv_buffer_size_(0) {}

MppEncoder::~MppEncoder() { Release(); }

int MppEncoder::Init(int width, int height, int fps, int bitrate, int codec_type) {
    MPP_RET ret = MPP_OK;

    if (initialized_ || width <= 0 || height <= 0 || (width & 1) || (height & 1) || width > INT_MAX - 15 ||
        height > INT_MAX - 15 || fps <= 0 || bitrate <= 0 || bitrate > INT_MAX / 2 ||
        static_cast<uint64_t>(width + 15) * static_cast<uint64_t>(height + 15) >
            static_cast<uint64_t>(INT_MAX) / 3U) {
        fprintf(stderr, "Invalid encoder configuration: %dx%d @ %dfps bitrate=%d\n", width, height, fps,
                bitrate);
        return -1;
    }

    width_ = width;
    height_ = height;
    horizontal_stride_ = (width + 15) & ~15;
    vertical_stride_ = (height + 15) & ~15;
    fps_ = fps;
    bitrate_ = bitrate;

    if (codec_type == 264) {
        mpp_type_ = MPP_VIDEO_CodingAVC;
    } else if (codec_type == 265) {
        mpp_type_ = MPP_VIDEO_CodingHEVC;
    } else {
        fprintf(stderr, "Unsupported codec type: %d\n", codec_type);
        return -1;
    }

    // 创建 MPP 上下文
    ret = mpp_create(&mpp_ctx_, &mpp_mpi_);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_create failed ret %d\n", ret);
        return -1;
    }

    // 初始化编码器
    ret = mpp_init(mpp_ctx_, MPP_CTX_ENC, mpp_type_);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_init failed ret %d\n", ret);
        mpp_destroy(mpp_ctx_);
        mpp_ctx_ = NULL;
        return -1;
    }

    // 创建编码配置
    ret = mpp_enc_cfg_init(&enc_cfg_);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_enc_cfg_init failed ret %d\n", ret);
        goto FAIL;
    }

    // 获取默认配置
    ret = mpp_mpi_->control(mpp_ctx_, MPP_ENC_GET_CFG, enc_cfg_);
    if (ret != MPP_OK) {
        fprintf(stderr, "get enc cfg failed ret %d\n", ret);
        goto FAIL;
    }

    // 基本图像参数
    if (!set_encoder_cfg(enc_cfg_, "prep:width", width) ||
        !set_encoder_cfg(enc_cfg_, "prep:height", height) ||
        !set_encoder_cfg(enc_cfg_, "prep:hor_stride", horizontal_stride_) ||
        !set_encoder_cfg(enc_cfg_, "prep:ver_stride", vertical_stride_) ||
        !set_encoder_cfg(enc_cfg_, "prep:format", MPP_FMT_YUV420P)) {
        fprintf(stderr, "failed to configure MPP input format\n");
        goto FAIL;
    }

    // 码率控制：简单的 VBR 设置，偏向速度
    if (!set_encoder_cfg(enc_cfg_, "rc:mode", MPP_ENC_RC_MODE_VBR) ||
        !set_encoder_cfg(enc_cfg_, "rc:bps_target", bitrate) ||
        !set_encoder_cfg(enc_cfg_, "rc:bps_max", bitrate * 2) ||
        !set_encoder_cfg(enc_cfg_, "rc:bps_min", bitrate / 2) ||
        !set_encoder_cfg(enc_cfg_, "rc:fps_in_flex", 0) || !set_encoder_cfg(enc_cfg_, "rc:fps_in_num", fps) ||
        !set_encoder_cfg(enc_cfg_, "rc:fps_in_denom", 1) ||
        !set_encoder_cfg(enc_cfg_, "rc:fps_out_flex", 0) ||
        !set_encoder_cfg(enc_cfg_, "rc:fps_out_num", fps) ||
        !set_encoder_cfg(enc_cfg_, "rc:fps_out_denom", 1) || !set_encoder_cfg(enc_cfg_, "rc:gop", fps)) {
        fprintf(stderr, "failed to configure MPP rate control\n");
        goto FAIL;
    }

    if (mpp_type_ == MPP_VIDEO_CodingAVC) {
        if (!set_encoder_cfg(enc_cfg_, "codec:type", MPP_VIDEO_CodingAVC) ||
            !set_encoder_cfg(enc_cfg_, "h264:profile", 66) || !set_encoder_cfg(enc_cfg_, "h264:level", 40) ||
            !set_encoder_cfg(enc_cfg_, "h264:cabac_en", 0) ||
            !set_encoder_cfg(enc_cfg_, "h264:qp_init", 26) || !set_encoder_cfg(enc_cfg_, "h264:qp_min", 20) ||
            !set_encoder_cfg(enc_cfg_, "h264:qp_max", 35)) {
            fprintf(stderr, "failed to configure H.264 parameters\n");
            goto FAIL;
        }
    } else if (mpp_type_ == MPP_VIDEO_CodingHEVC) {
        if (!set_encoder_cfg(enc_cfg_, "codec:type", MPP_VIDEO_CodingHEVC) ||
            !set_encoder_cfg(enc_cfg_, "h265:profile", 1) || !set_encoder_cfg(enc_cfg_, "h265:level", 120)) {
            fprintf(stderr, "failed to configure H.265 parameters\n");
            goto FAIL;
        }
    }

    // 应用配置
    ret = mpp_mpi_->control(mpp_ctx_, MPP_ENC_SET_CFG, enc_cfg_);
    if (ret != MPP_OK) {
        fprintf(stderr, "set enc cfg failed ret %d\n", ret);
        goto FAIL;
    }

    // 输入/输出缓冲区组
    ret = mpp_buffer_group_get_internal(&frm_grp_, MPP_BUFFER_TYPE_ION);
    if (ret != MPP_OK) {
        fprintf(stderr, "failed to get buffer group for input frame ret %d\n", ret);
        goto FAIL;
    }

    ret = mpp_buffer_group_get_internal(&pkt_grp_, MPP_BUFFER_TYPE_ION);
    if (ret != MPP_OK) {
        fprintf(stderr, "failed to get buffer group for output packet ret %d\n", ret);
        goto FAIL;
    }

    // 预分配 YUV 缓冲区
    yuv_buffer_size_ = width * height * 3 / 2;
    yuv_buffer_ = (uint8_t *)malloc(yuv_buffer_size_);
    if (!yuv_buffer_) {
        fprintf(stderr, "failed to allocate YUV buffer (%d bytes)\n", yuv_buffer_size_);
        goto FAIL;
    }

    initialized_ = true;
    faulted_ = false;
    return 0;

FAIL:
    if (yuv_buffer_) {
        free(yuv_buffer_);
        yuv_buffer_ = NULL;
        yuv_buffer_size_ = 0;
    }
    if (enc_cfg_) {
        mpp_enc_cfg_deinit(enc_cfg_);
        enc_cfg_ = NULL;
    }
    if (frm_grp_) {
        mpp_buffer_group_put(frm_grp_);
        frm_grp_ = NULL;
    }
    if (pkt_grp_) {
        mpp_buffer_group_put(pkt_grp_);
        pkt_grp_ = NULL;
    }
    if (mpp_ctx_) {
        mpp_destroy(mpp_ctx_);
        mpp_ctx_ = NULL;
    }
    initialized_ = false;
    return -1;
}

int MppEncoder::EncodeFrame(uint8_t *bgr_data, int width, int height, uint8_t *packet_data, int *packet_size,
                            int bgr_stride) {
    return EncodeFrameDma((void *)bgr_data, width, height, packet_data, packet_size, bgr_stride);
}

int MppEncoder::EncodeFrameDma(void *bgr_va, int width, int height, uint8_t *packet_data, int *packet_size,
                               int bgr_stride) {
    if (!initialized_) {
        fprintf(stderr, "Encoder not initialized\n");
        return -1;
    }

    if (width != width_ || height != height_) {
        fprintf(stderr, "Frame size mismatch: %dx%d vs %dx%d\n", width, height, width_, height_);
        return -1;
    }

    if (!yuv_buffer_ || yuv_buffer_size_ < width * height * 3 / 2) {
        fprintf(stderr, "YUV buffer not allocated or too small\n");
        return -1;
    }

    int wstride_pix = (bgr_stride > 0) ? (bgr_stride / 3) : width;
    int hstride_pix = height;
    rga_buffer_t src_buf =
        wrapbuffer_virtualaddr_t(bgr_va, width, height, wstride_pix, hstride_pix, RK_FORMAT_BGR_888);
    rga_buffer_t dst_buf =
        wrapbuffer_virtualaddr_t((void *)yuv_buffer_, width, height, width, height, RK_FORMAT_YCbCr_420_P);

    IM_STATUS status =
        imcvtcolor(src_buf, dst_buf, RK_FORMAT_BGR_888, RK_FORMAT_YCbCr_420_P, IM_COLOR_SPACE_DEFAULT);
    if (status != IM_STATUS_SUCCESS) {
        static int fallback_count = 0;
        if (fallback_count++ < 3) {
            fprintf(stderr, "RGA color conversion failed (%d), using OpenCV fallback\n", (int)status);
        }
        int row_stride = (bgr_stride > 0) ? bgr_stride : (width * 3);
        cv::Mat bgr(height, width, CV_8UC3, bgr_va, row_stride);
        if (!bgr.isContinuous()) {
            bgr = bgr.clone();
        }
        cv::Mat yuv(height * 3 / 2, width, CV_8UC1, yuv_buffer_);
        cv::cvtColor(bgr, yuv, cv::COLOR_BGR2YUV_I420);
    }

    int yuv_size = width * height * 3 / 2;
    return Encode(yuv_buffer_, yuv_size, packet_data, packet_size);
}

int MppEncoder::EncodeNV12(const uint8_t *nv12_data, int width, int height, int stride_y, int stride_uv,
                           int data_size, uint8_t *packet_data, int *packet_size) {
    if (!initialized_ || faulted_ || !nv12_data || !packet_data || !packet_size || *packet_size <= 0 ||
        width != width_ || height != height_) {
        fprintf(stderr, "Invalid NV12 frame or encoder is not initialized\n");
        return -1;
    }
    if (stride_y <= 0)
        stride_y = width;
    if (stride_uv <= 0)
        stride_uv = stride_y;
    if (stride_y < width || stride_uv < width || (width & 1) || (height & 1))
        return -1;
    const size_t required = static_cast<size_t>(stride_y) * static_cast<size_t>(height) +
                            static_cast<size_t>(stride_uv) * static_cast<size_t>(height / 2);
    if (data_size <= 0 || static_cast<size_t>(data_size) < required) {
        fprintf(stderr, "NV12 frame is smaller than its negotiated stride layout\n");
        return -1;
    }

    bool converted = false;
    // RGA can handle a single-plane NV12 layout when both planes have the
    // same stride. The output remains the encoder-owned I420 workspace.
    if (stride_y == stride_uv) {
        rga_buffer_t src = wrapbuffer_virtualaddr_t(const_cast<uint8_t *>(nv12_data), width, height, stride_y,
                                                    height, RK_FORMAT_YCbCr_420_SP);
        rga_buffer_t dst = wrapbuffer_virtualaddr_t(static_cast<void *>(yuv_buffer_), width, height, width,
                                                    height, RK_FORMAT_YCbCr_420_P);
        const IM_STATUS status =
            imcvtcolor(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_YCbCr_420_P, IM_COLOR_SPACE_DEFAULT);
        converted = (status == IM_STATUS_SUCCESS);
    }

    if (!converted) {
        // Pack padded V4L2 rows before asking OpenCV for a deterministic I420
        // layout. This is also the fallback for drivers with different Y/UV
        // strides or RGA implementations without this conversion.
        const size_t packed_size = static_cast<size_t>(width) * height * 3 / 2;
        if (yuv_buffer_size_ < static_cast<int>(packed_size)) {
            fprintf(stderr, "I420 workspace is too small for NV12 conversion\n");
            return -1;
        }
        const uint8_t *src_y = nv12_data;
        uint8_t *dst_y = yuv_buffer_;
        for (int row = 0; row < height; ++row) {
            memcpy(dst_y + static_cast<size_t>(row) * width, src_y + static_cast<size_t>(row) * stride_y,
                   width);
        }
        const uint8_t *src_uv = nv12_data + static_cast<size_t>(stride_y) * height;
        uint8_t *dst_u = yuv_buffer_ + static_cast<size_t>(width) * height;
        uint8_t *dst_v = dst_u + static_cast<size_t>(width) * (height / 2) / 2;
        for (int row = 0; row < height / 2; ++row) {
            const uint8_t *src_row = src_uv + static_cast<size_t>(row) * stride_uv;
            for (int col = 0; col < width / 2; ++col) {
                dst_u[static_cast<size_t>(row) * (width / 2) + col] = src_row[col * 2];
                dst_v[static_cast<size_t>(row) * (width / 2) + col] = src_row[col * 2 + 1];
            }
        }
    }
    return Encode(yuv_buffer_, width * height * 3 / 2, packet_data, packet_size);
}

bool MppEncoder::reset_and_release_pending() {
    if (!pending_frame_ && !pending_buffer_) {
        return true;
    }
    // The BSP's timeout/error completion contract is not available in this
    // host tree. Do not guess that reset() has stopped hardware access. Keep
    // the pending buffer until Release() tears down the MPP context, which is
    // the only ownership boundary this wrapper can enforce without a BSP
    // specific completion callback.
    return false;
}

int MppEncoder::Encode(uint8_t *yuv_data, int yuv_size, uint8_t *packet_data, int *packet_size) {
    if (!initialized_ || faulted_) {
        fprintf(stderr, "Encoder not initialized\n");
        return -1;
    }
    if (!yuv_data || yuv_size <= 0 || !packet_size || !packet_data || *packet_size <= 0) {
        fprintf(stderr, "Invalid encoder input or packet buffer\n");
        return -1;
    }

    const size_t expected_yuv_size = static_cast<size_t>(width_) * static_cast<size_t>(height_) * 3U / 2U;
    if (static_cast<size_t>(yuv_size) != expected_yuv_size) {
        fprintf(stderr, "YUV input must match configured packed frame size\n");
        return -1;
    }

    MPP_RET ret = MPP_OK;
    MppFrame frame = NULL;
    MppPacket packet = NULL;
    MppBuffer buffer = NULL;

    ret = mpp_frame_init(&frame);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_frame_init failed\n");
        return -1;
    }

    const size_t padded_y = static_cast<size_t>(horizontal_stride_) * vertical_stride_;
    const size_t buf_size = padded_y * 3U / 2U;
    ret = mpp_buffer_get(frm_grp_, &buffer, static_cast<size_t>(buf_size));
    if (ret != MPP_OK) {
        fprintf(stderr, "failed to get buffer for input frame ret %d\n", ret);
        mpp_frame_deinit(&frame);
        return -1;
    }

    void *buf_ptr = mpp_buffer_get_ptr(buffer);
    if (!buf_ptr) {
        fprintf(stderr, "failed to get buffer pointer\n");
        mpp_buffer_put(buffer);
        mpp_frame_deinit(&frame);
        return -1;
    }
    auto *destination = static_cast<uint8_t *>(buf_ptr);
    memset(destination, 16, padded_y);
    memset(destination + padded_y, 128, padded_y / 2U);
    for (int row = 0; row < height_; ++row)
        memcpy(destination + static_cast<size_t>(row) * horizontal_stride_,
               yuv_data + static_cast<size_t>(row) * width_, width_);
    const size_t source_y = static_cast<size_t>(width_) * height_;
    for (int plane = 0; plane < 2; ++plane) {
        const auto *source_uv = yuv_data + source_y + static_cast<size_t>(plane) * source_y / 4U;
        auto *destination_uv = destination + padded_y + static_cast<size_t>(plane) * padded_y / 4U;
        for (int row = 0; row < height_ / 2; ++row)
            memcpy(destination_uv + static_cast<size_t>(row) * (horizontal_stride_ / 2),
                   source_uv + static_cast<size_t>(row) * (width_ / 2), width_ / 2);
    }

    mpp_frame_set_width(frame, width_);
    mpp_frame_set_height(frame, height_);
    mpp_frame_set_hor_stride(frame, horizontal_stride_);
    mpp_frame_set_ver_stride(frame, vertical_stride_);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420P);
    mpp_frame_set_buffer(frame, buffer);
    mpp_frame_set_eos(frame, 0);

    ret = mpp_mpi_->encode_put_frame(mpp_ctx_, frame);
    if (ret != MPP_OK) {
        fprintf(stderr, "encode_put_frame failed ret %d\n", ret);
        mpp_frame_deinit(&frame);
        mpp_buffer_put(buffer);
        return -1;
    }

    // The official MPP encode_put_frame/get_packet flow releases the frame
    // wrapper immediately after submission. The buffer handle remains owned
    // here until a packet is received or the reset path establishes a
    // completion boundary.
    mpp_frame_deinit(&frame);

    // 轮询获取编码输出，避免帧在编码器内堆积导致后续卡顿
    const int max_retries = 50;
    for (int r = 0; r < max_retries; r++) {
        ret = mpp_mpi_->encode_get_packet(mpp_ctx_, &packet);
        if (ret == MPP_OK && packet) {
            break;
        }
        if (ret != MPP_ERR_TIMEOUT)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (ret != MPP_OK || !packet) {
        // encode_put_frame transferred the frame to MPP. On timeout and on
        // non-timeout errors the API does not prove that the hardware has
        // stopped reading it. Retain the buffer until Release tears down the
        // MPP context, and report an error instead of silently dropping it.
        pending_buffer_ = buffer;
        buffer = NULL;
        faulted_ = true;
        if (!reset_and_release_pending()) {
            fprintf(stderr, "MPP completion is unverified; retaining pending input until Release\n");
        }
        *packet_size = 0;
        return (ret == MPP_ERR_TIMEOUT) ? -2 : -3;
    }

    void *pkt_data = mpp_packet_get_data(packet);
    size_t pkt_size = mpp_packet_get_length(packet);

    if (pkt_data && pkt_size > 0) {
        if (pkt_size <= static_cast<size_t>(INT_MAX) && packet_data &&
            *packet_size >= static_cast<int>(pkt_size)) {
            memcpy(packet_data, pkt_data, pkt_size);
            *packet_size = pkt_size;
        } else {
            *packet_size = 0;
            mpp_packet_deinit(&packet);
            mpp_buffer_put(buffer);
            mpp_frame_deinit(&frame);
            return -1;
        }
    } else {
        *packet_size = 0;
    }

    mpp_packet_deinit(&packet);
    mpp_buffer_put(buffer);
    mpp_frame_deinit(&frame);

    return 0;
}

int MppEncoder::GetHeader(uint8_t *header_data, int *header_size) {
    if (!initialized_ || faulted_ || !header_data || !header_size || *header_size <= 0) {
        fprintf(stderr, "Encoder not initialized\n");
        return -1;
    }

    MPP_RET ret = MPP_OK;
    MppPacket packet = NULL;

    ret = mpp_mpi_->control(mpp_ctx_, MPP_ENC_GET_EXTRA_INFO, &packet);
    if (ret != MPP_OK || !packet) {
        fprintf(stderr, "get extra info failed ret %d\n", ret);
        return -1;
    }

    void *pkt_data = mpp_packet_get_data(packet);
    size_t pkt_size = mpp_packet_get_length(packet);

    if (pkt_data && pkt_size > 0) {
        if (header_data && *header_size >= (int)pkt_size) {
            memcpy(header_data, pkt_data, pkt_size);
            *header_size = pkt_size;
            ret = MPP_OK;
        } else {
            fprintf(stderr, "Header buffer too small: %d < %zu\n", *header_size, pkt_size);
            *header_size = 0;
            ret = MPP_NOK;
        }
    } else {
        *header_size = 0;
        ret = MPP_NOK;
    }

    mpp_packet_deinit(&packet);
    return (ret == MPP_OK) ? 0 : -1;
}

void MppEncoder::Release() {
    if (pending_frame_ || pending_buffer_) {
        if (!reset_and_release_pending()) {
            // Destroying the MPP context is the final completion boundary
            // available to this wrapper when no BSP completion callback is
            // available. The pending buffer is released only afterwards.
            // Do not return the buffer to its group before the context is
            // destroyed.
            if (mpp_ctx_) {
                mpp_destroy(mpp_ctx_);
                mpp_ctx_ = NULL;
            }
            mpp_mpi_ = NULL;
            if (pending_frame_) {
                mpp_frame_deinit(&pending_frame_);
            }
            if (pending_buffer_) {
                mpp_buffer_put(pending_buffer_);
                pending_buffer_ = NULL;
            }
        }
    }
    if (enc_cfg_) {
        mpp_enc_cfg_deinit(enc_cfg_);
        enc_cfg_ = NULL;
    }

    if (frm_grp_) {
        mpp_buffer_group_put(frm_grp_);
        frm_grp_ = NULL;
    }

    if (pkt_grp_) {
        mpp_buffer_group_put(pkt_grp_);
        pkt_grp_ = NULL;
    }

    if (mpp_ctx_) {
        mpp_destroy(mpp_ctx_);
        mpp_ctx_ = NULL;
    }
    mpp_mpi_ = NULL;

    if (yuv_buffer_) {
        free(yuv_buffer_);
        yuv_buffer_ = NULL;
        yuv_buffer_size_ = 0;
    }

    initialized_ = false;
    faulted_ = false;
}
