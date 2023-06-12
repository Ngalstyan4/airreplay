#include "airreplay.h"

namespace airreplay {

void Airreplay::externalReplayerLoop() {
  throw std::runtime_error("not implemented");
  while (true) {
    // std::lock_guard lock(traceLock_);
    if (traceEvents_.empty()) {
      return;
    }
    // there is a deadlock here!! release traceLock_ before calling the hook

    /*
    event not popped from traceEvents_ because the hook will re-deliver
    the message and appropriate RR method will be called so that will pop it
    */
  }
}
}  // namespace airreplay