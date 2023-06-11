#include <functional>
#include <gtest/gtest.h>
#include "airreplay/airreplay.h"
#include "airreplay/airreplay.pb.h"

using airreplay::PingPongRequest;
using airreplay::PingPongResponse;
enum RPCKind { INCOMING_ASYNC_PINGGONG = 1, INCOMING_SYNC_PINGGONG, OUTGOING };
class MockSystemNode {
    private:
	std::vector<std::weak_ptr<MockSystemNode> > neighbors_;
	bool isRecorded_;

    public:
	MockSystemNode(bool isRecorded = false) : isRecorded_(isRecorded)
	{
	}

	void addNeighbor(std::weak_ptr<MockSystemNode> node)
	{
		neighbors_.push_back(std::move(node));
	}

	//RPCs
	void PingPong(const PingPongRequest &request,
		      PingPongResponse *response)
	{

		// print the reqest

		std::cerr << "request is " << request.DebugString()
			  << std::endl;
		isRecorded_ &&airreplay::airr("I was called with", request,
					      response,
					      RPCKind::INCOMING_SYNC_PINGGONG);
		response->set_message("Pong++" + request.message());
		// todo:: rethink this!! a request came in. I SHOULD NOT short sircuit and send the response. I should do the computation and
		// possibly modify internal state. so the next line is wrong. desperate to get RR mockRR working now so fix later
		// maybe add some state the RPC server, write a test that fails with this approach (incoming req modifies state not
		// reflected in the response, then fix it)
		if (!airreplay::isReplay()) {
			isRecorded_ &&airreplay::airr("I responded with",
						      *response);
		}
	}

	void AsyncPingPong(const PingPongRequest &request)
	{
		PingPongResponse response;
		response.set_message("Pong++" + request.message());
		isRecorded_ &&airreplay::airr("I was called with(async)",
					      request, &response,
					      RPCKind::INCOMING_ASYNC_PINGGONG);

		neighbors_[0].lock()->CallCallback(&response);
		isRecorded_ &&airreplay::airr("I responded with(callback)",
					      response);
	}

	void DispatchPingPong(const PingPongRequest &request,
			      PingPongResponse *response)
	{
		isRecorded_ &&airreplay::airr("I am requesting", request,
					      response);
		if (!airreplay::isReplay()) {
			neighbors_[0].lock()->PingPong(request, response);
			isRecorded_ &&airreplay::airr("I was responded with",
						      *response);
		}
	}

	void
	DispatchAsyncPingPong(const PingPongRequest &request,
			      std::function<void(PingPongResponse *)> callback)
	{
		callback_ = callback;
		PingPongResponse response;
		isRecorded_ &&airreplay::airr(
			"I am async requesting", request, response,
			[this](google::protobuf::Message &resp) {
				callback_(
					static_cast<PingPongResponse *>(&resp));
			});
		// todo:: find a way to call the callback in replay
		if (!airreplay::isReplay()) {
			neighbors_[0].lock()->AsyncPingPong(request);
		}
	}

	std::function<void(PingPongResponse *)> callback_;

	void CallCallback(PingPongResponse *response)
	{
		isRecorded_ &&airreplay::airr("I was async responded with",
					      *response);
		if (!airreplay::isReplay()) {
			callback_(response);
		}
	}
};

struct ReqResPair {
	PingPongRequest request;
	PingPongResponse response;
};

// Define a fixture for the test suite
class PingPongTest : public ::testing::Test {
    protected:
	std::shared_ptr<MockSystemNode> node1_;
	std::shared_ptr<MockSystemNode> node2_;
	// Set up the test environment
	void SetUp() override
	{
		node1_ = std::make_shared<MockSystemNode>(true);
		node2_ = std::make_shared<MockSystemNode>();
		node1_->addNeighbor(node2_);
		node2_->addNeighbor(node1_);
	}

	// Tear down the test environment
	void TearDown() override
	{
		// Stop the RPC server
	}

	void RunNode1(std::vector<ReqResPair> &history)
	{
		PingPongRequest request;
		PingPongResponse response;
		request.set_message("Ping1");
		node1_->DispatchPingPong(request, &response);
		history.push_back({ request, response });
		request.set_message("Ping2");
		node1_->DispatchPingPong(request, &response);
		history.push_back({ request, response });
		node1_->DispatchAsyncPingPong(
			request, [&](PingPongResponse *response) {
				history.push_back({ request, *response });
			});
	}

	void RunNode2(std::vector<ReqResPair> &history)
	{
		PingPongRequest request;
		PingPongResponse response;
		request.set_message("Ping2fromNODE2");
		node2_->DispatchPingPong(request, &response);
		history.push_back({ request, response });
	}
};

// Test case for the ping-pong RPC call
TEST_F(PingPongTest, testCase)
{
	airreplay::init(4444, airreplay::RECORD);
	std::vector<ReqResPair> rec_history1, rec_history2;
	std::vector<ReqResPair> rep_history1;
	RunNode1(rec_history1);
	RunNode2(rec_history2);
	airreplay::init(4444, airreplay::REPLAY);
	EXPECT_TRUE(node2_.unique());
	node2_.reset();
	EXPECT_TRUE(node2_ == nullptr);

	// set up replay hooks
	std::map<int, std::function<void(const google::protobuf::Message &)> >
		hooks;

	// hooks[RPCKind::INCOMING_ASYNC_PINGGONG] = [&](const auto &msg) {
	//     PingPongRequest req;
	//     static_cast<const google::protobuf::Any &>(msg).UnpackTo(&req);
	//     node1_->AsyncPingPong(req);
	// };

	hooks[RPCKind::INCOMING_SYNC_PINGGONG] = [&](const auto &msg) {
		PingPongRequest req;
		PingPongResponse res;
		static_cast<const google::protobuf::Any &>(msg).UnpackTo(&req);
		node1_->PingPong(req, &res);
	};
	airreplay::setReplayHooks(hooks);

	std::cerr << "replaying";
	RunNode1(rep_history1);

	EXPECT_EQ(rec_history1.size(), rep_history1.size());
	for (int i = 0; i < rec_history1.size(); i++) {
		EXPECT_EQ(rec_history1[i].request.DebugString(),
			  rep_history1[i].request.DebugString());
		EXPECT_EQ(rec_history1[i].response.DebugString(),
			  rep_history1[i].response.DebugString());
	}

	// EXPECT_EQ(response.message(), "Pong"); // Replace with the appropriate field and expected value from the response
}