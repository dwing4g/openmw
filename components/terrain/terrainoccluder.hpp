#ifndef OPENMW_COMPONENTS_TERRAIN_TERRAINOCCLUDER_H
#define OPENMW_COMPONENTS_TERRAIN_TERRAINOCCLUDER_H

#include <osg/Vec2f>
#include <osg/Vec2i>
#include <osg/Vec3f>

#include <components/esm/refid.hpp>

#include <vector>

namespace Terrain
{
    class Storage;

    /// Generates conservative terrain geometry for software occlusion rasterization.
    /// Fetches full-resolution heights and min-pools them to a coarse grid, ensuring
    /// the occluder mesh is always at or below the actual terrain surface.
    class TerrainOccluder
    {
    public:
        TerrainOccluder(Storage* storage, float cellWorldSize);

        void setWorldspace(ESM::RefId worldspace) { mWorldspace = worldspace; }
        void setLodLevel(int lod) { mLodLevel = lod; }

        /// Build occluder geometry for terrain cells around the camera.
        /// Generates world-space positions and triangle indices.
        /// @param eyePoint Camera position in world space
        /// @param radius Radius in cells to include
        void build(const osg::Vec3f& eyePoint, int radiusCells,
            std::vector<osg::Vec3f>& outPositions, std::vector<unsigned int>& outIndices);

        bool hasTerrainData() const;

    private:
        Storage* mStorage;
        ESM::RefId mWorldspace;
        float mCellWorldSize;
        int mLodLevel = 3;

        // Cache: last built position (cell coords) and data
        osg::Vec2i mCachedCellPos;
        int mCachedRadius = -1;
        std::vector<osg::Vec3f> mCachedPositions;
        std::vector<unsigned int> mCachedIndices;

        // Scratch buffer for min-height pooling (reused across cells)
        std::vector<float> mQuadMins;
    };
}

#endif
