#include "airreplay.h"
#include "mock_socket_traffic.h"

namespace airreplay {
void Airreplay::externalReplayerLoop() {
  int pos = 0;
  while (true) {
    if (shutdown_) {
      throw std::runtime_error("shutdown_ is true");
      return;
    }

    {
      std::lock_guard lock(recordOrder_);

      if (!trace_.HasNext()) {
        log("ExternalReplayer", "external replayer reach end of the trace");
        return;
      }
      const airreplay::OpequeEntry &req = trace_.PeekNext(&pos);
      log("ExternalReplayer@" + std::to_string(pos), "external replayer loop");

      if (MaybeReplayExternalRPCUnlocked(req)) {
        log("replayed external RPC", "@" + std::to_string(pos));
      } else {
        log("did not replay external RPC", "@" + std::to_string(pos));
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
  }
}
}  // namespace airreplay