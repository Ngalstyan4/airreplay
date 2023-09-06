#include "airreplay.h"

#include <sys/prctl.h>
#include <glog/logging.h>

namespace airreplay {

void Airreplay::externalReplayerLoop() {
  int err = prctl(PR_SET_NAME, "AirReplayExternalReplayerLoop");
  DCHECK(err >= 0 || err == EPERM) << "prctl(PR_SET_NAME) failed. errno: " << err;
  int pos = 0;
  while (true) {
    log("ExternalReplayer", "external replayer loop");
    if (shutdown_) {
      throw std::runtime_error("shutdown_ is true");
      return;
    }

    {
      log("ExternalReplayer", "external replayer loop lock");
      std::lock_guard lock(recordOrder_);
      log("ExternalReplayer", "external replayer loop lock acquired");

      if (!trace_.HasNext()) {
        log("ExternalReplayer", "external replayer reach end of the trace");
        return;
      }
      const airreplay::OpequeEntry &req = trace_.PeekNext(&pos);
      if (MaybeReplayExternalRPCUnlocked(req)) {
        log("replayed external RPC", "@" + std::to_string(pos));
        continue;
      }
    }

    log("did not replay external RPC", "@" + std::to_string(pos));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
}  // namespace airreplay