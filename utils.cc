#include "utils.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/reflection.h>

#include <cmath>

using namespace google::protobuf;

namespace airreplay {
namespace utils {

std::string compareMessages(const Message& message1, const Message& message2,
                            const std::string& parentField) {
  const Descriptor* descriptor1 = message1.GetDescriptor();
  const Reflection* reflection1 = message1.GetReflection();
  const Descriptor* descriptor2 = message2.GetDescriptor();
  const Reflection* reflection2 = message2.GetReflection();
  if (descriptor1->full_name() != descriptor2->full_name()) {
    return "Descriptor Mismatch m1:" + descriptor1->full_name() +
           " m2:" + descriptor2->full_name();
  }

  int fieldCount = descriptor1->field_count();
  for (int i = 0; i < fieldCount; ++i) {
    const FieldDescriptor* fieldDescriptor = descriptor1->field(i);
    const std::string fieldName =
        parentField.empty() ? fieldDescriptor->name()
                            : parentField + "." + fieldDescriptor->name();

    if (fieldDescriptor->is_repeated()) {
      int fieldSize1 = reflection1->FieldSize(message1, fieldDescriptor);
      int fieldSize2 = reflection2->FieldSize(message2, fieldDescriptor);

      if (fieldSize1 != fieldSize2) {
        return "Field: " + fieldName + " - Size Mismatch" +
               " f1size:" + std::to_string(fieldSize1) +
               " f2size:" + std::to_string(fieldSize2);
      } else {
        for (int j = 0; j < fieldSize1; ++j) {
          const Message& nestedMessage1 =
              reflection1->GetRepeatedMessage(message1, fieldDescriptor, j);
          const Message& nestedMessage2 =
              reflection2->GetRepeatedMessage(message2, fieldDescriptor, j);
          auto res = compareMessages(nestedMessage1, nestedMessage2, fieldName);
          if (res != "") return res;
        }
      }
    } else if (fieldDescriptor->cpp_type() ==
               FieldDescriptor::CPPTYPE_MESSAGE) {
      const Message& nestedMessage1 =
          reflection1->GetMessage(message1, fieldDescriptor);
      const Message& nestedMessage2 =
          reflection2->GetMessage(message2, fieldDescriptor);
      auto res = compareMessages(nestedMessage1, nestedMessage2, fieldName);
      if (res != "") return res;
    } else if (fieldDescriptor->cpp_type() == FieldDescriptor::CPPTYPE_STRING) {
      std::string value1 = reflection1->GetString(message1, fieldDescriptor);
      std::string value2 = reflection2->GetString(message2, fieldDescriptor);

      if (value1 != value2) {
        return "Field: " + fieldName + " - Value Mismatch" +
               " f1value:" + value1 + " f2value:" + value2;
      }
    } else if (fieldDescriptor->cpp_type() == FieldDescriptor::CPPTYPE_FLOAT) {
      float value1 = reflection1->GetFloat(message1, fieldDescriptor);
      float value2 = reflection2->GetFloat(message2, fieldDescriptor);

      if (abs(value1 - value2) > 0.0000001) {
        return "Field: " + fieldName + " - Value Mismatch" +
               " f1value:" + std::to_string(value1) +
               " f2value:" + std::to_string(value2);
      }
    } else if (fieldDescriptor->cpp_type() == FieldDescriptor::CPPTYPE_INT32) {
      int32_t value1 = reflection1->GetInt32(message1, fieldDescriptor);
      int32_t value2 = reflection2->GetInt32(message2, fieldDescriptor);
      if (value1 != value2) {
        return "Field: " + fieldName + " - Value Mismatch" +
               " f1value:" + std::to_string(value1) +
               " f2value:" + std::to_string(value2);
      }

    } else if (fieldDescriptor->cpp_type() == FieldDescriptor::CPPTYPE_INT64) {
      int64_t value1 = reflection1->GetInt64(message1, fieldDescriptor);
      int64_t value2 = reflection2->GetInt64(message2, fieldDescriptor);
      if (value1 != value2) {
        return "Field: " + fieldName + " - Value Mismatch" +
               " f1value:" + std::to_string(value1) +
               " f2value:" + std::to_string(value2);
      }

    } else {
      return "Field: " + fieldName + ":typenum(" +
             std::to_string(fieldDescriptor->cpp_type()) +
             ") - type comparison not implemented";
    }
  }
  return "";
}

std::string compareMessageWithAny(const Message& message1, const Any& any) {
  google::protobuf::Message* message2 = message1.New();
  any.UnpackTo(message2);
  return compareMessages(message1, *message2);
}
}  // namespace utils
}  // namespace airreplay