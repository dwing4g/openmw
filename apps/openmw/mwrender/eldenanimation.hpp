#ifndef OPENMW_MWRENDER_ELDENANIMATION_H
#define OPENMW_MWRENDER_ELDENANIMATION_H

#include "animation.hpp"

#include <memory>
#include <string_view>

namespace EldenCombat
{
    class AnimationBank;
}

namespace MWRender
{
    /// Retargets sampled ERAN joint positions onto the existing NIF skeleton.
    /// Attach controllers after native controllers, including accumulation resets.
    /// The physics controller owns horizontal root movement; this adapter only poses the bones.
    class EldenAnimation
    {
    public:
        EldenAnimation(const Animation::NodeMap& nodes, osg::Group& objectRoot,
            std::shared_ptr<const EldenCombat::AnimationBank> bank);
        ~EldenAnimation();

        bool valid() const;
        bool play(std::string_view clip, float frame, bool loop, float duration, float yaw = 0.f,
            std::string_view leftArmClip = {}, std::string_view rightArmClip = {}, std::string_view gripClip = {},
            bool alignSpear = false, bool twoHandedSword = false);
        void clear();

        const ActiveControllersVector& controllers() const { return mControllers; }
        const ActiveControllersVector& beforeNativeControllers() const { return mBeforeNativeControllers; }

    private:
        struct Impl;
        std::shared_ptr<Impl> mImpl;
        ActiveControllersVector mControllers;
        ActiveControllersVector mBeforeNativeControllers;
    };
}

#endif
