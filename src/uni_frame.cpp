#include "uni_frame.hpp"

#include <iostream>
#include <memory> // for std::unique_ptr, std::make_unique
#include <mutex>  // for std::mutex, std::lock_guard
#include <queue>  // for std::queue

namespace uniframe {

// FramePool::FramePool(size_t maxsize)
// {
//     for (size_t i = 0; i < maxsize; ++i) {
//         pool.push(std::make_unique<ImageRGB565>());
//     }
// }

// FramePool::~FramePool()
// {

// }

// std::unique_ptr<ImageRGB565> FramePool::acquire()
// {
//     std::lock_guard<std::mutex> lock(mtx);
//     if (!pool.empty()) {
//         auto frame = std::move(pool.front());
//         pool.pop();
//         return frame;
//     } else {
//         std::cerr << "[WARN] FramePool depleted" << std::endl;
//         return nullptr;
//     }
// }

// void FramePool::release(std::unique_ptr<ImageRGB565> frameRaw)
// {
//     std::lock_guard<std::mutex> lock(mtx);
//     pool.push(std::move(frameRaw));
// }


}