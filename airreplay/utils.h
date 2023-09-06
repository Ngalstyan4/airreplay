#include <google/protobuf/any.pb.h>
#include <google/protobuf/message.h>
#define KUDU_HEADERS_USE_SHORT_STATUS_MACROS 1
// to allow Slice construction from const faststring&, needed form
// row_operations.h
#define KUDU_HEADERS_USE_RICH_SLICE 1

#include "kudu/common/row.h"
namespace airreplay {
namespace utils {
const std::string PROTO_COMPARE_FALSE_ALARM = "PROTO_COMPARE_FALSE_ALARM";

std::string compareMessages(const google::protobuf::Message& message1,
                            const google::protobuf::Message& message2,
                            const std::string& parentField = "",
                            const kudu::SchemaPB* schemapb = nullptr);
std::string compareMessageWithAny(const google::protobuf::Message& message1,
                                  const google::protobuf::Any& any);

bool isAscii(const std::string& s);
}  // namespace utils
}  // namespace airreplay