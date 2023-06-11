#ifndef AIRREPLAY_H
#define AIRREPLAY_H
#include <google/protobuf/any.pb.h>

#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <string>

#include "airreplay/airreplay.pb.h"
namespace kudu {
namespace rpc {
class OutboundCall;
}  // namespace rpc
}  // namespace kudu

namespace airreplay {
enum Mode { RECORD, REPLAY };

enum RecordKind {
  kIncoming,
  kOutgoing
  /*
  maybe this should be
  INCOMING_REQUEST
  INCOMING_RESPONSE
  OUTGOING_REQUEST
  OUTGOING_RESPONSE
  */
};
class Airreplay {
 private:
  std::string txttracename_;
  std::string tracename_;
  // std::vector<std::string> trace;
  // because ofstream holds onto the string it is passed
  std::fstream *tracetxt;
  std::fstream *tracebin;

  // mode is record or replay
  Mode RRmode_;
  // ********************** below are only used in replay
  // ********************************
  std::thread externalReplayerThread_;
  // serializes access to traceEvents_ between externalReplayerThread and
  // application calls to airr
  std::mutex traceLock_;
  std::condition_variable traceCond_;
  std::map<int, std::function<void(const google::protobuf::Message &msg)> >
      hooks_;

  // partially parsed(proto::Any) trace events for replay
  std::deque<airreplay::OpequeEntry> traceEvents_;

  // outstanding Outbound calls
  std::map<std::string, std::shared_ptr<kudu::rpc::OutboundCall> >
      outstandingCalls_;

  void airr_record(const std::string &debugstring,
                   const google::protobuf::Message &request, int kind);

 public:
  // same as the static interface below but allows for multiple independent
  // recordings in the same app used for testing mainly
  Airreplay(std::string tracename, Mode mode);

  std::string txttracename();
  std::string tracename();
  bool isReplay();
  std::deque<airreplay::OpequeEntry> getTrace();
  // todo:: drop const or even support moving later if need be
  void setReplayHooks(
      std::map<int, std::function<void(const google::protobuf::Message &msg)> >
          hooks);
  // todo:: turn callback argument into a template to make calling less awkward?
  bool rr(const std::string &method, const google::protobuf::Message &request,
          google::protobuf::Message &response,
          std::function<void(google::protobuf::Message &)> callback,
          int kind = 0);

  bool rr(const std::string &method, const google::protobuf::Message &request,
          google::protobuf::Message *response = nullptr, int kind = 0);
  void recordOutboundCall(const std::string &method,
                          const google::protobuf::Message &request,
                          std::shared_ptr<kudu::rpc::OutboundCall> call);

  void replayerThread();
};
extern Airreplay *airr;

// void init(int port, Mode mode);
}  // namespace airreplay

#endif