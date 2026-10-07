//
// Created by 郭智 on 2026/10/7.
//

#pragma once

#include <cstdint>

namespace Doodle
{
    /**
     * 同时在途的帧数
     *
     * 「在途」指一帧的命令已经提交、但 GPU 还没执行完。凡是这段窗口内会被
     * 读写的资源，都必须按这个数量复制多份、用帧索引轮转取用，
     * 否则 host 录制下一帧时会和 GPU 正在执行的这一帧互相踩踏。
     * 目前涉及：命令缓冲、imageAvailable 信号量、帧栅栏。
     * 以后加 uniform buffer 时，同样按这个数量分配。
     *
     * 取 2 的目的是让 host 与 GPU 能交错开工，同时只引入一帧输入延迟。
     * 调到 3 以上可以抹平帧率抖动，但延迟会随之线性增加。
     *
     * 注意它和「交换链图像数量」是两个独立维度：图像数量由交换链决定，
     * 通常为 3，不要拿它当在途帧数用
     */
    inline constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
}
