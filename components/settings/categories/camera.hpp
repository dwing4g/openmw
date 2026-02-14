#ifndef OPENMW_COMPONENTS_SETTINGS_CATEGORIES_CAMERA_H
#define OPENMW_COMPONENTS_SETTINGS_CATEGORIES_CAMERA_H

#include <components/settings/sanitizerimpl.hpp>
#include <components/settings/settingvalue.hpp>

#include <osg/Math>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <cstdint>
#include <string>
#include <string_view>

namespace Settings
{
    struct CameraCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<float> mNearClip{ mIndex, "Camera", "near clip", makeMaxSanitizerFloat(0.005f) };
        SettingValue<bool> mSmallFeatureCulling{ mIndex, "Camera", "small feature culling" };
        SettingValue<float> mSmallFeatureCullingPixelSize{ mIndex, "Camera", "small feature culling pixel size",
            makeMaxStrictSanitizerFloat(0) };
        SettingValue<float> mViewingDistance{ mIndex, "Camera", "viewing distance", makeMaxStrictSanitizerFloat(0) };
        SettingValue<float> mFieldOfView{ mIndex, "Camera", "field of view", makeClampSanitizerFloat(1, 179) };
        SettingValue<float> mFirstPersonFieldOfView{ mIndex, "Camera", "first person field of view",
            makeClampSanitizerFloat(1, 179) };
        SettingValue<bool> mReverseZ{ mIndex, "Camera", "reverse z" };
        SettingValue<bool> mOcclusionCulling{ mIndex, "Camera", "occlusion culling" };
        SettingValue<bool> mOcclusionCullingTerrain{ mIndex, "Camera", "occlusion culling terrain" };
        SettingValue<bool> mOcclusionCullingStatics{ mIndex, "Camera", "occlusion culling statics" };
        SettingValue<int> mOcclusionBufferWidth{ mIndex, "Camera", "occlusion buffer width",
            makeClampSanitizerInt(64, 2048) };
        SettingValue<int> mOcclusionBufferHeight{ mIndex, "Camera", "occlusion buffer height",
            makeClampSanitizerInt(64, 1024) };
        SettingValue<int> mOcclusionTerrainLod{ mIndex, "Camera", "occlusion terrain lod",
            makeClampSanitizerInt(0, 6) };
        SettingValue<int> mOcclusionTerrainRadius{ mIndex, "Camera", "occlusion terrain radius",
            makeClampSanitizerInt(1, 20) };
        SettingValue<float> mOcclusionOccluderMinRadius{ mIndex, "Camera", "occlusion occluder min radius",
            makeClampSanitizerFloat(50.0f, 50000.0f) };
        SettingValue<float> mOcclusionOccluderMaxRadius{ mIndex, "Camera", "occlusion occluder max radius",
            makeClampSanitizerFloat(500.0f, 100000.0f) };
        SettingValue<float> mOcclusionOccluderShrinkFactor{ mIndex, "Camera", "occlusion occluder shrink factor",
            makeClampSanitizerFloat(0.1f, 2.0f) };
        SettingValue<int> mOcclusionOccluderMeshResolution{ mIndex, "Camera", "occlusion occluder mesh resolution",
            makeClampSanitizerInt(4, 32) };
        SettingValue<float> mOcclusionOccluderInsideThreshold{ mIndex, "Camera", "occlusion occluder inside threshold",
            makeClampSanitizerFloat(0.1f, 5.0f) };
        SettingValue<float> mOcclusionOccluderMaxDistance{ mIndex, "Camera", "occlusion occluder max distance",
            makeClampSanitizerFloat(1000.0f, 100000.0f) };
        SettingValue<bool> mOcclusionDebugOverlay{ mIndex, "Camera", "occlusion debug overlay" };
    };
}

#endif
