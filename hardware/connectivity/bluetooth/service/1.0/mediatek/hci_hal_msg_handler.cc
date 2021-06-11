/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#if defined(MTK_BT_HAL_STATE_MACHINE) && (TRUE == MTK_BT_HAL_STATE_MACHINE)

#define LOG_TAG "mtk.hal.bt-msg-handler"

#include "hci_hal_msg_handler.h"

#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <semaphore.h>

#include <log/log.h>

#include <mtk_util.h>

namespace vendor {
namespace mediatek {
namespace bluetooth {
namespace hal {

const eventfd_t kStopEvent = 1;
sem_t sem;

BtHalMessageLooper::BtHalMessageLooper()
  : looper_(nullptr), event_fd_(-1) {}

int BtHalMessageLooper::StartMessageLooper(const std::string& thread_name) {
  return StartThread(thread_name);
}

void BtHalMessageLooper::StopMessageLooper() {
  StopThread();
}

void BtHalMessageLooper::EnqueueMessage(const android::sp<android::MessageHandler>& handler,
    const android::Message& message) const {
  LOG_ALWAYS_FATAL_IF(!handler.get(), "%s: No handler interface!", __func__);
  looper_->sendMessage(handler, message);
}

int BtHalMessageLooper::StartThread(const std::string& thread_name) {
  if (std::atomic_exchange(&running_, true)) {
    return 0;
  }

  int res = sem_init(&sem,0,0);
  if (res != 0) {
    ALOGE("create semaphore fail");
  }

  event_fd_ = eventfd(0, 0);
  if (-1 == event_fd_) {
    LOG_ALWAYS_FATAL("%s: failed to create msg event fd!", __func__);
    return -1;
  }

  thread_ = std::thread(
      [this](std::string thread_name)
        { ThreadRoutine(thread_name); },
      thread_name
  );
  if (!thread_.joinable()) return -1;

  sem_wait(&sem);

  ALOGI("StartThread Done." );

  return 0;
}

int BtHalMessageLooper::StopThread() {
  if (!std::atomic_exchange(&running_, false)) {
    return 0;
  }

  eventfd_write(event_fd_, kStopEvent);
  if (std::this_thread::get_id() != thread_.get_id()) {
    thread_.join();
  }
  looper_->removeFd(event_fd_);
  close(event_fd_);
  looper_.clear();
  looper_ = nullptr;
  sem_destroy(&sem);
  return 0;
}

void BtHalMessageLooper::RunLooper() {
  ALOGI("Start RunLooper");
  looper_ = android::Looper::prepare(0);
  LOG_ALWAYS_FATAL_IF(!looper_.get(), "%s: No looper interface!", __func__);
  looper_->addFd(event_fd_, kStopEvent, android::Looper::EVENT_INPUT,
      [](int fd, int events, void* data) -> int {
        ALOGW("%s: stop routine: fd %d, events %d, data %p",
            __func__, fd, events, data);
        eventfd_t value(-1);
        int ret = static_cast<int>(eventfd_read(fd, &value));
        return ret;
      },
      nullptr);
  int result(android::Looper::POLL_ERROR);
  sem_post(&sem);
  do {
    result = looper_->pollOnce(-1);
  } while (android::Looper::POLL_WAKE == result || running_);
}

void BtHalMessageLooper::ThreadRoutine(const std::string& thread_name) {
  const int kThreadNameLenMax(16);
  char name_buffer[kThreadNameLenMax+1] = {'0'};
  size_t copy_len = thread_name.length() < kThreadNameLenMax ?
      thread_name.length() : kThreadNameLenMax;
  thread_name.copy(name_buffer, copy_len);
  pid_t tid = gettid();
  if (prctl(PR_SET_NAME, (unsigned long)name_buffer) == -1) {
    ALOGE("%s: unable to set thread name: %s for %s",
        __func__, name_buffer, strerror(errno));
  } else {
    ALOGW("%s: thread id %d, thread name %s started",
        __func__, tid, name_buffer);
  }
  RunLooper();
  ALOGW("%s: thread id %d, thread name %s exited",
      __func__, tid, name_buffer);
}

}  // namespace hal
}  // namespace bluetooth
}  // namespace mediatek
}  // namespace vendor

#endif
