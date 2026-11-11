/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#ifndef _rknnPool_H
#define _rknnPool_H

#include <queue>
#include <vector>
#include <iostream>
#include <new>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include "rga.h"
#include "im2d.h"
#include "RgaUtils.h"
#include "rknn_api.h"
#include "postprocess.h"
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include "ThreadPool.hpp"
using cv::Mat;
using std::queue;
using std::vector;

static unsigned char *load_data(FILE *fp, size_t ofst, size_t sz);
static unsigned char *load_model(const char *filename, int *model_size);

class rknn_lite
{
private:
    rknn_context rkModel = 0;
    unsigned char *model_data = nullptr;
    rknn_sdk_version version{};
    rknn_input_output_num io_num{};
    rknn_tensor_attr *input_attrs = nullptr;
    rknn_tensor_attr *output_attrs = nullptr;
    rknn_input inputs[1];
    int ret = 0;
    int channel = 3;
    int width = 0;
    int height = 0;
    int class_num = 0;
    int id;
    bool initialized_ = false;

public:
    Mat ori_img;
    int interf(detect_result_group_t &detect_result_group);
    rknn_lite(const char *dst, int n, int class_num, int id);
    ~rknn_lite();
    bool valid() const { return initialized_; }
};

rknn_lite::rknn_lite(const char *model_name, int n, int class_num, int id)
{
    this->class_num = class_num;
    this->id = id;
    /* Create the neural network */
    printf("Loading model id = %d\n", id);
    int model_data_size = 0;
    // 读取模型文件数据
    if (!model_name)
    {
        printf("RKNN model path is null\n");
        return;
    }
    model_data = load_model(model_name, &model_data_size);
    if (!model_data || model_data_size <= 0)
    {
        printf("RKNN model is unavailable: %s\n", model_name ? model_name : "(null)");
        return;
    }
    // 通过模型文件初始化rknn类
    ret = rknn_init(&rkModel, model_data, model_data_size, 0, NULL);
    if (ret < 0)
    {
        printf("rknn_init error ret=%d\n", ret);
        return;
    }
    rknn_core_mask core_mask;
    if (n == 0)
        core_mask = RKNN_NPU_CORE_0;
    else if (n == 1)
        core_mask = RKNN_NPU_CORE_1;
    else
        core_mask = RKNN_NPU_CORE_2;
    ret = rknn_set_core_mask(rkModel, core_mask);
    if (ret < 0)
    {
        printf("rknn_set_core_mask error ret=%d\n", ret);
        rknn_destroy(rkModel);
        rkModel = 0;
        return;
    }

    // 初始化rknn类的版本
    ret = rknn_query(rkModel, RKNN_QUERY_SDK_VERSION, &version, sizeof(rknn_sdk_version));
    if (ret < 0)
    {
        printf("rknn_init error ret=%d\n", ret);
        rknn_destroy(rkModel);
        rkModel = 0;
        return;
    }

    // 获取模型的输入参数
    ret = rknn_query(rkModel, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0)
    {
        printf("rknn_init error ret=%d\n", ret);
        rknn_destroy(rkModel);
        rkModel = 0;
        return;
    }

    if (io_num.n_input == 0 || io_num.n_output < 3)
    {
        printf("RKNN model must expose one input and at least three outputs\n");
        rknn_destroy(rkModel);
        rkModel = 0;
        return;
    }

    // 设置输入数组
    input_attrs = new (std::nothrow) rknn_tensor_attr[io_num.n_input];
    if (!input_attrs)
    {
        rknn_destroy(rkModel);
        rkModel = 0;
        return;
    }
    memset(input_attrs, 0, sizeof(rknn_tensor_attr) * io_num.n_input);
    for (int i = 0; i < io_num.n_input; i++)
    {
        input_attrs[i].index = i;
        ret = rknn_query(rkModel, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0)
        {
            printf("rknn_init error ret=%d\n", ret);
            delete[] input_attrs;
            input_attrs = nullptr;
            rknn_destroy(rkModel);
            rkModel = 0;
            return;
        }
    }

    // 设置输出数组
    output_attrs = new (std::nothrow) rknn_tensor_attr[io_num.n_output];
    if (!output_attrs)
    {
        delete[] input_attrs;
        input_attrs = nullptr;
        rknn_destroy(rkModel);
        rkModel = 0;
        return;
    }
    memset(output_attrs, 0, sizeof(rknn_tensor_attr) * io_num.n_output);
    for (int i = 0; i < io_num.n_output; i++)
    {
        output_attrs[i].index = i;
        ret = rknn_query(rkModel, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0)
        {
            delete[] input_attrs;
            delete[] output_attrs;
            input_attrs = nullptr;
            output_attrs = nullptr;
            rknn_destroy(rkModel);
            rkModel = 0;
            return;
        }
    }

    // 设置输入参数
    /*
    CHW：Batch(批次) -> Channel(通道) -> Height(高) -> Width(宽)。
    这是 PyTorch 等框架默认的格式。
    NHWC：Batch(批次) -> Height(高) -> Width(宽) -> Channel(通道)。
    这是 TensorFlow 等框架以及我们常用的 OpenCV 图像默认的格式。
    */
    if (input_attrs[0].fmt == RKNN_TENSOR_NCHW)
    {
        channel = input_attrs[0].dims[1];
        height = input_attrs[0].dims[2];
        width = input_attrs[0].dims[3];
    }
    else
    {
        height = input_attrs[0].dims[1];
        width = input_attrs[0].dims[2];
        channel = input_attrs[0].dims[3];
    }

    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].size = width * height * channel;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].pass_through = 0;
    initialized_ = true;
}

rknn_lite::~rknn_lite()
{
    if (initialized_ && rkModel)
        ret = rknn_destroy(rkModel);
    delete[] input_attrs;
    delete[] output_attrs;
    if (model_data)
        free(model_data);
}

int rknn_lite::interf(detect_result_group_t &detect_result_group)
{
    if (!initialized_ || ori_img.empty() || io_num.n_output < 3)
        return -1;
    cv::Mat img;
    // 获取图像宽高
    int img_width = ori_img.cols;
    int img_height = ori_img.rows;
    cv::cvtColor(ori_img, img, cv::COLOR_BGR2RGB);
    if (img_width != width || img_height != height)
        cv::resize(img, img, cv::Size(width, height));//原地覆盖
    inputs[0].buf = (void *)img.data;
    // 设置rknn的输入数据
    ret = rknn_inputs_set(rkModel, io_num.n_input, inputs);
    if (ret < 0)
        return ret;

    // 设置输出
    rknn_output outputs[io_num.n_output];
    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < io_num.n_output; i++)
        outputs[i].want_float = 0; // 调用npu进行推演
    ret = rknn_run(rkModel, NULL);
    if (ret < 0)
        return ret;
    // 获取npu的推演输出结果
    ret = rknn_outputs_get(rkModel, io_num.n_output, outputs, NULL);
    if (ret < 0)
        return ret;

    const float nms_threshold = NMS_THRESH;
    /*
    nms_threshold（非极大值抑制阈值）：
    这个阈值用来控制模型对框的“信度”。
    比如：
    假设模型对画面中的两个框有 90% 的信度，并且两个框的 IOU 也是 0.5。
    那么这个阈值就会把信度低于 90% 的框给砍掉。
    */
    const float box_conf_threshold = BOX_THRESH;
    /*
    box_conf_threshold（置信度阈值）：
    模型会在画面中预测出成百上千个“可能有物体”的框。这个阈值用来砍掉大部分瞎猜的框。
    意思是：“如果模型对这个框里有目标的把握低于设定值（比如 25%），就直接丢弃，不要拿来烦我”。
    */

    float scale_w = (float)width / img_width;
    float scale_h = (float)height / img_height;

    std::vector<float> out_scales;
    std::vector<int32_t> out_zps;
    for (int i = 0; i < io_num.n_output; ++i)
    {
        out_scales.push_back(output_attrs[i].scale);
        out_zps.push_back(output_attrs[i].zp);
    }

    ret = post_process((int8_t *)outputs[0].buf, (int8_t *)outputs[1].buf, (int8_t *)outputs[2].buf,
                       height, width, box_conf_threshold, nms_threshold, scale_w, scale_h,
                       out_zps, out_scales, &detect_result_group, class_num);
    if (ret < 0)
    {
        rknn_outputs_release(rkModel, io_num.n_output, outputs);
        return ret;
    }

    // Draw Objects
    char text[256];
    // std::cout << detect_result_group.count << " objects detected.\n";
    for (int i = 0; i < detect_result_group.count; i++)
    {
        detect_result_t *det_result = &(detect_result_group.results[i]);
        sprintf(text, "%s %.1f%%", det_result->name, det_result->prop * 100);
        int x1 = det_result->box.left;
        int y1 = det_result->box.top;
        if (id == 0)
            rectangle(ori_img, cv::Point(x1, y1), cv::Point(det_result->box.right, det_result->box.bottom), cv::Scalar(0, 255, 0, 0), 3);
        else
        {
            rectangle(ori_img, cv::Point(x1, y1), cv::Point(det_result->box.right, det_result->box.bottom), cv::Scalar(0, 0, 255, 0), 3);
        }
        //putText(ori_img, text, cv::Point(x1, y1 + 12), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255));
    }

    ret = rknn_outputs_release(rkModel, io_num.n_output, outputs);

    return 0;
}

static unsigned char *load_data(FILE *fp, size_t ofst, size_t sz)
{
    unsigned char *data;
    int ret;

    data = NULL;

    if (NULL == fp)
    {
        return NULL;
    }

    ret = fseek(fp, ofst, SEEK_SET);
    if (ret != 0)
    {
        printf("blob seek failure.\n");
        return NULL;
    }

    data = (unsigned char *)malloc(sz);
    if (data == NULL)
    {
        printf("buffer malloc failure.\n");
        return NULL;
    }
    // fread 函数从文件指针 fp 中读取 sz 个字节，并保存在 data 中。以一个字节为单位，每次读取一个字节。
    ret = static_cast<int>(fread(data, 1, sz, fp));
    //fread 是有返回值的，它会告诉你实际成功读取了多少个字节。
    //高级的写法应该去检查 if (ret != sz)。如果文件刚好损坏，要求读 10MB，实际只读出 8MB，
    //这段代码根本不会报错，而是直接把残缺的大脑喂给 NPU，导致 NPU 吐出极其诡异的乱码。
    if (ret != static_cast<int>(sz))
    {
        free(data);
        return NULL;
    }
    return data;
}

static unsigned char *load_model(const char *filename, int *model_size)
{
    FILE *fp;
    unsigned char *data;

    fp = fopen(filename, "rb");
    if (NULL == fp)
    {
        printf("Open file %s failed.\n", filename);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return NULL;
    }
    // ftell 函数返回文件指针 fp 的当前位置。
    long size_long = ftell(fp);
    if (size_long <= 0 || size_long > static_cast<long>(INT_MAX))
    {
        fclose(fp);
        return NULL;
    }
    int size = static_cast<int>(size_long);

    data = load_data(fp, 0, size);

    fclose(fp);

    *model_size = size;
    return data;
}

#endif
