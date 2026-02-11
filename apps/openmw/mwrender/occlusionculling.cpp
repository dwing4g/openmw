#include "occlusionculling.hpp"

#include <osg/BoundingBox>
#include <osg/BoundingSphere>
#include <osg/Camera>
#include <osg/ComputeBoundsVisitor>
#include <osg/Geometry>
#include <osg/Group>
#include <osgUtil/CullVisitor>

#include <components/debug/debuglog.hpp>
#include <components/misc/constants.hpp>
#include <components/sceneutil/occlusionculling.hpp>
#include <components/terrain/terrainoccluder.hpp>

namespace MWRender
{
    SceneOcclusionCallback::SceneOcclusionCallback(
        SceneUtil::OcclusionCuller* culler, Terrain::TerrainOccluder* occluder, int radiusCells,
        bool enableTerrainOccluder, bool enableDebugOverlay)
        : mCuller(culler)
        , mTerrainOccluder(occluder)
        , mRadiusCells(radiusCells)
        , mEnableTerrainOccluder(enableTerrainOccluder)
        , mEnableDebugOverlay(enableDebugOverlay)
    {
    }

    void SceneOcclusionCallback::setupDebugOverlay()
    {
        unsigned int w, h;
        mCuller->getResolution(w, h);
        if (w == 0 || h == 0)
            return;

        mDepthPixels.resize(w * h);

        // Create image to hold depth data (luminance float -> converted to RGBA)
        mDebugImage = new osg::Image;
        mDebugImage->allocateImage(w, h, 1, GL_LUMINANCE, GL_FLOAT);

        // Create texture from image
        mDebugTexture = new osg::Texture2D(mDebugImage);
        mDebugTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
        mDebugTexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
        mDebugTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        mDebugTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        mDebugTexture->setResizeNonPowerOfTwoHint(false);

        // Create POST_RENDER camera in corner of screen
        mDebugCamera = new osg::Camera;
        mDebugCamera->setName("OcclusionDebugCamera");
        mDebugCamera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
        mDebugCamera->setRenderOrder(osg::Camera::POST_RENDER, 100);
        mDebugCamera->setAllowEventFocus(false);
        mDebugCamera->setClearMask(0);
        mDebugCamera->setProjectionMatrix(osg::Matrix::ortho2D(0, 1, 0, 1));
        mDebugCamera->setViewMatrix(osg::Matrix::identity());
        mDebugCamera->getOrCreateStateSet()->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
        mDebugCamera->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        mDebugCamera->setCullingActive(false);

        // Scale viewport to show in bottom-left corner (400px wide, aspect-correct height)
        float displayWidth = 400.0f;
        float displayHeight = displayWidth * static_cast<float>(h) / static_cast<float>(w);
        mDebugCamera->setViewport(0, 0, static_cast<int>(displayWidth), static_cast<int>(displayHeight));

        // Create textured quad
        osg::ref_ptr<osg::Geometry> quad
            = osg::createTexturedQuadGeometry(osg::Vec3(0, 0, 0), osg::Vec3(1, 0, 0), osg::Vec3(0, 1, 0));
        quad->setCullingActive(false);

        osg::StateSet* ss = quad->getOrCreateStateSet();
        ss->setTextureAttributeAndModes(0, mDebugTexture, osg::StateAttribute::ON);

        mDebugCamera->addChild(quad);
    }

    void SceneOcclusionCallback::updateDebugOverlay(osgUtil::CullVisitor* cv)
    {
        if (!mDebugCamera)
            return;

        unsigned int w, h;
        mCuller->getResolution(w, h);

        // Read depth buffer from MOC
        mCuller->computePixelDepthBuffer(mDepthPixels.data());

        // Copy to image (normalize: MOC stores 1/w, so closer = larger values)
        float* imageData = reinterpret_cast<float*>(mDebugImage->data());
        for (unsigned int i = 0; i < w * h; ++i)
        {
            float d = mDepthPixels[i];
            // MOC depth is 1/w (reciprocal clip-space w). 0 = far/empty, larger = closer.
            // Clamp and invert for visualization: dark = far, bright = near
            imageData[i] = std::min(d * 50.0f, 1.0f);
        }
        mDebugImage->dirty();

        // Inject debug camera into the cull visitor so it gets rendered
        unsigned int traversalMask = cv->getTraversalMask();
        cv->setTraversalMask(0xffffffff);
        mDebugCamera->accept(*cv);
        cv->setTraversalMask(traversalMask);
    }

    void SceneOcclusionCallback::operator()(osg::Node* node, osgUtil::CullVisitor* cv)
    {
        // Only run occlusion for the main scene camera.
        // Skip shadow cameras, water reflection, and any other cameras.
        osg::Camera* cam = cv->getCurrentCamera();
        if (cam->getName() != Constants::SceneCamera)
        {
            traverse(node, cv);
            return;
        }

        // Skip if no terrain data (interiors)
        if (!mTerrainOccluder->hasTerrainData())
        {
            traverse(node, cv);
            return;
        }

        // Begin occlusion frame with camera matrices
        mCuller->beginFrame(cam->getViewMatrix(), cam->getProjectionMatrix());

        // Build and rasterize terrain occluder mesh
        if (mEnableTerrainOccluder)
        {
            mPositions.clear();
            mIndices.clear();
            mTerrainOccluder->build(cv->getEyePoint(), mRadiusCells, mPositions, mIndices);

            if (!mPositions.empty())
                mCuller->rasterizeOccluder(mPositions, mIndices);
        }

        // Continue normal cull traversal — CellOcclusionCallbacks will test against the buffer
        traverse(node, cv);

        // Update debug overlay AFTER traversal (terrain + building occluders now in buffer)
        if (mEnableDebugOverlay)
        {
            if (!mDebugCamera)
                setupDebugOverlay();
            updateDebugOverlay(cv);
        }

        static int frameCount = 0;
        if (++frameCount % 300 == 0)
            Log(Debug::Info) << "OcclusionCull: terrain tris=" << (mIndices.size() / 3)
                                << " bldg occluders=" << mCuller->getNumBuildingOccluders()
                                << " tested=" << mCuller->getNumTested()
                                << " occluded=" << mCuller->getNumOccluded();
    }

    CellOcclusionCallback::CellOcclusionCallback(
        SceneUtil::OcclusionCuller* culler, float occluderMinRadius, float occluderMaxRadius,
        float occluderShrinkFactor, bool enableStaticOccluders)
        : mCuller(culler)
        , mOccluderMinRadius(occluderMinRadius)
        , mOccluderMaxRadius(occluderMaxRadius)
        , mOccluderShrinkFactor(occluderShrinkFactor)
        , mEnableStaticOccluders(enableStaticOccluders)
    {
    }

    const osg::BoundingBox& CellOcclusionCallback::getTightBounds(osg::Node* node)
    {
        auto it = mTightBoundsCache.find(node);
        if (it != mTightBoundsCache.end())
            return it->second;

        osg::ComputeBoundsVisitor cbv;
        node->accept(cbv);
        const osg::BoundingBox& bb = cbv.getBoundingBox();

        Log(Debug::Info) << "OccBB cached: \"" << node->getName() << "\" tight="
                           << (bb.xMax() - bb.xMin()) << "x" << (bb.yMax() - bb.yMin()) << "x"
                           << (bb.zMax() - bb.zMin()) << " sphere=" << node->getBound().radius();

        return mTightBoundsCache.emplace(node, bb).first->second;
    }

    void CellOcclusionCallback::operator()(osg::Group* node, osgUtil::CullVisitor* cv)
    {
        // If occlusion is not active this frame (interior, shadow camera, etc.), traverse normally
        if (!mCuller->isFrameActive())
        {
            traverse(node, cv);
            return;
        }

        // Test cell bounding box first — if fully occluded, skip entire cell
        const osg::BoundingSphere& cellBS = node->getBound();
        if (cellBS.valid())
        {
            osg::BoundingBox cellBB;
            cellBB.expandBy(cellBS);

            if (!mCuller->testVisibleAABB(cellBB))
                return; // Entire cell occluded — no children traversed
        }

        const unsigned int numChildren = node->getNumChildren();

        // Pass 1: Large objects — test against terrain depth, optionally rasterize as occluders
        for (unsigned int i = 0; i < numChildren; ++i)
        {
            osg::Node* child = node->getChild(i);
            const osg::BoundingSphere& bs = child->getBound();

            if (!bs.valid() || bs.radius() < mOccluderMinRadius)
                continue;

            // Objects above maxRadius are still rendered, just not used as occluders
            // (they're likely terrain chunks or object paging nodes)
            if (bs.radius() > mOccluderMaxRadius)
            {
                child->accept(*cv);
                continue;
            }

            // Use tight AABB from ComputeBoundsVisitor for accurate occluder shape
            const osg::BoundingBox& tightBB = getTightBounds(child);
            if (!tightBB.valid())
                continue;

            if (mCuller->testVisibleAABB(tightBB))
            {
                if (mEnableStaticOccluders)
                {
                    // Don't rasterize as occluder if camera is inside or very near the object —
                    // otherwise its AABB fills the screen and falsely occludes everything behind it
                    float distSq = (bs.center() - cv->getEyePoint()).length2();
                    if (distSq > bs.radius() * bs.radius())
                    {
                        osg::Vec3f center = tightBB.center();
                        osg::Vec3f halfExtents = (tightBB._max - tightBB._min) * 0.5f * mOccluderShrinkFactor;
                        osg::BoundingBox occBB;
                        occBB.set(center - halfExtents, center + halfExtents);
                        mCuller->rasterizeAABBOccluder(occBB);
                    }
                }

                child->accept(*cv);
            }
            // else: occluded by terrain — skip entirely
        }

        // Pass 2: Small objects — test against enriched depth buffer (terrain + buildings)
        for (unsigned int i = 0; i < numChildren; ++i)
        {
            osg::Node* child = node->getChild(i);
            const osg::BoundingSphere& bs = child->getBound();

            if (!bs.valid())
            {
                child->accept(*cv);
                continue;
            }

            if (bs.radius() >= mOccluderMinRadius)
                continue; // Already handled in pass 1

            // Never occlude doors — they sit flush against building surfaces
            // and are easily falsely hidden by the parent building's AABB occluder
            bool skipOcclusion = false;
            child->getUserValue("skipOcclusion", skipOcclusion);

            osg::BoundingBox childBB;
            childBB.expandBy(bs);

            if (skipOcclusion || mCuller->testVisibleAABB(childBB))
                child->accept(*cv);
            // else: occluded — skip
        }
    }
}
