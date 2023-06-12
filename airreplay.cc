
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

//   externalReplayerThread_ = std::thread(&Airreplay::externalReplayerLoop, this);
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

// implements recording side of rr
void Airreplay::doRecord(const std::string &debugstring,
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
                   const google::protobuf::Message &request, int kind) {
  if (RRmode_ == Mode::kRecord) {
    doRecord(method, request, kind);
  } else {
    assert(!traceEvents_.empty());
    airreplay::OpequeEntry req = traceEvents_[0];
    if (req.message().value() != request.SerializeAsString()) {
      throw std::runtime_error("async replay diverged");
    }
    traceEvents_.pop_front();

    // replay external RPCs
    if (traceEvents_.size() > 0) {
      req = traceEvents_[0];
      if (hooks_.find(req.kind()) != hooks_.end()) {
        // q:: does this make horriffic stack traces?
        // if I have 15 incoming calls one after another, this will add 30
        // frames (15*(rr, hooks[kind])) calls to the stack
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
  if (RRmode_ == Mode::kReplay) {
    // todo:: consider call->call_id() as key
    outstandingCalls_[request.ShortDebugString()] = call;
    // ^^ the replayer loop will be a thread here that will monitor
    // outstandingCalls_ and the trace and will call appropriate callbacks
    // outstandingCalls_[request.ShortDebugString()]->CallCallback();
  }
}

}  // namespace airreplay
