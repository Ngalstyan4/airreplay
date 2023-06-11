
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

  // in record mode, delete the files if they exist. so the following 2 lines
  // create new files
  if (mode == Mode::RECORD) {
    std::remove(txttracename_.c_str());
    std::remove(tracename_.c_str());
  }

  tracetxt = new std::fstream(txttracename_.c_str(),
                              std::ios::in | std::ios::out | std::ios::app);
  tracebin = new std::fstream(tracename_.c_str(),
                              std::ios::in | std::ios::out | std::ios::app);

  if (RRmode_ == Mode::REPLAY) {
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

  externalReplayerThread_ = std::thread(&Airreplay::externalReplayerLoop, this);
}

Airreplay::~Airreplay() {
  if (externalReplayerThread_.joinable()) {
    externalReplayerThread_.join();
  }
  tracetxt->close();
  tracebin->close();
}

std::deque<airreplay::OpequeEntry> Airreplay::getTrace() {
  return traceEvents_;
}

bool Airreplay::isReplay() { return RRmode_ == Mode::REPLAY; }

std::string Airreplay::txttracename() { return txttracename_; }

std::string Airreplay::tracename() { return tracename_; }

void Airreplay::setReplayHooks(
    std::map<int, std::function<void(const google::protobuf::Message &msg)> >
        hooks) {
  hooks_ = hooks;
}

void Airreplay::airr_record(const std::string &debugstring,
                            const google::protobuf::Message &request,
                            int kind) {
  // trace.push_back(any.ShortDebugString());

  airreplay::OpequeEntry header;
  size_t reqLen = request.ByteSizeLong();
  header.set_kind(kind);
  header.set_body_size(reqLen);
  // early dev debug mode: populate Any and do not bother with second payload
  header.mutable_message()->PackFrom(request);

  *tracetxt << debugstring << " " << header.message().ShortDebugString()
            << std::endl
            << std::flush;

  auto hdr_len = header.ByteSizeLong();
  tracebin->write((char *)&hdr_len, sizeof(size_t));
  header.SerializeToOstream(tracebin);
  // when perf becomes more important, will get rid of any recording above, will
  // add the following and will reconstruct any in replay
  // request.SerializeToOstream(tracebin);

  tracetxt->flush();
  tracebin->flush();
}

bool Airreplay::rr(const std::string &method,
                   const google::protobuf::Message &request,
                   google::protobuf::Message &response,
                   std::function<void(google::protobuf::Message &)> callback,
                   int kind) {
  if (RRmode_ == Mode::RECORD) {
    airr_record(method, request, kind);
  } else {
    assert(!traceEvents_.empty());
    airreplay::OpequeEntry req = traceEvents_[0];
    if (req.message().value() != request.SerializeAsString()) {
      throw std::runtime_error("async replay diverged");
    }
    traceEvents_.pop_front();
    assert(!traceEvents_.empty());
    req = traceEvents_[0];

    auto ok = req.message().UnpackTo(&response);
    assert(ok);
    // traceEvents_.pop_front(); <<-- do not pop response. callback will get
    // back here

    if (callback != nullptr) {
      std::cerr << "calling callback with " << response.DebugString()
                << std::endl;
      callback(response);
    }

    if (traceEvents_.size() > 0) {
      req = traceEvents_[0];
      std::cerr << "there are " << hooks_.size() << " hooks" << std::endl
                << req.kind() << " " << hooks_.begin()->first << std::endl;
      if (hooks_.find(req.kind()) != hooks_.end()) {
        std::cerr << "calling hook for " << req.DebugString() << std::endl;
        hooks_[req.kind()](req.message());
      }
    }
  }
  return true;
}

bool Airreplay::rr(const std::string &debugstring,
                   const google::protobuf::Message &request,
                   google::protobuf::Message *response, int kind) {
  if (RRmode_ == Mode::RECORD) {
    airr_record(debugstring, request, kind);

  } else {
    assert(traceEvents_.size() > 0);

    airreplay::OpequeEntry req = traceEvents_[0];
    if (req.message().value() != request.SerializeAsString()) {
      throw std::runtime_error("replay diverged");
    }
    traceEvents_.pop_front();
    // assert(!traceEvents_.empty());
    if (traceEvents_.empty()) {
      return true;  // currently this is called with responses as well.. in
                    // which case there is no response-repsonse}
    }
    req = traceEvents_[0];
    if (response != nullptr) {
      bool ok = req.message().UnpackTo(response);
      assert(ok);
      traceEvents_.pop_front();
    }

    // assert(!traceEvents_.empty());
    if (!traceEvents_.empty()) {
      req = traceEvents_[0];
      std::cerr << "there are " << hooks_.size() << " hooks" << std::endl
                << req.kind() << " " << hooks_.begin()->first << std::endl;
      if (hooks_.find(req.kind()) != hooks_.end()) {
        std::cerr << "calling hook for " << req.DebugString() << std::endl;
        hooks_[req.kind()](req.message());
      }
    }
  }
  return true;
}

void Airreplay::recordOutboundCall(
    const std::string &debugstring, const google::protobuf::Message &request,
    std::shared_ptr<kudu::rpc::OutboundCall> call) {
  rr(debugstring, request);
  if (RRmode_ == Mode::REPLAY) {
    // todo:: consider call->call_id() as key
    outstandingCalls_[request.ShortDebugString()] = call;
    // ^^ the replayer loop will be a thread here that will monitor
    // outstandingCalls_ and the trace and will call appropriate callbacks
    // outstandingCalls_[request.ShortDebugString()]->CallCallback();
  }
}

}  // namespace airreplay
