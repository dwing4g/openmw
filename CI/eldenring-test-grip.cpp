// Measure support-hand placement without a renderer or proprietary game data.
// An optional flat triangle fixture permits a local audit of installed meshes.
#include <apps/openmw/mwrender/eldengrip.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
    using MWRender::EldenPose::GripTriangle;
    using MWRender::EldenPose::findSwordSupportGrip;

    int checks = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
        ++checks;
    }

    void close(float actual, float expected, const char* message)
    {
        check(std::isfinite(actual) && std::abs(actual - expected) < 0.002f, message);
    }

    void box(std::vector<GripTriangle>& triangles, const osg::Vec3f& low, const osg::Vec3f& high)
    {
        std::array<osg::Vec3f, 8> corners;
        for (unsigned i = 0; i < corners.size(); ++i)
            corners[i].set(i & 1 ? high.x() : low.x(), i & 2 ? high.y() : low.y(),
                i & 4 ? high.z() : low.z());
        constexpr unsigned faces[6][4] = {
            { 0, 1, 3, 2 }, { 4, 6, 7, 5 }, { 0, 4, 5, 1 },
            { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 5, 7, 3 },
        };
        for (const auto& face : faces)
        {
            triangles.push_back({ corners[face[0]], corners[face[1]], corners[face[2]] });
            triangles.push_back({ corners[face[0]], corners[face[2]], corners[face[3]] });
        }
    }

    osg::Vec3f grip(const std::vector<GripTriangle>& triangles, float desired = -12.5f, float palm = 3.f)
    {
        const auto result = findSwordSupportGrip(triangles, desired, palm);
        check(result.has_value(), "usable sword handle was not found");
        check(std::isfinite(result->x()) && std::isfinite(result->y()) && std::isfinite(result->z()),
            "grip placement is non-finite");
        return *result;
    }

    void handleLengths()
    {
        std::vector<GripTriangle> longHandle;
        box(longHandle, { -1.f, -25.f, -0.8f }, { 1.f, 3.f, 0.8f });
        const osg::Vec3f normal = grip(longHandle);
        close(normal.x(), 0.f, "straight handle shifted the support hand sideways");
        close(normal.y(), -12.5f, "long handle changed the requested hand spacing");
        close(normal.z(), 0.f, "straight handle shifted the support hand out of its plane");

        std::vector<GripTriangle> shortHandle;
        box(shortHandle, { -1.5f, -10.f, -1.f }, { 1.5f, 3.f, 1.f });
        close(grip(shortHandle).y(), -7.f, "short handle leaves part of the supporting palm beyond the pommel");
        close(grip(longHandle, -1.f).y(), -3.f, "supporting palm overlaps the leading hand's attachment origin");
        for (auto& triangle : shortHandle)
            for (auto& vertex : triangle)
                vertex *= 2.f;
        close(grip(shortHandle, -25.f, 6.f).y(), -14.f, "grip measurement does not follow model and palm scale");

        std::vector<GripTriangle> tiny;
        box(tiny, { -1.f, -4.f, -1.f }, { 1.f, 3.f, 1.f });
        check(!findSwordSupportGrip(tiny, -12.5f, 3.f), "palm was placed on a handle too short to support it");
    }

    void offsetsAndOrnaments()
    {
        std::vector<GripTriangle> offset;
        box(offset, { -2.f, -20.f, -1.5f }, { 0.f, 3.f, 0.f });
        box(offset, { 10.f, -30.f, -1.f }, { 35.f, 20.f, 1.f });
        const auto result = grip(offset);
        close(result.x(), -1.f, "blade ornament displaced the measured handle center");
        close(result.y(), -12.5f, "disconnected blade changed the handle's usable length");
        close(result.z(), -0.75f, "offset handle lost its measured depth");
        std::reverse(offset.begin(), offset.end());
        const auto reversed = grip(offset);
        close((reversed - result).length(), 0.f, "mesh traversal order changed the grip target");

        std::vector<GripTriangle> guarded;
        box(guarded, { -1.f, -15.f, -0.6f }, { 1.f, 3.f, 0.6f });
        box(guarded, { -8.f, -22.f, -0.8f }, { 8.f, -15.f, 0.8f });
        close(grip(guarded, -20.f).y(), -12.f, "broad lower guard was mistaken for additional handle length");

        std::vector<GripTriangle> gap;
        box(gap, { -1.f, -8.f, -1.f }, { 1.f, 3.f, 1.f });
        box(gap, { -1.f, -25.f, -1.f }, { 1.f, -12.f, 1.f });
        close(grip(gap).y(), -5.f, "measurement jumped a gap to a detached piece of geometry");
    }

    void unavailableGeometry()
    {
        check(!findSwordSupportGrip({}, -12.5f, 3.f), "missing geometry fabricated a grip point");
        std::vector<GripTriangle> sheet = {
            { osg::Vec3f(-1, -20, 0), osg::Vec3f(1, -20, 0), osg::Vec3f(1, 3, 0) },
        };
        check(!findSwordSupportGrip(sheet, -12.5f, 3.f), "open blade surface was mistaken for a handle");
        std::vector<GripTriangle> handle;
        box(handle, { -1.f, -20.f, -1.f }, { 1.f, 3.f, 1.f });
        for (float palm : { 0.f, -1.f, std::numeric_limits<float>::infinity() })
            check(!findSwordSupportGrip(handle, -12.5f, palm), "invalid palm dimensions were accepted");
        for (float desired : { 0.f, 1.f, -10000.f, std::numeric_limits<float>::quiet_NaN() })
            check(!findSwordSupportGrip(handle, desired, 3.f), "invalid or unbounded search was accepted");
        handle.front()[0].x() = std::numeric_limits<float>::quiet_NaN();
        const auto partial = findSwordSupportGrip(handle, -12.5f, 3.f);
        check(!partial || (std::isfinite(partial->x()) && std::isfinite(partial->y()) && std::isfinite(partial->z())),
            "invalid mesh data produced a non-finite grip");
    }

    void installedMeshes(const char* filename)
    {
        std::ifstream stream(filename);
        check(static_cast<bool>(stream), "could not open local sword geometry fixture");
        std::string name;
        unsigned count;
        float lowY, highY, centerX, centerZ, centerTolerance;
        unsigned models = 0;
        // Each model: name, triangle count, expected Y range, approximate X/Z
        // center and tolerance; then nine floating-point values per triangle.
        while (stream >> name >> count >> lowY >> highY >> centerX >> centerZ >> centerTolerance)
        {
            check(count > 0 && count <= 32768, "invalid local triangle count");
            std::vector<GripTriangle> triangles(count);
            for (auto& triangle : triangles)
                for (auto& vertex : triangle)
                    check(static_cast<bool>(stream >> vertex.x() >> vertex.y() >> vertex.z()),
                        "truncated local sword geometry fixture");
            const auto result = grip(triangles);
            check(result.y() >= lowY && result.y() <= highY, "installed sword support hand exceeds its handle range");
            check(std::hypot(result.x() - centerX, result.z() - centerZ) <= centerTolerance,
                "installed sword grip follows an ornament instead of its handle");
            std::cout << name << ": " << result.x() << ' ' << result.y() << ' ' << result.z() << '\n';
            ++models;
        }
        check(stream.eof() && models > 0, "invalid or empty local sword geometry fixture");
        std::cout << models << " installed sword meshes checked\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        handleLengths();
        offsetsAndOrnaments();
        unavailableGeometry();
        if (argc > 1)
            installedMeshes(argv[1]);
        std::cout << "Elden sword grip: " << checks << " checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
