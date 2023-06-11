
#include "airreplay.h"
#include "airreplay/airreplay.pb.h"
#include <deque>

namespace airreplay
{
namespace
{
// std::vector<std::string> trace;
// because ofstream holds onto the string it is passed
std::fstream *tracetxt;
std::fstream *tracebin;

// mode is record or replay
Mode RRmode_;
std::map<int, std::function<void(const google::protobuf::Message &msg)> >
	hooks_;

// partially parsed(proto::Any) trace events for replay
std::deque<airreplay::OpequeEntry> traceEvents_;

// outstanding Outbound calls
std::map<std::string, std::shared_ptr<kudu::rpc::OutboundCall> >
	outstandingCalls_;
} // namespace

void init(int port, Mode mode)
{
	RRmode_ = mode;
	std::string name("namespace_consensus_service_requests");
	name.append(std::to_string(port));
	std::string bname = name;
	name.append(".txt");
	bname.append(".bin");
	if (mode == Mode::RECORD) {
		std::remove(name.c_str());
		std::remove(bname.c_str());
	}
	tracetxt = new std::fstream(name.c_str(), std::ios::in | std::ios::out |
							  std::ios::app);
	tracebin = new std::fstream(
		bname.c_str(), std::ios::in | std::ios::out | std::ios::app);

	if (RRmode_ == Mode::REPLAY) {
		tracebin->seekg(0, std::ios::beg);
		// replay mode
		ssize_t nread;
		airreplay::OpequeEntry header;
		size_t headerLen;
		while (tracebin->peek() /*force eof to be set if next byte is eof*/
		       ,
		       !tracebin->eof()) {
			tracebin->read((char *)&headerLen, sizeof(size_t));
			nread = tracebin->gcount();
			if (nread < sizeof(size_t)) {
				throw std::runtime_error(
					"trace file is corrupted " +
					std::to_string(nread));
			}
			char *buf = new char[headerLen];
			tracebin->read(buf, headerLen);
			nread = tracebin->gcount();
			if (nread < headerLen) {
				throw std::runtime_error(
					"trace file is corrupted buffer " +
					std::to_string(nread));
			}
			if (!header.ParseFromArray(buf, headerLen)) {
				throw std::runtime_error(
					"trace file is corrupted ");
			}
			traceEvents_.push_back(header);
		}
	}
}

std::deque<airreplay::OpequeEntry> getTrace()
{
	return traceEvents_;
}

bool isReplay()
{
	return RRmode_ == Mode::REPLAY;
}
void setReplayHooks(
	std::map<int,
		 std::function<void(const google::protobuf::Message &msg)> >
		hooks)
{
	hooks_ = hooks;
}

void airr_record(const std::string &debugstring,
		 const google::protobuf::Message &request, int kind)
{
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
	// when perf becomes more important, will get rid of any recording above, will add the following and
	// will reconstruct any in replay
	// request.SerializeToOstream(tracebin);

	tracebin->flush();
}

bool airr(const std::string &method, const google::protobuf::Message &request,
	  google::protobuf::Message &response,
	  std::function<void(google::protobuf::Message &)> callback, int kind)
{
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

bool airr(const std::string &debugstring,
	  const google::protobuf::Message &request,
	  google::protobuf::Message *response, int kind)
{
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
			std::cerr << "calling hook for " << req.DebugString()
				  << std::endl;
			hooks_[req.kind()](req.message());
		}
	}
	return true;
}

void recordOutboundCall(const std::string &debugstring,
			const google::protobuf::Message &request,
			std::shared_ptr<kudu::rpc::OutboundCall> call)
{
	airr(debugstring, request);
	if (RRmode_ == Mode::REPLAY) {
		// todo:: consider call->call_id() as key
		outstandingCalls_[request.ShortDebugString()] = call;
		// ^^ the replayer loop will be a thread here that will monitor outstandingCalls_ and the trace and will call appropriate callbacks
		// outstandingCalls_[request.ShortDebugString()]->CallCallback();
	}
}
} // namespace airreplay
