#ifndef OPENMW_MWRENDER_ELDENGRIP_H
#define OPENMW_MWRENDER_ELDENGRIP_H

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <osg/Vec3f>

namespace MWRender::EldenPose
{
    using GripTriangle = std::array<osg::Vec3f, 3>;

    namespace GripDetail
    {
        inline bool finite(const osg::Vec3f& value)
        {
            return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z());
        }

        // Intersect actual surfaces, keeping separate ornaments separate. A
        // whole-model bounding box includes the blades of a crescent sword.
        inline std::optional<osg::Vec3f> shaftSection(
            std::span<const GripTriangle> triangles, float y, float palmHalfWidth)
        {
            struct Point
            {
                osg::Vec3f mPosition;
                std::size_t mGroup;
                unsigned mEdges = 0;
            };
            std::vector<Point> points;
            const float epsilon = palmHalfWidth * 0.001f;
            const auto pointIndex = [&](const osg::Vec3f& position) {
                for (std::size_t i = 0; i < points.size(); ++i)
                    if ((points[i].mPosition - position).length2() <= epsilon * epsilon)
                        return i;
                points.push_back({ position, points.size(), 0 });
                return points.size() - 1;
            };
            for (const GripTriangle& triangle : triangles)
            {
                if (!finite(triangle[0]) || !finite(triangle[1]) || !finite(triangle[2]))
                    continue;
                std::array<osg::Vec3f, 2> ends;
                unsigned count = 0;
                for (unsigned i = 0; i < 3; ++i)
                {
                    const osg::Vec3f& a = triangle[i];
                    const osg::Vec3f& b = triangle[(i + 1) % 3];
                    if ((a.y() <= y && y < b.y()) || (b.y() <= y && y < a.y()))
                        ends[count++] = a + (b - a) * ((y - a.y()) / (b.y() - a.y()));
                }
                if (count != 2 || (ends[0] - ends[1]).length2() <= epsilon * epsilon)
                    continue;
                const std::size_t a = pointIndex(ends[0]);
                const std::size_t b = pointIndex(ends[1]);
                if (a == b)
                    continue;
                ++points[a].mEdges;
                ++points[b].mEdges;
                const std::size_t oldGroup = points[b].mGroup;
                const std::size_t newGroup = points[a].mGroup;
                if (oldGroup != newGroup)
                    for (Point& point : points)
                        if (point.mGroup == oldGroup)
                            point.mGroup = newGroup;
                // Malformed or unusually detailed meshes use the normal pose.
                if (points.size() > 2048)
                    return std::nullopt;
            }

            std::optional<osg::Vec3f> nearest;
            float nearestDistance = std::numeric_limits<float>::max();
            for (std::size_t group = 0; group < points.size(); ++group)
            {
                osg::Vec3f low(std::numeric_limits<float>::max(), y, std::numeric_limits<float>::max());
                osg::Vec3f high(-std::numeric_limits<float>::max(), y, -std::numeric_limits<float>::max());
                unsigned count = 0;
                bool closed = true;
                for (const Point& point : points)
                {
                    if (point.mGroup != group)
                        continue;
                    ++count;
                    closed = closed && point.mEdges >= 2;
                    low.x() = std::min(low.x(), point.mPosition.x());
                    low.z() = std::min(low.z(), point.mPosition.z());
                    high.x() = std::max(high.x(), point.mPosition.x());
                    high.z() = std::max(high.z(), point.mPosition.z());
                }
                if (count < 3 || !closed || high.x() - low.x() <= epsilon || high.z() - low.z() <= epsilon
                    || high.x() - low.x() > 2.f * palmHalfWidth || high.z() - low.z() > 2.f * palmHalfWidth)
                    continue;
                const osg::Vec3f center = (low + high) * 0.5f;
                const float distance = center.x() * center.x() + center.z() * center.z();
                if (distance <= 4.f * palmHalfWidth * palmHalfWidth && distance < nearestDistance)
                {
                    nearest = center;
                    nearestDistance = distance;
                }
            }
            return nearest;
        }
    }

    // Morrowind swords attach at the leading hand near local Y=0. Their
    // handles extend towards negative Y. Keep the complete supporting palm on
    // the connected shaft, shortening its spacing for shorter weapon models.
    // Call when a weapon changes, using triangles transformed into its local
    // attachment coordinates. No geometry is changed by this measurement.
    inline std::optional<osg::Vec3f> findSwordSupportGrip(
        std::span<const GripTriangle> triangles, float desiredY, float palmHalfWidth)
    {
        if (triangles.empty() || triangles.size() > 32768 || !std::isfinite(desiredY)
            || !std::isfinite(palmHalfWidth) || palmHalfWidth <= 0.f || desiredY >= 0.f
            || -desiredY > 32.f * palmHalfWidth)
            return std::nullopt;
        const float desired = std::min(desiredY, -palmHalfWidth);
        const float end = desired - palmHalfWidth;
        const unsigned steps = std::clamp(static_cast<unsigned>(std::ceil(-end / (palmHalfWidth / 6.f))), 1u, 128u);
        const float origin = -palmHalfWidth * 0.001f;
        if (!GripDetail::shaftSection(triangles, origin, palmHalfWidth))
            return std::nullopt;
        float bottom = origin;
        for (unsigned step = 1; step <= steps; ++step)
        {
            const float y = end * static_cast<float>(step) / static_cast<float>(steps);
            if (GripDetail::shaftSection(triangles, y, palmHalfWidth))
            {
                bottom = y;
                continue;
            }
            // Find the end of the usable handle without snapping to the scan
            // interval. Binary refinement stays on the last supported span.
            float outside = y;
            for (unsigned refinement = 0; refinement < 10; ++refinement)
            {
                const float middle = (bottom + outside) * 0.5f;
                if (GripDetail::shaftSection(triangles, middle, palmHalfWidth))
                    bottom = middle;
                else
                    outside = middle;
            }
            break;
        }
        if (bottom > -2.f * palmHalfWidth)
            return std::nullopt;
        const float supported = std::max(desired, bottom + palmHalfWidth);
        return GripDetail::shaftSection(triangles, supported, palmHalfWidth);
    }
}

#endif
