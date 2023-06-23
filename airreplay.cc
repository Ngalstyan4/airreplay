#include "airreplay.h"

#include <cassert>
#include <deque>
#include <thread>

#include "airreplay/airreplay.pb.h"
#include "utils.h"

namespace airreplay {
Airreplay *airr;
std::mutex log_mutex;

void log(const std::string &context, const std::string &msg) {
  std::lock_guard<std::mutex> lock(log_mutex);
  std::cerr << context << ": " << msg << std::endl;
}

Airreplay::Airreplay(std::string tracename, Mode mode)
    : rrmode_(mode), trace_(tracename, mode) {
  rrmode_ = mode;
}

Airreplay::~Airreplay() {
  for (auto &t : running_callbacks_) {
    t.join();
  }
}

bool Airreplay::isReplay() { return rrmode_ == Mode::kReplay; }

void Airreplay::setReplayHooks(
    std::map<int, std::function<void(const google::protobuf::Message &msg)> >
        hooks) {
  throw std::runtime_error("not implemented");
  hooks_ = hooks;
}

// todo:: turn this into a fency OpequeEntry constructor anc call trace_.Record
// after calling this
int Airreplay::doRecord(const std::string &debugstring,
                        const google::protobuf::Message &request, int kind,
                        int linkToken) {
  if (!(kind == 42 || kind == 4242)) return 0;
  // trace.push_back(any.ShortDebugString());

  airreplay::OpequeEntry header;
  header.set_kind(kind);
  header.set_link_to_token(linkToken);
  // early dev debug mode: populate Any and do not bother with second payload
  if (request.IsInitialized()) {
    // exception will be thrown if request is serialized when it has unfilled
    // required fields relevant for GetNodeInstanceResponsePB
    size_t reqLen = request.ByteSizeLong();
    header.set_body_size(reqLen);
    *header.mutable_rr_debug_string() = debugstring;
    header.mutable_message()->PackFrom(request);
  }
  return trace_.Record(header);
}

// void Proxy::AsyncRequest(
//     const string& method,
//     const google::protobuf::Message& req,
//     google::protobuf::Message* response,
//     RpcController* controller,
//     const ResponseCallback& callback);

// in recording:: save method, serialized req, response pointer and wrap
// callback in another call and return
// the wrapped callback(while maintaining its ownership)
// when wrapped callback is called, record serialized response, free wrapped
// callback

// in replay:: match method and request to the trace, queue req fingerprint,
// response pointer
//  and callback to be called when the trace reaches to the appropriate point
// set wrapped callback to nullptr; Actual async request should not be
// dispatched in replay so that func should never be called
std::function<void()> Airreplay::RrAsync(
    const std::string &method, const google::protobuf::Message &request,
    google::protobuf::Message *response, std::function<void()> callback) {
  // make sure that one thread gets here at a time.
  // eventually this will be enforced structurally (given we do rr in the
  // right places) and have appropriate app-level locks held for now this
  // ensured the debug txt trace does not get corrupted when rr is called from
  // multiple threads.
  if (rrmode_ == Mode::kRecord) {
    std::lock_guard lock(recordOrder_);
    int recordToken = doRecord(method, request, 42);

    auto myCallback =
        boost::function<void()>([this, recordToken, response, callback]() {
          // RPC dispatcher insures that response is valid at least until
          // callback is called (have not read this anywhere but it seems this
          // is how callback() uses it and we use it before callback)
          assert(response != nullptr);
          // this should be the global Airreplay object so effectively has
          // static lifetime
          assert(this != nullptr);
          log("RrAsync", "my callback successfully called !!");
          // todo:: call RecordReplay public interface here. RrAsync should be a
          // mere helper wrapper around RecordReplay and hooks interface.
          // this->FinishRecord(recordToken, response);
          callback();
        });
    return myCallback;
  } else {
    while (true) {
      std::unique_lock lock(recordOrder_);
      assert(trace_.HasNext());
      int pos = -1;

      const airreplay::OpequeEntry &req_peek = trace_.PeekNext(&pos);
      if (req_peek.kind() != 42) {
        log("RrAsync Replay attempt", "RrAsync not replaying@" +
                                          std::to_string(pos) +
                                          " \nexpected kind 42 BUT_GOT\n" +
                                          std::to_string(req_peek.kind()));
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        continue;
      }
      if (req_peek.message().value() != request.SerializeAsString()) {
        google::protobuf::Message *copy = request.New();
        req_peek.message().UnpackTo(copy);
        auto mismatch = utils::compareMessages(request, *copy);
        assert(mismatch != "");

        log("RrAsync Replay attempt",
            "RrAsync not replaying@" + std::to_string(pos) + "\n" + mismatch);
        // " \nexpected\n " +
        //     req_peek.ShortDebugString() + " BUT_GOT\n" +
        //     request.ShortDebugString());
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        continue;
      }

      // bool insertSuccess = pending_callbacks_.insert({pos, callback}).second;
      // assert(insertSuccess);
      // MaybeReplayExternalRPCUnlocked(response);
      auto running_callback = std::thread(callback);
      // q:: does std::move do something here?
      running_callbacks_.push_back(std::move(running_callback));
      std::string s = req_peek.ShortDebugString();
      trace_.ConsumeHead(req_peek);
      return kUnreachableCallback_;
    }
  }
}

void Airreplay::MaybeReplayExternalRPCUnlocked(
    google::protobuf::Message *response) {
  // replay external RPCs
  if (trace_.HasNext()) {
    int pos = -1;
    const airreplay::OpequeEntry &req = trace_.PeekNext(&pos);

    if (hooks_.find(req.kind()) != hooks_.end()) {
      // q:: does this make horriffic stack traces?
      // if I have 15 incoming calls one after another, this will add 30
      // frames (15*(rr, hooks[kind])) calls to the stack
      hooks_[req.kind()](req.message());
      // not calling ReplayNext() since rr will be called from the hook
      // which will ReplayNext()
    }

    int tokenLink = req.link_to_token();
    auto elem = pending_callbacks_.find(tokenLink);
    log("ReplayExternal", "looking for tokenLink " + std::to_string(tokenLink) +
                              "(next is " + std::to_string(trace_.pos()) +
                              ")\n");
    if (tokenLink != -1 && elem != pending_callbacks_.end()) {
      auto f = elem->second;
      log("ReplayExternal",
          "calling callback for " + req.ShortDebugString() + "\n");
      // unlike the hooks approach above there is no further airreplay
      // involvement after f() is called so we are done with req and should move
      // on <-- this will change soonz
      int pos = -1;
      airreplay::OpequeEntry rep = trace_.ReplayNext(&pos);
      if (response != nullptr) {
        rep.message().UnpackTo(response);
      } else {
        log("ReplayExternal", "WARNING! null response\n");
      }
      auto running_callback = std::thread(f);
      // q:: does std::move do something here?
      running_callbacks_.push_back(std::move(running_callback));
    }
  }
}

int Airreplay::SaveRestore(const std::string &key,
                           google::protobuf::Message &message) {
  return SaveRestoreInternal(key, nullptr, nullptr, &message);
}

int Airreplay::SaveRestore(const std::string &key, std::string &message) {
  return SaveRestoreInternal(key, &message, nullptr, nullptr);
}

int Airreplay::SaveRestore(const std::string &key, uint64 &message) {
  return SaveRestoreInternal(key, nullptr, &message, nullptr);
}

int Airreplay::SaveRestoreInternal(const std::string &key,
                                   std::string *str_message,
                                   uint64 *int_message,
                                   google::protobuf::Message *proto_message) {
  // exactly one type of pointer can be saved/restored per call
  assert((str_message != nullptr) + (int_message != nullptr) +
             (proto_message != nullptr) ==
         1);
  if (rrmode_ == Mode::kRecord) {
    std::lock_guard lock(recordOrder_);
    if (save_restore_keys_.find(key) != save_restore_keys_.end()) {
      log("WARN: SaveRestore", "key " + key + " already saved");
      // I cannot fail here because this is ok when two tuplicate keys
      // are not inflight concurrently. E.g., when one GetInstanceRequest fails,
      // and another one is issued against the same host/port, the keys will
      // match but this will not cause issues since the first one is guaranteed
      // to be fully replayed when the second one comes around. Would be good to
      // enforce the more subtle invariant for debugging but for now will just
      // have to remember to check for it
    }
    save_restore_keys_.insert(key);

    airreplay::OpequeEntry header;
    header.set_kind(4242);
    *header.mutable_rr_debug_string() = key;
    if (str_message != nullptr) {
      *header.mutable_str_message() = *str_message;
    }

    if (int_message != nullptr) {
      header.set_num_message(*int_message);
    }

    if (proto_message != nullptr) {
      if (proto_message->IsInitialized()) {
        int len = proto_message->ByteSizeLong();
        header.set_body_size(len);
        header.mutable_message()->PackFrom(*proto_message);
      }
    }
    // make sure that one thread gets here at a time.
    // eventually this will be enforced structurally (given we do rr in the
    // right places) and have appropriate app-level locks held for now this
    // ensured the debug txt trace does not get corrupted when rr is called from
    // multiple threads.
    return trace_.Record(header);
  } else {
    int pos = -1;
    while (true) {
      std::unique_lock lock(recordOrder_);

      const airreplay::OpequeEntry &req = trace_.PeekNext(&pos);
      if (req.kind() != 4242 || req.rr_debug_string() != key) {
        if (req.kind() != 4242) {
          log("SaveRestoreInternal",
              "not the right kind " + std::to_string(req.kind()) + " != 4242\t\tkey: " + key);
        } else {
          log("SaveRestoreInternal",
              "saverestore: not the right kind or method (((((((((((" + key +
                  ")))))))))))");
        }
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }
      // todo find abetter way of saying "there is no other data in
      if (str_message != nullptr) {
        assert(!req.str_message().empty());
        assert(req.message().ByteSizeLong() == 0);
        *str_message = req.str_message();
      }
      if (int_message != nullptr) {
        *int_message = req.num_message();
      }
      if (proto_message != nullptr) {
        assert(req.str_message().empty());
        // req.message().ByteSizeLong() could still be zero for, e.g., recording
        // of failed responses of GetNodeInstance
        req.message().UnpackTo(proto_message);
      }
      log("SaveRestoreInternal", "just SaveRESTORED " + req.ShortDebugString());

      trace_.ConsumeHead(req);
      assert(lock.owns_lock());
      return pos;
    }
  }
}

// for incoming requests
// todo: should be used in some places of outgoing request where we currently
// use save/restore
int Airreplay::RecordReplay(const std::string &key,
                            const google::protobuf::Message &message) {
  return -1;
}
// int Airreplay::rr(const std::string &method, google::protobuf::Message
// &request,
//                   int kind) {
//   if (rrmode_ == Mode::kRecord) {
//     // make sure that one thread gets here at a time.
//     // eventually this will be enforced structurally (given we do rr in the
//     // right places) and have appropriate app-level locks held for now this
//     // ensured the debug txt trace does not get corrupted when rr is called
//     // from multiple threads.
//     std::lock_guard lock(recordOrder_);
//     return doRecord(method, request, kind);
//   } else {
//     int pos = -1;
//     airreplay::OpequeEntry req = trace_.ReplayNext(&pos);
//     if (req.rr_debug_string() != method) {
//       throw std::runtime_error(
//           "async replay diverged(" + method + "!=" + req.rr_debug_string() +
//           "): expected \n" + method + " \nbut got \n" +
//           req.rr_debug_string());
//     }
//     std::cerr << "just consumed a rr object" << req.ShortDebugString() <<
//     "\n"; req.message().UnpackTo(&request); return pos;
//   }
// }

// void Airreplay::recordOutboundCall(
//     const std::string &debugstring, const google::protobuf::Message
//     &request, std::shared_ptr<kudu::rpc::OutboundCall> call) {
//   rr(debugstring, request);
//   if (rrmode_ == Mode::kReplay) {
//     // todo:: consider call->call_id() as key
//     outstandingCalls_[request.ShortDebugString()] = call;
//     // ^^ the replayer loop will be a thread here that will monitor
//     // outstandingCalls_ and the trace and will call appropriate callbacks
//     // outstandingCalls_[request.ShortDebugString()]->CallCallback();
//   }
// }

}  // namespace airreplay
