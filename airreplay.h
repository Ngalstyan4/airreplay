#ifndef AIRREPLAY_H
#define AIRREPLAY_H
#include <google/protobuf/any.pb.h>

#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "airreplay/airreplay.pb.h"
namespace kudu {
namespace rpc {
class OutboundCall;
}  // namespace rpc
}  // namespace kudu

namespace airreplay {
enum Mode { kRecord, kReplay };
using HookFunction = std::function<void(const google::protobuf::Message &msg)>;

class Airreplay {
 private:
  std::string txttracename_;
  std::string tracename_;
  std::fstream *tracetxt;
  std::fstream *tracebin;

  Mode RRmode_;

  // ****************** below are only used in replay ******************

  std::map<int, HookFunction> hooks_;

  // partially parsed(proto::Any) trace events for replay
  std::deque<airreplay::OpequeEntry> traceEvents_;

  // outstanding Outbound calls
  std::map<std::string, std::shared_ptr<kudu::rpc::OutboundCall> >
      outstandingCalls_;

  void doRecord(const std::string &debugstring,
                   const google::protobuf::Message &request, int kind);

 public:
  // same as the static interface below but allows for multiple independent
  // recordings in the same app used for testing mainly
  Airreplay(std::string tracename, Mode mode);
  ~Airreplay();

  std::string txttracename();
  std::string tracename();
  bool isReplay();
  std::deque<airreplay::OpequeEntry> getTraceForTest();
  // todo:: drop const or even support moving later if need be
  void setReplayHooks(std::map<int, HookFunction> hooks);

  /**
   * This is the main interface applications use to integrate record/replay into
   * them The interface processes the pair (message, kind). During recording it
   * writes the pair into the binary trace file. During replay it asserts that
   * the passed pair is at the current head of the recorded trace. If this
   * fails, the recorded execution and the current replay have divereged. If the
   * replay has not diverged, rr looks further to see whether there are other
   * requests after the current one which have a "kind" such that the system
   * should reproduce them If so, rr calls appropriate reproduction functions
   * (see more in setReplayHooks)
   */
  bool rr(const std::string &debuginfo,
          const google::protobuf::Message &message, int kind = 0);

  void recordOutboundCall(const std::string &method,
                          const google::protobuf::Message &request,
                          std::shared_ptr<kudu::rpc::OutboundCall> call);

  void externalReplayerLoop();
};
extern Airreplay *airr;

// void init(int port, Mode mode);
}  // namespace airreplay

#endif