#pragma once

#include <cstddef>
#include <cstdint>

namespace Kernel {

enum class LZ4DecodeResult {
    Success,
    MalformedInput,
    OutputSizeMismatch,
};

enum class LZ4EncodeResult {
    Success,
    InputTooLarge,
    OutputTooSmall,
};

LZ4DecodeResult DecodeLZ4Block(const std::uint8_t* input,
                               std::size_t inputSize,
                               std::uint8_t* output,
                               std::size_t outputSize);

LZ4EncodeResult EncodeLZ4Block(const std::uint8_t* input,
                               std::size_t inputSize,
                               std::uint8_t* output,
                               std::size_t& outputSize);

} // namespace Kernel
