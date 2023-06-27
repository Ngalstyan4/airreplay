#include <gtest/gtest.h>

#include <condition_variable>
#include <functional>

#include "airreplay/airreplay.h"
#include "airreplay/airreplay.pb.h"

using airreplay::PingPongRequest;
using airreplay::PingPongResponse;
enum RPCKind {
  INCOMING_ASYNC_PINGGONG = 1,
  INCOMING_SYNC_PINGGONG,
  OUTGOING,
  kIncomingCallback
};
class MockAsyncSystemNode {
 private:
  std::string name_;
  std::vector<std::weak_ptr<MockAsyncSystemNode> > neighbors_;
  std::mutex callbackMutex_;
  std::deque<std::function<void()> > callbacks_;
  std::thread callbackServicer_;
  bool sendersDone_ = false;
  bool isRecorded_;

  std::function<void(PingPongResponse *)> callback_;
  std::condition_variable callbackCV_;

 public:
  MockAsyncSystemNode(std::string name) : MockAsyncSystemNode(name, nullptr) {}
  MockAsyncSystemNode(std::string name,
                      std::unique_ptr<airreplay::Airreplay> airr)
      : name_(name), airr_(std::move(airr)) {
    if (airr_) {
      isRecorded_ = true;
    } else {
      isRecorded_ = false;
    }
    callbackServicer_ = std::thread([this]() {
      while (true) {
        // this has a pointer to the created thread and has to join it before it
        // can be destroyed so this invariant is enforced
        assert(this != nullptr);
        try {
          std::cerr << "callback servicer for " << name_ << " waiting\n";
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
          std::lock_guard lock(callbackMutex_);
          std::cerr << "callback servicer executing next" << callbacks_.size()
                    << "\n";
          if (callbacks_.size() > 0) {
            auto call = callbacks_[0];
            callbacks_.pop_front();
            call();
            std::cerr << "successfull callback exec!!" << name_ << std::endl;
          } else {
            if (sendersDone_) {
              return;
            }
          }
        } catch (std::exception &e) {
          std::cerr << "callback servicer exception " << e.what() << "\n";
          std::terminate();
        }
      }
    });
  }

  std::unique_ptr<airreplay::Airreplay> airr_;
  void RR(const std::string &method, const google::protobuf::Message &request,
          int kind = 0) {
    if (isRecorded_) {
      airr_->rr(method, request, kind);
    }
  }

  void waitAsyncs() {
    std::cerr << "waiting for asyncs to finish\n";
    {
      std::lock_guard lock(callbackMutex_);
      sendersDone_ = true;
    }
    callbackServicer_.join();
  }

  void addNeighbor(std::weak_ptr<MockAsyncSystemNode> node) {
    neighbors_.push_back(std::move(node));
  }

  // RPCs
  void AsyncPingPong(const PingPongRequest &request) {
    RR("I was called with(async)", request, RPCKind::INCOMING_ASYNC_PINGGONG);

    PingPongRequest req_copy = request;

    auto clbck = [req_copy, this]() {
      PingPongResponse response;
      assert(this != nullptr);
      // request below captured by value from the caller
      response.set_message("Pong++" + req_copy.message());
      if (!(airr_ && airr_->isReplay())) {
        neighbors_[0].lock()->CallCallback(&response);
      }
      RR("I responded with(callback)", response);
    };
    std::lock_guard lock(callbackMutex_);
    callbacks_.push_back(clbck);
  }

  void DispatchAsyncPingPong(const PingPongRequest &request,
                             std::function<void(PingPongResponse *)> callback) {
    // the following is the reason the mock does not support multiple
    // outstanding requests at a time. Callback is associated with the node and
    // not with this particular RPC invocation. fwiw, kudu has similar design
    // but there callback is associated with channel so the limitation is one
    // callback per neighbor not one globally
    callback_ = callback;

    PingPongResponse response;
    RR("I am async requesting", request);
    // todo:: find a way to call the callback in replay
    if (!(airr_ && airr_->isReplay())) {
      neighbors_[0].lock()->AsyncPingPong(request);
    }
  }

  void CallCallback(PingPongResponse *response) {
    assert(response != nullptr && "response passed to CallCallback is null");
    std::cerr << "Callcallback calling callback with "
              << response->DebugString() << " " << std::endl;

    RR("I was async responded with", *response, RPCKind::kIncomingCallback);
    assert(callback_ != nullptr && "callback_ is null");
    callback_(response);
    callback_ = nullptr;
    std::cerr << "DONE Callcallback calling callback with "
              << response->DebugString() << " " << std::endl;
  }

  void WaitForCallback() {
    while (callback_ != nullptr) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
};

struct ReqResPair {
  PingPongRequest request;
  PingPongResponse response;
};

// Define a fixture for the test suite
class PingPongTestAsync : public ::testing::Test {
 protected:
  std::shared_ptr<MockAsyncSystemNode> node1_;
  std::shared_ptr<MockAsyncSystemNode> node2_;

  std::string trace_fname_ = "class_trace_async" + std::to_string(4444);
  // Set up the test environment
  void SetUp() override {
    auto airr = std::make_unique<airreplay::Airreplay>(trace_fname_,
                                                       airreplay::kRecord);
    node1_ = std::make_shared<MockAsyncSystemNode>("node1", std::move(airr));
    node2_ = std::make_shared<MockAsyncSystemNode>("node2");
    node1_->addNeighbor(node2_);
    node2_->addNeighbor(node1_);
  }

  // Tear down the test environment
  void TearDown() override {
    // Stop the RPC server
  }

  void RunNode1(std::vector<ReqResPair> &history) {
    PingPongRequest request;
    request.set_message("Ping1");
    std::function<void(PingPongResponse * response)> clbck =
        [request, &history](PingPongResponse *response) {
          std::cerr << "in Dispactch async ping pong callback "
                    << response->DebugString() << std::endl;
          history.push_back({request, *response});
        };
    node1_->DispatchAsyncPingPong(request, clbck);
    node1_->WaitForCallback();
    request.set_message("Ping2");
    clbck = [request, &history](PingPongResponse *response) {
      std::cerr << "in Dispactch async ping pong callback "
                << response->DebugString() << std::endl;
      history.push_back({request, *response});
    };
    node1_->DispatchAsyncPingPong(request, clbck);
    node1_->WaitForCallback();
    request.set_message("Ping3");
    clbck = [request, &history](PingPongResponse *response) {
      std::cerr << "in Dispactch async ping pong callback "
                << response->DebugString() << std::endl;
      history.push_back({request, *response});
    };
    node1_->DispatchAsyncPingPong(request, clbck);
    node1_->WaitForCallback();
  }

  void RunNode2(std::vector<ReqResPair> &history) {
    PingPongRequest request;
    request.set_message("Ping2fromNODE2");
    auto clbck = [request, &history](PingPongResponse *response) {
      std::cerr << "in Dispactch async ping pong callback "
                << response->DebugString() << std::endl;
      history.push_back({request, *response});
    };
    node2_->DispatchAsyncPingPong(request, clbck);
    node1_->WaitForCallback();
  }
};

// Test case for the ping-pong RPC call
TEST_F(PingPongTestAsync, testCaseAsync) {
  std::cerr << "starting async test\n";
  std::vector<ReqResPair> rec_history1, rec_history2;
  std::vector<ReqResPair> rep_history1;
  RunNode1(rec_history1);
  RunNode2(rec_history2);

  EXPECT_TRUE(node2_.unique());
  node2_->waitAsyncs();
  node2_.reset();
  EXPECT_TRUE(node2_ == nullptr);

  node1_->airr_ =
      std::make_unique<airreplay::Airreplay>(trace_fname_, airreplay::kReplay);
  EXPECT_EQ(node1_->airr_->getTraceForTest().size(), 8)
      << "Hm.. wrong number of recorded events at start of replay";
  // set up replay hooks
  std::map<int, std::function<void(const google::protobuf::Message &)> > hooks;

  hooks[RPCKind::INCOMING_ASYNC_PINGGONG] = [&](const auto &msg) {
    PingPongRequest req;
    static_cast<const google::protobuf::Any &>(msg).UnpackTo(&req);
    node1_->AsyncPingPong(req);
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

  std::cerr << "replaying" << std::endl;
  RunNode1(rep_history1);
  node1_->waitAsyncs();

  EXPECT_EQ(rec_history1.size(), rep_history1.size());
  for (int i = 0; i < rec_history1.size(); i++) {
    EXPECT_EQ(rec_history1[i].request.DebugString(),
              rep_history1[i].request.DebugString());
    EXPECT_EQ(rec_history1[i].response.DebugString(),
              rep_history1[i].response.DebugString());
  }

  // print trace elements
  for (auto &e : node1_->airr_->getTraceForTest()) {
    std::cerr << e.DebugString() << std::endl;
  }
  EXPECT_EQ(node1_->airr_->getTraceForTest().size(), 0)
      << "Some events at the tail did not replay properly";

  delete airreplay::airr;
}