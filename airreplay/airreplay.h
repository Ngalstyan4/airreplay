#pragma once

#include <google/protobuf/any.pb.h>

#include <boost/function.hpp>  // AsyncRequest uses boost::function
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "airreplay.pb.h"
#include "mock_socket_traffic.h"
#include "trace.h"

namespace airreplay {
// todo:: drop const or even support moving later if need be
using ReproducerFunction = std::function<void(
    const std::string &connection_info, const google::protobuf::Message &msg)>;
using google::protobuf::uint64;

enum ReservedMsgKinds {
  kInvalid,
  kDefault,
  kSaveRestore,

  kMaxReservedMsgKind,  // should be last!
};

void log(const std::string &context, const std::string &msg);
class Airreplay {
 public:
  // same as the static interface below but allows for multiple independent
  // recordings in the same app used for testing mainly
  Airreplay(std::string tracename, Mode mode);
  ~Airreplay();

  std::string MessageKindName(int kind);
  void RegisterMessageKindName(int kind, const std::string &name);

  int SaveRestore(const std::string &key, google::protobuf::Message &message);
  int SaveRestore(const std::string &key, std::string &message);
  int SaveRestore(const std::string &key, uint64 &message);

  /**
   * This is the main interface applications use to integrate record/replay into
   * them The interface processes the pair (message, kind). During recording it
   * saves its arguments into the binary trace file. During replay it checks
   * that the passed arguments are at the current head of the recorded trace. If
   * this fails, the interface blocks the caller until this becomes the case. If
   * this never becomes the case, the recorded execution and the current replay
   * have divereged.
   *
   *  If the replay has not diverged, rr looks to see whether there are other
   * requests after the current one which have a "kind" such that the system
   * should reproduce them If so, rr calls appropriate reproduction functions
   * (see more in RegisterReproducers)
   *
   * Returns the index of the recorded request (=recordToken).
   */
  int RecordReplay(const std::string &key, const std::string &connection_info,
                   const google::protobuf::Message &message, int kind = 0, const std::string &debug_info = "");

  bool isReplay();
  void RegisterReproducers(std::map<int, ReproducerFunction> reproduers);
  void RegisterReproducer(int kind, ReproducerFunction reproducer);

 private:
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
  Mode rrmode_;
  Trace trace_;
  SocketTraffic socketReplay_;

  std::map<int, std::string> userMsgKinds_;

  // used to inform background threads about shutdown
  bool shutdown_ = false;

  // mutex and vars protected by it
  std::mutex recordOrder_;
  std::map<int, std::function<void()>> pending_callbacks_;
  std::vector<std::thread> running_callbacks_;

  std::set<std::string> save_restore_keys_;

  // ****************** below are only used in replay ******************
  bool MaybeReplayExternalRPCUnlocked(const airreplay::OpequeEntry &req_peek);
  // Constructs and returns an opeque entry
  airreplay::OpequeEntry NewOpequeEntry(
      const std::string &debugstring, const google::protobuf::Message &request,
      int kind, int linkToken = -1);

  void externalReplayerLoop();

  std::map<int, ReproducerFunction> hooks_;
  std::function<void()> kUnreachableCallback_{
      []() { std::runtime_error("must have been unreachable"); }};

};
extern Airreplay *airr;

}  // namespace airreplay


// the below are specific to kudu integration of AirReplay
// could be in a kudu header but this makes dev easier so leaving it here for now
namespace kudu {
namespace rrsupport {

enum KuduMsgKinds {
  kOutboundRequest = 9,
  kOutboundResponse = 10,
  kInboundRequest = 11,
  // the replay trigger is on this message type
  kInboundResponse = 12,
};

extern std::mutex mockCallbackerMutex;
extern std::map<std::string, std::function<void()>> mockCallbacker;

}  // namespace rrsupport
}  // namespace kudu