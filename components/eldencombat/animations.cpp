#include "animations.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <istream>
#include <limits>
#include <set>
#include <stdexcept>

namespace EldenCombat
{
    LocomotionAnimation locomotionAnimation(
        float moveX, float moveY, bool crouching, bool running, bool sprinting, bool turnToMovement)
    {
        const unsigned direction = turnToMovement ? 0 : (std::abs(moveY) >= std::abs(moveX)
            ? (moveY < 0.f ? 1 : 0) : (moveX < 0.f ? 2 : 3));
        const float yaw = turnToMovement ? std::atan2(-moveX, moveY) : 0.f;
        if (crouching)
            return { (running ? 320100u : 320000u) + direction, running ? 2.982f : 1.370f, yaw };
        if (sprinting)
            return { 20200, 6.035f, std::atan2(-moveX, moveY) };
        return { (running ? 20100u : 20000u) + direction,
            running ? (direction == 0 ? 4.012f : direction == 1 ? 3.736f : 3.936f) : 1.5f, yaw };
    }

    LocomotionAnimation LocomotionAnimationState::update(
        float moveX, float moveY, bool crouching, bool running, bool sprinting, bool turnToMovement)
    {
        if (turnToMovement || (sprinting && !crouching) || (moveX == 0.f && moveY == 0.f))
        {
            reset();
            return locomotionAnimation(moveX, moveY, crouching, running, sprinting, turnToMovement);
        }
        const unsigned direction = std::abs(moveY) >= std::abs(moveX)
            ? (moveY < 0.f ? 1 : 0) : (moveX < 0.f ? 2 : 3);
        const float projections[] = { moveY, -moveY, -moveX, moveX };
        // About five degrees of hysteresis on either side of a diagonal.
        // Compare projections so this also works at any analog stick strength.
        if (mDirection >= 4 || projections[direction] > 1.2f * projections[mDirection])
            mDirection = direction;
        const float x = mDirection == 2 ? -1.f : mDirection == 3 ? 1.f : 0.f;
        const float y = mDirection == 1 ? -1.f : mDirection == 0 ? 1.f : 0.f;
        return locomotionAnimation(x, y, crouching, running, sprinting, false);
    }

    namespace
    {
        // The current 1,479-clip bank is 50 MiB. Bound allocations before trusting
        // counts in a damaged or replaced resource; no partial bank is published.
        constexpr std::size_t MaximumBankBytes = 256 * 1024 * 1024;
        constexpr std::size_t MaximumJoints = 256;
        constexpr std::size_t MaximumClips = 65536;

        [[noreturn]] void invalid(const char* detail)
        {
            throw std::runtime_error(std::string("Invalid Elden animation bank: ") + detail);
        }

        class Reader
        {
        public:
            explicit Reader(std::istream& stream)
            {
                std::array<char, 65536> buffer;
                while (stream.read(buffer.data(), buffer.size()) || stream.gcount() != 0)
                {
                    const auto count = static_cast<std::size_t>(stream.gcount());
                    if (count > MaximumBankBytes - mBytes.size())
                        invalid("file exceeds the size limit");
                    mBytes.insert(mBytes.end(), buffer.begin(), buffer.begin() + count);
                }
                if (!stream.eof() || stream.bad())
                    invalid("cannot read resource");
            }

            const char* bytes(std::size_t count)
            {
                if (count > remaining())
                    invalid("truncated file");
                const char* result = mBytes.data() + mAt;
                mAt += count;
                return result;
            }

            std::uint32_t u32()
            {
                const auto* data = reinterpret_cast<const unsigned char*>(bytes(4));
                return std::uint32_t(data[0]) | (std::uint32_t(data[1]) << 8)
                    | (std::uint32_t(data[2]) << 16) | (std::uint32_t(data[3]) << 24);
            }

            float f32()
            {
                static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559);
                const std::uint32_t bits = u32();
                float value;
                std::memcpy(&value, &bits, sizeof(value));
                if (!std::isfinite(value))
                    invalid("non-finite sample");
                return value;
            }

            std::string name()
            {
                const auto count = static_cast<unsigned char>(*bytes(1));
                if (count == 0)
                    invalid("empty name");
                std::string result(bytes(count), count);
                if (result.find('\0') != std::string::npos)
                    invalid("name contains a null byte");
                return result;
            }

            std::size_t remaining() const { return mBytes.size() - mAt; }

        private:
            std::vector<char> mBytes;
            std::size_t mAt = 0;
        };

        std::vector<std::string> names(Reader& reader, std::size_t count)
        {
            std::vector<std::string> result;
            std::set<std::string> unique;
            result.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                std::string name = reader.name();
                if (!unique.insert(name).second)
                    invalid("duplicate joint name");
                result.push_back(std::move(name));
            }
            return result;
        }
    }

    AnimationBank::AnimationBank(std::istream& stream)
    {
        Reader reader(stream);
        if (std::memcmp(reader.bytes(4), "ERAN", 4) != 0)
            invalid("wrong magic");
        const std::size_t jointCount = reader.u32();
        const std::size_t orientedCount = reader.u32();
        const std::size_t clipCount = reader.u32();
        if (jointCount == 0 || jointCount > MaximumJoints || orientedCount > jointCount
            || clipCount == 0 || clipCount > MaximumClips)
            invalid("invalid joint or clip count");
        mJoints = names(reader, jointCount);
        mOriented = names(reader, orientedCount);
        for (const auto& name : mOriented)
            if (std::find(mJoints.begin(), mJoints.end(), name) == mJoints.end())
                invalid("oriented joint is missing its position");
        mStride = jointCount * 3 + orientedCount * 9;
        for (std::size_t index = 0; index < clipCount; ++index)
        {
            std::string name = reader.name();
            AnimationClip clip;
            clip.mBlendFrames = reader.f32();
            clip.mFrames = reader.u32();
            if (clip.mBlendFrames < 0.f || clip.mFrames == 0)
                invalid("invalid blend or frame count");
            // Division before multiplication also protects 32-bit builds.
            if (clip.mFrames > reader.remaining() / sizeof(float) / mStride)
                invalid("truncated clip samples");
            const std::size_t samples = clip.mFrames * mStride;
            clip.mSamples.reserve(samples);
            for (std::size_t sample = 0; sample < samples; ++sample)
                clip.mSamples.push_back(reader.f32());
            if (!mClips.emplace(std::move(name), std::move(clip)).second)
                invalid("duplicate clip name");
        }
        if (reader.remaining() != 0)
            invalid("unexpected trailing data");
    }

    const AnimationClip* AnimationBank::find(std::string_view name) const
    {
        const auto found = mClips.find(name);
        return found == mClips.end() ? nullptr : &found->second;
    }

    std::optional<std::size_t> AnimationBank::jointOffset(std::string_view name) const
    {
        const auto found = std::find(mJoints.begin(), mJoints.end(), name);
        if (found == mJoints.end())
            return std::nullopt;
        return static_cast<std::size_t>(found - mJoints.begin()) * 3;
    }

    std::optional<std::size_t> AnimationBank::axesOffset(std::string_view name) const
    {
        const auto found = std::find(mOriented.begin(), mOriented.end(), name);
        if (found == mOriented.end())
            return std::nullopt;
        return mJoints.size() * 3 + static_cast<std::size_t>(found - mOriented.begin()) * 9;
    }

    void AnimationBank::sample(const AnimationClip& clip, float frame, bool looped, std::vector<float>& output) const
    {
        if (!std::isfinite(frame))
            throw std::invalid_argument("Non-finite Elden animation frame");
        if (clip.mFrames == 0 || clip.mFrames != clip.mSamples.size() / mStride
            || clip.mSamples.size() % mStride != 0)
            throw std::invalid_argument("Elden animation clip does not match this bank");
        const float last = static_cast<float>(clip.mFrames - 1);
        if (looped && last > 0.f)
        {
            frame = std::fmod(frame, last);
            if (frame < 0.f)
                frame += last;
        }
        else
            frame = std::clamp(frame, 0.f, last);
        const auto first = std::min(static_cast<std::size_t>(frame), clip.mFrames - 1);
        const auto second = std::min(first + 1, clip.mFrames - 1);
        const float t = frame - static_cast<float>(first);
        output.resize(mStride);
        for (std::size_t offset = 0; offset < mStride; ++offset)
        {
            const float a = clip.mSamples[first * mStride + offset];
            const float b = clip.mSamples[second * mStride + offset];
            // Weighted terms avoid overflowing b-a for opposite finite extremes.
            output[offset] = (1.f - t) * a + t * b;
        }
    }
}
