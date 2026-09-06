#pragma once
#include <span>


class BufferReader
{
public:
    BufferReader(std::span<const uint8_t> view) : _view(view) {}
    BufferReader(const uint8_t* buffer, size_t size) : _view(buffer, size) {}

    bool CanRead(size_t byteSize)
    {
        return !_error && (_view.size_bytes() >= byteSize);
    }

    template<typename Type>
    bool CanRead()
    {
        return CanRead(sizeof(Type));
    }

    size_t GetBytesLeft() const 
    {
        return _view.size_bytes();
    }

    bool IsError() const { return _error; }

    template<typename T>
    bool Read(T& outValue)
    {
        if (_view.size_bytes() < sizeof(T))
        {
            _error = true;
            return false;
        }
        std::memcpy(&outValue, _view.data(), sizeof(T));

        _view = _view.subspan(sizeof(T));
        return true;
    }

    std::span<const uint8_t> ReadView(size_t size)
    {
        if (_view.size_bytes() < size)
        {
            _error = true;
            return {};
        }

        auto result = _view.subspan(0, size);
        _view = _view.subspan(size);
        return result;
    }

private:
    std::span<const uint8_t> _view;
    bool _error = false;
};
