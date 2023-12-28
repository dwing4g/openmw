#include "animblendrules.hpp"

#include <iterator>
#include <map>

#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>

#include <components/debug/debuglog.hpp>
#include <components/files/configfileparser.hpp>
#include <components/files/conversion.hpp>
#include <components/sceneutil/controller.hpp>
#include <components/sceneutil/textkeymap.hpp>

#include <yaml-cpp/yaml.h>

namespace SceneUtil
{
    namespace
    {
        std::pair<std::string, std::string> splitRuleName(std::string full)
        {
            std::string group;
            std::string key;
            size_t delimiterInd = full.find(":");

            Misc::StringUtils::lowerCaseInPlace(full);

            if (delimiterInd == std::string::npos)
            {
                group = full;
                Misc::StringUtils::trim(group);
            }
            else
            {
                group = full.substr(0, delimiterInd);
                key = full.substr(delimiterInd + 1);
                Misc::StringUtils::trim(group);
                Misc::StringUtils::trim(key);
            }
            return std::make_pair(group, key);
        }

    }

    using BlendRule = AnimBlendRules::BlendRule;

    AnimBlendRules::AnimBlendRules(const AnimBlendRules& copy, const osg::CopyOp& copyop)
        : mConfigPath(copy.mConfigPath)
        , mRules(copy.mRules)
    {
    }

    AnimBlendRules::AnimBlendRules(const VFS::Manager* vfs, std::string yamlpath)
        : mConfigPath(yamlpath)
    {
        Log(Debug::Info) << "Attempting to load animation blending config '" << yamlpath << "'";

        if (yamlpath.find(".yaml") == std::string::npos)
            return;

        if (!vfs->exists(yamlpath))
        {
            Misc::StringUtils::replaceLast(yamlpath, ".yaml", ".json");
        }
        if (!vfs->exists(yamlpath))
            return;

        // Retrieving and parsing animation rules
        std::string rawYaml(std::istreambuf_iterator<char>(*vfs->get(yamlpath)), {});
        auto rules = parseYaml(rawYaml, yamlpath);

        mRules = rules;
    }

    void AnimBlendRules::addOverrideRules(const AnimBlendRules& overrideRules)
    {
        auto rules = overrideRules.getRules();
        // Concat the rules together, overrides added at the end since the bottom-most rule has the highest priority.
        mRules.insert(mRules.end(), rules.begin(), rules.end());
    }

    std::vector<BlendRule> AnimBlendRules::parseYaml(const std::string& rawYaml, const std::string& path)
    {

        std::vector<BlendRule> rules;

        YAML::Node root = YAML::Load(rawYaml);

        if (!root.IsDefined() || root.IsNull() || root.IsScalar())
        {
            Log(Debug::Warning) << "Warning: Can't parse YAML/JSON file '" << path
                                << "'. Check that it's a valid YAML/JSON file.";
            return rules;
        }

        if (root["blending_rules"])
        {
            for (const auto& it : root["blending_rules"])
            {
                if (it["from"] && it["to"] && it["duration"] && it["easing"])
                {
                    auto fromNames = splitRuleName(it["from"].as<std::string>());
                    auto toNames = splitRuleName(it["to"].as<std::string>());

                    BlendRule ruleObj = {
                        .mFromGroup = fromNames.first,
                        .mFromKey = fromNames.second,
                        .mToGroup = toNames.first,
                        .mToKey = toNames.second,
                        .mDuration = it["duration"].as<float>(),
                        .mEasing = it["easing"].as<std::string>(),
                    };

                    rules.emplace_back(ruleObj);
                }
                else
                {
                    Log(Debug::Warning) << "Warning: Blending rule '"
                                        << (it["from"] ? it["from"].as<std::string>() : "undefined") << "->"
                                        << (it["to"] ? it["to"].as<std::string>() : "undefined")
                                        << "' is missing some properties. File: '" << path << "'.";
                }
            }
        }
        else
        {
            Log(Debug::Warning) << "Warning: 'blending_rules' object not found in '" << path << "' file!";
        }

        return rules;
    }

    inline bool AnimBlendRules::fitsRuleString(const std::string& str, const std::string& ruleStr) const
    {
        // A wildcard only supported in the beginning or the end of the rule string in hopes that this will be more
        // performant. And most likely this kind of support is enough.
        return ruleStr == "*" || str == ruleStr || (ruleStr.starts_with("*") && str.ends_with(ruleStr.substr(1)))
            || (ruleStr.ends_with("*") && str.starts_with(ruleStr.substr(0, ruleStr.length() - 1)));
    }

    std::optional<BlendRule> AnimBlendRules::findBlendingRule(
        std::string fromGroup, std::string fromKey, std::string toGroup, std::string toKey) const
    {
        Misc::StringUtils::lowerCaseInPlace(fromGroup);
        Misc::StringUtils::lowerCaseInPlace(fromKey);
        Misc::StringUtils::lowerCaseInPlace(toGroup);
        Misc::StringUtils::lowerCaseInPlace(toKey);
        for (auto rule = mRules.rbegin(); rule != mRules.rend(); ++rule)
        {
            // TO DO: Also allow for partial wildcards at the end of groups and keys via std::string startswith method
            bool fromMatch = false;
            bool toMatch = false;

            // Pseudocode:
            // If not a wildcard and found a wildcard
            // starts with substr(0,wildcard)

            if (fitsRuleString(fromGroup, rule->mFromGroup)
                && (fitsRuleString(fromKey, rule->mFromKey) || rule->mFromKey == ""))
            {
                fromMatch = true;
            }

            if ((fitsRuleString(toGroup, rule->mToGroup) || (rule->mToGroup == "$" && toGroup == fromGroup))
                && (fitsRuleString(toKey, rule->mToKey) || rule->mToKey == ""))
            {
                toMatch = true;
            }

            if (fromMatch && toMatch)
                return std::make_optional<BlendRule>(*rule);
        }

        return std::nullopt;
    }

}
