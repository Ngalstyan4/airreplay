#include "trace.h"

#include <glog/logging.h>

#include <iomanip>
#include <sstream>

#include "airreplay.pb.h"

namespace airreplay {

Trace::Trace(std::string &traceprefix, Mode mode, bool overwrite)
    : mode_(mode), soft_consumed_(nullptr) {
  if (mode == Mode::kRecord && !overwrite) {
    int i = 0;
    while (std::ifstream(tracename_ + "." + std::to_string(i) + ".bin")) {
      i++;
    }
    tracename_ += "." + std::to_string(i);
    txttracename_ += "." + std::to_string(i);
  }
  txttracename_ = traceprefix + ".txt";
  tracename_ = traceprefix + ".bin";
  pos_ = 0;

  if (mode == Mode::kRecord && overwrite) {
    std::remove(txttracename_.c_str());
    std::remove(tracename_.c_str());
  }

  tracetxt_ = new std::fstream(txttracename_.c_str(),
                               std::ios::in | std::ios::out | std::ios::app);
  tracebin_ = new std::fstream(tracename_.c_str(),
                               std::ios::in | std::ios::out | std::ios::app);

  if (mode == Mode::kReplay) {
    tracebin_->seekg(0, std::ios::beg);

    ssize_t nread;
    airreplay::OpequeEntry header;
    size_t headerLen;
    std::vector<char> buf;
    /*peek() below is to force eof to be set if next byte is eof*/
    while (tracebin_->peek(), !tracebin_->eof()) {
      tracebin_->read((char *)&headerLen, sizeof(size_t));
      nread = tracebin_->gcount();
      if (nread < sizeof(size_t)) {
        throw std::runtime_error("trace file is corrupted " +
                                 std::to_string(nread));
      }
      buf.reserve(headerLen);
      tracebin_->read(buf.data(), headerLen);
      nread = tracebin_->gcount();
      if (nread < headerLen) {
        throw std::runtime_error(
            "trace file is corrupted "
            "buffer " +
            std::to_string(nread));
      }
      if (!header.ParseFromArray(buf.data(), headerLen)) {
        throw std::runtime_error("trace file is corrupted. parsed" +
                                 std::to_string(traceEvents_.size()) +
                                 " events");
      }
      traceEvents_.push_back(header);
    }
    std::cerr << "trace parsed " << traceEvents_.size()
              << " events for replay \n";
    const std::atomic<bool> &do_exit = debug_thread_exit_;
    debug_thread_ = std::thread(&Trace::DebugThread, this, &do_exit);
  }
}

Trace::~Trace() {
  tracetxt_->close();
  tracebin_->close();
  debug_thread_exit_ = true;
  if (debug_thread_.joinable()) {
    debug_thread_.join();
  }
}

std::string Trace::tracename() { return tracename_; }
std::size_t Trace::size() { return traceEvents_.size(); }
bool Trace::isReplay() { return mode_; }
int Trace::pos() { return pos_; }

int Trace::Record(const airreplay::OpequeEntry &header) {
  assert(mode_ == Mode::kRecord);
  *tracetxt_ << header.ShortDebugString() << std::endl;
#ifdef USE_OLD_PROTOBUF
  size_t hdr_len = header.ByteSize();
#else
  size_t hdr_len = header.ByteSizeLong();
#endif
  tracebin_->write((char *)&hdr_len, sizeof(size_t));
  header.SerializeToOstream(tracebin_);

  tracetxt_->flush();
  tracebin_->flush();
  return pos_++;
}

int Trace::Record(const std::string &payload, const std::string &debug_string) {
  assert(mode_ == Mode::kRecord);
  airreplay::OpequeEntry oe;
  *oe.mutable_bytes_message() = payload;
  *oe.mutable_rr_debug_string() = debug_string;
  oe.set_body_size(payload.size());

  return Record(oe);
}

bool Trace::HasNext() { return !traceEvents_.empty(); }

const OpequeEntry &Trace::PeekNext(int *pos) {
  assert(mode_ == Mode::kReplay);
  if (traceEvents_.empty()) {
    std::cerr << "TRACE: \n\n GOT TO THE END OF THE TRACE \n\n";
    throw std::runtime_error("trace is empty");
  }
  assert(!traceEvents_.empty());
  *pos = pos_;
  return traceEvents_.front();
}

OpequeEntry Trace::ReplayNext(int *pos) {
  assert(mode_ == Mode::kReplay);
  assert(!traceEvents_.empty());
  *pos = pos_;
  auto header = traceEvents_.front();
  traceEvents_.pop_front();
  pos_++;
  return header;
}

OpequeEntry Trace::ReplayNext(int *pos,
                              const airreplay::OpequeEntry &expectedNext) {
  assert(mode_ == Mode::kReplay);
  assert(!traceEvents_.empty());
  auto header = ReplayNext(pos);
  if (header.ShortDebugString() != expectedNext.ShortDebugString()) {
    throw std::runtime_error(
        "replay next: expected " + expectedNext.ShortDebugString() + " got " +
        header.ShortDebugString() + " at pos " + std::to_string(pos_));
  }
  return header;
}

void Trace::ConsumeHead(const OpequeEntry &expectedHead) {
  assert(mode_ == Mode::kReplay);
  assert(!traceEvents_.empty());
  OpequeEntry &header = traceEvents_.front();
  assert(&header == &expectedHead);
  traceEvents_.pop_front();
  pos_++;
  if (soft_consumed_ != nullptr) {
    assert(soft_consumed_ == &header);
  }
  soft_consumed_ = nullptr;
}

bool Trace::SoftConsumeHead(const OpequeEntry &expectedHead) {
  assert(mode_ == Mode::kReplay);
  assert(!traceEvents_.empty());
  OpequeEntry &header = traceEvents_.front();
  assert(&header == &expectedHead);
  if (soft_consumed_ != nullptr) {
    assert(soft_consumed_ == &header);
    return false;
  }
  soft_consumed_ = &header;
  return true;
}

void Trace::DebugThread(const std::atomic<bool> &do_exit) {
  assert(mode_ == Mode::kReplay);

  while (!do_exit.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cerr << "At " << pos_ << "/" << traceEvents_.size() << std::endl;
    std::cerr << "Next " << traceEvents_.front().ShortDebugString()
              << std::endl;
  }
}

// todo::: I think this should only be done for reads to make sure
//  we do not accidentally pass more data to the wire than needed
void Trace::Coalesce() {
  assert(mode_ == Mode::kReplay);
  if (traceEvents_.empty()) {
    return;
  }
  int orig_len = traceEvents_.size();
  std::deque<airreplay::OpequeEntry> newTraceEvents;
  newTraceEvents.push_back(traceEvents_.front());
  traceEvents_.pop_front();

  while (!traceEvents_.empty()) {
    auto &reg_header = traceEvents_.front();
    auto &compacted_header = newTraceEvents.back();
    DCHECK(compacted_header.rr_debug_string() == "Socket Read" ||
           compacted_header.rr_debug_string() == "Socket Write" ||
           compacted_header.rr_debug_string().find("Socket writev of") !=
               std::string::npos)
        << "got " << compacted_header.rr_debug_string();
    if (reg_header.rr_debug_string() == compacted_header.rr_debug_string()) {
      compacted_header.set_body_size(compacted_header.body_size() +
                                     reg_header.body_size());
      compacted_header.mutable_bytes_message()->append(
          reg_header.bytes_message());
    } else {
      newTraceEvents.push_back(reg_header);
    }
    traceEvents_.pop_front();
  }
  traceEvents_ = std::move(newTraceEvents);
  LOG(INFO) << "Coalesced " << orig_len << " to " << traceEvents_.size()
            << std::endl;
}

TraceGroup::TraceGroup() {}
void TraceGroup::AddTrace(std::deque<airreplay::OpequeEntry> &&trace) {
  traces_.push_back(std::move(trace));
}

TraceGroup::TraceGroup(std::vector<std::deque<airreplay::OpequeEntry>> traces)
    : traces_(traces) {}

bool TraceGroup::NextIs(std::string val, bool empty_is_ok = true) {
  for (auto &trace : traces_) {
    if (trace.empty()) {
      LOG(INFO) << "TraceGroup::NextIsReadOrEmpty: trace is empty" << std::endl;
      return empty_is_ok;
    }
    auto &header = trace.front();
    if (header.rr_debug_string().find(val) == std::string::npos) {
      return false;
    }
  }
  return true;
}

bool TraceGroup::NextIsWrite() {
  return NextIs("Socket Write", false) || NextIs("Socket writev of", false);
}

bool TraceGroup::NextIsReadOrEmpty() { return NextIs("Socket Read", true); }

bool TraceGroup::AllEmpty() {
  for (const auto &trace : traces_) {
    if (!trace.empty()) {
      return false;
    }
  }
  return true;
}

void TraceGroup::ConsumeRead(uint8_t *buffer, int len) {
  std::vector<std::deque<airreplay::OpequeEntry>> updated_traces;
  bool pop_front = false;
  for (auto &trace : traces_) {
    int remaining_on_head = -1;

    if (trace.empty()) {
      LOG(INFO) << "TraceGroup::ConsumeRead: trace is empty" << std::endl;
      continue;
    }
    auto &header = trace.front();
    if (remaining_on_head == -1) remaining_on_head = header.body_size() - pos_;

    // all traces must agree how large the read is
    if (header.rr_debug_string() != "Socket Read") {
      throw std::runtime_error(
          "TraceGroup::ConsumeRead: header.rr_debug_string() != \"Socket "
          "Read\"");
    }

    if (len > remaining_on_head) {
      LOG(INFO) << "TraceGroup::ConsumeRead: len=" + std::to_string(len) +
                       " > remaining_on_head=" +
                       std::to_string(remaining_on_head)
                << "DROPPING THE TRACE FROM THE GROUP" << std::endl;
      continue;
    }

    if (memcmp(header.bytes_message().substr(pos_).data(), buffer, len) != 0) {
      LOG(INFO) << "TraceGroup::ConsumeRead: "
                   "memcmp(header.bytes_message().data(), buffer, len) != 0"
                << std::endl;
      continue;
    } else {
      LOG(INFO) << "actually found a trace with matching message" << std::endl;
    }

    if (len == remaining_on_head) {
      pop_front = true;
    }
    updated_traces.push_back(std::move(trace));
  }

  if (!pop_front) {
    // we did not drop the head but just consumed len bytes from it. update pos_
    // to reflect that.
    pos_ += len;
  } else {
    for (auto &trace : updated_traces) {
      DCHECK(trace.front().body_size() == pos_ + len);
      trace.pop_front();
    }
    pos_ = 0;
  }

  LOG(INFO) << "TraceGroup:: Updated traces from length " << traces_.size()
            << " to " << updated_traces.size() << std::endl;
  traces_ = std::move(updated_traces);
}

std::string toHex(const std::string &m) {
  std::stringstream ss;
  ss << std::setw(2) << std::setfill('0') << std::hex;
  for (auto c : m) {
    ss << (int)c;
  }
  return ss.str();
}

bool TraceGroup::StillBefore(const airreplay::OpequeEntry &msg) {
  for (auto &trace : traces_) {
    if (trace.empty()) {
      LOG(INFO) << "TraceGroup::StillBefore: trace is empty" << std::endl;
      continue;
    }

    bool in_curr_trace = false;
    for (const auto &header : trace) {

      if (header.bytes_message().find(msg.message().value().substr(
              10, msg.message().value().size() - 12)) != std::string::npos) {
        LOG(INFO) << "TraceGroup::StillBefore: header.rr_debug_string()=" +
                         header.rr_debug_string() +
                         " msg.rr_debug_string()=" + msg.rr_debug_string() +
                         " msg_body=(" + msg.message().value() + ")" +
                         " header_body=(" + header.bytes_message() + ")"
                  << std::endl;
        in_curr_trace = true;
        break;
      }
    }
    if (!in_curr_trace) {
      return false;
    }
  }
  return true;
}

int TraceGroup::NextCommonWrite(uint8_t *buffer, int buffer_len) {
  DCHECK(traces_.size() > 0);
  std::string msg = traces_.front().front().bytes_message();
  DCHECK(msg.size() == traces_.front().front().body_size());

  if (msg.size() > buffer_len) {
    throw std::runtime_error("write size is bigger than buffer");
  }

  for (auto &tarce : traces_) {
    if (tarce.empty()) {
      throw std::runtime_error("trace is empty");
    }
    auto &header = tarce.front();
    if (header.rr_debug_string() != "Socket Write" &&
        header.rr_debug_string().find("Socket writev of") ==
            std::string::npos) {
      throw std::runtime_error("trace is not a write");
    }
    if (header.body_size() != msg.size()) {
      LOG(INFO) << "TraceGroup::NextCommonWrite: header.body_size() =" +
                       std::to_string(header.body_size()) +
                       " != msg.size()=" + std::to_string(msg.size())
                << std::endl;
      throw std::runtime_error("trace is not a write of the same size" +
                               std::to_string(header.body_size()) + " vs " +
                               std::to_string(msg.size()));
    }

    if (memcmp(header.bytes_message().data(), msg.data(), msg.size()) != 0) {
      throw std::runtime_error("trace is not a write of the same data");
    }
  }

  memcpy(buffer, msg.data(), msg.size());
  for (auto &trace : traces_) {
    trace.pop_front();
  }
  return msg.size();
}

}  // namespace airreplay