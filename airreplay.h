#ifndef AIRREPLAY_H
#define AIRREPLAY_H
#include <google/protobuf/any.pb.h>

#include <boost/function.hpp>  // AsyncRequest uses boost::function
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "airreplay/airreplay.pb.h"
#include "airreplay/trace.h"
#include "kudu/rpc/rpc_controller.h"
namespace kudu {
namespace rpc {
class OutboundCall;
}  // namespace rpc
}  // namespace kudu

namespace airreplay {
// todo:: drop const or even support moving later if need be
using HookFunction = std::function<void(const google::protobuf::Message &msg)>;

void log(const std::string &context, const std::string &msg);
class Airreplay {
 public:
  // same as the static interface below but allows for multiple independent
  // recordings in the same app used for testing mainly
  Airreplay(std::string tracename, Mode mode);
  ~Airreplay();

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
  // int rr(const std::string &debuginfo, google::protobuf::Message &message,
  //        int kind = 0);
  // static int rr(const std::string &debuginfo,
  //               const google::protobuf::Message &message, int kind = 0) {
  //   std::cerr << "old rr called";
  //   // throw std::runtime_error("shall not be called! old relic, not fully
  //   // removed yet");
  //   return 0;
  // };

  int SaveRestore(const std::string &key, google::protobuf::Message &message);
  int SaveRestore(const std::string &key, std::string &message);
  int SaveRestore(const std::string &key, uint64 &message);

  int RecordReplay(const std::string &key, const google::protobuf::Message &message);

  bool isReplay();
  void setReplayHooks(std::map<int, HookFunction> hooks);

  std::function<void()> RrAsync(const std::string &method,
                                const google::protobuf::Message &request,
                                google::protobuf::Message *response,
                                std::function<void()> callback);

 private:
  Mode rrmode_;
  Trace trace_;

  // mutex and vars protected by it
  std::mutex recordOrder_;
  std::map<int, std::function<void()> > pending_callbacks_;
  std::vector<std::thread> running_callbacks_;

  std::set<std::string> save_restore_keys_;
  // ****************** below are only used in replay ******************

  std::map<int, HookFunction> hooks_;

  std::function<void()> kUnreachableCallback_{
      []() { std::runtime_error("must have been unreachable"); }};

  void MaybeReplayExternalRPCUnlocked(
      google::protobuf::Message *response = nullptr);
  // Records the passed message to disk.
  // does not take any locks and assumes all necessary synchronization is done
  // by the caller reutrns the intex of the record on trace
  int doRecord(const std::string &debugstring,
               const google::protobuf::Message &request, int kind,
               int linkToken = -1);

  // this API is necessary for 2 reasons
  // 1. unlike in go, here replayHooks are argumentless callbacks so the
  // replayer cannot pass the proto being replayed to the replay hook.
  // (replayHooks being argumentless is a design decision with its own
  // justification)
  // 2. some kuduraft state (uuid, request status) is tracked outside of
  // protobufs.
  int SaveRestoreInternal(const std::string &key, std::string *str_message,
                          uint64 *int_message,
                          google::protobuf::Message *proto_message);
  void externalReplayerLoop();
};
extern Airreplay *airr;

// void init(int port, Mode mode);
}  // namespace airreplay

#endif