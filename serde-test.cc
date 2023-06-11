#include <gtest/gtest.h>
#include "airreplay/airreplay.h"
#include <sys/stat.h>
#include <filesystem>

#include "airreplay/airreplay.pb.h"

class MyTest: public ::testing::Test {
 protected:
  virtual void SetUp() {
    std::remove("namespace_consensus_service_requests4444.txt");
    std::remove("namespace_consensus_service_requests4444.bin");
  }

  virtual void TearDown() {
    std::remove("namespace_consensus_service_requests4444.txt");
    std::remove("namespace_consensus_service_requests4444.bin");
  }
};

TEST_F(MyTest, Serialization) {
  airreplay::init(4444, airreplay::Mode::RECORD);
  std::ifstream ifstxt("namespace_consensus_service_requests4444.txt", std::ios::in);
  std::ifstream ifsbin("namespace_consensus_service_requests4444.bin", std::ios::in);
 
  EXPECT_TRUE(ifstxt);
  EXPECT_TRUE(ifsbin);
  EXPECT_FALSE(airreplay::isReplay());
  auto request1 = airreplay::TestMessagePB();
  request1.set_message("message1");
  auto request2 = airreplay::TestMessage2PB();
  request2.set_message("tablet2");
  request2.set_cnt(44);
  request2.set_info("info1");

  airreplay::airr("request1", request1);
  airreplay::airr("request2", request2);

  airreplay::init(4444, airreplay::Mode::REPLAY);
  EXPECT_TRUE(airreplay::isReplay());
  auto trace = airreplay::getTrace();
  EXPECT_EQ(trace.size(), 2);
  EXPECT_TRUE(trace[0].message().Is<airreplay::TestMessagePB>());
  EXPECT_TRUE(trace[1].message().Is<airreplay::TestMessage2PB>());
  airreplay::TestMessagePB unpacked_request1;
  trace[0].message().UnpackTo(&unpacked_request1);
  EXPECT_EQ(unpacked_request1.SerializeAsString(), request1.SerializeAsString());

}
