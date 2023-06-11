
#include "airreplay.h"

#include <deque>

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
  tracetxt = new std::fstream(txttracename_.c_str(),
                              std::ios::in | std::ios::out | std::ios::app);
  tracebin = new std::fstream(tracename_.c_str(),
                              std::ios::in | std::ios::out | std::ios::app);

  if (mode == Mode::RECORD) {
    std::remove(txttracename_.c_str());
    std::remove(tracename_.c_str());
  }

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
}

Airreplay::~Airreplay() {
  tracetxt->close();
  tracebin->close();
}

Airreplay::replayerThread() {}

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
    airreplay::OpequeEntry req = traceEvents_[0];
    if (req.message().value() != request.SerializeAsString()) {
      throw std::runtime_error("async replay diverged");
    }
    traceEvents_.pop_front();
    req = traceEvents_[0];

    req.message().UnpackTo(&response);
    traceEvents_.pop_front();

    if (callback != nullptr) {
      callback(response);
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
    airreplay::OpequeEntry req = traceEvents_[0];
    if (req.message().value() != request.SerializeAsString()) {
      throw std::runtime_error("replay diverged");
    }
    traceEvents_.pop_front();
    req = traceEvents_[0];
    if (response != nullptr) {
      req.message().UnpackTo(response);
    }
    traceEvents_.pop_front();

    req = traceEvents_[0];
    if (hooks_.find(req.kind()) != hooks_.end()) {
      std::cerr << "calling hook for " << req.DebugString() << std::endl;
      hooks_[req.kind()](req.message());
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
