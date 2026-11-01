#pragma once

#include <cstdint>

struct ID3D11Texture2D;

namespace emuscale {

// These contracts are for future GPU implementations. v0.1 has no concrete
// upscaler or frame generator; the capture preview displays captured frames.
struct FrameTexture {
    ID3D11Texture2D* texture = nullptr; // borrowed for the duration of a call
    std::uint64_t timestamp_qpc = 0;
};

struct ProcessResult {
    bool succeeded = false;
    FrameTexture output{};
};

class IUpscaler {
public:
    virtual ~IUpscaler() = default;
    virtual ProcessResult process(FrameTexture source,
                                  std::uint32_t output_width,
                                  std::uint32_t output_height) = 0;
};

class IFrameGenerator {
public:
    virtual ~IFrameGenerator() = default;
    virtual ProcessResult interpolate(FrameTexture previous,
                                      FrameTexture current,
                                      std::uint64_t target_qpc) = 0;
    virtual void reset() noexcept = 0;
};

} // namespace emuscale
