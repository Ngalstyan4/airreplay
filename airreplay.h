#ifndef AIRREPLAY_H
#define AIRREPLAY_H
#include <google/protobuf/any.pb.h>

#include <boost/function.hpp>  // AsyncRequest uses boost::function
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "airreplay/airreplay.pb.h"
#include "kudu/rpc/rpc_controller.h"
namespace kudu {
namespace rpc {
class OutboundCall;
}  // namespace rpc
}  // namespace kudu

namespace airreplay {
enum Mode { kRecord, kReplay };
// todo:: drop const or even support moving later if need be
using HookFunction = std::function<void(const google::protobuf::Message &msg)>;

class Airreplay {
 private:
  std::string txttracename_;
  std::string tracename_;
  std::fstream *tracetxt;
  std::fstream *tracebin;

  Mode RRmode_;

  // mutex and vars protected by it
  std::mutex recordOrder_;
  int recordIndex_ = 0;
  std::map<int, std::function<void()> > pendingCallbacks_;
  // partially parsed(proto::Any) trace events for replay
  std::deque<airreplay::OpequeEntry> traceEvents_;

  // ****************** below are only used in replay ******************

  std::map<int, HookFunction> hooks_;

  std::function<void()> kUnreachableCallback_{[]() {
    assert(false);
    std::runtime_error("must have been unreachable");
  }};

  void MaybeReplayExternalRPC(google::protobuf::Message *response = nullptr);
  int doRecordInternal(const std::string &debugstring,
                       const airreplay::OpequeEntry &request, int kind);
  // Records the passed message to disk.
  // does not take any locks and assumes all necessary synchronization is done
  // by the caller reutrns the intex of the record on trace
  int doRecord(const std::string &debugstring,
               const google::protobuf::Message &request, int kind,
               int linkToken = -1);
  void FinishRecord(int recordToken, const google::protobuf::Message *response);

 public:
  // same as the static interface below but allows for multiple independent
  // recordings in the same app used for testing mainly
  Airreplay(std::string tracename, Mode mode);
  ~Airreplay();

  std::string txttracename();
  std::string tracename();
  bool isReplay();
  std::deque<airreplay::OpequeEntry> getTraceForTest();
  void setReplayHooks(std::map<int, HookFunction> hooks);

  std::function<void()> RrAsync(const std::string &method,
                                const google::protobuf::Message &request,
                                google::protobuf::Message *response,
                                std::function<void()> callback);

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
   *
   * Returns the index of the recorded request (=recordToken).
   */
  int rr(const std::string &debuginfo, google::protobuf::Message &message,
         int kind = 0);
  static int rr(const std::string &debuginfo, const google::protobuf::Message &message,
         int kind = 0) {
          std::cerr << "old rr called";
          // throw std::runtime_error("shall not be called! old relic, not fully removed yet");
          return 0;
         };

  int rr(const std::string &debuginfo, std::string &message, int kind = 0);

  // int rr(const std::string &debuginfo, int &message,
  //        int kind = 0);

  void externalReplayerLoop();
};
extern Airreplay *airr;

// void init(int port, Mode mode);
}  // namespace airreplay

#endif