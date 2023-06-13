
#include "airreplay.h"

#include <cassert>
#include <deque>
#include <thread>

#include "airreplay/airreplay.pb.h"

namespace airreplay {
Airreplay *airr;

Airreplay::Airreplay(std::string tracename, Mode mode) {
  RRmode_ = mode;
  std::string txtname = tracename;
  txtname.append(".txt");
  tracename.append(".bin");
  txttracename_ = txtname;
  tracename_ = tracename;

  // in record mode, we delete the files with tracename if they already exist.
  if (mode == Mode::kRecord) {
    std::remove(txttracename_.c_str());
    std::remove(tracename_.c_str());
  }

  tracetxt = new std::fstream(txttracename_.c_str(),
                              std::ios::in | std::ios::out | std::ios::app);
  tracebin = new std::fstream(tracename_.c_str(),
                              std::ios::in | std::ios::out | std::ios::app);

  if (RRmode_ == Mode::kReplay) {
    tracebin->seekg(0, std::ios::beg);

    ssize_t nread;
    airreplay::OpequeEntry header;
    size_t headerLen;
    while (tracebin->peek() /*force eof to be set if next byte is eof*/
           ,
           !tracebin->eof()) {
      tracebin->read((char *)&headerLen, sizeof(size_t));
      nread = tracebin->gcount();
      if (nread < sizeof(size_t)) {
        throw std::runtime_error("trace file is corrupted " +
                                 std::to_string(nread));
      }
      char *buf = new char[headerLen];
      tracebin->read(buf, headerLen);
      nread = tracebin->gcount();
      if (nread < headerLen) {
        throw std::runtime_error(
            "trace file is corrupted "
            "buffer " +
            std::to_string(nread));
      }
      if (!header.ParseFromArray(buf, headerLen)) {
        throw std::runtime_error("trace file is corrupted ");
      }
      traceEvents_.push_back(header);
    }
  }

  //   externalReplayerThread_ = std::thread(&Airreplay::externalReplayerLoop,
  //   this);
}

Airreplay::~Airreplay() {
  //   if (externalReplayerThread_.joinable()) {
  //     externalReplayerThread_.join();
  //   }
  tracetxt->close();
  tracebin->close();
}

std::deque<airreplay::OpequeEntry> Airreplay::getTraceForTest() {
  return traceEvents_;
}

bool Airreplay::isReplay() { return RRmode_ == Mode::kReplay; }

std::string Airreplay::txttracename() { return txttracename_; }

std::string Airreplay::tracename() { return tracename_; }

void Airreplay::setReplayHooks(
    std::map<int, std::function<void(const google::protobuf::Message &msg)> >
        hooks) {
  hooks_ = hooks;
}

int Airreplay::doRecord(const std::string &debugstring,
                        const google::protobuf::Message &request, int kind,
                        int linkToken) {
  if (kind != 42) return 0;
  // trace.push_back(any.ShortDebugString());

  airreplay::OpequeEntry header;
  size_t reqLen = request.ByteSizeLong();
  header.set_kind(kind);
  header.set_link_to_token(linkToken);
  // early dev debug mode: populate Any and do not bother with second payload
  if (request.IsInitialized()) {
    // exception will be thrown if request is serialized when it has unfilled
    // required fields relevant for GetNodeInstanceResponsePB
    header.set_body_size(reqLen);
    header.mutable_message()->PackFrom(request);
  }

  *tracetxt << debugstring << " " << header.ShortDebugString() << std::endl
            << std::flush;
  auto hdr_len = header.ByteSizeLong();
  tracebin->write((char *)&hdr_len, sizeof(size_t));
  header.SerializeToOstream(tracebin);
  // when perf becomes more important, will get rid of any recording above, will
  // add the following and will reconstruct any in replay
  // request.SerializeToOstream(tracebin);

  tracetxt->flush();
  tracebin->flush();
  return recordIndex_++;
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
    const std::string &method,
    const google::protobuf::Message &request,
    google::protobuf::Message *response, std::function<void()> callback) {
  if (RRmode_ == Mode::kRecord) {
    // make sure that one thread gets here at a time.
    // eventually this will be enforced structurally (given we do rr in the
    // right places) and have appropriate app-level locks held for now this
    // ensured the debug txt trace does not get corrupted when rr is called from
    // multiple threads.
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
          std::cerr << "my callback successfully called !!";
          this->FinishRecord(recordToken, response);
          callback();
        });
    return myCallback;
  } else {
    std::cerr << "RrAsync replay! consuming the request\n";
    int recordToken = rr(method, request, 42);
    {
      std::cerr << "Acquiring lock to insert pending callback with recordToken "
                << recordToken << "\n";
      // q:: rr may replay more than one request (if there are other pending
      // responses)
      //  hope that is ok.. check later
      std::lock_guard lock(recordOrder_);
      bool insertSuccess =
          pendingCallbacks_.insert({recordToken, callback}).second;
      assert(insertSuccess);
    }
    std::cerr << "Trying to replay external RPC\n";
    MaybeReplayExternalRPC(response);
    return kUnreachableCallback_;
  }
}

void Airreplay::FinishRecord(int recordToken,
                             const google::protobuf::Message *response) {
  assert(response != nullptr);
  std::lock_guard lock(recordOrder_);
  doRecord("finishRecord", *response, 42, recordToken);
}

void Airreplay::MaybeReplayExternalRPC(google::protobuf::Message *response) {
  // replay external RPCs
  if (traceEvents_.size() > 0) {
    airreplay::OpequeEntry req = traceEvents_[0];
    if (hooks_.find(req.kind()) != hooks_.end()) {
      // q:: does this make horriffic stack traces?
      // if I have 15 incoming calls one after another, this will add 30
      // frames (15*(rr, hooks[kind])) calls to the stack
      hooks_[req.kind()](req.message());
    }

    int tokenLink = req.link_to_token();
    auto elem = pendingCallbacks_.find(tokenLink);
    std::cerr << "looking for tokenLink " << tokenLink << " "
              << req.ShortDebugString() << "\n";
    if (tokenLink != -1 && elem != pendingCallbacks_.end()) {
      auto f = elem->second;
      std::cerr << "calling callback\n";
      // unlike the hooks approach above there is no further airreplay
      // involvement after f() is called so we are done with req and should move
      // on
      {
        std::lock_guard lock(recordOrder_);
        traceEvents_.pop_front();
        recordIndex_++;
      }
      if (response != nullptr) {
        std::cerr <<"unpacking response!\n";
        req.message().UnpackTo(response);
      } else {
        std::cerr << "WARNING! null response\n";
      }
      f();
    }
  }
}

int Airreplay::rr(const std::string &method,
                  const google::protobuf::Message &request, int kind) {
  if (RRmode_ == Mode::kRecord) {
    // make sure that one thread gets here at a time.
    // eventually this will be enforced structurally (given we do rr in the
    // right places) and have appropriate app-level locks held for now this
    // ensured the debug txt trace does not get corrupted when rr is called from
    // multiple threads.
    std::lock_guard lock(recordOrder_);
    return doRecord(method, request, kind);
  } else {
    assert(!traceEvents_.empty());
    airreplay::OpequeEntry req = traceEvents_[0];
    if (req.message().value() != request.SerializeAsString()) {
      throw std::runtime_error("async replay diverged");
    }
    traceEvents_.pop_front();
    int ret = recordIndex_++;
    std::cerr << "just consumed a request " << req.ShortDebugString() << "\n";

    MaybeReplayExternalRPC();

    return ret;
  }
}

// void Airreplay::recordOutboundCall(
//     const std::string &debugstring, const google::protobuf::Message &request,
//     std::shared_ptr<kudu::rpc::OutboundCall> call) {
//   rr(debugstring, request);
//   if (RRmode_ == Mode::kReplay) {
//     // todo:: consider call->call_id() as key
//     outstandingCalls_[request.ShortDebugString()] = call;
//     // ^^ the replayer loop will be a thread here that will monitor
//     // outstandingCalls_ and the trace and will call appropriate callbacks
//     // outstandingCalls_[request.ShortDebugString()]->CallCallback();
//   }
// }

}  // namespace airreplay
