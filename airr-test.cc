#include <gtest/gtest.h>

#include <functional>

#include "airreplay/airreplay.h"
#include "airreplay/airreplay.pb.h"
#include "kudu/gutil/bind.h"
#include "kudu/gutil/callback.h"

using airreplay::PingPongRequest;
using airreplay::PingPongResponse;
enum RPCKind {
  INCOMING_ASYNC_PINGGONG = 1,
  INCOMING_SYNC_PINGGONG,
  OUTGOING,
  kIncomingCallback
};
class MockSystemNode {
 private:
  std::vector<std::weak_ptr<MockSystemNode> > neighbors_;
  bool isRecorded_;

  // write generic forwarder to airr->rr if isRecorded_ is true
  void RR(const std::string &method,
                   const google::protobuf::Message &request, int kind=0)  {
    if (isRecorded_) {
      airr_->rr(method, request, kind);
    }
  }

 public:
  MockSystemNode() : isRecorded_(false){};
  MockSystemNode(std::unique_ptr<airreplay::Airreplay> airr)
      : airr_(std::move(airr)) {
    if (airr_) {
      isRecorded_ = true;
    } else {
      isRecorded_ = false;
    }
  }

  std::unique_ptr<airreplay::Airreplay> airr_;
  const std::unique_ptr<airreplay::Airreplay> &get_airr() { return airr_; }

  void addNeighbor(std::weak_ptr<MockSystemNode> node) {
    neighbors_.push_back(std::move(node));
  }

  // RPCs
  void PingPong(const PingPongRequest &request, PingPongResponse *response) {
    // print the reqest

    RR("I was called with", request, RPCKind::INCOMING_SYNC_PINGGONG);
    response->set_message("Pong++" + request.message());
    // todo:: rethink this!! a request came in. I SHOULD NOT short sircuit and
    // send the response. I should do the computation and possibly modify
    // internal state. so the next line is wrong. desperate to get RR mockRR
    // working now so fix later maybe add some state the RPC server, write a
    // test that fails with this approach (incoming req modifies state not
    // reflected in the response, then fix it)
    RR("I responded with", *response);
  }

  void AsyncPingPong(const PingPongRequest &request) {
    PingPongResponse response;
    response.set_message("Pong++" + request.message());
    RR("I was called with(async)", request, RPCKind::INCOMING_ASYNC_PINGGONG);

    if (!(airr_  && airr_->isReplay())) {
      neighbors_[0].lock()->CallCallback(&response);
    }
    RR("I responded with(callback)", response);
  }

  // supporting sync RPCs is kind of messy and does not conform with the rest of
  // the api because replay must be provided inside the same function call
  // (requiring the replayer run some event loop for the cases when there are
  // other requests between sync call and sync response) since kudu does not use
  // these, skipping it for now
  void DispatchPingPong(const PingPongRequest &request,
                        PingPongResponse *response) {
    RR("I am requesting", request);
    if (!(airr_  && airr_->isReplay())) {
      neighbors_[0].lock()->PingPong(request, response);
    }
    RR("I was responded with", *response);
  }

  void DispatchAsyncPingPong(const PingPongRequest &request,
                             std::function<void(PingPongResponse *)> callback) {
    callback_ = callback;

    std::cerr << "set callback function to be "
              << reinterpret_cast<void *>(&callback_) << std::endl;
    PingPongResponse response;
    RR("I am async requesting", request);
    // todo:: find a way to call the callback in replay
    if (!(airr_  && airr_->isReplay())) {
      neighbors_[0].lock()->AsyncPingPong(request);
    }
  }

  std::function<void(PingPongResponse *)> callback_;

  void CallCallback(PingPongResponse *response) {
    assert(response != nullptr && "response passed to CallCallback is null");
    std::cerr << "Callcallback calling callback with "
              << response->DebugString() << " " << std::endl;

    RR("I was async responded with", *response,
                            RPCKind::kIncomingCallback);
    assert(callback_ != nullptr && "callback_ is null");
    std::cerr << " callback function is" << reinterpret_cast<void *>(&callback_)
              << std::endl;
    callback_(response);
    std::cerr << "DONE Callcallback calling callback with "
              << response->DebugString() << " " << std::endl;
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
  void SetUp() override {
    auto airr = std::make_unique<airreplay::Airreplay>(
        "class_trace" + std::to_string(4444), airreplay::kRecord);
    node1_ = std::make_shared<MockSystemNode>(std::move(airr));
    node2_ = std::make_shared<MockSystemNode>();
    assert(airr == nullptr);
    node1_->addNeighbor(node2_);
    node2_->addNeighbor(node1_);
  }

  // Tear down the test environment
  void TearDown() override {
    // Stop the RPC server
  }

  void RunNode1(std::vector<ReqResPair> &history) {
    PingPongRequest request;
    auto clbck = [&](PingPongResponse *response) {
      std::cerr << "in Dispactch async ping pong callback "
                << response->DebugString() << std::endl;
      history.push_back({request, *response});
    };
    request.set_message("Ping1");
    node1_->DispatchAsyncPingPong(request, clbck);
    request.set_message("Ping2");
    node1_->DispatchAsyncPingPong(request, clbck);
    request.set_message("Ping3");
    node1_->DispatchAsyncPingPong(request, clbck);
  }

  void RunNode2(std::vector<ReqResPair> &history) {
    PingPongRequest request;
    auto clbck = [&](PingPongResponse *response) {
      std::cerr << "in Dispactch async ping pong callback "
                << response->DebugString() << std::endl;
      history.push_back({request, *response});
    };
    request.set_message("Ping2fromNODE2");
    node2_->DispatchAsyncPingPong(request, clbck);
  }
};

// Test case for the ping-pong RPC call
TEST_F(PingPongTest, testCase) {
  std::vector<ReqResPair> rec_history1, rec_history2;
  std::vector<ReqResPair> rep_history1;
  RunNode1(rec_history1);
  RunNode2(rec_history2);
  node1_->airr_ = std::make_unique<airreplay::Airreplay>(
      "class_trace" + std::to_string(4444), airreplay::kReplay);
  EXPECT_TRUE(node2_.unique());
  node2_.reset();
  EXPECT_TRUE(node2_ == nullptr);

  // set up replay hooks
  std::map<int, std::function<void(const google::protobuf::Message &)> > hooks;

  hooks[RPCKind::INCOMING_ASYNC_PINGGONG] = [&](const auto &msg) {
    PingPongRequest req;
    static_cast<const google::protobuf::Any &>(msg).UnpackTo(&req);
    node1_->AsyncPingPong(req);
  };

  hooks[RPCKind::INCOMING_SYNC_PINGGONG] = [&](const auto &msg) {
    PingPongRequest req;
    PingPongResponse res;
    static_cast<const google::protobuf::Any &>(msg).UnpackTo(&req);
    node1_->PingPong(req, &res);
  };


  hooks[RPCKind::kIncomingCallback] =
      [&](const google::protobuf::Message &msg) {
        PingPongResponse resp;
        static_cast<const google::protobuf::Any &>(msg).UnpackTo(&resp);
        std::cerr << " in rr callback calling callback with "
                  << resp.DebugString() << std::endl;
        node1_->CallCallback(static_cast<PingPongResponse *>(&resp));
      };
  node1_->airr_->RegisterReproducers(hooks);

  EXPECT_EQ(node1_->airr_->getTraceForTest().size(), 8)
      << "Some events at the tail did not replay properly";
  std::cerr << "replaying" << std::endl;
  RunNode1(rep_history1);

  EXPECT_EQ(rec_history1.size(), rep_history1.size());
  for (int i = 0; i < rec_history1.size(); i++) {
    EXPECT_EQ(rec_history1[i].request.DebugString(),
              rep_history1[i].request.DebugString());
    EXPECT_EQ(rec_history1[i].response.DebugString(),
              rep_history1[i].response.DebugString());
  }
  EXPECT_EQ(node1_->airr_->getTraceForTest().size(), 0)
      << "Some events at the tail did not replay properly";
}

int Return5() { return 5; }
std::function<int(void)> *savedcp;
void SaveCallback(std::function<int(void)> &cp) { savedcp = &cp; }
TEST(SanityTest, stdBind) {
  std::function<int(void)> cp = []() { return 42; };

  kudu::Callback<int(void)> func_cb = kudu::Bind(&Return5);
  std::cerr << "kudu callback result: " << std::to_string(func_cb.Run())
            << std::endl;
  auto cp2 = std::bind(&Return5);
  std::cerr << "kudu callback result: " << cp2() << std::endl;
  // LOG(INFO) << func_cb.Run();  // Prints 5.

  {
    std::function<int(void)> cp3 = []() { return 42; };
    SaveCallback(cp3);
  }
  (*savedcp)();
}