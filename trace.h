#ifndef TRACE_H
#define TRACE_H
#include <deque>
#include <fstream>
#include <thread>
#include <atomic>

#include "airreplay/airreplay.pb.h"

namespace airreplay {
enum Mode { kRecord, kReplay };

// Single-threaded trace representation
// assumes external synchronization to ensure exactly one member function is
// envoked at a time
class Trace {
 public:
  Trace(std::string &traceprefix, Mode mode);
  ~Trace();

  std::string tracename();
  int pos();
  int Record(const airreplay::OpequeEntry &header);
  bool HasNext();
  const OpequeEntry &PeekNext(int *pos);
  OpequeEntry ReplayNext(int *pos);
  OpequeEntry ReplayNext(int *pos, const airreplay::OpequeEntry &expectedNext);

  // asserts that expectedHead is the next message in the trace and consumes it
  void ConsumeHead(const OpequeEntry &expectedHead);
  bool SoftConsumeHead(const OpequeEntry &expectedHead);

 private:
  Mode mode_;
  std::string txttracename_;
  std::string tracename_;
  std::fstream *tracetxt_;
  std::fstream *tracebin_;
  airreplay::OpequeEntry  *soft_consumed_;

  // partially parsed(proto::Any) trace events for replay
  std::deque<airreplay::OpequeEntry> traceEvents_;

  // the index of the next message to be recorded or replayed
  int pos_  = 0;
  std::thread debug_thread_;
  // used by Trace destructor to terminate the debug thread
  std::atomic<bool> debug_thread_exit_ = false;
  void DebugThread(const std::atomic<bool> &do_exit);
};

}  // namespace airreplay

#endif /* TRACE_H */