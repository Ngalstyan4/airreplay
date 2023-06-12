#include <gtest/gtest.h>
#include <sys/stat.h>

#include <filesystem>

#include "airreplay/airreplay.h"
#include "airreplay/airreplay.pb.h"

using airreplay::airr;

class MyTest : public ::testing::Test {
 protected:
  virtual void SetUp() {}

  virtual void TearDown() {
    std::remove("namespace_consensus_service_requests4444.txt");
    std::remove("namespace_consensus_service_requests4444.bin");
  }
};

TEST_F(MyTest, Serialization) {
  // the requests being recorded
  auto request1 = airreplay::TestMessagePB();
  request1.set_message("message1");
  auto request2 = airreplay::TestMessage2PB();
  request2.set_message("tablet2");
  request2.set_cnt(44);
  request2.set_info("info1");
  // airreplay::init(4444, airreplay::Mode::kRecord);
  {
    auto airr = new airreplay::Airreplay("class_trace" + std::to_string(4444),
                                         airreplay::Mode::kRecord);
    std::ifstream ifstxt(airr->txttracename(), std::ios::in);
    std::ifstream ifsbin(airr->tracename(), std::ios::in);

    EXPECT_TRUE(ifstxt);
    EXPECT_TRUE(ifsbin);
    EXPECT_FALSE(airr->isReplay());

    airr->rr("request1", request1);
    airr->rr("request2", request2);
    // todo:: add destructors

  }
  {
    auto airr = new airreplay::Airreplay("class_trace" + std::to_string(4444),
                                         airreplay::Mode::kReplay);

    // airreplay::init(4444, airreplay::Mode::kReplay);
    EXPECT_TRUE(airr->isReplay());
    auto trace = airr->getTraceForTest();
    EXPECT_EQ(trace.size(), 2);
    EXPECT_TRUE(trace[0].message().Is<airreplay::TestMessagePB>());
    EXPECT_TRUE(trace[1].message().Is<airreplay::TestMessage2PB>());
    airreplay::TestMessagePB unpacked_request1;
    airreplay::TestMessage2PB unpacked_request2;
    EXPECT_TRUE(trace[0].message().UnpackTo(&unpacked_request1))
        << "proto unmarshal failed";
    EXPECT_TRUE(trace[1].message().UnpackTo(&unpacked_request2))
        << "proto unmarshal failed";
    EXPECT_EQ(unpacked_request1.SerializeAsString(),
              request1.SerializeAsString());
    EXPECT_EQ(unpacked_request2.SerializeAsString(),
              request2.SerializeAsString());
    std::cerr << "unpacked_request1: " << unpacked_request1.DebugString();
    std::cerr << "unpacked_request2: " << unpacked_request2.DebugString();
  }
}
