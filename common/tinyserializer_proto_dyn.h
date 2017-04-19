// Copyright (c) 2017 david++
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef TINYWORLD_TINYSERIALIZER_PROTO_DYN_H
#define TINYWORLD_TINYSERIALIZER_PROTO_DYN_H

#include <cstdarg>

#include "tinyworld.h"
#include "tinyserializer_proto.h"
#include "tinyserializer_proto_mapping.h"

TINY_NAMESPACE_BEGIN

template<typename T, uint32_t proto_case>
struct ProtoDynSerializerImpl;

template<typename T>
struct ProtoDynSerializer : public ProtoDynSerializerImpl<T, ProtoCase<T>::value> {
};

// Impl

//
// 整数
//
template<typename T>
struct ProtoDynSerializerImpl<T, kProtoType_Integer> {

    std::string serialize(const T &value) const {
        IntegerProto proto;
        proto.set_value(value);
        return proto.SerializeAsString();
    }

    bool deserialize(T &value, const std::string &data) const {
        IntegerProto proto;
        if (proto.ParseFromString(data)) {
            value = proto.value();
            return true;
        }
        return false;
    }
};


//
// 浮点
//
template<typename T>
struct ProtoDynSerializerImpl<T, kProtoType_Float> {

    std::string serialize(const T &value) const {
        FloatProto proto;
        proto.set_value(value);
        return proto.SerializeAsString();
    }

    bool deserialize(T &value, const std::string &data) const {
        FloatProto proto;
        if (proto.ParseFromString(data)) {
            value = proto.value();
            return true;
        }
        return false;
    }
};

//
// 字符串
//
template<typename T>
struct ProtoDynSerializerImpl<T, kProtoType_String> {

    std::string serialize(const T &value) const {
        StringProto proto;
        proto.set_value(value);
        return proto.SerializeAsString();
    }

    bool deserialize(T &value, const std::string &data) const {
        StringProto proto;
        if (proto.ParseFromString(data)) {
            value = proto.value();
            return true;
        }
        return false;
    }
};


//
// Protobuf生成的类型
//
template<typename T>
struct ProtoDynSerializerImpl<T, kProtoType_Proto> {
public:
    std::string serialize(const T &proto) const {
        return proto.SerializeAsString();
    }

    bool deserialize(T &proto, const std::string &data) const {
        return proto.ParseFromString(data);
    }
};

//
// vector<T>
//

template<typename T>
struct ProtoDynSerializerImpl<std::vector<T>, kProtoType_List> {

    std::string serialize(const std::vector<T> &objects) const {
        SequenceProto proto;
        ProtoDynSerializer<T> member_serializer;
        for (auto &v : objects) {
            ArchiveMemberProto *mem = proto.add_values();
            if (mem) {
                mem->set_data(member_serializer.serialize(v));
            }
        }

        return proto.SerializeAsString();
    }

    bool deserialize(std::vector<T> &objects, const std::string &data) const {
        SequenceProto proto;
        ProtoDynSerializer<T> member_serializer;
        if (proto.ParseFromString(data)) {
            for (int i = 0; i < proto.values_size(); ++i) {
                T obj;
                if (member_serializer.deserialize(obj, proto.values(i).data()))
                    objects.push_back(obj);
            }
            return true;
        }
        return false;
    }
};

//
// map<KeyT, ValueT>
//
template<typename KeyT, typename ValueT>
struct ProtoDynSerializerImpl<std::map<KeyT, ValueT>, kProtoType_Map> {
    std::string serialize(const std::map<KeyT, ValueT> &objects) const {
        AssociateProto proto;
        ProtoDynSerializer<KeyT> key_serializer;
        ProtoDynSerializer<ValueT> value_serializer;
        for (auto &v : objects) {
            AssociateProto::ValueType *mem = proto.add_values();
            if (mem) {
                ArchiveMemberProto *key = mem->mutable_key();
                ArchiveMemberProto *value = mem->mutable_value();
                if (key && value) {
                    key->set_data(key_serializer.serialize(v.first));
                    value->set_data(value_serializer.serialize(v.second));
                }
            }
        }

        return proto.SerializeAsString();
    }

    bool deserialize(std::map<KeyT, ValueT> &objects, const std::string &bin) const {
        AssociateProto proto;
        ProtoDynSerializer<KeyT> key_serializer;
        ProtoDynSerializer<ValueT> value_serializer;
        if (proto.ParseFromString(bin)) {
            for (int i = 0; i < proto.values_size(); ++i) {
                KeyT key;
                ValueT value;
                if (key_serializer.deserialize(key, proto.values(i).key().data())
                    && value_serializer.deserialize(value, proto.values(i).value().data())) {
                    objects.insert(std::make_pair(key, value));
                }
            }
            return true;
        }
        return false;
    }
};


class ProtoSerializerException : public std::exception {
public:
    ProtoSerializerException(const char *format, ...) {
        va_list ap;
        va_start(ap, format);
        vsnprintf(errmsg_, sizeof(errmsg_), format, ap);
        va_end(ap);
    }

    virtual const char *what() const noexcept {
        return errmsg_;
    }

private:
    char errmsg_[1024];
};


//
// 用户自定义类型
//
template<typename T>
struct ProtoDynSerializerImpl<T, kProtoType_UserDefined> {
public:
    std::string serialize(const T &object) const {
        using namespace google::protobuf;
        ProtoMapping<T> *mapping = ProtoMappingFactory::instance().template mappingByType<T>();
        if (!mapping) {
            throw ProtoSerializerException("%s : mapping is NULL", __PRETTY_FUNCTION__);
            return "";
        }

        const Descriptor *descriptor = mapping->descriptorPool()->FindMessageTypeByName(mapping->protoName());
        if (!descriptor) {
            throw ProtoSerializerException("%s : descriptor is NULL", __PRETTY_FUNCTION__);
            return "";
        }

        const Message *prototype = mapping->messageFactory()->GetPrototype(descriptor);
        if (!prototype) {
            throw ProtoSerializerException("%s : prototype is NULL", __PRETTY_FUNCTION__);
            return "";
        }

        Message *proto = prototype->New();
        const Reflection *refl = proto->GetReflection();
        const Descriptor *desc = proto->GetDescriptor();
        if (!refl || !desc) {
            throw ProtoSerializerException("%s : reflection is NULL", __PRETTY_FUNCTION__);
            return "";
        }

        try {
            for (auto &cpp_prop : mapping->struct_->propertyIterator()) {
                const FieldDescriptor *proto_fd = desc->FindFieldByName(cpp_prop->name());
                if (proto_fd && FieldDescriptor::TYPE_BYTES == proto_fd->type()) {
                    refl->SetString(proto, proto_fd, cpp_prop->serialize(object));
                }
            }
        } catch (const std::exception &e) {
            throw ProtoSerializerException("%s : serialize error : %s", __PRETTY_FUNCTION__, e.what());
            return "";
        }

        return proto->SerializeAsString();
    }


    bool deserialize(T &object, const std::string &data) const {
//        std::cout << __PRETTY_FUNCTION__ << std::endl;
        using namespace google::protobuf;
        ProtoMapping<T> *mapping = ProtoMappingFactory::instance().template mappingByType<T>();
        if (!mapping) {
            throw ProtoSerializerException("%s : mapping is NULL", __PRETTY_FUNCTION__);
            return false;
        }

        const Descriptor *descriptor = mapping->descriptorPool()->FindMessageTypeByName(mapping->protoName());
        if (!descriptor) {
            throw ProtoSerializerException("%s : descriptor is NULL", __PRETTY_FUNCTION__);
            return false;
        }

        const Message *prototype = mapping->messageFactory()->GetPrototype(descriptor);
        if (!prototype) {
            throw ProtoSerializerException("%s : prototype is NULL", __PRETTY_FUNCTION__);
            return false;
        }

        Message *proto = prototype->New();
        const Reflection *refl = proto->GetReflection();
        const Descriptor *desc = proto->GetDescriptor();
        if (!refl || !desc) {
            throw ProtoSerializerException("%s : reflection is NULL", __PRETTY_FUNCTION__);
            return false;
        }

        if (!proto->ParseFromString(data)) {
            throw ProtoSerializerException("%s : ParseFromString failed", __PRETTY_FUNCTION__);
            return false;
        }

        try {
            for (auto &cpp_prop : mapping->struct_->propertyIterator()) {
                const FieldDescriptor *proto_fd = desc->FindFieldByName(cpp_prop->name());
                if (proto_fd && FieldDescriptor::TYPE_BYTES == proto_fd->type()) {
                    cpp_prop->deserialize(object, refl->GetString(*proto, proto_fd));
                }
            }
        } catch (
                const std::exception &e
        ) {
            throw ProtoSerializerException("%s : deserialize error : %s", __PRETTY_FUNCTION__, e.what());
            return false;
        }

        return true;
    }

};


TINY_NAMESPACE_END

#endif //TINYWORLD_TINYSERIALIZER_PROTO_DYN_H
