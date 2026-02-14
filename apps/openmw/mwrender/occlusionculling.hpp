#ifndef OPENMW_MWRENDER_OCCLUSIONCULLING_H
#define OPENMW_MWRENDER_OCCLUSIONCULLING_H

#include <osg/BoundingBox>
#include <osg/Camera>
#include <osg/Image>
#include <osg/Texture2D>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <unordered_map>
#include <vector>

#include <components/sceneutil/nodecallback.hpp>

namespace MWRender
{
    struct OccluderMesh
    {
        osg::BoundingBox aabb;
        std::vector<osg::Vec3f> vertices; // world-space, shrunk toward centroid
        std::vector<unsigned int> indices; // triangle indices from actual geometry
    };
}

namespace osgUtil
{
    class CullVisitor;
}

namespace osg
{
    class Group;
    class Node;
}

namespace SceneUtil
{
    class OcclusionCuller;
}

namespace Terrain
{
    class TerrainOccluder;
}

namespace MWRender
{
    /// Installed on the SceneRoot (LightManager). At the start of each main-camera cull,
    /// rasterizes terrain into the software occlusion buffer. Skips RTT cameras (shadows,
    /// water reflection) and interiors (no terrain data).
    class SceneOcclusionCallback
        : public SceneUtil::NodeCallback<SceneOcclusionCallback, osg::Node*, osgUtil::CullVisitor*>
    {
    public:
        SceneOcclusionCallback(SceneUtil::OcclusionCuller* culler, Terrain::TerrainOccluder* occluder, int radiusCells,
            bool enableTerrainOccluder, bool enableDebugOverlay);

        void operator()(osg::Node* node, osgUtil::CullVisitor* cv);

    private:
        void setupDebugOverlay();
        void updateDebugOverlay(osgUtil::CullVisitor* cv);

        osg::ref_ptr<SceneUtil::OcclusionCuller> mCuller;
        Terrain::TerrainOccluder* mTerrainOccluder;
        int mRadiusCells;
        bool mEnableTerrainOccluder;
        bool mEnableDebugOverlay;

        // Scratch buffers reused across frames
        std::vector<osg::Vec3f> mPositions;
        std::vector<unsigned int> mIndices;

        // Debug overlay
        osg::ref_ptr<osg::Camera> mDebugCamera;
        osg::ref_ptr<osg::Image> mDebugImage;
        osg::ref_ptr<osg::Texture2D> mDebugTexture;
        std::vector<float> mDepthPixels;
    };

    /// Installed on each Cell Root group. Two-pass approach:
    /// Pass 1: Large objects (radius >= threshold) are tested against terrain depth, and if
    ///         visible, their shrunken AABB is rasterized as an occluder, then traversed.
    /// Pass 2: Small objects are tested against the enriched depth buffer (terrain + buildings).
    class CellOcclusionCallback
        : public SceneUtil::NodeCallback<CellOcclusionCallback, osg::Group*, osgUtil::CullVisitor*>
    {
    public:
        CellOcclusionCallback(SceneUtil::OcclusionCuller* culler, float occluderMinRadius, float occluderMaxRadius,
            float occluderShrinkFactor, int occluderMeshResolution, float occluderInsideThreshold,
            float occluderMaxDistance, bool enableStaticOccluders);

        void operator()(osg::Group* node, osgUtil::CullVisitor* cv);

    private:
        /// Get cached occluder mesh (actual triangles + AABB) for a node.
        const OccluderMesh& getOccluderMesh(osg::Node* node);

        osg::ref_ptr<SceneUtil::OcclusionCuller> mCuller;
        float mOccluderMinRadius;
        float mOccluderMaxRadius;
        float mOccluderShrinkFactor;
        int mOccluderMeshResolution;
        float mOccluderInsideThreshold;
        float mOccluderMaxDistanceSq;
        bool mEnableStaticOccluders;

        std::unordered_map<osg::Node*, OccluderMesh> mMeshCache;
    };
}

#endif
