#ifndef TRACE_H
#define TRACE_H
#include <atomic>
#include <deque>
#include <fstream>
#include <thread>

#include "airreplay.pb.h"

namespace airreplay {
enum Mode { kRecord, kReplay };

// group of traces, used to figure out what to replay as a as server
class TraceGroup {
 public:
  TraceGroup();
  TraceGroup(std::vector<std::deque<airreplay::OpequeEntry>> traces);

  void AddTrace(std::deque<airreplay::OpequeEntry> &&trace);
  // returns true if all members of the trace group have a Socket Read at the
  // next position
  bool AllEmpty();
  bool NextIsReadOrEmpty();
  // goes through referenced traces and advances all traces that had a read
  // operation with the exact value corresponding to the passed buffer. all
  // traces that had a read with a different value are dropped from the current
  // TraceGroup
  void ConsumeRead(uint8_t *buffer, int len);
  // returns true if all members of the trace group have a Socket Write at the
  // next position
  bool NextIsWrite();
  // Returns a prefix (not necessarily longest!) Write() operaration that is
  // common among all traces
  // never returns zero, throws if it had to return zero
  // so, clients are expected to check whether a common write is available via a
  // NextIsWrite Call
  int NextCommonWrite(uint8_t *buffer, int buffer_len);

 private:
  std::vector<std::deque<airreplay::OpequeEntry>> traces_;
  int pos_ = 0;

  bool NextIs(std::string debug_string_prefix, bool empty_is_ok);
};

// Single-threaded trace representation
// assumes external synchronization to ensure exactly one member function is
// envoked at a time
class Trace {
 public:
  Trace(std::string &traceprefix, Mode mode, bool overwrite = true);
  Trace(const Trace &) = delete;
  Trace &operator=(const Trace &) = delete;
  Trace(Trace &&) = default;
  Trace &operator=(Trace &&) = default;
  ~Trace();

  std::string tracename();
  std::size_t size();
  bool isReplay();
  int pos();
  int Record(const airreplay::OpequeEntry &header);
  int Record(const std::string &payload, const std::string &debug_string = "");
  bool HasNext();
  const OpequeEntry &PeekNext(int *pos);
  OpequeEntry ReplayNext(int *pos);
  OpequeEntry ReplayNext(int *pos, const airreplay::OpequeEntry &expectedNext);

  // asserts that expectedHead is the next message in the trace and consumes it
  void ConsumeHead(const OpequeEntry &expectedHead);
  bool SoftConsumeHead(const OpequeEntry &expectedHead);
  // Utility function used to coalsece sequential socket reads and sequential
  // socket writes in replay. must be called right after construction  (pos_ =
  // 0)
  void Coalesce();
  // todo:: make this private again and add a proper getter.
  // made it public to use in TraceGroup. Had some copy/move constructor issues
  // and could not use Trace as a result.
  // partially parsed(proto::Any) trace events for replay
  std::deque<airreplay::OpequeEntry> traceEvents_;

 private:
  Mode mode_;
  std::string txttracename_;
  std::string tracename_;
  std::fstream *tracetxt_;
  std::fstream *tracebin_;
  airreplay::OpequeEntry *soft_consumed_;

  // the index of the next message to be recorded or replayed
  int pos_ = 0;
  std::thread debug_thread_;
  // used by Trace destructor to terminate the debug thread
  std::atomic<bool> debug_thread_exit_ = false;
  void DebugThread(const std::atomic<bool> &do_exit);
};

}  // namespace airreplay

#endif /* TRACE_H */