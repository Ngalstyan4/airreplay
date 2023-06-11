#ifndef AIRREPLAY_H
#define AIRREPLAY_H
#include <fstream>
#include <functional>
#include <string>
#include <memory>
#include <deque>
#include <google/protobuf/any.pb.h>
#include "airreplay/airreplay.pb.h"
namespace kudu
{
namespace rpc
{
class OutboundCall;
} // namespace rpc
} // namespace kudu

namespace airreplay
{
enum Mode { RECORD, REPLAY };

enum RecordKind {
	INCOMING,
	OUTGOING
	/*
        maybe this should be
        INCOMING_REQUEST
        INCOMING_RESPONSE
        OUTGOING_REQUEST
        OUTGOING_RESPONSE
        */
};
class AiRR {
	// same as the static interface below but allows for multiple independent recordings in the same app
	// used for testing mainly
	AiRR(std::string tracename, Mode mode);
};

void init(int port, Mode mode);
bool isReplay();
std::deque<airreplay::OpequeEntry> getTrace();
//todo:: drop const or even support moving later if need be
void setReplayHooks(
	std::map<int,
		 std::function<void(const google::protobuf::Message &msg)> >
		hooks);
// todo:: turn callback argument into a template to make calling less awkward?
bool airr(const std::string &method, const google::protobuf::Message &request,
	  google::protobuf::Message &response,
	  std::function<void(google::protobuf::Message &)> callback,
	  int kind = 0);

bool airr(const std::string &method, const google::protobuf::Message &request,
	  google::protobuf::Message *response = nullptr, int kind = 0);
void recordOutboundCall(const std::string &method,
			const google::protobuf::Message &request,
			std::shared_ptr<kudu::rpc::OutboundCall> call);
} // namespace airreplay

#endif