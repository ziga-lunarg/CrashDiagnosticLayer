/*
 * Copyright (c) 2024 The Khronos Group Inc.
 * Copyright (c) 2024 Valve Corporation
 * Copyright (c) 2024 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "test_icd_queue.h"

#include "test_icd_command.h"
#include "test_icd_fence.h"
#include "test_icd_semaphore.h"

#include <vulkan/utility/vk_struct_helper.hpp>

namespace icd {

Queue::Queue() { set_loader_magic_value(&loader_data); }
Queue::~Queue() {
    if (thread_.joinable()) {
        exit_thread_ = true;
        cond_.notify_one();
        idle_cond_.notify_all();
        thread_.join();
    }
}

void Queue::StartThread() {
    if (!thread_.joinable()) {
        thread_ = std::thread(&Queue::ThreadFunc, this);
    }
}

VkResult Queue::Submit(uint32_t count, const VkSubmitInfo *submits, VkFence fence) {
    VkResult result = VK_SUCCESS;

    StartThread();

    Submission submission;
    submission.fence = reinterpret_cast<Fence *>(fence);
    for (uint32_t i = 0; i < count; i++) {
        const auto &submit = submits[i];
        SubmitInfo info;
        const auto *timeline_info = vku::FindStructInPNextChain<VkTimelineSemaphoreSubmitInfo>(submit.pNext);
        for (uint32_t wi = 0; wi < submit.waitSemaphoreCount; wi++) {
            SemInfo sem_info;
            sem_info.semaphore = reinterpret_cast<Semaphore *>(submit.pWaitSemaphores[wi]);
            sem_info.value = timeline_info ? timeline_info->pSignalSemaphoreValues[wi] : 0;
            info.signal_semaphores.push_back(sem_info);
        }
        for (uint32_t ci = 0; ci < submit.commandBufferCount; ci++) {
            info.cmd_buffers.push_back(reinterpret_cast<CommandBuffer *>(submit.pCommandBuffers[ci]));
        }
        for (uint32_t si = 0; si < submit.signalSemaphoreCount; si++) {
            SemInfo sem_info;
            sem_info.semaphore = reinterpret_cast<Semaphore *>(submit.pSignalSemaphores[si]);
            sem_info.value = timeline_info ? timeline_info->pSignalSemaphoreValues[si] : 0;
            info.signal_semaphores.push_back(sem_info);
        }
        submission.submit_infos.emplace_back(std::move(info));
    }
    {
        auto guard = Lock();
        submissions_.emplace_back(std::move(submission));
        cond_.notify_one();
    }
    return result;
}

VkResult Queue::Submit2(uint32_t count, const VkSubmitInfo2 *submits, VkFence fence) {
    VkResult result = VK_SUCCESS;

    StartThread();

    Submission submission;
    submission.fence = reinterpret_cast<Fence *>(fence);
    for (uint32_t i = 0; i < count; i++) {
        const auto &submit = submits[i];
        SubmitInfo info;
        for (uint32_t wi = 0; wi < submit.waitSemaphoreInfoCount; wi++) {
            SemInfo sem_info;
            const auto &wait = submit.pWaitSemaphoreInfos[wi];
            sem_info.semaphore = reinterpret_cast<Semaphore *>(wait.semaphore);
            sem_info.value = wait.value;
            info.signal_semaphores.push_back(sem_info);
        }
        for (uint32_t ci = 0; ci < submit.commandBufferInfoCount; ci++) {
            info.cmd_buffers.push_back(reinterpret_cast<CommandBuffer *>(submit.pCommandBufferInfos[ci].commandBuffer));
        }
        for (uint32_t si = 0; si < submit.signalSemaphoreInfoCount; si++) {
            SemInfo sem_info;
            const auto &sig = submit.pSignalSemaphoreInfos[si];
            sem_info.semaphore = reinterpret_cast<Semaphore *>(sig.semaphore);
            sem_info.value = sig.value;
            info.signal_semaphores.push_back(sem_info);
        }
        submission.submit_infos.emplace_back(std::move(info));
    }
    {
        auto guard = Lock();
        submissions_.emplace_back(std::move(submission));
        cond_.notify_one();
    }
    return result;
}

VkResult Queue::BindSparse(uint32_t count, const VkBindSparseInfo *binds, VkFence fence) {
    VkResult result = VK_SUCCESS;

    StartThread();

    Submission submission;
    submission.fence = reinterpret_cast<Fence *>(fence);
    for (uint32_t i = 0; i < count; i++) {
        const auto &bind = binds[i];
        SubmitInfo info;
        const auto *timeline_info = vku::FindStructInPNextChain<VkTimelineSemaphoreSubmitInfo>(bind.pNext);
        for (uint32_t wi = 0; wi < bind.waitSemaphoreCount; wi++) {
            SemInfo sem_info;
            sem_info.semaphore = reinterpret_cast<Semaphore *>(bind.pWaitSemaphores[wi]);
            sem_info.value = timeline_info ? timeline_info->pSignalSemaphoreValues[wi] : 0;
            info.signal_semaphores.push_back(sem_info);
        }
        for (uint32_t si = 0; si < bind.signalSemaphoreCount; si++) {
            SemInfo sem_info;
            sem_info.semaphore = reinterpret_cast<Semaphore *>(bind.pSignalSemaphores[si]);
            sem_info.value = timeline_info ? timeline_info->pSignalSemaphoreValues[si] : 0;
            info.signal_semaphores.push_back(sem_info);
        }
        submission.submit_infos.emplace_back(std::move(info));
    }
    {
        auto guard = Lock();
        submissions_.emplace_back(std::move(submission));
        cond_.notify_one();
    }
    return result;
}

VkResult Queue::WaitIdle() {
    if (thread_.joinable()) {
        auto guard = Lock();
        idle_cond_.wait(guard, [this] { return submissions_.empty() || exit_thread_; });
    }
    return VK_SUCCESS;
}

Queue::Submission *Queue::NextSubmission() {
    Submission *result = nullptr;
    // Find if the next submission is ready so that the thread function doesn't need to worry
    // about locking.
    auto guard = Lock();
    while (!exit_thread_ && submissions_.empty()) {
        idle_cond_.notify_all();
        // The queue thread must wait forever if nothing is happening, until we tell it to exit
        cond_.wait(guard);
    }
    if (!exit_thread_) {
        result = &submissions_.front();
        // NOTE: the submission must remain on the dequeue until we're done processing it so that
        // anyone waiting for it can find the correct waiter
    }
    return result;
}

VkResult Queue::Execute(Queue::Submission &submission) {
    VkResult result = VK_SUCCESS;
    for (auto &submit_info : submission.submit_infos) {
        for (auto &wait : submit_info.wait_semaphores) {
            if (result == VK_SUCCESS) {
                result = wait.semaphore->QueueWait(wait.value);
            } else {
                wait.semaphore->DeviceLost();
            }
        }
        if (result == VK_SUCCESS) {
            for (auto &cb : submit_info.cmd_buffers) {
                result = cb->Execute(*this);
                if (result != VK_SUCCESS) {
                    break;
                }
            }
        }
        for (auto &signal : submit_info.signal_semaphores) {
            if (result == VK_SUCCESS) {
                signal.semaphore->QueueSignal(signal.value);
            } else {
                signal.semaphore->DeviceLost();
            }
        }
    }
    if (submission.fence) {
        if (result == VK_SUCCESS) {
            submission.fence->Signal();
        } else {
            submission.fence->DeviceLost();
        }
    }
    return result;
}

void Queue::ThreadFunc() {
    Submission *submission = nullptr;
    VkResult result = VK_SUCCESS;
    // Roll this queue forward, one submission at a time.
    while (result == VK_SUCCESS) {
        submission = NextSubmission();
        if (submission == nullptr) {
            break;
        }
        result = Execute(*submission);
        {
            auto guard = Lock();
            submissions_.pop_front();
        }
    }
}

void Queue::TrackCheckpoint(uintptr_t checkpoint, VkPipelineStageFlagBits stage) {
    auto guard = Lock();
    checkpoints_.push_back(vku::InitStruct<VkCheckpointDataNV>(nullptr, stage, reinterpret_cast<void *>(checkpoint)));
}

void Queue::GetCheckpointData(uint32_t *count, VkCheckpointDataNV *checkpoints) {
    if (!checkpoints) {
        *count = uint32_t(checkpoints_.size());
        return;
    }
    if (*count > uint32_t(checkpoints_.size())) {
        *count = uint32_t(checkpoints_.size());
    }
    for (uint32_t i = 0; i < *count; i++) {
        checkpoints[i] = checkpoints_[i];
    }
}

void Queue::GetCheckpointData2(uint32_t *count, VkCheckpointData2NV *checkpoints) {
    if (!checkpoints) {
        *count = uint32_t(checkpoints_.size());
        return;
    }
    if (*count > uint32_t(checkpoints_.size())) {
        *count = uint32_t(checkpoints_.size());
    }
    for (uint32_t i = 0; i < *count; i++) {
        auto checkpoint =
            vku::InitStruct<VkCheckpointData2NV>(nullptr, checkpoints_[i].stage, checkpoints_[i].pCheckpointMarker);
        checkpoints[i] = checkpoint;
    }
}

}  // namespace icd