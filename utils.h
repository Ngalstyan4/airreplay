#include <google/protobuf/any.pb.h>
#include <google/protobuf/message.h>

namespace airreplay {
namespace utils {
std::string compareMessages(const google::protobuf::Message& message1,
                            const google::protobuf::Message& message2,
                            const std::string& parentField = "");
std::string compareMessageWithAny(const google::protobuf::Message& message1, const google::protobuf::Any& any);
}  // namespace utils
}  // namespace airreplay