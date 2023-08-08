#include "airreplay.h"

#include <glog/logging.h>

#include <cassert>
#include <deque>
#include <thread>

#include "airreplay.pb.h"
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

  if (rrmode_ == Mode::kReplay) {
    // start replay thread
    running_callbacks_.push_back(
        std::thread(&airreplay::Airreplay::externalReplayerLoop, this));
  }
}

Airreplay::~Airreplay() {
  shutdown_ = true;
  for (auto &t : running_callbacks_) {
    t.join();
  }
}

// *** accounting and convenience ***
std::string Airreplay::MessageKindName(int kind) {
  switch (kind) {
    case kInvalid:
      return "kInvalid";
    case kDefault:
      return "kDefault";
    case kSaveRestore:
      return "kSaveRestore";
  }
  if (userMsgKinds_.find(kind) != userMsgKinds_.end()) {
    return "UserMessage(" + userMsgKinds_[kind] + ")";
  }
  return "UnnamedMessageKind(" + std::to_string(kind) + ")";
}

void Airreplay::RegisterMessageKindName(int kind, const std::string &name) {
  if (kind <= kMaxReservedMsgKind) {
    throw std::runtime_error(
        "kind " + std::to_string(kind) + " is reserved for internal use" +
        "Please use kinds larger than " + std::to_string(kMaxReservedMsgKind));
  }
  userMsgKinds_[kind] = name;
}

bool Airreplay::isReplay() { return rrmode_ == Mode::kReplay; }

void Airreplay::RegisterReproducers(std::map<int, ReproducerFunction> hooks) {
  for (auto it = hooks.begin(); it != hooks.end(); ++it) {
    if (it->first <= kMaxReservedMsgKind) {
      throw std::runtime_error("kind " + std::to_string(it->first) +
                               " is reserved for internal use" +
                               "Please use kinds larger than " +
                               std::to_string(kMaxReservedMsgKind));
    }
  }
  hooks_ = hooks;
}

void Airreplay::RegisterReproducer(int kind, ReproducerFunction reproducer) {
  if (kind <= kMaxReservedMsgKind) {
    throw std::runtime_error(
        "kind " + std::to_string(kind) + " is reserved for internal use" +
        "Please use kinds larger than " + std::to_string(kMaxReservedMsgKind));
  }
  hooks_[kind] = reproducer;
}

airreplay::OpequeEntry Airreplay::NewOpequeEntry(
    const std::string &debugstring, const google::protobuf::Message &request,
    int kind, int linkToken) {
  airreplay::OpequeEntry header;
  header.set_kind(kind);
  header.set_link_to_token(linkToken);
  // early dev debug mode: populate Any and do not bother with second payload
  if (request.IsInitialized()) {
    // exception will be thrown if request is serialized when it has unfilled
    // required fields relevant for GetNodeInstanceResponsePB
#ifdef USE_OLD_PROTOBUF
    size_t reqLen = request.ByteSize();
#else
    size_t reqLen = request.ByteSizeLong();
#endif
    header.set_body_size(reqLen);
    *header.mutable_rr_debug_string() = debugstring;
    header.mutable_message()->PackFrom(request);
  }
  return header;
}

// todo:: move to external_replayer.cc
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

  auto callback = [=]() {
    hooks_[req_peek.kind()](req_peek.connection_info(), req_peek.message());
  };
  auto running_callback = std::thread(callback);
  // q:: does std::move do something here?
  running_callbacks_.push_back(std::move(running_callback));
  return true;
}

// *** Core AirReplay ***
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
    header.set_kind(kSaveRestore);
    *header.mutable_rr_debug_string() = key;
    if (str_message != nullptr) {
      if (utils::isAscii(*str_message)) {
        *header.mutable_str_message() = *str_message;
      } else {
        *header.mutable_bytes_message() = *str_message;
      }
    }

    if (int_message != nullptr) {
      header.set_num_message(*int_message);
    }

    if (proto_message != nullptr) {
      if (proto_message->IsInitialized()) {
#if USE_OLD_PROTOBUF
        size_t len = proto_message->ByteSize();
#else
        size_t len = proto_message->ByteSizeLong();
#endif
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

      if (req.kind() != kSaveRestore || req.rr_debug_string() != key) {
        if (!MaybeReplayExternalRPCUnlocked(req)) {
          if (req.kind() != kSaveRestore) {
            log("SaveRestoreInternal@" + std::to_string(pos),
                "not the right kind. expected: " +
                    MessageKindName(kSaveRestore) + " got tracePeek:" +
                    MessageKindName(req.kind()) + ")\t\tkey: " + key);
          } else {
            log("SaveRestoreInternal@" + std::to_string(pos),
                "saverestore: not the right key. expected: " +
                    req.rr_debug_string() + " called with: " + key);
          }
        }
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        continue;
      }

      // determine whether the save-restored value was numeric, string or proto,
      // and recover it accordingly
      if (str_message != nullptr) {
        // c++ str may be either valid ascii or not valid ascii
        assert(!(req.str_message().empty() && req.bytes_message().empty()));
#if USE_OLD_PROTOBUF
        assert(req.message().ByteSize() == 0);
#else
        assert(req.message().ByteSizeLong() == 0);
#endif

        *str_message = !req.str_message().empty() ? req.str_message()
                                                  : req.bytes_message();
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
      log("SaveRestoreInternal@" + std::to_string(pos),
          "just SaveRESTORED " + req.ShortDebugString());

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
                            const std::string &connection_info,
                            const google::protobuf::Message &message, int kind,
                            const std::string &debug_info) {
  if (rrmode_ == Mode::kRecord) {
    std::lock_guard lock(recordOrder_);

    airreplay::OpequeEntry header;
    if (kind == 0) {
      kind = kDefault;
    }
    header.set_kind(kind);
    header.set_rr_debug_string(key);
    header.set_connection_info(connection_info);

    if (message.IsInitialized()) {
#if USE_OLD_PROTOBUF
      size_t mlen = message.ByteSize();
#else
      size_t mlen = message.ByteSizeLong();
#endif
      header.set_body_size(mlen);
      header.mutable_message()->PackFrom(message);
    }
    return trace_.Record(header);
  } else {
    int num_replay_attempts = 0;
    int pos = -1;
    while (true) {
      // if I keep trying to replay the same message without making progres,
      // there must be bug or there is non-determinism in the application that
      // was not instrumented DCHECK prints a stack trace and helps me go patch
      // the non-determinism in the application
      DCHECK(num_replay_attempts < 400);

      if (num_replay_attempts > 20) {
        DLOG(ERROR) << "Replay attempt " << num_replay_attempts
                    << " for key: " << key << " kind: " << kind
                    << " connection_info: " << connection_info
                    << " message: " << message.ShortDebugString();
      }
      num_replay_attempts++;

      std::unique_lock lock(recordOrder_);
      const airreplay::OpequeEntry &req_peek = trace_.PeekNext(&pos);
      if (req_peek.kind() != kind) {
        log("RecordReplay@" + std::to_string(pos),
            "not the right kind expected: " + MessageKindName(req_peek.kind()) +
                " called with: " + MessageKindName(kind) + "\t\tkey: " + key);
      } else if (key != req_peek.rr_debug_string()) {
        log("RecordReplay@" + std::to_string(pos),
            "right kind(" + MessageKindName(kind) +
                ") but not the right entry\texpected key:" +
                req_peek.rr_debug_string() + " but was called with:" + key);
      } else if (connection_info != req_peek.connection_info()) {
        log("RecordReplay@" + std::to_string(pos),
            "right kind and entry key. wrong connection info. expected: " +
                req_peek.connection_info() +
                " called with: " + connection_info);
      } else if (req_peek.message().value() != message.SerializeAsString()) {
        auto mismatch =
            utils::compareMessageWithAny(message, req_peek.message());
        assert(mismatch != "");
        log("RecordReplay@" + std::to_string(pos),
            "right kind and entry key and connection. wrong proto message. " +
                mismatch);
      } else {
        assert(req_peek.kind() == kind);
        assert(req_peek.rr_debug_string() == key);
        assert(req_peek.connection_info() == connection_info);
        assert(req_peek.message().value() == message.SerializeAsString());

        log("RecordReplay@" + std::to_string(pos),
            "Just REPLAYED" + req_peek.ShortDebugString());
        trace_.ConsumeHead(req_peek);
        assert(lock.owns_lock());
        return pos;
      }

      lock.unlock();
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      continue;
    }
  }
}
}  // namespace airreplay

// specific to kudu stuff
namespace kudu {
namespace rrsupport {

// in replay this acts the event loop that during recording is provided by libev
// and sockets
std::mutex mockCallbackerMutex;
std::map<std::string, std::function<void()>> mockCallbacker;

}  // namespace rrsupport
}  // namespace kudu