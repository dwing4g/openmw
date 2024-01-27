#ifndef GAME_MWMECHANICS_MAGICEFFECTS_H
#define GAME_MWMECHANICS_MAGICEFFECTS_H

#include <map>
#include <optional>
#include <string>

#include <components/esm/refid.hpp>

namespace ESM
{
    struct Attribute;
    struct ENAMstruct;
    struct EffectList;
    struct MagicEffect;
    struct MagicEffects;
    struct Skill;
}

namespace MWMechanics
{
    struct EffectKey
    {
        int mId;
        ESM::RefId mArg; // skill or ability

        EffectKey();

        EffectKey(int id, ESM::RefId arg = {})
            : mId(id)
            , mArg(arg)
        {
        }

        EffectKey(const ESM::ENAMstruct& effect);

        std::string toString() const;
    };

    bool operator<(const EffectKey& left, const EffectKey& right);
    bool operator==(const EffectKey& left, const EffectKey& right);

    struct EffectParam
    {
    private:
        // Note usually this would be int, but applying partial resistance might introduce a decimal point.
        float mModifier;

        int mBase;

    public:
        /// Get the total magnitude including base and modifier.
        float getMagnitude() const;

        void setModifier(float mod);
        float getModifier() const;

        /// Change mBase by \a diff
        void modifyBase(int diff);
        void setBase(int base);
        int getBase() const;

        EffectParam();

        EffectParam(float magnitude)
            : mModifier(magnitude)
            , mBase(0)
        {
        }

        EffectParam& operator+=(const EffectParam& param);

        EffectParam& operator-=(const EffectParam& param);
    };

    inline EffectParam operator+(const EffectParam& left, const EffectParam& right)
    {
        EffectParam param(left);
        return param += right;
    }

    inline EffectParam operator-(const EffectParam& left, const EffectParam& right)
    {
        EffectParam param(left);
        return param -= right;
    }

    /// \brief Effects currently affecting a NPC or creature
    class MagicEffects
    {
    public:
        typedef std::map<EffectKey, EffectParam> Collection;

    private:
        Collection mCollection;

    public:
        Collection::const_iterator begin() const { return mCollection.begin(); }

        Collection::const_iterator end() const { return mCollection.end(); }

        void readState(const ESM::MagicEffects& state);
        void writeState(ESM::MagicEffects& state) const;

        void add(const EffectKey& key, const EffectParam& param);
        void remove(const EffectKey& key);

        void modifyBase(const EffectKey& key, int diff);

        /// Copy Modifier values from \a effects, but keep original mBase values.
        void setModifiers(const MagicEffects& effects);

        EffectParam getOrDefault(const EffectKey& key) const;
        std::optional<EffectParam> get(const EffectKey& key) const;
        ///< This function can safely be used for keys that are not present.

        static MagicEffects diff(const MagicEffects& prev, const MagicEffects& now);
        ///< Return changes from \a prev to \a now.
    };

    std::string getMagicEffectString(
        const ESM::MagicEffect& effect, const ESM::Attribute* attribute, const ESM::Skill* skill);
}

#endif
