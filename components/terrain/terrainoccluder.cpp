#include "terrainoccluder.hpp"
#include "storage.hpp"

#include <algorithm>
#include <limits>

#include <osg/Array>

namespace Terrain
{
    TerrainOccluder::TerrainOccluder(Storage* storage, float cellWorldSize)
        : mStorage(storage)
        , mCellWorldSize(cellWorldSize)
    {
    }

    bool TerrainOccluder::hasTerrainData() const
    {
        // Only exterior worldspaces have terrain
        return !mWorldspace.empty();
    }

    void TerrainOccluder::build(const osg::Vec3f& eyePoint, int radiusCells, std::vector<osg::Vec3f>& outPositions,
        std::vector<unsigned int>& outIndices)
    {
        if (!hasTerrainData())
            return;

        // Determine camera cell
        int cellX = static_cast<int>(std::floor(eyePoint.x() / mCellWorldSize));
        int cellY = static_cast<int>(std::floor(eyePoint.y() / mCellWorldSize));
        osg::Vec2i cellPos(cellX, cellY);

        // Use cache if camera hasn't moved to a different cell
        if (cellPos == mCachedCellPos && radiusCells == mCachedRadius && !mCachedPositions.empty())
        {
            outPositions = mCachedPositions;
            outIndices = mCachedIndices;
            return;
        }

        outPositions.clear();
        outIndices.clear();

        // For each cell in range, generate a conservative coarse terrain mesh.
        // We fetch full-resolution heights (LOD 0) and min-pool them to the coarse grid,
        // ensuring the occluder is always at or below the actual terrain surface.
        // This prevents false occlusion in valleys and rapid elevation changes.
        const int step = 1 << mLodLevel;

        for (int cy = cellY - radiusCells; cy <= cellY + radiusCells; ++cy)
        {
            for (int cx = cellX - radiusCells; cx <= cellX + radiusCells; ++cx)
            {
                osg::Vec2f center(cx + 0.5f, cy + 0.5f);

                // Get full-resolution heights
                osg::ref_ptr<osg::Vec3Array> fullRes(new osg::Vec3Array);
                osg::ref_ptr<osg::Vec3Array> normals(new osg::Vec3Array);
                osg::ref_ptr<osg::Vec4ubArray> colors(new osg::Vec4ubArray);
                colors->setNormalize(true);

                mStorage->fillVertexBuffers(0, 1.0f, center, mWorldspace, *fullRes, *normals, *colors);

                if (fullRes->empty())
                    continue;

                int fullPerSide = static_cast<int>(std::sqrt(static_cast<float>(fullRes->size())));
                if (fullPerSide < 2)
                    continue;

                int coarsePerSide = (fullPerSide - 1) / step + 1;
                if (coarsePerSide < 2)
                    continue;

                // Pass 1: compute min height for each coarse quad
                int numQuads = (coarsePerSide - 1) * (coarsePerSide - 1);
                mQuadMins.resize(numQuads);
                for (int qj = 0; qj < coarsePerSide - 1; ++qj)
                {
                    for (int qi = 0; qi < coarsePerSide - 1; ++qi)
                    {
                        int startI = qi * step;
                        int startJ = qj * step;
                        int endI = std::min((qi + 1) * step, fullPerSide - 1);
                        int endJ = std::min((qj + 1) * step, fullPerSide - 1);

                        float minH = std::numeric_limits<float>::max();
                        for (int fj = startJ; fj <= endJ; ++fj)
                            for (int fi = startI; fi <= endI; ++fi)
                                minH = std::min(minH, (*fullRes)[fj * fullPerSide + fi].z());

                        mQuadMins[qj * (coarsePerSide - 1) + qi] = minH;
                    }
                }

                // Pass 2: each coarse vertex gets the min height of its surrounding quads
                osg::Vec3f worldOffset(center.x() * mCellWorldSize, center.y() * mCellWorldSize, 0.0f);
                unsigned int baseIndex = static_cast<unsigned int>(outPositions.size());

                for (int cj = 0; cj < coarsePerSide; ++cj)
                {
                    for (int ci = 0; ci < coarsePerSide; ++ci)
                    {
                        float minH = std::numeric_limits<float>::max();
                        // A vertex touches up to 4 quads
                        for (int dj = -1; dj <= 0; ++dj)
                        {
                            for (int di = -1; di <= 0; ++di)
                            {
                                int qi = ci + di;
                                int qj = cj + dj;
                                if (qi >= 0 && qi < coarsePerSide - 1 && qj >= 0 && qj < coarsePerSide - 1)
                                    minH = std::min(minH, mQuadMins[qj * (coarsePerSide - 1) + qi]);
                            }
                        }

                        osg::Vec3f pos = (*fullRes)[(cj * step) * fullPerSide + (ci * step)];
                        pos.z() = minH;
                        outPositions.push_back(pos + worldOffset);
                    }
                }

                // Generate triangle indices for the coarse grid
                for (int row = 0; row < coarsePerSide - 1; ++row)
                {
                    for (int col = 0; col < coarsePerSide - 1; ++col)
                    {
                        unsigned int tl = baseIndex + row * coarsePerSide + col;
                        unsigned int tr = tl + 1;
                        unsigned int bl = tl + coarsePerSide;
                        unsigned int br = bl + 1;

                        outIndices.push_back(tl);
                        outIndices.push_back(bl);
                        outIndices.push_back(tr);

                        outIndices.push_back(tr);
                        outIndices.push_back(bl);
                        outIndices.push_back(br);
                    }
                }
            }
        }

        // Update cache
        mCachedCellPos = cellPos;
        mCachedRadius = radiusCells;
        mCachedPositions = outPositions;
        mCachedIndices = outIndices;
    }
}
