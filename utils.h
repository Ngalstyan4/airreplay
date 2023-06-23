#include <google/protobuf/message.h>

namespace airreplay {
namespace utils {
std::string compareMessages(const google::protobuf::Message& message1, const  google::protobuf::Message& message2,
                     const std::string& parentField = "");
}
}  // namespace airreplay