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

  std::string errorCtx =
      "\nm1: " + message1.ShortDebugString() +
      "descriptor: " + descriptor1->full_name() +
      "field_count: " + std::to_string(descriptor1->field_count()) + "\n" +
      "m2: " + message2.ShortDebugString() +
      "descriptor: " + descriptor2->full_name() +
      "field_count: " + std::to_string(descriptor2->field_count());

  if (descriptor1->full_name() != descriptor2->full_name()) {
    return "Descriptor Mismatch m1:" + descriptor1->full_name() +
           " m2:" + descriptor2->full_name() + errorCtx;
  }

  if (message1.IsInitialized() != message2.IsInitialized()) {
    errorCtx += "\nProto Message initialization Mismatch - m1initialized:" +
                std::to_string(message1.IsInitialized()) +
                " m2initialized:" + std::to_string(message2.IsInitialized());
    return errorCtx;
  }

  // some messages may have no required fields. proto3 drops the concept
  // alltogether. in those cases, it is still useful to check whether the
  // message is equal to the default and report that information.
  std::unique_ptr<Message> defaultMsg(message1.New());
  // the check makes sure that it is ok to call SerializeAsString on all
  // arguments AND the default message before proceeding
  if (message1.IsInitialized() && message2.IsInitialized() &&
      defaultMsg->IsInitialized()) {
    bool m1default =
        message1.SerializeAsString() == defaultMsg->SerializeAsString();
    bool m2default =
        message2.SerializeAsString() == defaultMsg->SerializeAsString();

    if (m1default != m2default) {
      errorCtx +=
          "\nWARNING: One of the messages has its default value and the other "
          "does not. m1default:" +
          std::to_string(m1default) +
          " m2default:" + std::to_string(m2default) + errorCtx;
    }
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
               " f2size:" + std::to_string(fieldSize2) + errorCtx;
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
               " f1value:" + value1 + " f2value:" + value2 + errorCtx;
      }
    } else if (fieldDescriptor->cpp_type() == FieldDescriptor::CPPTYPE_FLOAT) {
      float value1 = reflection1->GetFloat(message1, fieldDescriptor);
      float value2 = reflection2->GetFloat(message2, fieldDescriptor);

      if (abs(value1 - value2) > 0.0000001) {
        return "Field: " + fieldName + " - Value Mismatch" +
               " f1value:" + std::to_string(value1) +
               " f2value:" + std::to_string(value2) + errorCtx;
      }
    } else if (fieldDescriptor->cpp_type() == FieldDescriptor::CPPTYPE_INT32) {
      int32_t value1 = reflection1->GetInt32(message1, fieldDescriptor);
      int32_t value2 = reflection2->GetInt32(message2, fieldDescriptor);
      if (value1 != value2) {
        return "Field: " + fieldName + " - Value Mismatch" +
               " f1value:" + std::to_string(value1) +
               " f2value:" + std::to_string(value2) + errorCtx;
      }

    } else if (fieldDescriptor->cpp_type() == FieldDescriptor::CPPTYPE_UINT32) {
      uint32_t value1 = reflection1->GetUInt32(message1, fieldDescriptor);
      uint32_t value2 = reflection2->GetUInt32(message2, fieldDescriptor);
      if (value1 != value2) {
        return "Field: " + fieldName + " - Value Mismatch" +
               " f1value:" + std::to_string(value1) +
               " f2value:" + std::to_string(value2) + errorCtx;
      }

    } else if (fieldDescriptor->cpp_type() == FieldDescriptor::CPPTYPE_BOOL) {
      bool value1 = reflection1->GetBool(message1, fieldDescriptor);
      bool value2 = reflection2->GetBool(message2, fieldDescriptor);
      if (value1 != value2) {
        return "Field: " + fieldName + " - Value Mismatch" +
               " f1value:" + std::to_string(value1) +
               " f2value:" + std::to_string(value2) + errorCtx;
      }

    } else if (fieldDescriptor->cpp_type() == FieldDescriptor::CPPTYPE_INT64) {
      int64_t value1 = reflection1->GetInt64(message1, fieldDescriptor);
      int64_t value2 = reflection2->GetInt64(message2, fieldDescriptor);
      if (value1 != value2) {
        return "Field: " + fieldName + " - Value Mismatch" +
               " f1value:" + std::to_string(value1) +
               " f2value:" + std::to_string(value2) + errorCtx;
      }

    } else if (fieldDescriptor->cpp_type() == FieldDescriptor::CPPTYPE_ENUM) {
      int64_t value1 = reflection1->GetEnumValue(message1, fieldDescriptor);
      int64_t value2 = reflection2->GetEnumValue(message2, fieldDescriptor);
      if (value1 != value2) {
        return "Field: " + fieldName + " - Value Mismatch" +
               " f1value:" + std::to_string(value1) +
               " f2value:" + std::to_string(value2) + errorCtx;
      }

    } else {
      return "Field: " + fieldName + ":typenum(" +
             std::to_string(fieldDescriptor->cpp_type()) +
             ") - type comparison not implemented" + errorCtx;
    }
  }
  return "";
}

std::string compareMessageWithAny(const Message& message1, const Any& any2) {
  // parse message1 into any1 for typestring comparison
  Any any1;
  // parse any2 into message2 for value comparison
  google::protobuf::Message* message2 = nullptr;
  any1.PackFrom(message1);
  if (any1.type_url() != any2.type_url()) {
    return "Type Mismatch m1:" + any1.type_url() + " m2:" + any2.type_url();
  }

  message2 = message1.New();
  any2.UnpackTo(message2);
  return compareMessages(message1, *message2);
}

bool isAscii(const std::string& s) {
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] < 0) return false;
  }
  return true;
}

}  // namespace utils
}  // namespace airreplay