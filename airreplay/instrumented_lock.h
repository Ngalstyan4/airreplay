#pragma once

#include <glog/logging.h>
#include <google/protobuf/any.pb.h>

#include <boost/function.hpp>  // AsyncRequest uses boost::function
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "airreplay.h"
#include "kudu/gutil/spinlock.h"

namespace airreplay {

class simple_spinlock {
 public:
  simple_spinlock() {
    std::cerr << "narek_simple_spinlock:: untracked constructed " << std::endl;
  }
  simple_spinlock(uint64_t tid, std::string airr_name) {
    CHECK(airr_name != "");
    // sometimes lock is created outside of kudu thread (see service_queue.h)
    // CHECK(kudu::Thread::current_thread());
    tid_ = tid;
    id_ = airr_name;
    // tid_ = kudu::Thread::current_thread()->tid();
    // id_ = "simple_spinlock_"+ kudu::Thread::current_thread()->name() +
    // std::to_string(tid_) + std::to_string((uint64)&l_);
  }

  void lock() {
    CHECK(id_ != "");
    int64_t curr_tid;
    if (kudu::Thread::current_thread()) {
      curr_tid = kudu::Thread::current_thread()->tid();
    } else {
      curr_tid = std::hash<std::thread::id>()(std::this_thread::get_id());
    }
    if (id_ != "") {
      uint64_t dummy;
      CHECK(airreplay::airr != nullptr);
      airr->SaveRestorePerThread(curr_tid, dummy, id_ + __FUNCTION__);
    } else {
      std::cerr << "WARNING:: simple_spinlock::lock() called without a name"
                << id_ << std::endl;
    }

    l_.Lock();
  }

  void unlock() {
    CHECK(id_ != "");
    // CHECK(kudu::Thread::current_thread());
    int64_t curr_tid;
    if (kudu::Thread::current_thread()) {
      curr_tid = kudu::Thread::current_thread()->tid();
    } else {
      curr_tid = std::hash<std::thread::id>()(std::this_thread::get_id());
    }
    uint64_t dummy;
    CHECK(airr != nullptr);
    airr->SaveRestorePerThread(curr_tid, dummy, id_ + __FUNCTION__);
    l_.Unlock();
  }

  bool try_lock() {
    CHECK(id_ != "");

    bool res = l_.TryLock();
    if (res && id_ != "") {
      // CHECK(kudu::Thread::current_thread());
      int64_t curr_tid;
      if (kudu::Thread::current_thread()) {
        curr_tid = kudu::Thread::current_thread()->tid();
      } else {
        curr_tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
      }
      uint64_t dummy;
      CHECK(airr != nullptr);
      airr->SaveRestorePerThread(curr_tid, dummy, id_ + __FUNCTION__);
    }
    return res;
  }

  // Return whether the lock is currently held.
  //
  // This state can change at any instant, so this is only really useful
  // for assertions where you expect to hold the lock. The success of
  // such an assertion isn't a guarantee that the current thread is the
  // holder, but the failure of such an assertion _is_ a guarantee that
  // the current thread is _not_ holding the lock!
  bool is_locked() { return l_.IsHeld(); }

 private:
  base::SpinLock l_;
  std::string id_;
  int64_t tid_;

  DISALLOW_COPY_AND_ASSIGN(simple_spinlock);
};
}  // namespace airreplay