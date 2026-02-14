#ifndef OPENMW_COMPONENTS_SCENEUTIL_OCCLUSIONCULLING_H
#define OPENMW_COMPONENTS_SCENEUTIL_OCCLUSIONCULLING_H

#include <osg/BoundingBox>
#include <osg/Matrixd>
#include <osg/Referenced>
#include <osg/Vec3f>

#include <vector>

class MaskedOcclusionCulling;

namespace SceneUtil
{
    /// Wraps Intel's Masked Software Occlusion Culling library.
    /// Provides a CPU-based hierarchical depth buffer for occlusion testing during cull traversal.
    class OcclusionCuller : public osg::Referenced
    {
    public:
        OcclusionCuller(unsigned int bufferWidth, unsigned int bufferHeight);
        ~OcclusionCuller();

        /// Call at the start of each frame's cull traversal.
        /// Clears the depth buffer and stores the view-projection matrix.
        void beginFrame(const osg::Matrixd& viewMatrix, const osg::Matrixd& projectionMatrix);

        /// Rasterize world-space triangles as occluders into the depth buffer.
        void rasterizeOccluder(const std::vector<osg::Vec3f>& worldPositions, const std::vector<unsigned int>& indices);

        /// Rasterize a world-space AABB as an occluder (12 triangles for 6 faces).
        void rasterizeAABBOccluder(const osg::BoundingBox& worldBB);

        /// Test if a world-space AABB is visible (not fully occluded).
        /// Returns true if the box may be visible, false if definitely occluded.
        bool testVisibleAABB(const osg::BoundingBox& worldBB) const;

        bool isActive() const { return mMOC != nullptr; }
        bool isFrameActive() const { return mFrameActive; }

        unsigned int getNumOccluded() const { return mNumOccluded; }
        unsigned int getNumTested() const { return mNumTested; }
        unsigned int getNumBuildingOccluders() const { return mNumBuildingOccluders; }
        unsigned int getNumBuildingTris() const { return mNumBuildingTris; }
        unsigned int getNumBuildingVerts() const { return mNumBuildingVerts; }
        void incrementBuildingOccluders(unsigned int tris, unsigned int verts)
        {
            ++mNumBuildingOccluders;
            mNumBuildingTris += tris;
            mNumBuildingVerts += verts;
        }

        /// Write the per-pixel depth buffer to depthData (width*height floats, bottom-to-top).
        void computePixelDepthBuffer(float* depthData) const;

        void getResolution(unsigned int& width, unsigned int& height) const;

    private:
        MaskedOcclusionCulling* mMOC;
        osg::Matrixd mViewProjection;
        float mVPFloat[16] = {};
        bool mFrameActive = false;

        mutable unsigned int mNumOccluded = 0;
        mutable unsigned int mNumTested = 0;
        unsigned int mNumBuildingOccluders = 0;
        unsigned int mNumBuildingTris = 0;
        unsigned int mNumBuildingVerts = 0;
    };
}

#endif
