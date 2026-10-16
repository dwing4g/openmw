#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_ANIMATIONS_H
#define OPENMW_COMPONENTS_ELDENCOMBAT_ANIMATIONS_H

#include <cstddef>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace EldenCombat
{
    struct LocomotionAnimation
    {
        unsigned mId;
        float mNativeSpeed; // Metres per second at the clip's original playback rate.
        float mYaw; // Pose rotation about +Z, relative to the actor's facing.
    };

    // Movement is actor-local (+X right, +Y forward), after movement smoothing.
    // Free movement uses forward loops facing travel when turnToMovement is set.
    // Lock-on and disabled turning use directional walk/run/crouch loops; the
    // single forward sprint clip always faces travel.
    LocomotionAnimation locomotionAnimation(
        float moveX, float moveY, bool crouching, bool running, bool sprinting, bool turnToMovement);

    // Retain the current directional gait near diagonal boundaries. Small
    // steering changes must not restart the pose transition every frame.
    class LocomotionAnimationState
    {
    public:
        void reset() { mDirection = 4; }
        LocomotionAnimation update(
            float moveX, float moveY, bool crouching, bool running, bool sprinting, bool turnToMovement);

    private:
        unsigned mDirection = 4;
    };

    struct AnimationClip
    {
        float mBlendFrames = 0.f;
        std::size_t mFrames = 0;

    private:
        friend class AnimationBank;
        std::vector<float> mSamples;
    };

    // The ERAN file produced by ELDEN-RING-Combat-Rewrite/tools/bake_anims.py.
    // Positions are model-space metres: +X left, +Y up, +Z forward. Each pose
    // contains joint XYZ positions, then three XYZ axes for each oriented joint.
    // Load once and share the immutable bank across actor animation controllers.
    class AnimationBank
    {
    public:
        explicit AnimationBank(std::istream& stream);

        const std::vector<std::string>& jointNames() const { return mJoints; }
        const std::vector<std::string>& orientedNames() const { return mOriented; }
        std::size_t stride() const { return mStride; }
        std::size_t size() const { return mClips.size(); }

        const AnimationClip* find(std::string_view name) const;
        std::optional<std::size_t> jointOffset(std::string_view name) const;
        std::optional<std::size_t> axesOffset(std::string_view name) const;
        // Frames are sampled at 30 fps. Looping treats the last frame as the
        // duplicate loop endpoint, exactly as the source recreation does.
        void sample(const AnimationClip& clip, float frame, bool looped, std::vector<float>& output) const;

    private:
        std::vector<std::string> mJoints;
        std::vector<std::string> mOriented;
        std::size_t mStride = 0;
        std::map<std::string, AnimationClip, std::less<>> mClips;
    };
}

#endif
