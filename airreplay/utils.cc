#include "utils.h"

#include <glog/logging.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/reflection.h>

#include <cmath>

#include "kudu/common/row.h"
#include "kudu/common/row_operations.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/common/wire_protocol.pb.h"
#include "kudu/util/slice.h"

using namespace google::protobuf;

namespace airreplay {
namespace utils {

const kudu::SchemaPB* FindSchemaField(
    const google::protobuf::Message& message) {
  auto* reflection = message.GetReflection();
  const google::protobuf::Descriptor* descriptor = message.GetDescriptor();

  for (int i = 0; i < descriptor->field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field = descriptor->field(i);

    if (field->name() == "schema") {
      // Found schema field
      const kudu::SchemaPB* schema = dynamic_cast<const kudu::SchemaPB*>(
          &reflection->GetMessage(message, field));
      return schema;
    }
    if (field->is_repeated()) {
      int fieldSize = reflection->FieldSize(message, field);

      for (int j = 0; j < fieldSize; ++j) {
        const kudu::SchemaPB* res;
        const Message& nestedMessage =
            reflection->GetRepeatedMessage(message, field, j);
        if ((res = FindSchemaField(nestedMessage)) != nullptr) {
          return res;
        }
      }
    } else if (field->cpp_type() ==
               google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      // Recursively search nested message
      const google::protobuf::Message& nested_message =
          reflection->GetMessage(message, field);
      const kudu::SchemaPB* schema = FindSchemaField(nested_message);
      if (schema != nullptr) {
        return schema;
      }
    }
  }
}

std::string compareMessages(const Message& message1, const Message& message2,
                            const std::string& parentField,
                            const kudu::SchemaPB* schemapb) {
  // value to be returned
  std::string res;
  const Descriptor* descriptor1 = message1.GetDescriptor();
  const Reflection* reflection1 = message1.GetReflection();
  const Descriptor* descriptor2 = message2.GetDescriptor();
  const Reflection* reflection2 = message2.GetReflection();

  if (message1.IsInitialized() != message2.IsInitialized()) {
    return "one of the messages is uninitialized";
    return "m1 descriptor: " + descriptor1->full_name() +
           "m2 descriptor: " + descriptor2->full_name() +
           "\nProto Message initialization Mismatch - m1initialized:" +
           std::to_string(message1.IsInitialized()) +
           " m2initialized:" + std::to_string(message2.IsInitialized());
  }

  if (!message1.IsInitialized() && !message2.IsInitialized()) {
    // uninitialized values of same kind should map to the same set of bytes
    // always (I think?)
    return "";
  }
  DCHECK(message1.IsInitialized() && message2.IsInitialized());

  std::string errorCtx =
      "\nm1: " + message1.ShortDebugString() +
      "\ndescriptor: " + descriptor1->full_name() +
      "\nfield_count: " + std::to_string(descriptor1->field_count()) +
      "\nm2: " + message2.ShortDebugString() +
      "\ndescriptor: " + descriptor2->full_name() +
      "\nfield_count: " + std::to_string(descriptor2->field_count());

  if (descriptor1->full_name() != descriptor2->full_name()) {
    return "Descriptor Mismatch m1:" + descriptor1->full_name() +
           " m2:" + descriptor2->full_name() + errorCtx;
  }

  /*
  <<<<<<< HEAD
    std::unique_ptr<Message> defaultMsg(message1.New());
    bool m1default = false;
    if (defaultMsg->IsInitialized()) {
      m1default = message1.SerializeAsString() ==
  defaultMsg->SerializeAsString();
    }
    bool m2default = false;
    if (defaultMsg->IsInitialized()) {
      m2default = message2.SerializeAsString() ==
  defaultMsg->SerializeAsString();
    }

    if (schemapb == nullptr) {
      schemapb = FindSchemaField(message1);
    }

    if (m1default != m2default) {
      errorCtx +=
          "\nWARNING: One of the messages has its default value and the other "
          "does not. m1default:" +
          std::to_string(m1default) + " m2default:" + std::to_string(m2default)
  + errorCtx;
  =======
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
            "\nWARNING: One of the messages has its default value and the other
  " "does not. m1default:" + std::to_string(m1default) + " m2default:" +
  std::to_string(m2default) + errorCtx;
      }
  >>>>>>> f1c287d (Wip attempt at socket replay)
    }
  */
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
          auto new_res = compareMessages(nestedMessage1, nestedMessage2,
                                         fieldName, schemapb);
          if (new_res != "" && new_res != PROTO_COMPARE_FALSE_ALARM)
            return new_res;
          DCHECK(res == "" || res == PROTO_COMPARE_FALSE_ALARM);
          res = new_res;
        }
      }
    } else if (fieldDescriptor->cpp_type() ==
               FieldDescriptor::CPPTYPE_MESSAGE) {
      const Message& nestedMessage1 =
          reflection1->GetMessage(message1, fieldDescriptor);
      const Message& nestedMessage2 =
          reflection2->GetMessage(message2, fieldDescriptor);

      auto new_res =
          compareMessages(nestedMessage1, nestedMessage2, fieldName, schemapb);
      if (new_res == "") continue;
      if (fieldDescriptor->name() != "row_operations") {
        return new_res;
      }

      /************************ ROW_OPERATIONS ************************/

      // it is possible that binary row_operations are different but the
      // proto-level parsed info is the same. this is becuse proto encodes null
      // fields in kudu custom format and these can have arbitrary values
      errorCtx += "\n\nrow oopeartions\n\n";
      bool op_mismatch = false;
      // this is row data in a custom format stored as a blob in a protobuf.
      // we should parse it in case the diff is from there
      const kudu::RowOperationsPB& row_ops1 =
          reinterpret_cast<const kudu::RowOperationsPB&>(nestedMessage1);
      const kudu::RowOperationsPB& row_ops2 =
          reinterpret_cast<const kudu::RowOperationsPB&>(nestedMessage1);

      kudu::Schema client_schema;
      DCHECK(kudu::SchemaFromPB(*schemapb, &client_schema).ok());
      const kudu::Schema tablet_schema = client_schema.CopyWithColumnIds();
      std::vector<kudu::DecodedRowOperation> parsed_ops1, parsed_ops2;
      kudu::Arena arena(100);
      kudu::ScopedDisableRedaction no_redaction;
      kudu::RowOperationsPBDecoder decoder1(&row_ops1, &client_schema,
                                            &tablet_schema, &arena);
      kudu::RowOperationsPBDecoder decoder2(&row_ops2, &client_schema,
                                            &tablet_schema, &arena);
      DCHECK(
          decoder1.DecodeOperations<kudu::DecoderMode::WRITE_OPS>(&parsed_ops1)
              .ok());
      DCHECK(
          decoder2.DecodeOperations<kudu::DecoderMode::WRITE_OPS>(&parsed_ops2)
              .ok());
      if (parsed_ops1.size() != parsed_ops2.size()) {
        errorCtx += "Field: " + fieldName + " - Size Mismatch" +
                    " f1size:" + std::to_string(parsed_ops1.size()) +
                    " f2size:" + std::to_string(parsed_ops2.size()) + errorCtx;
        op_mismatch = true;
      }
      for (int i = 0; i < std::min(parsed_ops1.size(), parsed_ops2.size());
           ++i) {
        auto s1 = parsed_ops1[i].ToString(client_schema);
        auto s2 = parsed_ops2[i].ToString(client_schema);
        if (s1 != s2) {
          errorCtx += "\n - RowOperation Mismatch f1[" + std::to_string(i) +
                      "]value:" + s1 + "\nf2[" + std::to_string(i) +
                      "]value:" + s2;
          op_mismatch = true;
        }
      }
      if (op_mismatch) return errorCtx;
      DCHECK(res == "" || res == PROTO_COMPARE_FALSE_ALARM);
      res = PROTO_COMPARE_FALSE_ALARM;
    } else if (fieldDescriptor->cpp_type() == FieldDescriptor::CPPTYPE_STRING) {
      std::string value1 = reflection1->GetString(message1, fieldDescriptor);
      std::string value2 = reflection2->GetString(message2, fieldDescriptor);

      if (value1 != value2) {
        if (std::all_of(value1.begin(), value1.end(),
                        [](unsigned char c) { return std::isprint(c); }) &&
            std::all_of(value2.begin(), value2.end(),
                        [](unsigned char c) { return std::isprint(c); })) {
          return "Field: " + fieldName + " - Value Mismatch" +
                 " f1value:" + value1 + " f2value:" + value2 + errorCtx;
        } else {
          return "Field: " + fieldName + " - Value Mismatch" +
                 " f1value:" + std::to_string(value1.size()) +
                 " f2value:" + std::to_string(value2.size()) + errorCtx;
        }
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
    } else if (fieldDescriptor->cpp_type() == FieldDescriptor::CPPTYPE_UINT64) {
      uint64_t value1 = reflection1->GetUInt64(message1, fieldDescriptor);
      uint64_t value2 = reflection2->GetUInt64(message2, fieldDescriptor);
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
  return res;
}

std::string compareMessageWithAny(const Message& message1, const Any& any2) {
  // parse any2 into message2 for value comparison
  google::protobuf::Message* message2 = nullptr;

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
