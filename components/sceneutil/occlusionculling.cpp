#include "occlusionculling.hpp"

#include <MaskedOcclusionCulling.h>

#include <algorithm>
#include <cmath>

namespace SceneUtil
{
    OcclusionCuller::OcclusionCuller(unsigned int bufferWidth, unsigned int bufferHeight)
        : mMOC(nullptr)
    {
        mMOC = MaskedOcclusionCulling::Create();
        if (mMOC)
        {
            // Width must be multiple of 8, height multiple of 4
            bufferWidth = (bufferWidth + 7) & ~7u;
            bufferHeight = (bufferHeight + 3) & ~3u;
            mMOC->SetResolution(bufferWidth, bufferHeight);
            mMOC->SetNearClipPlane(0.1f);
        }
    }

    OcclusionCuller::~OcclusionCuller()
    {
        if (mMOC)
            MaskedOcclusionCulling::Destroy(mMOC);
    }

    void OcclusionCuller::beginFrame(const osg::Matrixd& viewMatrix, const osg::Matrixd& projectionMatrix)
    {
        mFrameActive = false;
        if (!mMOC)
            return;

        mMOC->ClearBuffer();
        mViewProjection = viewMatrix * projectionMatrix;

        const double* vpDouble = mViewProjection.ptr();
        for (int i = 0; i < 16; ++i)
            mVPFloat[i] = static_cast<float>(vpDouble[i]);

        mNumOccluded = 0;
        mNumTested = 0;
        mNumBuildingOccluders = 0;
        mFrameActive = true;
    }

    void OcclusionCuller::rasterizeOccluder(
        const std::vector<osg::Vec3f>& worldPositions, const std::vector<unsigned int>& indices)
    {
        if (!mFrameActive || worldPositions.empty() || indices.empty())
            return;

        const int numTris = static_cast<int>(indices.size()) / 3;
        if (numTris <= 0)
            return;

        // Vec3f layout: stride=12 bytes, yOffset=4, zOffset=8
        // MOC treats (x,y,z) as (x,y,w_component) and transforms via the VP matrix
        MaskedOcclusionCulling::VertexLayout vtxLayout(12, 4, 8);

        mMOC->RenderTriangles(
            reinterpret_cast<const float*>(worldPositions.data()),
            indices.data(),
            numTris,
            mVPFloat,
            MaskedOcclusionCulling::BACKFACE_NONE, // terrain can be seen from below at edges
            MaskedOcclusionCulling::CLIP_PLANE_ALL,
            vtxLayout);
    }

    void OcclusionCuller::rasterizeAABBOccluder(const osg::BoundingBox& worldBB)
    {
        if (!mFrameActive)
            return;

        // 8 corners of the AABB
        const osg::Vec3f verts[8] = {
            osg::Vec3f(worldBB.xMin(), worldBB.yMin(), worldBB.zMin()), // 0
            osg::Vec3f(worldBB.xMax(), worldBB.yMin(), worldBB.zMin()), // 1
            osg::Vec3f(worldBB.xMin(), worldBB.yMax(), worldBB.zMin()), // 2
            osg::Vec3f(worldBB.xMax(), worldBB.yMax(), worldBB.zMin()), // 3
            osg::Vec3f(worldBB.xMin(), worldBB.yMin(), worldBB.zMax()), // 4
            osg::Vec3f(worldBB.xMax(), worldBB.yMin(), worldBB.zMax()), // 5
            osg::Vec3f(worldBB.xMin(), worldBB.yMax(), worldBB.zMax()), // 6
            osg::Vec3f(worldBB.xMax(), worldBB.yMax(), worldBB.zMax()), // 7
        };

        // 12 triangles (6 faces x 2 tris)
        static const unsigned int indices[36] = {
            // -Z face
            0, 1, 3, 0, 3, 2,
            // +Z face
            4, 6, 7, 4, 7, 5,
            // -Y face
            0, 4, 5, 0, 5, 1,
            // +Y face
            2, 3, 7, 2, 7, 6,
            // -X face
            0, 2, 6, 0, 6, 4,
            // +X face
            1, 5, 7, 1, 7, 3,
        };

        MaskedOcclusionCulling::VertexLayout vtxLayout(12, 4, 8);

        mMOC->RenderTriangles(
            reinterpret_cast<const float*>(verts),
            indices,
            12,
            mVPFloat,
            MaskedOcclusionCulling::BACKFACE_NONE, // both sides, nearest depth wins
            MaskedOcclusionCulling::CLIP_PLANE_ALL,
            vtxLayout);

        ++mNumBuildingOccluders;
    }

    bool OcclusionCuller::testVisibleAABB(const osg::BoundingBox& worldBB) const
    {
        if (!mFrameActive)
            return true;

        ++mNumTested;

        // Project all 8 AABB corners to clip space, find NDC screen rect + minimum w
        const osg::Vec3f corners[8] = {
            osg::Vec3f(worldBB.xMin(), worldBB.yMin(), worldBB.zMin()),
            osg::Vec3f(worldBB.xMax(), worldBB.yMin(), worldBB.zMin()),
            osg::Vec3f(worldBB.xMin(), worldBB.yMax(), worldBB.zMin()),
            osg::Vec3f(worldBB.xMax(), worldBB.yMax(), worldBB.zMin()),
            osg::Vec3f(worldBB.xMin(), worldBB.yMin(), worldBB.zMax()),
            osg::Vec3f(worldBB.xMax(), worldBB.yMin(), worldBB.zMax()),
            osg::Vec3f(worldBB.xMin(), worldBB.yMax(), worldBB.zMax()),
            osg::Vec3f(worldBB.xMax(), worldBB.yMax(), worldBB.zMax()),
        };

        float ndcMinX = 1.0f, ndcMinY = 1.0f;
        float ndcMaxX = -1.0f, ndcMaxY = -1.0f;
        float wMin = std::numeric_limits<float>::max();
        bool anyInFront = false;

        const double* m = mViewProjection.ptr();
        for (int i = 0; i < 8; ++i)
        {
            // Transform to clip space: pos * ViewProjection
            double x = corners[i].x(), y = corners[i].y(), z = corners[i].z();
            float cx = static_cast<float>(x * m[0] + y * m[4] + z * m[8] + m[12]);
            float cy = static_cast<float>(x * m[1] + y * m[5] + z * m[9] + m[13]);
            float cw = static_cast<float>(x * m[3] + y * m[7] + z * m[11] + m[15]);

            if (cw > 0.0f)
            {
                anyInFront = true;
                float invW = 1.0f / cw;
                float ndcX = cx * invW;
                float ndcY = cy * invW;
                ndcMinX = std::min(ndcMinX, ndcX);
                ndcMinY = std::min(ndcMinY, ndcY);
                ndcMaxX = std::max(ndcMaxX, ndcX);
                ndcMaxY = std::max(ndcMaxY, ndcY);
                wMin = std::min(wMin, cw);
            }
            else
            {
                // Corner is behind camera — conservatively expand to full screen
                ndcMinX = -1.0f;
                ndcMinY = -1.0f;
                ndcMaxX = 1.0f;
                ndcMaxY = 1.0f;
                anyInFront = true; // still test it
                wMin = std::min(wMin, 0.0001f);
            }
        }

        if (!anyInFront)
            return true; // entirely behind camera, let frustum culling handle it

        // Clamp to NDC range
        ndcMinX = std::max(ndcMinX, -1.0f);
        ndcMinY = std::max(ndcMinY, -1.0f);
        ndcMaxX = std::min(ndcMaxX, 1.0f);
        ndcMaxY = std::min(ndcMaxY, 1.0f);

        if (ndcMinX >= ndcMaxX || ndcMinY >= ndcMaxY)
            return true; // degenerate rect, assume visible

        auto result = mMOC->TestRect(ndcMinX, ndcMinY, ndcMaxX, ndcMaxY, wMin);

        if (result == MaskedOcclusionCulling::OCCLUDED)
        {
            ++mNumOccluded;
            return false;
        }
        return true;
    }

    void OcclusionCuller::computePixelDepthBuffer(float* depthData) const
    {
        if (mMOC)
            mMOC->ComputePixelDepthBuffer(depthData, true); // flipY for OpenGL bottom-to-top
    }

    void OcclusionCuller::getResolution(unsigned int& width, unsigned int& height) const
    {
        if (mMOC)
            mMOC->GetResolution(width, height);
        else
        {
            width = 0;
            height = 0;
        }
    }
}
