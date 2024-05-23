#include "recastmesh.hpp"

#include <algorithm>

#include <components/detournavigator/recastmesh.hpp>
#include <components/detournavigator/settings.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/recastmesh.hpp>

#include <osg/PositionAttitudeTransform>

#include "vismask.hpp"

#include "../mwbase/environment.hpp"

namespace MWRender
{
    RecastMesh::RecastMesh(const osg::ref_ptr<osg::Group>& root, bool enabled)
        : mRootNode(root)
        , mEnabled(enabled)
    {
    }

    RecastMesh::~RecastMesh()
    {
        if (mEnabled)
            disable();
    }

    bool RecastMesh::toggle()
    {
        if (mEnabled)
            disable();
        else
            enable();

        return mEnabled;
    }

    void RecastMesh::update(const DetourNavigator::RecastMeshTiles& tiles, const DetourNavigator::Settings& settings)
    {
        if (!mEnabled)
            return;

        for (auto it = mGroups.begin(); it != mGroups.end();)
        {
            const auto tile = tiles.find(it->first);
            if (tile == tiles.end())
            {
                mRootNode->removeChild(it->second.mValue);
                it = mGroups.erase(it);
                continue;
            }

            if (it->second.mVersion != tile->second->getVersion())
            {
                const auto group = SceneUtil::createRecastMeshGroup(*tile->second, settings.mRecast);
                MWBase::Environment::get().getResourceSystem()->getSceneManager()->recreateShaders(group, "debug");
                group->setNodeMask(Mask_Debug);
                mRootNode->removeChild(it->second.mValue);
                mRootNode->addChild(group);
                it->second.mValue = group;
                it->second.mVersion = tile->second->getVersion();
            }

            ++it;
        }

        for (const auto& [position, mesh] : tiles)
        {
            const auto it = mGroups.find(position);

            if (it != mGroups.end())
            {
                if (it->second.mVersion == mesh->getVersion())
                    continue;

                mRootNode->removeChild(it->second.mValue);
            }

            const auto group = SceneUtil::createRecastMeshGroup(*mesh, settings.mRecast);
            MWBase::Environment::get().getResourceSystem()->getSceneManager()->recreateShaders(group, "debug");
            group->setNodeMask(Mask_Debug);
            mGroups.insert_or_assign(it, position, Group{ mesh->getVersion(), group });
            mRootNode->addChild(group);
        }
    }

    void RecastMesh::reset()
    {
        std::for_each(mGroups.begin(), mGroups.end(), [&](const auto& v) { mRootNode->removeChild(v.second.mValue); });
        mGroups.clear();
    }

    void RecastMesh::enable()
    {
        std::for_each(mGroups.begin(), mGroups.end(), [&](const auto& v) { mRootNode->addChild(v.second.mValue); });
        mEnabled = true;
    }

    void RecastMesh::disable()
    {
        std::for_each(mGroups.begin(), mGroups.end(), [&](const auto& v) { mRootNode->removeChild(v.second.mValue); });
        mEnabled = false;
    }
}
