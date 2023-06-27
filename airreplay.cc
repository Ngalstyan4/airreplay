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

void Airreplay::RegisterReproducers(std::map<int, ReproducerFunction> hooks) {
  hooks_ = hooks;
}

void Airreplay::RegisterReproducer(int kind, ReproducerFunction reproducer) {}

// todo:: turn this into a fency OpequeEntry constructor anc call trace_.Record
// after calling this
int Airreplay::doRecord(const std::string &debugstring,
                        const google::protobuf::Message &request, int kind,
                        int linkToken) {
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
std::function<void()> Airreplay::RROutgoingCallAsync(
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
        if (!MaybeReplayExternalRPCUnlocked(req_peek)) {
          log("RROutgoingCallAsync Replay attempt",
              "RROutgoingCallAsync not replaying@" + std::to_string(pos) +
                  " \nexpected kind 42 BUT_GOT\n" +
                  std::to_string(req_peek.kind()));
        }
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        continue;
      }

      if (req_peek.message().value() != request.SerializeAsString()) {
        auto mismatch =
            utils::compareMessageWithAny(request, req_peek.message());
        assert(mismatch != "");

        log("RROutgoingCallAsync Replay attempt",
            "RROutgoingCallAsync not replaying@" + std::to_string(pos) + "\n" +
                mismatch);

        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        continue;
      }

      auto running_callback = std::thread(callback);
      running_callbacks_.push_back(std::move(running_callback));
      std::string s = req_peek.ShortDebugString();
      trace_.ConsumeHead(req_peek);
      // ^^ once callbacks move to the body of RROutgoingCallAsync, turn this
      // into softConsume
      return kUnreachableCallback_;
    }
  }
}

// replay external RPCs. Since Responses of outgoing RPCs are taken care of
// by RROutgoingCallAsync, this function only handles incoming RPC calls
bool Airreplay::MaybeReplayExternalRPCUnlocked(
    const airreplay::OpequeEntry &req_peek) {
  if (hooks_.find(req_peek.kind()) == hooks_.end()) return false;

  if (!trace_.SoftConsumeHead(req_peek)) {
    log("MaybeReplayExternalRPCUnlocked",
        "Warning: callback had previously been scheduled but still is on the "
        "trace");
    return false;
  }

  auto callback = [=]() { hooks_[req_peek.kind()](req_peek.message()); };
  auto running_callback = std::thread(callback);
  // q:: does std::move do something here?
  running_callbacks_.push_back(std::move(running_callback));
  return true;
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
      // I cannot fail here because this is ok when two tuplicate keys
      // are not inflight concurrently. E.g., when one GetInstanceRequest fails,
      // and another one is issued against the same host/port, the keys will
      // match but this will not cause issues since the first one is guaranteed
      // to be fully replayed when the second one comes around. Would be good to
      // enforce the more subtle invariant for debugging but for now will just
      // have to remember to check for it
      log("WARN: SaveRestore", "key " + key + " already saved");
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
        if (!MaybeReplayExternalRPCUnlocked(req)) {
          if (req.kind() != 4242) {
            log("SaveRestoreInternal@" + std::to_string(pos),
                "not the right kind " + std::to_string(req.kind()) +
                    " != 4242\t\tkey: " + key);
          } else {
            log("SaveRestoreInternal",
                "saverestore: not the right kind or method (((((((((((" + key +
                    ")))))))))))");
          }
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
                            const google::protobuf::Message &message,
                            int kind) {
  if (rrmode_ == Mode::kRecord) {
    std::lock_guard lock(recordOrder_);

    airreplay::OpequeEntry header;
    if (kind == 0) {
      kind = 3232;
    }
    header.set_kind(kind);
    header.set_rr_debug_string(key);
    // header.set_link_to_token(linkToken);
    if (message.IsInitialized()) {
      size_t mlen = message.ByteSizeLong();
      header.set_body_size(mlen);
      header.mutable_message()->PackFrom(message);
    }
    return trace_.Record(header);
  } else {
    int pos = -1;
    while (true) {
      std::unique_lock lock(recordOrder_);
      const airreplay::OpequeEntry &req_peek = trace_.PeekNext(&pos);

      if (req_peek.kind() != 3232 && req_peek.kind() != 32) {
        log("RecordReplay@" + std::to_string(pos),
            "not the right kind " + std::to_string(req_peek.kind()) +
                " !=" + std::to_string(kind) + "\t\tkey: " + key);
      } else if (key != req_peek.rr_debug_string()) {
        log("RecordReplay@" + std::to_string(pos),
            "right kind(" + std::to_string(kind) +
                ") but not the right entry\tgot key:" + key +
                " expected:" + req_peek.rr_debug_string());
      } else if (req_peek.message().value() != message.SerializeAsString()) {
        auto mismatch =
            utils::compareMessageWithAny(message, req_peek.message());
        assert(mismatch != "");
        log("RecordReplay@" + std::to_string(pos),
            "right kind and entry key. wrong proto message. " + mismatch);
      } else {
        assert(req_peek.kind() == 32 || req_peek.kind() == 3232);
        assert(req_peek.message().value() == message.SerializeAsString());
        // todo find abetter way of saying "there is no other data in
        log("RecordReplay", "Just REPLAYED" + req_peek.ShortDebugString());
        trace_.ConsumeHead(req_peek);
        assert(lock.owns_lock());
        return pos;
      }

      lock.unlock();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
  }
}
}  // namespace airreplay
