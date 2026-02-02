#pragma once

#include <variant>
#include <cassert>

enum class Error
{
    TIMEOUT,
    CRC_MISSMATCH,
    INVALID_RESPONSE,
    PORT_ERROR,
    NFC_INIT,
    NFC_AUTH,
    CMD_FAILURE,
    READ_ERROR,
    WRITE_ERROR,
    PARSE_ERROR,
    DEVICE_ERROR

};

template<typename T> 
class Result
{
public:
    bool ok() const
    {
        return std::holds_alternative<T>(data_);
    }

    const T& value() const
    {
        return std::get<T>(data_);
    }

    const T* value_if() const
    {
        return std::get_if<T>(&data_);
    }

    Error error() const
    {
        return std::get<Error>(data_);
    }

    static Result success(T value)
    {
        return Result(std::move(value));
    }

    static Result failure(Error error)
    {
        return Result(error);
    }

private:
    std::variant<T, Error> data_;

    explicit Result(T value) : data_(std::move(value)) {}
    explicit Result(Error error) : data_(error) {}
};
