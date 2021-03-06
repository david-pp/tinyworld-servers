#include "redis_cmd.h"
#include "redis.h"
#include "tinylogger.h"

namespace tiny {

static std::string redisReplyTypeString(int type) {
    switch (type) {
        case REDIS_REPLY_STRING :
            return "STRING";
        case REDIS_REPLY_ARRAY  :
            return "ARRAY";
        case REDIS_REPLY_INTEGER:
            return "INTEGER";
        case REDIS_REPLY_NIL    :
            return "NILL";
        case REDIS_REPLY_STATUS :
            return "STATUS";
        case REDIS_REPLY_ERROR  :
            return "ERROR";
    }
    return "UNKOWN";
}

/////////////////////////////////////////////////////////////

template<typename ReplyT>
RedisCommand<ReplyT>::~RedisCommand() {
}

template<typename ReplyT>
void RedisCommand<ReplyT>::call() {
    AsyncTask::call();

    if (redis_ && redis_->submitToServer(id(), cmd_))
        return;

    // TODO: CANCEL...
    reply_status_ = RedisCmdStatus::send_error;
    last_error_ = "send error.";
    LOGGER_ERROR("redis", cmd() << ": " << last_error_);
}

template<typename ReplyT>
void RedisCommand<ReplyT>::done(void *data) {
    processReply((redisReply *) data);
    AsyncTask::done(data);
}

template<typename ReplyT>
void RedisCommand<ReplyT>::timeout() {
    if (on_timeout_) {
        on_timeout_();
        return;
    }

    reply_status_ = RedisCmdStatus::timeout;
    last_error_ = "timeout.";
    LOGGER_ERROR("redis", cmd() << ": " << last_error_);
}

template<typename ReplyT>
void RedisCommand<ReplyT>::on_emit(AsyncScheduler *scheduler) {
    redis_ = (AsyncRedisClient *) scheduler;
    AsyncTask::on_emit(scheduler);
}

template<typename ReplyT>
AsyncTaskPtr RedisCommand<ReplyT>::clone() {
    return RedisCmd(cmd_, callback_);
}

template<typename ReplyT>
void RedisCommand<ReplyT>::processReply(redisReply *r) {
    last_error_.clear();
    reply_obj_ = r;

    if (reply_obj_ == nullptr) {
        reply_status_ = RedisCmdStatus::error;
        last_error_ = "Received null redisReply* from hiredis.";
        LOGGER_ERROR("redis", last_error_);
        // TODO: CANCEL...
    } else {
        parseReplyObject();
    }

    invoke();
}


template<class ReplyT>
void RedisCommand<ReplyT>::freeReply() {

    if (reply_obj_ == nullptr)
        return;

    freeReplyObject(reply_obj_);
    reply_obj_ = nullptr;
}

/**
* Create a copy of the reply and return it. Use a guard
* to make sure we don't return a reply while it is being
* modified.
*/
template<class ReplyT>
ReplyT& RedisCommand<ReplyT>::reply() {
    if (!ok()) {
        LOGGER_WARN("redis", cmd() << ": Accessing reply value while status != OK.");
    }
    return reply_val_;
}

template<class ReplyT>
std::string RedisCommand<ReplyT>::cmd() const {
    return AsyncRedisClient::vecToStr(cmd_);
}

template<class ReplyT>
bool RedisCommand<ReplyT>::isExpectedReply(int type) {
    if (reply_obj_->type == type) {
        reply_status_ = RedisCmdStatus::okay;
        return true;
    }

    if (checkErrorReply() || checkNilReply())
        return false;

    std::stringstream errorMessage;
    errorMessage << "Received reply of type " << redisReplyTypeString(reply_obj_->type)
                 << ", expected type " << redisReplyTypeString(type)
                 << ".";

    last_error_ = errorMessage.str();
    LOGGER_ERROR("redis", cmd() << ": " << last_error_);
    reply_status_ = RedisCmdStatus::wrone_type;
    return false;
}

template<class ReplyT>
bool RedisCommand<ReplyT>::isExpectedReply(int typeA, int typeB) {

    if ((reply_obj_->type == typeA) || (reply_obj_->type == typeB)) {
        reply_status_ = RedisCmdStatus::okay;
        return true;
    }

    if (checkErrorReply() || checkNilReply())
        return false;

    std::stringstream errorMessage;
    errorMessage << "Received reply of type " << redisReplyTypeString(reply_obj_->type)
                 << ", expected type " << redisReplyTypeString(typeA)
                 << " or " << redisReplyTypeString(typeB)
                 << ".";

    last_error_ = errorMessage.str();
    LOGGER_ERROR("redis", cmd() << ": " << last_error_);
    reply_status_ = RedisCmdStatus::wrone_type;
    return false;
}

template<class ReplyT>
bool RedisCommand<ReplyT>::checkErrorReply() {
    if (reply_obj_->type == REDIS_REPLY_ERROR) {
        if (reply_obj_->str != 0) {
            last_error_ = reply_obj_->str;
        }

        LOGGER_ERROR("redis", cmd() << ": " << last_error_);
        reply_status_ = RedisCmdStatus::error;
        return true;
    }
    return false;
}

template<class ReplyT>
bool RedisCommand<ReplyT>::checkNilReply() {
    if (reply_obj_->type == REDIS_REPLY_NIL) {
        LOGGER_WARN("redis", cmd() << ": Nil reply.");
        reply_status_ = RedisCmdStatus::nil;
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
// Specializations of parseReplyObject for all expected return types
// ----------------------------------------------------------------------------

template<>
void RedisCommand<redisReply *>::parseReplyObject() {
    if (!checkErrorReply())
        reply_status_ = RedisCmdStatus::okay;
    reply_val_ = reply_obj_;
}

template<>
void RedisCommand<std::string>::parseReplyObject() {
    if (!isExpectedReply(REDIS_REPLY_STRING, REDIS_REPLY_STATUS))
        return;
    reply_val_ = {reply_obj_->str, static_cast<size_t>(reply_obj_->len)};
}

template<>
void RedisCommand<char *>::parseReplyObject() {
    if (!isExpectedReply(REDIS_REPLY_STRING, REDIS_REPLY_STATUS))
        return;
    reply_val_ = reply_obj_->str;
}

template<>
void RedisCommand<int>::parseReplyObject() {

    if (!isExpectedReply(REDIS_REPLY_INTEGER))
        return;
    reply_val_ = (int) reply_obj_->integer;
}

template<>
void RedisCommand<uint16_t>::parseReplyObject() {

    if (!isExpectedReply(REDIS_REPLY_INTEGER))
        return;
    reply_val_ = static_cast<uint16_t>(reply_obj_->integer);
}

template<>
void RedisCommand<uint32_t>::parseReplyObject() {

    if (!isExpectedReply(REDIS_REPLY_INTEGER))
        return;
    reply_val_ = static_cast<uint32_t>(reply_obj_->integer);
}

template<>
void RedisCommand<uint64_t>::parseReplyObject() {

    if (!isExpectedReply(REDIS_REPLY_INTEGER))
        return;
    reply_val_ = static_cast<uint64_t>(reply_obj_->integer);
}

template<>
void RedisCommand<long long int>::parseReplyObject() {

    if (!isExpectedReply(REDIS_REPLY_INTEGER))
        return;
    reply_val_ = reply_obj_->integer;
}

template<>
void RedisCommand<std::nullptr_t>::parseReplyObject() {

    if (!isExpectedReply(REDIS_REPLY_NIL))
        return;
    reply_val_ = nullptr;
}

template<>
void RedisCommand<std::vector<std::string>>::parseReplyObject() {

    if (!isExpectedReply(REDIS_REPLY_ARRAY))
        return;

    for (size_t i = 0; i < reply_obj_->elements; i++) {
        redisReply *r = *(reply_obj_->element + i);
        reply_val_.emplace_back(r->str, r->len);
    }
}

template<>
void RedisCommand<std::unordered_set<std::string>>::parseReplyObject() {

    if (!isExpectedReply(REDIS_REPLY_ARRAY))
        return;

    for (size_t i = 0; i < reply_obj_->elements; i++) {
        redisReply *r = *(reply_obj_->element + i);
        reply_val_.emplace(r->str, r->len);
    }
}

template<>
void RedisCommand<std::set<std::string>>::parseReplyObject() {

    if (!isExpectedReply(REDIS_REPLY_ARRAY))
        return;

    for (size_t i = 0; i < reply_obj_->elements; i++) {
        redisReply *r = *(reply_obj_->element + i);
        reply_val_.emplace(r->str, r->len);
    }
}

// Explicit template instantiation for available types, so that the generated
// library contains them and we can keep the method definitions out of the
// header file.
template
class RedisCommand<redisReply *>;

template
class RedisCommand<std::string>;

template
class RedisCommand<char *>;

template
class RedisCommand<int>;

template
class RedisCommand<long long int>;

template
class RedisCommand<uint16_t>;

template
class RedisCommand<uint32_t>;

template
class RedisCommand<uint64_t>;

template
class RedisCommand<std::nullptr_t>;

template
class RedisCommand<std::vector<std::string>>;

template
class RedisCommand<std::set<std::string>>;

template
class RedisCommand<std::unordered_set<std::string>>;

} // namespace tiny