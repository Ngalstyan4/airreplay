#include "trace.h"

#include "airreplay/airreplay.pb.h"

namespace airreplay {

Trace::Trace(std::string &traceprefix, Mode mode) : mode_(mode) {
  txttracename_ = traceprefix + ".txt";
  tracename_ = traceprefix + ".bin";
  pos_ = 0;

  if (mode == Mode::kRecord) {
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
        throw std::runtime_error("trace file is corrupted. parsed" + std::to_string(traceEvents_.size()) + " events");
      }
      traceEvents_.push_back(header);
    }
    std::cerr << "trace parsed " << traceEvents_.size()
              << " events for replay \n";
    const std::atomic<bool> &do_exit = debug_thread_exit_;
    //q:: why do I need ref at &do_exit when in everywhere else I do not need that?
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
int Trace::pos() { return pos_; }
// std::deque<airreplay::OpequeEntry> Airreplay::getTraceForTest() {
//   return traceEvents_;
// }
// std::string Airreplay::txttracename() { return txttracename_; }

int Trace::Record(const airreplay::OpequeEntry &header) {
  assert(mode_ == Mode::kRecord);
  *tracetxt_ << header.ShortDebugString() << std::endl;
  auto hdr_len = header.ByteSizeLong();
  tracebin_->write((char *)&hdr_len, sizeof(size_t));
  header.SerializeToOstream(tracebin_);
  // when perf becomes more important, will get rid of any recording above, will
  // add the following and will reconstruct any in replay
  // request.SerializeToOstream(tracebin);

  tracetxt_->flush();
  tracebin_->flush();
  return pos_++;
}

bool Trace::HasNext() { return !traceEvents_.empty(); }

const OpequeEntry &Trace::PeekNext(int *pos) {
  assert(mode_ == Mode::kReplay);
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

}  // namespace airreplay