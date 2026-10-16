#ifndef OPENMW_MWRENDER_ELDENANATOMY_H
#define OPENMW_MWRENDER_ELDENANATOMY_H

#include <osg/Quat>
#include <osg/Vec3f>

#include <algorithm>
#include <cmath>

namespace MWRender::EldenPose
{
    // Blend one shared heading for the whole pose. Keep the target unwrapped
    // during a fade so crossing the +/-pi boundary cannot reverse its turn.
    class HeadingBlend
    {
    public:
        void reset() { mFrom = mTarget = mShown = 0.f; }

        void start(float targetYaw)
        {
            mFrom = mShown;
            mTarget = mFrom + normalize(targetYaw - mFrom);
        }

        float update(float targetYaw, float blend)
        {
            mTarget += normalize(targetYaw - mTarget);
            mShown = normalize(mFrom + (mTarget - mFrom) * blend);
            if (blend >= 1.f)
                mTarget = normalize(mTarget);
            return mShown;
        }

        float shown() const { return mShown; }
        float from() const { return mFrom; }

    private:
        static float normalize(float yaw)
        {
            constexpr float pi = 3.14159265358979323846f;
            float wrapped = std::remainder(yaw, 2.f * pi);
            // An exact reversal always turns in the same direction, including
            // atan2 results selected by a signed zero input.
            if (wrapped <= -pi)
                wrapped = pi;
            return wrapped == 0.f ? 0.f : wrapped;
        }

        float mFrom = 0.f;
        float mTarget = 0.f;
        float mShown = 0.f;
    };

    inline osg::Vec3f unit(osg::Vec3f value, const osg::Vec3f& fallback)
    {
        if (value.length2() < 1e-10f)
            return fallback;
        value.normalize();
        return value;
    }

    inline osg::Vec3f perpendicular(const osg::Vec3f& axis, osg::Vec3f reference)
    {
        reference -= axis * (reference * axis);
        if (reference.length2() < 1e-10f)
        {
            reference = std::abs(axis.x()) < .8f ? osg::Vec3f(1, 0, 0) : osg::Vec3f(0, 1, 0);
            reference -= axis * (reference * axis);
        }
        return unit(reference, osg::Vec3f(0, 0, 1));
    }

    struct TwoBonePosition
    {
        osg::Vec3f mJoint;
        osg::Vec3f mEnd;
    };

    // Close a hand onto its grip without stretching either native arm bone.
    // Transport the existing elbow bend around the new reach direction, so a
    // target crossing the old elbow direction cannot flip its bend side.
    inline TwoBonePosition solveTwoBone(const osg::Vec3f& root, const osg::Vec3f& joint,
        const osg::Vec3f& end, const osg::Vec3f& target)
    {
        const float upper = (joint - root).length();
        const float lower = (end - joint).length();
        const float requested = (target - root).length();
        if (!std::isfinite(upper) || !std::isfinite(lower) || !std::isfinite(requested)
            || upper < 1e-5f || lower < 1e-5f)
            return { joint, end };

        const osg::Vec3f oldAxis = unit(end - root, unit(joint - root, { 1, 0, 0 }));
        const osg::Vec3f axis = unit(target - root, oldAxis);
        const float distance = std::clamp(requested, std::abs(upper - lower), upper + lower);
        if (distance < 1e-5f)
            return { joint, root };

        osg::Vec3f bend = perpendicular(oldAxis, joint - root);
        const osg::Vec3f turn = oldAxis ^ axis;
        const float cosine = std::clamp(oldAxis * axis, -1.f, 1.f);
        if (cosine > -.9999f)
            bend += (turn ^ bend) + (turn ^ (turn ^ bend)) / (1.f + cosine);
        bend = perpendicular(axis, bend);
        float along = upper;
        float height = 0.f;
        if (requested <= std::abs(upper - lower))
            along = upper >= lower ? upper : -upper;
        else if (requested < upper + lower)
        {
            const double reach = distance;
            const double axial = (double(upper) * upper - double(lower) * lower + reach * reach) / (2.0 * reach);
            along = static_cast<float>(axial);
            height = static_cast<float>(std::sqrt(std::max(0.0, double(upper) * upper - axial * axial)));
        }
        return { root + axis * along + bend * height, root + axis * distance };
    }

    // referenceNormal describes the anatomical bend plane, independently of
    // the limb's elevation. In particular, a raised thigh must not reverse
    // the reference when it passes through horizontal during sprint/crouch.
    inline osg::Vec3f bendSecondary(const osg::Vec3f& upper, const osg::Vec3f& lower,
        const osg::Vec3f& primary, const osg::Vec3f& referenceNormal)
    {
        const osg::Vec3f a = unit(upper, osg::Vec3f(0, 0, -1));
        const osg::Vec3f b = unit(lower, a);
        const osg::Vec3f axis = unit(primary, a);
        const osg::Vec3f reference = perpendicular(axis, referenceNormal);
        osg::Vec3f normal = a ^ b;
        // Near a straight elbow/knee, positions cannot determine twist. Fade
        // to the anatomical plane rather than amplifying quantization noise.
        const float bend = std::clamp(normal.length() / 0.15f, 0.f, 1.f);
        normal = unit(normal, reference);
        if (normal * reference < 0)
            normal = -normal;
        normal = unit(reference * (1.f - bend) + normal * bend, reference);
        return normal ^ primary;
    }

    inline osg::Vec3f kneeSecondary(const osg::Vec3f& upper, const osg::Vec3f& lower,
        const osg::Vec3f& primary, const osg::Vec3f& hipRight)
    {
        return bendSecondary(upper, lower, primary, -hipRight);
    }

    // Limbs can turn their bend plane all the way around the body during a
    // strike. Choosing its sign against the body on every sample introduces
    // a half-turn whenever that plane crosses the reference hemisphere.
    // Carry the last plane along the segment instead, including through a
    // straight elbow/knee where positions do not determine its twist.
    class LimbBendPlane
    {
    public:
        void reset() { mValid = false; }

        osg::Vec3f secondary(const osg::Vec3f& upper, const osg::Vec3f& lower,
            const osg::Vec3f& primary, const osg::Vec3f& reference, float duration)
        {
            const osg::Vec3f axis = unit(primary, osg::Vec3f(0, 0, -1));
            if (mValid && upper == mUpper && lower == mLower && primary == mPrimary)
                return mNormal ^ primary;

            osg::Vec3f normal;
            if (!mValid)
                normal = unit(axis ^ bendSecondary(upper, lower, primary, reference), osg::Vec3f(1, 0, 0));
            else
            {
                // Shortest-arc transport keeps the previous normal orthogonal
                // to the new segment without adding a rotation around it.
                const osg::Vec3f turn = mAxis ^ axis;
                const float cosine = std::clamp(mAxis * axis, -1.f, 1.f);
                normal = mNormal;
                if (cosine > -0.9999f)
                    normal += (turn ^ mNormal) + (turn ^ (turn ^ mNormal)) / (1.f + cosine);
                normal = perpendicular(axis, normal);

                const osg::Vec3f a = unit(upper, axis);
                const osg::Vec3f b = unit(lower, a);
                osg::Vec3f bend = a ^ b;
                const float strength = std::clamp(bend.length() / 0.15f, 0.f, 1.f);
                bend = unit(bend, normal);
                if (bend * normal < 0.f)
                    bend = -bend;
                // Match the straight-limb stabilization across frame rates.
                const float amount = duration > 0.f ? 1.f - std::pow(1.f - strength, duration * 30.f) : 0.f;
                normal = unit(normal * (1.f - amount) + bend * amount, normal);
            }
            mUpper = upper;
            mLower = lower;
            mPrimary = primary;
            mAxis = axis;
            mNormal = normal;
            mValid = true;
            return normal ^ primary;
        }

    private:
        osg::Vec3f mUpper, mLower, mPrimary, mAxis, mNormal;
        bool mValid = false;
    };

    // The toe can point straight down during every running stride. Projecting
    // torso-up directly against that toe axis loses the foot's roll there and
    // flips its sole when the toe passes vertical. Use the anatomical lateral
    // axis instead; playback transports it along the toe direction.
    inline osg::Vec3f footSecondary(const osg::Vec3f& toeDirection, const osg::Vec3f& hipRight)
    {
        return bendSecondary(toeDirection, hipRight ^ toeDirection, toeDirection, hipRight);
    }

    // Source ankle/toe heights encode foot anatomy, not just heel lift. Calibrate
    // against the source's neutral foot frame so its downward ankle-to-toe slope
    // does not pitch a flat native sole downward. Preserve the source heading,
    // including toe-out, while keeping all later rotations relative to neutral.
    // A constant frame correction also follows rolls/death without consulting
    // world up again during playback.
    inline osg::Quat footCorrection(const osg::Quat& nativeRest, const osg::Vec3f& nativeToeDirection,
        const osg::Quat& sourceNeutralFrame)
    {
        const osg::Vec3f sourceToeDirection = sourceNeutralFrame * osg::Vec3f(1, 0, 0);
        const double heading = std::atan2(sourceToeDirection.y(), sourceToeDirection.x())
            - std::atan2(nativeToeDirection.y(), nativeToeDirection.x());
        const osg::Quat yaw(0.0, 0.0, std::sin(heading * 0.5), std::cos(heading * 0.5));
        // OSG multiplies rotations in application order (left to right).
        return nativeRest * yaw * sourceNeutralFrame.inverse();
    }
}

#endif
