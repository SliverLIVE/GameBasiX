/* GameBasiX - OpenGL 3D module
 *
 * A complete 3D wrapper with two interchangeable backends:
 *
 *   Windows: a hidden Win32 window with an OpenGL 1.1 context. The finished
 *            frame is read back and handed to gbxinline::blitScreen(), so the
 *            normal GameBasiX screen, input and 2D drawing keep working.
 *   HTML5:   a built-in software rasterizer with z-buffer, Gouraud colours,
 *            alpha blending, back-face culling, thick lines and point squares.
 *
 * Geometry is generated on the CPU by the shared builders in this header; the
 * backends only rasterise it. Transform stack, lighting and render state
 * therefore behave the same on both paths.
 *
 * Define GL3D_SOFTWARE to force the software renderer on Windows; that is how
 * the browser path can be compiled and tested on a desktop machine.
 *
 * This header is meant to be included once, from the INLINE block of
 * OPENGL3D.gbx.
 */
#ifndef GBX_OPENGL3D_H
#define GBX_OPENGL3D_H

#include <gbx_inline.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32) && !defined(GL3D_SOFTWARE)
#define GL3D_WINDOWS_OPENGL 1
#else
#define GL3D_WINDOWS_OPENGL 0
#endif

#if GL3D_WINDOWS_OPENGL
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gl3d {

/* --------------------------------------------------------------- colours */

struct Rgb {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

/** Unpacks a GBX colour (0xRRGGBB or 0xAARRGGBB); the alpha byte is ignored. */
inline Rgb unpackColor(double packed) {
    const unsigned int value =
        static_cast<unsigned int>(static_cast<unsigned long long>(packed) & 0xFFFFFFFFull);
    return Rgb{static_cast<float>((value >> 16) & 0xFFu) / 255.0f,
               static_cast<float>((value >> 8) & 0xFFu) / 255.0f,
               static_cast<float>(value & 0xFFu) / 255.0f};
}

inline Rgb shade(const Rgb& color, float factor) {
    return Rgb{color.r * factor, color.g * factor, color.b * factor};
}

inline unsigned char channel(float value) {
    const int byte = static_cast<int>(value * 255.0f + 0.5f);
    return static_cast<unsigned char>(byte < 0 ? 0 : (byte > 255 ? 255 : byte));
}

/* --------------------------------------------------------------- vectors */

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 makeVec(double x, double y, double z) {
    return Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

inline Vec3 addVec(const Vec3& a, const Vec3& b) {
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 subVec(const Vec3& a, const Vec3& b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 scaleVec(const Vec3& v, float factor) {
    return Vec3{v.x * factor, v.y * factor, v.z * factor};
}

inline float dotVec(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 crossVec(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float lengthVec(const Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline Vec3 normalize(const Vec3& v) {
    const float len = lengthVec(v);
    if (len < 1e-9f) return Vec3{0.0f, 1.0f, 0.0f};
    const float inv = 1.0f / len;
    return Vec3{v.x * inv, v.y * inv, v.z * inv};
}

inline float clampFloat(float value, float minimum, float maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

/* ------------------------------------------------------------- matrices */

/** 4x4 matrix in OpenGL order (column-major, translation in m[12..14]). */
struct Mat4 {
    float m[16];
};

inline Mat4 identityMatrix() {
    return Mat4{{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
}

inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 out{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[column * 4 + k];
            out.m[column * 4 + row] = sum;
        }
    }
    return out;
}

inline Mat4 translationMatrix(double x, double y, double z) {
    Mat4 out = identityMatrix();
    out.m[12] = static_cast<float>(x);
    out.m[13] = static_cast<float>(y);
    out.m[14] = static_cast<float>(z);
    return out;
}

inline Mat4 scaleMatrix(double x, double y, double z) {
    Mat4 out = identityMatrix();
    out.m[0] = static_cast<float>(x);
    out.m[5] = static_cast<float>(y);
    out.m[10] = static_cast<float>(z);
    return out;
}

inline Mat4 rotationXMatrix(double degrees) {
    Mat4 out = identityMatrix();
    const double radians = degrees * 3.14159265358979323846 / 180.0;
    const float c = static_cast<float>(std::cos(radians));
    const float s = static_cast<float>(std::sin(radians));
    out.m[5] = c;
    out.m[6] = s;
    out.m[9] = -s;
    out.m[10] = c;
    return out;
}

inline Mat4 rotationYMatrix(double degrees) {
    Mat4 out = identityMatrix();
    const double radians = degrees * 3.14159265358979323846 / 180.0;
    const float c = static_cast<float>(std::cos(radians));
    const float s = static_cast<float>(std::sin(radians));
    out.m[0] = c;
    out.m[2] = -s;
    out.m[8] = s;
    out.m[10] = c;
    return out;
}

inline Mat4 rotationZMatrix(double degrees) {
    Mat4 out = identityMatrix();
    const double radians = degrees * 3.14159265358979323846 / 180.0;
    const float c = static_cast<float>(std::cos(radians));
    const float s = static_cast<float>(std::sin(radians));
    out.m[0] = c;
    out.m[1] = s;
    out.m[4] = -s;
    out.m[5] = c;
    return out;
}

/** Rotation of `degrees` around an arbitrary axis (Rodrigues). */
inline Mat4 rotationAxisMatrix(double degrees, double ax, double ay, double az) {
    Vec3 axis = normalize(makeVec(ax, ay, az));
    if (lengthVec(makeVec(ax, ay, az)) < 1e-9f) return identityMatrix();
    const double radians = degrees * 3.14159265358979323846 / 180.0;
    const float c = static_cast<float>(std::cos(radians));
    const float s = static_cast<float>(std::sin(radians));
    const float t = 1.0f - c;
    const float x = axis.x, y = axis.y, z = axis.z;

    Mat4 out = identityMatrix();
    out.m[0] = t * x * x + c;
    out.m[1] = t * x * y + s * z;
    out.m[2] = t * x * z - s * y;
    out.m[4] = t * x * y - s * z;
    out.m[5] = t * y * y + c;
    out.m[6] = t * y * z + s * x;
    out.m[8] = t * x * z + s * y;
    out.m[9] = t * y * z - s * x;
    out.m[10] = t * z * z + c;
    return out;
}

inline Mat4 perspectiveMatrix(double fovDegrees, double aspect, double nearValue,
                              double farValue) {
    Mat4 out{};
    const double radians = fovDegrees * 3.14159265358979323846 / 180.0;
    const float focus = static_cast<float>(1.0 / std::tan(radians * 0.5));
    out.m[0] = static_cast<float>(focus / (aspect > 0.0 ? aspect : 1.0));
    out.m[5] = focus;
    out.m[10] = static_cast<float>((farValue + nearValue) / (nearValue - farValue));
    out.m[11] = -1.0f;
    out.m[14] = static_cast<float>((2.0 * farValue * nearValue) / (nearValue - farValue));
    return out;
}

inline Mat4 orthoMatrix(double left, double right, double bottom, double top, double nearValue,
                        double farValue) {
    Mat4 out = identityMatrix();
    const double width = right - left;
    const double height = top - bottom;
    const double depth = farValue - nearValue;
    if (std::fabs(width) < 1e-9 || std::fabs(height) < 1e-9 || std::fabs(depth) < 1e-9) {
        return out;
    }
    out.m[0] = static_cast<float>(2.0 / width);
    out.m[5] = static_cast<float>(2.0 / height);
    out.m[10] = static_cast<float>(-2.0 / depth);
    out.m[12] = static_cast<float>(-(right + left) / width);
    out.m[13] = static_cast<float>(-(top + bottom) / height);
    out.m[14] = static_cast<float>(-(farValue + nearValue) / depth);
    return out;
}

/** LookAt view matrix; a camera looking straight up or down falls back to
 *  another world up. Degenerate eye == target keeps the identity. */
inline Mat4 viewMatrix(double ex, double ey, double ez, double tx, double ty, double tz) {
    Mat4 out = identityMatrix();
    double fx = tx - ex, fy = ty - ey, fz = tz - ez;
    const double flen = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (flen < 1e-9) return out;
    fx /= flen;
    fy /= flen;
    fz /= flen;

    double ux0 = 0.0, uy0 = 1.0, uz0 = 0.0;
    double sx = fy * uz0 - fz * uy0;
    double sy = fz * ux0 - fx * uz0;
    double sz = fx * uy0 - fy * ux0;
    double slen = std::sqrt(sx * sx + sy * sy + sz * sz);
    if (slen < 1e-6) {
        ux0 = 0.0;
        uy0 = 0.0;
        uz0 = 1.0;
        sx = fy * uz0 - fz * uy0;
        sy = fz * ux0 - fx * uz0;
        sz = fx * uy0 - fy * ux0;
        slen = std::sqrt(sx * sx + sy * sy + sz * sz);
    }
    if (slen < 1e-9) return out;
    sx /= slen;
    sy /= slen;
    sz /= slen;

    const double ux = sy * fz - sz * fy;
    const double uy = sz * fx - sx * fz;
    const double uz = sx * fy - sy * fx;

    out.m[0] = static_cast<float>(sx);
    out.m[1] = static_cast<float>(ux);
    out.m[2] = static_cast<float>(-fx);
    out.m[3] = 0.0f;
    out.m[4] = static_cast<float>(sy);
    out.m[5] = static_cast<float>(uy);
    out.m[6] = static_cast<float>(-fy);
    out.m[7] = 0.0f;
    out.m[8] = static_cast<float>(sz);
    out.m[9] = static_cast<float>(uz);
    out.m[10] = static_cast<float>(-fz);
    out.m[11] = 0.0f;
    out.m[12] = static_cast<float>(-(sx * ex + sy * ey + sz * ez));
    out.m[13] = static_cast<float>(-(ux * ex + uy * ey + uz * ez));
    out.m[14] = static_cast<float>(fx * ex + fy * ey + fz * ez);
    out.m[15] = 1.0f;
    return out;
}

inline Vec3 transformPoint(const Mat4& matrix, const Vec3& point) {
    const float* m = matrix.m;
    return Vec3{m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12],
                m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13],
                m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14]};
}

inline Vec3 transformDirection(const Mat4& matrix, const Vec3& direction) {
    const float* m = matrix.m;
    return normalize(Vec3{m[0] * direction.x + m[4] * direction.y + m[8] * direction.z,
                          m[1] * direction.x + m[5] * direction.y + m[9] * direction.z,
                          m[2] * direction.x + m[6] * direction.y + m[10] * direction.z});
}

/* ------------------------------------------------------------ scene state */

/** Render state shared by both backends. */
struct State {
    /* direction towards the light, default above/right/front */
    Vec3 light{0.376f, 0.799f, 0.470f};
    float ambient = 0.35f;
    float alpha = 1.0f;
    float lineWidth = 1.0f;
    float pointSize = 3.0f;
    bool depthTest = true;
    bool cull = false;
    bool blend = true;
};

/** Model matrix with the usual push/pop stack. Reset by GL3D_BEGIN. */
struct ModelStack {
    Mat4 matrix = identityMatrix();
    std::vector<Mat4> stack;

    void reset() {
        matrix = identityMatrix();
        stack.clear();
    }

    void push() {
        if (stack.size() < 64) stack.push_back(matrix);
    }

    void pop() {
        if (stack.empty()) return;
        matrix = stack.back();
        stack.pop_back();
    }

    void translate(double x, double y, double z) {
        matrix = multiply(matrix, translationMatrix(x, y, z));
    }

    void rotate(double degrees, double ax, double ay, double az) {
        matrix = multiply(matrix, rotationAxisMatrix(degrees, ax, ay, az));
    }

    void scale(double x, double y, double z) {
        matrix = multiply(matrix, scaleMatrix(x, y, z));
    }
};

/* ---------------------------------------------------------------- meshes */

struct Vertex {
    Vec3 position;
    Rgb color;
    /* texture coordinates, 0,0 = top-left of the bound image */
    float u = 0.0f;
    float v = 0.0f;
};

/** Vertices for one frame; the backend only has to rasterise them. */
struct Mesh {
    std::vector<Vertex> triangles;
    std::vector<Vertex> lines;
    std::vector<Vertex> points;
    /** true = sample the bound texture with the vertex uv coordinates */
    bool textured = false;

    void clear() {
        triangles.clear();
        lines.clear();
        points.clear();
        textured = false;
    }
};

/* -------------------------------------------------------------- geometry */

inline Rgb litColor(const Rgb& base, const Vec3& normal, const State& state) {
    const float lambert = std::max(0.0f, dotVec(normal, state.light));
    float factor = state.ambient + (1.0f - state.ambient) * lambert;
    if (factor > 1.0f) factor = 1.0f;
    return shade(base, factor);
}

inline void addFlatTriangle(Mesh& mesh, const Mat4& matrix, const Rgb& color, const Vec3& a,
                            const Vec3& b, const Vec3& c) {
    mesh.triangles.push_back(Vertex{transformPoint(matrix, a), color});
    mesh.triangles.push_back(Vertex{transformPoint(matrix, b), color});
    mesh.triangles.push_back(Vertex{transformPoint(matrix, c), color});
}

inline void addLitTriangle(Mesh& mesh, const Mat4& matrix, const State& state, const Rgb& base,
                           const Vec3& normal, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Rgb tone = litColor(base, transformDirection(matrix, normal), state);
    addFlatTriangle(mesh, matrix, tone, a, b, c);
}

inline void addSmoothTriangle(Mesh& mesh, const Mat4& matrix, const State& state, const Rgb& base,
                              const Vec3& a, const Vec3& na, const Vec3& b, const Vec3& nb,
                              const Vec3& c, const Vec3& nc) {
    mesh.triangles.push_back(
        Vertex{transformPoint(matrix, a), litColor(base, transformDirection(matrix, na), state)});
    mesh.triangles.push_back(
        Vertex{transformPoint(matrix, b), litColor(base, transformDirection(matrix, nb), state)});
    mesh.triangles.push_back(
        Vertex{transformPoint(matrix, c), litColor(base, transformDirection(matrix, nc), state)});
}

/** Box with half extents; the rotations turn the box around its own centre. */
inline void addBox(Mesh& mesh, const ModelStack& model, const State& state, double x, double y,
                   double z, double hx, double hy, double hz, const Rgb& color, double rotX,
                   double rotY, double rotZ) {
    if (hx <= 0.0 || hy <= 0.0 || hz <= 0.0) return;
    const Mat4 local =
        multiply(translationMatrix(x, y, z),
                 multiply(rotationXMatrix(rotX), multiply(rotationYMatrix(rotY), rotationZMatrix(rotZ))));
    const Mat4 matrix = multiply(model.matrix, local);

    /* corners are counter-clockwise seen from outside, so back-face culling
     * and the lighting normals agree on both backends */
    const Vec3 corners[6][4] = {
        /* +X */ {makeVec(hx, -hy, -hz), makeVec(hx, hy, -hz), makeVec(hx, hy, hz), makeVec(hx, -hy, hz)},
        /* -X */ {makeVec(-hx, -hy, hz), makeVec(-hx, hy, hz), makeVec(-hx, hy, -hz), makeVec(-hx, -hy, -hz)},
        /* +Y */ {makeVec(-hx, hy, -hz), makeVec(-hx, hy, hz), makeVec(hx, hy, hz), makeVec(hx, hy, -hz)},
        /* -Y */ {makeVec(-hx, -hy, -hz), makeVec(hx, -hy, -hz), makeVec(hx, -hy, hz), makeVec(-hx, -hy, hz)},
        /* +Z */ {makeVec(-hx, -hy, hz), makeVec(hx, -hy, hz), makeVec(hx, hy, hz), makeVec(-hx, hy, hz)},
        /* -Z */ {makeVec(hx, -hy, -hz), makeVec(-hx, -hy, -hz), makeVec(-hx, hy, -hz), makeVec(hx, hy, -hz)},
    };
    const Vec3 normals[6] = {Vec3{1, 0, 0},  Vec3{-1, 0, 0}, Vec3{0, 1, 0},
                             Vec3{0, -1, 0}, Vec3{0, 0, 1},  Vec3{0, 0, -1}};

    for (int face = 0; face < 6; ++face) {
        addLitTriangle(mesh, matrix, state, color, normals[face], corners[face][0],
                       corners[face][1], corners[face][2]);
        addLitTriangle(mesh, matrix, state, color, normals[face], corners[face][0],
                       corners[face][2], corners[face][3]);
    }
}

/** Sphere around (x, y, z) with smooth normals. */
inline void addSphere(Mesh& mesh, const ModelStack& model, const State& state, double x, double y,
                      double z, double radius, const Rgb& color, int segments) {
    if (radius <= 0.0) return;
    int segs = segments > 0 ? segments : 24;
    segs = std::max(6, std::min(segs, 96));
    const int rings = std::max(4, segs / 2);
    const Mat4 matrix = multiply(model.matrix, translationMatrix(x, y, z));
    const float r = static_cast<float>(radius);
    const float pi = 3.14159265358979323846f;

    auto point = [&](int ring, int segment) {
        const float theta = pi * static_cast<float>(ring) / static_cast<float>(rings);
        const float phi =
            2.0f * pi * static_cast<float>(segment % segs) / static_cast<float>(segs);
        const Vec3 normal{std::sin(theta) * std::cos(phi), std::cos(theta),
                          std::sin(theta) * std::sin(phi)};
        return Vertex{scaleVec(normal, r), Rgb{}};
    };

    for (int ring = 0; ring < rings; ++ring) {
        for (int segment = 0; segment < segs; ++segment) {
            const Vec3 a = point(ring, segment).position;
            const Vec3 b = point(ring, segment + 1).position;
            const Vec3 c = point(ring + 1, segment + 1).position;
            const Vec3 d = point(ring + 1, segment).position;
            const Vec3 na = normalize(a), nb = normalize(b), nc = normalize(c), nd = normalize(d);
            addSmoothTriangle(mesh, matrix, state, color, a, na, b, nb, c, nc);
            addSmoothTriangle(mesh, matrix, state, color, a, na, c, nc, d, nd);
        }
    }
}

/** Cylinder standing on (x, y, z); a top radius of 0 makes a cone. */
inline void addCylinder(Mesh& mesh, const ModelStack& model, const State& state, double x, double y,
                        double z, double radius, double height, const Rgb& color, int segments,
                        double topRadius) {
    if (radius <= 0.0 || height <= 0.0) return;
    int segs = segments > 0 ? segments : 24;
    segs = std::max(3, std::min(segs, 96));
    const float r = static_cast<float>(radius);
    float top = topRadius < 0.0 ? r : static_cast<float>(topRadius);
    if (top > r) top = r;
    const float h = static_cast<float>(height);
    const Mat4 matrix = multiply(model.matrix, translationMatrix(x, y, z));
    const float pi = 3.14159265358979323846f;
    const Vec3 up{0.0f, h, 0.0f};

    const Vec3 sideNormal = normalize(Vec3{h, r - top, 0.0f});

    for (int segment = 0; segment < segs; ++segment) {
        const float a0 = 2.0f * pi * static_cast<float>(segment) / static_cast<float>(segs);
        const float a1 = 2.0f * pi * static_cast<float>(segment + 1) / static_cast<float>(segs);
        const Vec3 d0{std::cos(a0), 0.0f, std::sin(a0)};
        const Vec3 d1{std::cos(a1), 0.0f, std::sin(a1)};

        const Vec3 b0 = scaleVec(d0, r);
        const Vec3 b1 = scaleVec(d1, r);
        const Vec3 t0 = addVec(scaleVec(d0, top), up);
        const Vec3 t1 = addVec(scaleVec(d1, top), up);

        /* normal tilted like the side, rotated into the segment direction */
        const Vec3 n0 = normalize(Vec3{sideNormal.x * d0.x, sideNormal.y, sideNormal.x * d0.z});
        const Vec3 n1 = normalize(Vec3{sideNormal.x * d1.x, sideNormal.y, sideNormal.x * d1.z});

        addSmoothTriangle(mesh, matrix, state, color, b0, n0, t0, n0, t1, n1);
        addSmoothTriangle(mesh, matrix, state, color, b0, n0, t1, n1, b1, n1);

        /* base disc, counter-clockwise seen from below */
        addLitTriangle(mesh, matrix, state, color, Vec3{0, -1, 0}, Vec3{0, 0, 0}, b0, b1);
        if (top > 0.0f) {
            const Vec3 q0 = addVec(scaleVec(d0, top), up);
            const Vec3 q1 = addVec(scaleVec(d1, top), up);
            addLitTriangle(mesh, matrix, state, color, Vec3{0, 1, 0}, up, q0, q1);
        }
    }
}

/** Flat quad through four points in local space, drawn in one colour. */
inline void addQuad(Mesh& mesh, const ModelStack& model, const Vec3& a, const Vec3& b, const Vec3& c,
                    const Vec3& d, const Rgb& color) {
    const Mat4 matrix = model.matrix;
    addFlatTriangle(mesh, matrix, color, a, b, c);
    addFlatTriangle(mesh, matrix, color, a, c, d);
}

inline void addGrid(Mesh& mesh, const ModelStack& model, double extent, double step,
                    const Rgb& color) {
    if (extent <= 0.0 || step <= 0.0) return;
    const Mat4 matrix = model.matrix;
    for (double offset = -extent; offset <= extent + 1e-9; offset += step) {
        mesh.lines.push_back(Vertex{transformPoint(matrix, makeVec(offset, 0.0, -extent)), color});
        mesh.lines.push_back(Vertex{transformPoint(matrix, makeVec(offset, 0.0, extent)), color});
        mesh.lines.push_back(Vertex{transformPoint(matrix, makeVec(-extent, 0.0, offset)), color});
        mesh.lines.push_back(Vertex{transformPoint(matrix, makeVec(extent, 0.0, offset)), color});
    }
}

inline void addLine(Mesh& mesh, const ModelStack& model, const Vec3& a, const Vec3& b,
                    const Rgb& color) {
    const Mat4 matrix = model.matrix;
    mesh.lines.push_back(Vertex{transformPoint(matrix, a), color});
    mesh.lines.push_back(Vertex{transformPoint(matrix, b), color});
}

inline void addTriangle(Mesh& mesh, const ModelStack& model, const Vec3& a, const Vec3& b,
                        const Vec3& c, const Rgb& color) {
    addFlatTriangle(mesh, model.matrix, color, a, b, c);
}

inline void addPoint(Mesh& mesh, const ModelStack& model, const Vec3& point, const Rgb& color) {
    mesh.points.push_back(Vertex{transformPoint(model.matrix, point), color});
}

/** X/Y/Z axes in red, green and blue. */
inline void addAxis(Mesh& mesh, const ModelStack& model, double length) {
    if (length <= 0.0) return;
    addLine(mesh, model, makeVec(0, 0, 0), makeVec(length, 0, 0), Rgb{0.95f, 0.35f, 0.35f});
    addLine(mesh, model, makeVec(0, 0, 0), makeVec(0, length, 0), Rgb{0.45f, 0.88f, 0.45f});
    addLine(mesh, model, makeVec(0, 0, 0), makeVec(0, 0, length), Rgb{0.42f, 0.66f, 0.98f});
}

/** Quad showing the bound texture; uv 0,0 is the top-left corner. */
inline void addTexturedQuad(Mesh& mesh, const ModelStack& model, const Vec3& a, const Vec3& b,
                            const Vec3& c, const Vec3& d, const Rgb& tint) {
    mesh.textured = true;
    const Mat4& matrix = model.matrix;
    const Vec3 corners[4] = {a, b, c, d};
    const float u[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float v[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) {
        const int index = order[i];
        mesh.triangles.push_back(Vertex{transformPoint(matrix, corners[index]), tint, u[index],
                                        v[index]});
    }
}

/** Box with the bound texture on every face, lit per face like addBox. */
inline void addTexturedBox(Mesh& mesh, const ModelStack& model, const State& state, double x,
                           double y, double z, double hx, double hy, double hz, const Rgb& tint,
                           double rotX, double rotY, double rotZ) {
    if (hx <= 0.0 || hy <= 0.0 || hz <= 0.0) return;
    mesh.textured = true;
    const Mat4 local =
        multiply(translationMatrix(x, y, z),
                 multiply(rotationXMatrix(rotX), multiply(rotationYMatrix(rotY), rotationZMatrix(rotZ))));
    const Mat4 matrix = multiply(model.matrix, local);

    const Vec3 corners[6][4] = {
        /* +X */ {makeVec(hx, -hy, -hz), makeVec(hx, hy, -hz), makeVec(hx, hy, hz), makeVec(hx, -hy, hz)},
        /* -X */ {makeVec(-hx, -hy, hz), makeVec(-hx, hy, hz), makeVec(-hx, hy, -hz), makeVec(-hx, -hy, -hz)},
        /* +Y */ {makeVec(-hx, hy, -hz), makeVec(-hx, hy, hz), makeVec(hx, hy, hz), makeVec(hx, hy, -hz)},
        /* -Y */ {makeVec(-hx, -hy, -hz), makeVec(hx, -hy, -hz), makeVec(hx, -hy, hz), makeVec(-hx, -hy, hz)},
        /* +Z */ {makeVec(-hx, -hy, hz), makeVec(hx, -hy, hz), makeVec(hx, hy, hz), makeVec(-hx, hy, hz)},
        /* -Z */ {makeVec(hx, -hy, -hz), makeVec(-hx, -hy, -hz), makeVec(-hx, hy, -hz), makeVec(hx, hy, -hz)},
    };
    const Vec3 normals[6] = {Vec3{1, 0, 0},  Vec3{-1, 0, 0}, Vec3{0, 1, 0},
                             Vec3{0, -1, 0}, Vec3{0, 0, 1},  Vec3{0, 0, -1}};
    const float u[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float v[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    const int order[6] = {0, 1, 2, 0, 2, 3};

    for (int face = 0; face < 6; ++face) {
        const Rgb tone = litColor(tint, transformDirection(matrix, normals[face]), state);
        for (int i = 0; i < 6; ++i) {
            const int index = order[i];
            mesh.triangles.push_back(Vertex{transformPoint(matrix, corners[face][index]), tone,
                                            u[index], v[index]});
        }
    }
}

/* ------------------------------------------------------------ backends */

#if GL3D_WINDOWS_OPENGL

/* -------------------------------------------------------- OpenGL 1.1 API */

using GlEnum = unsigned int;
using GlBitfield = unsigned int;
using GlInt = int;
using GlSizei = int;
using GlFloat = float;
using GlDouble = double;
using GlUbyte = unsigned char;

enum : unsigned int {
    kPoints = 0x0000u,
    kLines = 0x0001u,
    kTriangles = 0x0004u,
    kQuads = 0x0007u,
    kDepthBufferBit = 0x00000100u,
    kColorBufferBit = 0x00004000u,
    kLequal = 0x0203u,
    kBack = 0x0405u,
    kSmooth = 0x1D01u,
    kUnsignedByte = 0x1401u,
    kBlend = 0x0BE2u,
    kDepthTest = 0x0B71u,
    kCullFace = 0x0B44u,
    kLineSmooth = 0x0B20u,
    kLineSmoothHint = 0x0C52u,
    kPackAlignment = 0x0D05u,
    kNicest = 0x1102u,
    kSrcAlpha = 0x0302u,
    kOneMinusSrcAlpha = 0x0303u,
    kRgba = 0x1908u,
    kTexture2D = 0x0DE1u,
    kTextureMagFilter = 0x2800u,
    kTextureMinFilter = 0x2801u,
    kTextureWrapS = 0x2802u,
    kTextureWrapT = 0x2803u,
    kNearest = 0x2600u,
    kLinear = 0x2601u,
    kClamp = 0x2900u,
    kTextureEnv = 0x2300u,
    kTextureEnvMode = 0x2200u,
    kModulate = 0x2100u,
    kUnpackAlignment = 0x0CF5u,
    kModelView = 0x1700u,
    kProjection = 0x1701u,
    kRenderer = 0x1F01u,
    kVersion = 0x1F02u,
};

/** Entry points of the system DLLs, bound once in start(). */
struct Api {
    HMODULE opengl = nullptr;
    HMODULE gdi = nullptr;
    HMODULE user = nullptr;

    void(APIENTRY* glClearColor)(GlFloat, GlFloat, GlFloat, GlFloat) = nullptr;
    void(APIENTRY* glClear)(GlBitfield) = nullptr;
    void(APIENTRY* glViewport)(GlInt, GlInt, GlSizei, GlSizei) = nullptr;
    void(APIENTRY* glMatrixMode)(GlEnum) = nullptr;
    void(APIENTRY* glLoadIdentity)() = nullptr;
    void(APIENTRY* glFrustum)(GlDouble, GlDouble, GlDouble, GlDouble, GlDouble, GlDouble) = nullptr;
    void(APIENTRY* glOrtho)(GlDouble, GlDouble, GlDouble, GlDouble, GlDouble, GlDouble) = nullptr;
    void(APIENTRY* glMultMatrixf)(const GlFloat*) = nullptr;
    void(APIENTRY* glBegin)(GlEnum) = nullptr;
    void(APIENTRY* glEnd)() = nullptr;
    void(APIENTRY* glColor3f)(GlFloat, GlFloat, GlFloat) = nullptr;
    void(APIENTRY* glColor4f)(GlFloat, GlFloat, GlFloat, GlFloat) = nullptr;
    void(APIENTRY* glVertex3f)(GlFloat, GlFloat, GlFloat) = nullptr;
    void(APIENTRY* glTexCoord2f)(GlFloat, GlFloat) = nullptr;
    void(APIENTRY* glGenTextures)(GlSizei, unsigned int*) = nullptr;
    void(APIENTRY* glBindTexture)(GlEnum, unsigned int) = nullptr;
    void(APIENTRY* glDeleteTextures)(GlSizei, const unsigned int*) = nullptr;
    void(APIENTRY* glTexImage2D)(GlEnum, GlInt, GlInt, GlSizei, GlSizei, GlInt, GlEnum, GlEnum,
                                 const void*) = nullptr;
    void(APIENTRY* glTexParameteri)(GlEnum, GlEnum, GlInt) = nullptr;
    void(APIENTRY* glTexEnvi)(GlEnum, GlEnum, GlInt) = nullptr;
    void(APIENTRY* glEnable)(GlEnum) = nullptr;
    void(APIENTRY* glDisable)(GlEnum) = nullptr;
    void(APIENTRY* glDepthFunc)(GlEnum) = nullptr;
    void(APIENTRY* glShadeModel)(GlEnum) = nullptr;
    void(APIENTRY* glCullFace)(GlEnum) = nullptr;
    void(APIENTRY* glLineWidth)(GlFloat) = nullptr;
    void(APIENTRY* glPointSize)(GlFloat) = nullptr;
    void(APIENTRY* glBlendFunc)(GlEnum, GlEnum) = nullptr;
    void(APIENTRY* glHint)(GlEnum, GlEnum) = nullptr;
    void(APIENTRY* glPixelStorei)(GlEnum, GlInt) = nullptr;
    void(APIENTRY* glReadBuffer)(GlEnum) = nullptr;
    void(APIENTRY* glReadPixels)(GlInt, GlInt, GlSizei, GlSizei, GlEnum, GlEnum, void*) = nullptr;
    void(APIENTRY* glFinish)() = nullptr;
    const GlUbyte*(APIENTRY* glGetString)(GlEnum) = nullptr;

    int(WINAPI* ChoosePixelFormat)(HDC, const PIXELFORMATDESCRIPTOR*) = nullptr;
    BOOL(WINAPI* SetPixelFormat)(HDC, int, const PIXELFORMATDESCRIPTOR*) = nullptr;
    HGLRC(WINAPI* wglCreateContext)(HDC) = nullptr;
    BOOL(WINAPI* wglMakeCurrent)(HDC, HGLRC) = nullptr;
    BOOL(WINAPI* wglDeleteContext)(HGLRC) = nullptr;
    ATOM(WINAPI* RegisterClassA)(const WNDCLASSA*) = nullptr;
    HWND(WINAPI* CreateWindowExA)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU,
                                 HINSTANCE, LPVOID) = nullptr;
    BOOL(WINAPI* DestroyWindow)(HWND) = nullptr;
    LRESULT(WINAPI* DefWindowProcA)(HWND, UINT, WPARAM, LPARAM) = nullptr;
    HDC(WINAPI* GetDC)(HWND) = nullptr;
    int(WINAPI* ReleaseDC)(HWND, HDC) = nullptr;
};

struct Renderer {
    static Renderer& instance() {
        static Renderer renderer;
        return renderer;
    }

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        Renderer& r = instance();
        if (message == WM_ERASEBKGND) return 1;
        if (r.api.DefWindowProcA) return r.api.DefWindowProcA(window, message, wparam, lparam);
        return 0;
    }

    Api api;
    bool apiLoaded = false;
    bool classRegistered = false;
    bool ready = false;
    int width = 0;
    int height = 0;
    HWND window = nullptr;
    HDC device = nullptr;
    HGLRC context = nullptr;

    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    bool orthographic = false;
    double orthoLeft = 0.0;
    double orthoRight = 0.0;
    double orthoBottom = 0.0;
    double orthoTop = 0.0;
    double orthoNear = -1.0;
    double orthoFar = 1.0;

    Mat4 view = identityMatrix();
    ModelStack model;
    State state;

    std::vector<unsigned char> bottomUp;
    std::vector<unsigned char> topDown;
    unsigned int textureName = 0;
    bool textureReady = false;
    std::vector<unsigned char> upload;

    std::string info = "OpenGL 3D was not initialised";
    std::string error;

    /* --------------------------------------------------------- binding */

    bool loadApi() {
        if (apiLoaded) return true;

#define GL3D_BIND(module, member, symbol)                                            \
    do {                                                                             \
        api.member = reinterpret_cast<decltype(api.member)>(GetProcAddress(module, symbol)); \
        if (!api.member) {                                                            \
            error = std::string("opengl32/gdi32/user32: missing ") + symbol;          \
            return false;                                                             \
        }                                                                             \
    } while (0)

        api.opengl = LoadLibraryA("opengl32.dll");
        api.gdi = LoadLibraryA("gdi32.dll");
        api.user = LoadLibraryA("user32.dll");
        if (!api.opengl || !api.gdi || !api.user) {
            error = "opengl32.dll, gdi32.dll or user32.dll could not be loaded";
            return false;
        }

        GL3D_BIND(api.opengl, glClearColor, "glClearColor");
        GL3D_BIND(api.opengl, glClear, "glClear");
        GL3D_BIND(api.opengl, glViewport, "glViewport");
        GL3D_BIND(api.opengl, glMatrixMode, "glMatrixMode");
        GL3D_BIND(api.opengl, glLoadIdentity, "glLoadIdentity");
        GL3D_BIND(api.opengl, glFrustum, "glFrustum");
        GL3D_BIND(api.opengl, glOrtho, "glOrtho");
        GL3D_BIND(api.opengl, glMultMatrixf, "glMultMatrixf");
        GL3D_BIND(api.opengl, glBegin, "glBegin");
        GL3D_BIND(api.opengl, glEnd, "glEnd");
        GL3D_BIND(api.opengl, glColor3f, "glColor3f");
        GL3D_BIND(api.opengl, glColor4f, "glColor4f");
        GL3D_BIND(api.opengl, glVertex3f, "glVertex3f");
        GL3D_BIND(api.opengl, glTexCoord2f, "glTexCoord2f");
        GL3D_BIND(api.opengl, glGenTextures, "glGenTextures");
        GL3D_BIND(api.opengl, glBindTexture, "glBindTexture");
        GL3D_BIND(api.opengl, glDeleteTextures, "glDeleteTextures");
        GL3D_BIND(api.opengl, glTexImage2D, "glTexImage2D");
        GL3D_BIND(api.opengl, glTexParameteri, "glTexParameteri");
        GL3D_BIND(api.opengl, glTexEnvi, "glTexEnvi");
        GL3D_BIND(api.opengl, glEnable, "glEnable");
        GL3D_BIND(api.opengl, glDisable, "glDisable");
        GL3D_BIND(api.opengl, glDepthFunc, "glDepthFunc");
        GL3D_BIND(api.opengl, glShadeModel, "glShadeModel");
        GL3D_BIND(api.opengl, glCullFace, "glCullFace");
        GL3D_BIND(api.opengl, glLineWidth, "glLineWidth");
        GL3D_BIND(api.opengl, glPointSize, "glPointSize");
        GL3D_BIND(api.opengl, glBlendFunc, "glBlendFunc");
        GL3D_BIND(api.opengl, glHint, "glHint");
        GL3D_BIND(api.opengl, glPixelStorei, "glPixelStorei");
        GL3D_BIND(api.opengl, glReadBuffer, "glReadBuffer");
        GL3D_BIND(api.opengl, glReadPixels, "glReadPixels");
        GL3D_BIND(api.opengl, glFinish, "glFinish");
        GL3D_BIND(api.opengl, glGetString, "glGetString");

        GL3D_BIND(api.gdi, ChoosePixelFormat, "ChoosePixelFormat");
        GL3D_BIND(api.gdi, SetPixelFormat, "SetPixelFormat");
        GL3D_BIND(api.opengl, wglCreateContext, "wglCreateContext");
        GL3D_BIND(api.opengl, wglMakeCurrent, "wglMakeCurrent");
        GL3D_BIND(api.opengl, wglDeleteContext, "wglDeleteContext");
        GL3D_BIND(api.user, RegisterClassA, "RegisterClassA");
        GL3D_BIND(api.user, CreateWindowExA, "CreateWindowExA");
        GL3D_BIND(api.user, DestroyWindow, "DestroyWindow");
        GL3D_BIND(api.user, DefWindowProcA, "DefWindowProcA");
        GL3D_BIND(api.user, GetDC, "GetDC");
        GL3D_BIND(api.user, ReleaseDC, "ReleaseDC");

#undef GL3D_BIND

        apiLoaded = true;
        return true;
    }

    /* ---------------------------------------------------------- context */

    bool start(int newWidth, int newHeight) {
        if (ready && newWidth == width && newHeight == height) return true;
        rendererShutdown();

        if (newWidth < 16) newWidth = 16;
        if (newHeight < 16) newHeight = 16;
        width = newWidth;
        height = newHeight;

        if (!loadApi()) {
            gbxinline::log("OpenGL 3D: " + error);
            return false;
        }

        HINSTANCE instance = GetModuleHandleA(nullptr);
        if (!classRegistered) {
            WNDCLASSA windowClass{};
            windowClass.style = CS_OWNDC;
            windowClass.lpfnWndProc = &Renderer::windowProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = "GameBasiXOpenGL3D";
            if (!api.RegisterClassA(&windowClass)) {
                error = "RegisterClass failed";
                gbxinline::log("OpenGL 3D: " + error);
                return false;
            }
            classRegistered = true;
        }

        window = api.CreateWindowExA(0, "GameBasiXOpenGL3D", "GameBasiX OpenGL 3D",
                                     WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, width,
                                     height, nullptr, nullptr, instance, nullptr);
        if (!window) {
            error = "CreateWindow failed";
            gbxinline::log("OpenGL 3D: " + error);
            return false;
        }

        device = api.GetDC(window);
        if (!device) {
            error = "GetDC failed";
            gbxinline::log("OpenGL 3D: " + error);
            rendererShutdown();
            return false;
        }

        PIXELFORMATDESCRIPTOR format{};
        format.nSize = sizeof(format);
        format.nVersion = 1;
        format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        format.iPixelType = PFD_TYPE_RGBA;
        format.cColorBits = 32;
        format.cDepthBits = 24;
        format.iLayerType = PFD_MAIN_PLANE;

        const int chosen = api.ChoosePixelFormat(device, &format);
        if (!chosen || !api.SetPixelFormat(device, chosen, &format)) {
            error = "no OpenGL pixel format";
            gbxinline::log("OpenGL 3D: " + error);
            rendererShutdown();
            return false;
        }

        context = api.wglCreateContext(device);
        if (!context || !api.wglMakeCurrent(device, context)) {
            error = "wglCreateContext failed";
            gbxinline::log("OpenGL 3D: " + error);
            rendererShutdown();
            return false;
        }

        api.glViewport(0, 0, width, height);
        api.glEnable(kDepthTest);
        api.glDepthFunc(kLequal);
        api.glDisable(kCullFace);
        api.glShadeModel(kSmooth);
        api.glEnable(kBlend);
        api.glBlendFunc(kSrcAlpha, kOneMinusSrcAlpha);
        api.glEnable(kLineSmooth);
        api.glHint(kLineSmoothHint, kNicest);

        model.reset();
        bottomUp.assign(static_cast<std::size_t>(width) * height * 4, 0);
        topDown.assign(static_cast<std::size_t>(width) * height * 4, 0);

        char version[64] = {0};
        if (const GlUbyte* text = api.glGetString(kVersion)) {
            std::snprintf(version, sizeof(version), "%s", reinterpret_cast<const char*>(text));
        }
        info = "OpenGL ";
        info += version[0] ? version : "1.1";
        if (const GlUbyte* gpu = api.glGetString(kRenderer)) {
            const char* name = reinterpret_cast<const char*>(gpu);
            if (name && *name) info += std::string(" - ") + name;
        }

        ready = true;
        return true;
    }

    void rendererShutdown() {
        if (textureName && api.glDeleteTextures) {
            api.glDeleteTextures(1, &textureName);
        }
        textureName = 0;
        textureReady = false;
        if (context) {
            if (api.wglMakeCurrent) api.wglMakeCurrent(nullptr, nullptr);
            if (api.wglDeleteContext) api.wglDeleteContext(context);
            context = nullptr;
        }
        if (device && window && api.ReleaseDC) {
            api.ReleaseDC(window, device);
        }
        device = nullptr;
        if (window && api.DestroyWindow) api.DestroyWindow(window);
        window = nullptr;
        ready = false;
    }

    /* ------------------------------------------------------- projection */

    void applyProjection() {
        if (!ready) return;
        api.glMatrixMode(kProjection);
        api.glLoadIdentity();
        if (orthographic) {
            api.glOrtho(orthoLeft, orthoRight, orthoBottom, orthoTop, orthoNear, orthoFar);
        } else {
            const double radians = static_cast<double>(fov) * 3.14159265358979323846 / 180.0;
            const double top = static_cast<double>(nearPlane) * std::tan(radians * 0.5);
            const double right = top * (height ? static_cast<double>(width) / height : 1.0);
            api.glFrustum(-right, right, -top, top, nearPlane, farPlane);
        }
        api.glMatrixMode(kModelView);
    }

    void applyView() {
        if (!ready) return;
        api.glLoadIdentity();
        api.glMultMatrixf(view.m);
    }

    void perspective(double newFov, double aspect, double nearValue, double farValue) {
        (void)aspect;
        if (!ready) return;
        if (newFov > 1.0 && newFov < 179.0) fov = static_cast<float>(newFov);
        if (nearValue > 0.0) nearPlane = static_cast<float>(nearValue);
        if (farValue > nearValue) farPlane = static_cast<float>(farValue);
        orthographic = false;
        applyProjection();
        applyView();
    }

    void ortho(double left, double right, double bottom, double top, double nearValue,
               double farValue) {
        if (!ready) return;
        orthoLeft = left;
        orthoRight = right;
        orthoBottom = bottom;
        orthoTop = top;
        orthoNear = nearValue;
        orthoFar = farValue;
        orthographic = true;
        /* screen coordinates: drop the camera so the HUD stays in pixels */
        view = identityMatrix();
        applyProjection();
        applyView();
    }

    void camera(double ex, double ey, double ez, double tx, double ty, double tz) {
        view = viewMatrix(ex, ey, ez, tx, ty, tz);
        applyView();
    }

    /* ------------------------------------------------------------ frame */

    void begin(const Rgb& background) {
        if (!ready) return;
        api.glViewport(0, 0, width, height);
        api.glClearColor(background.r, background.g, background.b, 1.0f);
        api.glClear(kColorBufferBit | kDepthBufferBit);
        model.reset();
        applyProjection();
        applyView();
    }

    /* ------------------------------------------------------------ state */

    void setDepthTest(bool on) {
        state.depthTest = on;
        if (!ready) return;
        if (on) api.glEnable(kDepthTest);
        else api.glDisable(kDepthTest);
    }

    void setCull(bool on) {
        state.cull = on;
        if (!ready) return;
        if (on) {
            api.glEnable(kCullFace);
            api.glCullFace(kBack);
        } else {
            api.glDisable(kCullFace);
        }
    }

    void setBlend(bool on) {
        state.blend = on;
        if (!ready) return;
        if (on) api.glEnable(kBlend);
        else api.glDisable(kBlend);
    }

    void setAlpha(double value) { state.alpha = clampFloat(static_cast<float>(value), 0.0f, 1.0f); }

    void setLineWidth(double value) {
        state.lineWidth = clampFloat(static_cast<float>(value), 1.0f, 32.0f);
        if (ready) api.glLineWidth(state.lineWidth);
    }

    void setPointSize(double value) {
        state.pointSize = clampFloat(static_cast<float>(value), 1.0f, 64.0f);
        if (ready) api.glPointSize(state.pointSize);
    }

    void resetTransform() { model.reset(); }
    void pushTransform() { model.push(); }
    void popTransform() { model.pop(); }
    void translate(double x, double y, double z) { model.translate(x, y, z); }
    void rotate(double degrees, double ax, double ay, double az) {
        model.rotate(degrees, ax, ay, az);
    }
    void scale(double x, double y, double z) { model.scale(x, y, z); }

    /* ---------------------------------------------------------- drawing */

    /** Uploads RGBA8 pixels (top-down) as the current texture. */
    bool setTexture(const void* rgba, int newWidth, int newHeight, bool smooth) {
        if (!ready || !rgba || newWidth <= 0 || newHeight <= 0) return false;
        if (!textureName) api.glGenTextures(1, &textureName);
        if (!textureName) return false;

        api.glBindTexture(kTexture2D, textureName);
        const std::size_t stride = static_cast<std::size_t>(newWidth) * 4;
        upload.assign(stride * static_cast<std::size_t>(newHeight), 0);
        for (int row = 0; row < newHeight; ++row) {
            std::memcpy(&upload[static_cast<std::size_t>(row) * stride],
                        static_cast<const unsigned char*>(rgba) +
                            stride * static_cast<std::size_t>(newHeight - 1 - row),
                        stride);
        }
        api.glPixelStorei(kUnpackAlignment, 1);
        api.glTexImage2D(kTexture2D, 0, static_cast<GlInt>(kRgba), newWidth, newHeight, 0, kRgba,
                         kUnsignedByte, upload.data());
        api.glTexParameteri(kTexture2D, kTextureMinFilter, smooth ? kLinear : kNearest);
        api.glTexParameteri(kTexture2D, kTextureMagFilter, smooth ? kLinear : kNearest);
        api.glTexParameteri(kTexture2D, kTextureWrapS, kClamp);
        api.glTexParameteri(kTexture2D, kTextureWrapT, kClamp);
        api.glTexEnvi(kTextureEnv, kTextureEnvMode, kModulate);
        textureReady = true;
        return true;
    }

    void clearTexture() { textureReady = false; }

    void draw(const Mesh& mesh) {
        if (!ready) return;
        applyView();
        const GlFloat alpha = state.alpha;
        const bool textured = mesh.textured && textureReady;
        if (textured) {
            api.glEnable(kTexture2D);
            api.glBindTexture(kTexture2D, textureName);
        } else {
            api.glDisable(kTexture2D);
        }

        if (!mesh.triangles.empty()) {
            api.glBegin(kTriangles);
            for (const Vertex& vertex : mesh.triangles) {
                if (textured) api.glTexCoord2f(vertex.u, vertex.v);
                api.glColor4f(vertex.color.r, vertex.color.g, vertex.color.b, alpha);
                api.glVertex3f(vertex.position.x, vertex.position.y, vertex.position.z);
            }
            api.glEnd();
        }
        if (!mesh.lines.empty()) {
            api.glBegin(kLines);
            for (const Vertex& vertex : mesh.lines) {
                api.glColor4f(vertex.color.r, vertex.color.g, vertex.color.b, alpha);
                api.glVertex3f(vertex.position.x, vertex.position.y, vertex.position.z);
            }
            api.glEnd();
        }
        if (!mesh.points.empty()) {
            api.glBegin(kPoints);
            for (const Vertex& vertex : mesh.points) {
                api.glColor4f(vertex.color.r, vertex.color.g, vertex.color.b, alpha);
                api.glVertex3f(vertex.position.x, vertex.position.y, vertex.position.z);
            }
            api.glEnd();
        }
    }

    void end() {
        if (!ready) return;
        api.glFinish();
        api.glReadBuffer(kBack);
        api.glPixelStorei(kPackAlignment, 1);
        api.glReadPixels(0, 0, width, height, kRgba, kUnsignedByte, bottomUp.data());

        /* OpenGL returns the bottom row first; blitScreen expects top-down. */
        const std::size_t stride = static_cast<std::size_t>(width) * 4;
        for (int row = 0; row < height; ++row) {
            std::memcpy(&topDown[static_cast<std::size_t>(row) * stride],
                        &bottomUp[static_cast<std::size_t>(height - 1 - row) * stride], stride);
        }
        gbxinline::blitScreen(topDown.data(), width, height, static_cast<int>(stride));
    }
};

#else /* portable software renderer (HTML5, Linux, Windows with GL3D_SOFTWARE) */

/**
 * Software renderer with the same API as the OpenGL path: z-buffered
 * triangles with Gouraud colours, lines with width, point squares, alpha
 * blending, back-face culling and both projections. No OpenGL context is
 * needed, so the browser and every other platform share one code path.
 */
struct Renderer {
    /** RGBA8 image of a loaded GBX sprite, rows top-down. */
    struct Image {
        int width = 0;
        int height = 0;
        bool smooth = false;
        std::vector<unsigned char> rgba;

        bool valid() const {
            return width > 0 && height > 0 &&
                   rgba.size() >= static_cast<std::size_t>(width) * height * 4;
        }

        void clear() {
            width = 0;
            height = 0;
            rgba.clear();
        }

        void sample(float u, float v, float& outR, float& outG, float& outB, float& outA) const {
            if (!valid()) {
                outR = outG = outB = outA = 1.0f;
                return;
            }
            const float fx = clampFloat(u, 0.0f, 1.0f) * (width - 1);
            const float fy = clampFloat(v, 0.0f, 1.0f) * (height - 1);
            auto texel = [&](int x, int y, float* out) {
                x = std::max(0, std::min(x, width - 1));
                y = std::max(0, std::min(y, height - 1));
                const unsigned char* p = &rgba[(static_cast<std::size_t>(y) * width + x) * 4];
                out[0] = static_cast<float>(p[0]) / 255.0f;
                out[1] = static_cast<float>(p[1]) / 255.0f;
                out[2] = static_cast<float>(p[2]) / 255.0f;
                out[3] = static_cast<float>(p[3]) / 255.0f;
            };
            if (!smooth) {
                float t[4];
                texel(static_cast<int>(fx + 0.5f), static_cast<int>(fy + 0.5f), t);
                outR = t[0];
                outG = t[1];
                outB = t[2];
                outA = t[3];
                return;
            }
            const int x0 = static_cast<int>(std::floor(fx));
            const int y0 = static_cast<int>(std::floor(fy));
            const float tx = fx - static_cast<float>(x0);
            const float ty = fy - static_cast<float>(y0);
            float t00[4], t10[4], t01[4], t11[4];
            texel(x0, y0, t00);
            texel(x0 + 1, y0, t10);
            texel(x0, y0 + 1, t01);
            texel(x0 + 1, y0 + 1, t11);
            const float top0 = t00[0] + (t10[0] - t00[0]) * tx;
            const float top1 = t00[1] + (t10[1] - t00[1]) * tx;
            const float top2 = t00[2] + (t10[2] - t00[2]) * tx;
            const float top3 = t00[3] + (t10[3] - t00[3]) * tx;
            const float bottom0 = t01[0] + (t11[0] - t01[0]) * tx;
            const float bottom1 = t01[1] + (t11[1] - t01[1]) * tx;
            const float bottom2 = t01[2] + (t11[2] - t01[2]) * tx;
            const float bottom3 = t01[3] + (t11[3] - t01[3]) * tx;
            outR = top0 + (bottom0 - top0) * ty;
            outG = top1 + (bottom1 - top1) * ty;
            outB = top2 + (bottom2 - top2) * ty;
            outA = top3 + (bottom3 - top3) * ty;
        }
    };

    struct ScreenPoint {
        float x = 0.0f;
        float y = 0.0f;
        /* larger means closer; 1/w for perspective, -ndcZ for ortho so the
         * depth buffer keeps working without a perspective divide */
        float depth = -1e30f;
        float invW = 1.0f;
        float u = 0.0f;
        float v = 0.0f;
        Rgb color;
        bool visible = false;
    };

    static Renderer& instance() {
        static Renderer renderer;
        return renderer;
    }

    int width = 0;
    int height = 0;
    bool started = false;
    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    bool orthographic = false;
    double orthoLeft = 0.0;
    double orthoRight = 0.0;
    double orthoBottom = 0.0;
    double orthoTop = 0.0;
    double orthoNear = -1.0;
    double orthoFar = 1.0;

    Mat4 projection = identityMatrix();
    Mat4 view = identityMatrix();
    ModelStack model;
    State state;

    std::vector<unsigned char> pixels;
    std::vector<float> depth;
    Image image;
    std::string info = "OpenGL 3D software renderer (HTML5)";

    bool start(int newWidth, int newHeight) {
        if (started && newWidth == width && newHeight == height) return true;
        if (newWidth < 16) newWidth = 16;
        if (newHeight < 16) newHeight = 16;
        width = newWidth;
        height = newHeight;
        pixels.assign(static_cast<std::size_t>(width) * height * 4, 0);
        depth.assign(static_cast<std::size_t>(width) * height, 0.0f);
        model.reset();
        projection = identityMatrix();
        view = identityMatrix();
        applyProjection();
        started = true;
        return true;
    }

    void rendererShutdown() {
        started = false;
        pixels.clear();
        depth.clear();
    }

    void applyProjection() {
        if (orthographic) {
            projection = orthoMatrix(orthoLeft, orthoRight, orthoBottom, orthoTop, orthoNear, orthoFar);
        } else {
            projection =
                perspectiveMatrix(fov, height ? static_cast<double>(width) / height : 1.5,
                                  nearPlane, farPlane);
        }
    }

    void perspective(double newFov, double aspect, double nearValue, double farValue) {
        if (!started) return;
        if (newFov > 1.0 && newFov < 179.0) fov = static_cast<float>(newFov);
        if (nearValue > 0.0) nearPlane = static_cast<float>(nearValue);
        if (farValue > nearValue) farPlane = static_cast<float>(farValue);
        orthographic = false;
        (void)aspect;
        applyProjection();
    }

    void ortho(double left, double right, double bottom, double top, double nearValue,
               double farValue) {
        if (!started) return;
        orthoLeft = left;
        orthoRight = right;
        orthoBottom = bottom;
        orthoTop = top;
        orthoNear = nearValue;
        orthoFar = farValue;
        orthographic = true;
        /* screen coordinates: drop the camera so the HUD stays in pixels */
        view = identityMatrix();
        applyProjection();
    }

    void camera(double ex, double ey, double ez, double tx, double ty, double tz) {
        view = viewMatrix(ex, ey, ez, tx, ty, tz);
    }

    void begin(const Rgb& background) {
        if (!started) return;
        clear(background);
        model.reset();
        applyProjection();
    }

    void setDepthTest(bool on) { state.depthTest = on; }
    void setCull(bool on) { state.cull = on; }
    void setBlend(bool on) { state.blend = on; }
    void setAlpha(double value) { state.alpha = clampFloat(static_cast<float>(value), 0.0f, 1.0f); }
    void setLineWidth(double value) {
        state.lineWidth = clampFloat(static_cast<float>(value), 1.0f, 32.0f);
    }
    void setPointSize(double value) {
        state.pointSize = clampFloat(static_cast<float>(value), 1.0f, 64.0f);
    }

    /** Copies RGBA8 pixels (top-down) into the texture used by textured meshes. */
    bool setTexture(const void* rgba, int newWidth, int newHeight, bool smooth) {
        if (!started || !rgba || newWidth <= 0 || newHeight <= 0) return false;
        const std::size_t size = static_cast<std::size_t>(newWidth) * newHeight * 4;
        image.rgba.assign(static_cast<const unsigned char*>(rgba),
                          static_cast<const unsigned char*>(rgba) + size);
        image.width = newWidth;
        image.height = newHeight;
        image.smooth = smooth;
        return true;
    }

    void clearTexture() { image.clear(); }

    void resetTransform() { model.reset(); }
    void pushTransform() { model.push(); }
    void popTransform() { model.pop(); }
    void translate(double x, double y, double z) { model.translate(x, y, z); }
    void rotate(double degrees, double ax, double ay, double az) {
        model.rotate(degrees, ax, ay, az);
    }
    void scale(double x, double y, double z) { model.scale(x, y, z); }

    /* ------------------------------------------------------- rasteriser */

    void clear(const Rgb& background) {
        const unsigned char r = channel(background.r);
        const unsigned char g = channel(background.g);
        const unsigned char b = channel(background.b);
        unsigned char* data = pixels.data();
        const std::size_t count = static_cast<std::size_t>(width) * height;
        for (std::size_t i = 0; i < count; ++i) {
            data[i * 4] = r;
            data[i * 4 + 1] = g;
            data[i * 4 + 2] = b;
            data[i * 4 + 3] = 255;
        }
        std::fill(depth.begin(), depth.end(), -1e30f);
    }

    ScreenPoint project(const Mat4& matrix, const Vec3& world, const Rgb& color, float u = 0.0f,
                        float v = 0.0f) const {
        ScreenPoint point;
        point.color = color;
        point.u = u;
        point.v = v;
        const float* m = matrix.m;
        const float cx = m[0] * world.x + m[4] * world.y + m[8] * world.z + m[12];
        const float cy = m[1] * world.x + m[5] * world.y + m[9] * world.z + m[13];
        const float cz = m[2] * world.x + m[6] * world.y + m[10] * world.z + m[14];
        const float cw = m[3] * world.x + m[7] * world.y + m[11] * world.z + m[15];
        if (cw <= 1e-4f) return point;
        const float inv = 1.0f / cw;
        const float ndcZ = cz * inv;
        if (ndcZ < -1.0f || ndcZ > 1.0f) return point;
        point.x = (cx * inv * 0.5f + 0.5f) * width;
        point.y = (0.5f - cy * inv * 0.5f) * height;
        point.depth = orthographic ? -ndcZ : inv;
        point.invW = inv;
        point.visible = true;
        return point;
    }

    void plot(int x, int y, float depthValue, const Rgb& color, float alpha) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const std::size_t index = static_cast<std::size_t>(y) * width + x;
        if (state.depthTest && depthValue <= depth[index]) return;
        unsigned char* pixel = &pixels[index * 4];
        if (alpha >= 0.999f || !state.blend) {
            pixel[0] = channel(color.r);
            pixel[1] = channel(color.g);
            pixel[2] = channel(color.b);
            if (state.depthTest) depth[index] = depthValue;
        } else {
            pixel[0] = static_cast<unsigned char>(channel(color.r) * alpha + pixel[0] * (1.0f - alpha));
            pixel[1] = static_cast<unsigned char>(channel(color.g) * alpha + pixel[1] * (1.0f - alpha));
            pixel[2] = static_cast<unsigned char>(channel(color.b) * alpha + pixel[2] * (1.0f - alpha));
        }
        pixel[3] = 255;
    }

    void rasterTriangle(const ScreenPoint& a, const ScreenPoint& b, const ScreenPoint& c,
                        bool textured) {
        if (!a.visible || !b.visible || !c.visible) return;
        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::fabs(area) < 1e-6f) return;
        /* Counter-clockwise in world space ends up clockwise on screen, so
         * front faces have a negative area here. */
        if (state.cull && area > 0.0f) return;

        int minX = static_cast<int>(std::floor(std::min(a.x, std::min(b.x, c.x))));
        int maxX = static_cast<int>(std::ceil(std::max(a.x, std::max(b.x, c.x))));
        int minY = static_cast<int>(std::floor(std::min(a.y, std::min(b.y, c.y))));
        int maxY = static_cast<int>(std::ceil(std::max(a.y, std::max(b.y, c.y))));
        minX = std::max(minX, 0);
        minY = std::max(minY, 0);
        maxX = std::min(maxX, width - 1);
        maxY = std::min(maxY, height - 1);
        if (minX > maxX || minY > maxY) return;

        const float invArea = 1.0f / area;
        const float alpha = state.alpha;
        for (int y = minY; y <= maxY; ++y) {
            const float py = static_cast<float>(y) + 0.5f;
            for (int x = minX; x <= maxX; ++x) {
                const float px = static_cast<float>(x) + 0.5f;
                const float e0 = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
                const float e1 = (c.x - b.x) * (py - b.y) - (c.y - b.y) * (px - b.x);
                const float e2 = (a.x - c.x) * (py - c.y) - (a.y - c.y) * (px - c.x);
                if (area > 0.0f) {
                    if (e0 < 0.0f || e1 < 0.0f || e2 < 0.0f) continue;
                } else {
                    if (e0 > 0.0f || e1 > 0.0f || e2 > 0.0f) continue;
                }
                const float wa = e1 * invArea;
                const float wb = e2 * invArea;
                const float wc = e0 * invArea;
                const float depthValue = wa * a.depth + wb * b.depth + wc * c.depth;
                Rgb color{wa * a.color.r + wb * b.color.r + wc * c.color.r,
                          wa * a.color.g + wb * b.color.g + wc * c.color.g,
                          wa * a.color.b + wb * b.color.b + wc * c.color.b};
                float pixelAlpha = alpha;
                if (textured) {
                    /* perspective-correct uv: interpolate u/w and divide by 1/w */
                    const float invSum = wa * a.invW + wb * b.invW + wc * c.invW;
                    const float divisor = invSum > 1e-9f ? invSum : 1.0f;
                    const float u = (wa * a.u * a.invW + wb * b.u * b.invW + wc * c.u * c.invW) /
                                    divisor;
                    const float v = (wa * a.v * a.invW + wb * b.v * b.invW + wc * c.v * c.invW) /
                                    divisor;
                    float tr = 1.0f, tg = 1.0f, tb = 1.0f, ta = 1.0f;
                    image.sample(u, v, tr, tg, tb, ta);
                    color = Rgb{color.r * tr, color.g * tg, color.b * tb};
                    pixelAlpha = alpha * ta;
                    if (pixelAlpha <= 0.004f) continue;
                }
                plot(x, y, depthValue, color, pixelAlpha);
            }
        }
    }

    void drawLineOffset(const ScreenPoint& a, const ScreenPoint& b, int offsetX, int offsetY) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const int steps = static_cast<int>(std::max(std::fabs(dx), std::fabs(dy)) + 0.5f);
        const float alpha = state.alpha;
        if (steps <= 0) {
            plot(static_cast<int>(a.x) + offsetX, static_cast<int>(a.y) + offsetY, a.depth + 1e-6f,
                 a.color, alpha);
            return;
        }
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const Rgb color{a.color.r + (b.color.r - a.color.r) * t,
                            a.color.g + (b.color.g - a.color.g) * t,
                            a.color.b + (b.color.b - a.color.b) * t};
            plot(static_cast<int>(std::floor(a.x + dx * t)) + offsetX,
                 static_cast<int>(std::floor(a.y + dy * t)) + offsetY,
                 a.depth + (b.depth - a.depth) * t, color, alpha);
        }
    }

    void drawLine(const ScreenPoint& a, const ScreenPoint& b) {
        if (!a.visible || !b.visible) return;
        const int thickness = std::max(1, static_cast<int>(std::lround(state.lineWidth)));
        if (thickness <= 1) {
            drawLineOffset(a, b, 0, 0);
            return;
        }
        const bool horizontal = std::fabs(b.x - a.x) >= std::fabs(b.y - a.y);
        for (int i = 0; i < thickness; ++i) {
            const int offset = i - (thickness - 1) / 2;
            drawLineOffset(a, b, horizontal ? 0 : offset, horizontal ? offset : 0);
        }
    }

    void drawPoint(const ScreenPoint& point) {
        if (!point.visible) return;
        const int size = std::max(1, static_cast<int>(std::lround(state.pointSize)));
        const int start = size / 2;
        const int baseX = static_cast<int>(std::floor(point.x));
        const int baseY = static_cast<int>(std::floor(point.y));
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                plot(baseX - start + x, baseY - start + y, point.depth + 1e-6f, point.color,
                     state.alpha);
            }
        }
    }

    void draw(const Mesh& mesh) {
        if (!started) return;
        const Mat4 mvp = multiply(projection, view);
        const bool textured = mesh.textured && image.valid();
        for (std::size_t i = 0; i + 2 < mesh.triangles.size(); i += 3) {
            const ScreenPoint a = project(mvp, mesh.triangles[i].position, mesh.triangles[i].color,
                                          mesh.triangles[i].u, mesh.triangles[i].v);
            const ScreenPoint b = project(mvp, mesh.triangles[i + 1].position,
                                          mesh.triangles[i + 1].color, mesh.triangles[i + 1].u,
                                          mesh.triangles[i + 1].v);
            const ScreenPoint c = project(mvp, mesh.triangles[i + 2].position,
                                          mesh.triangles[i + 2].color, mesh.triangles[i + 2].u,
                                          mesh.triangles[i + 2].v);
            rasterTriangle(a, b, c, textured);
        }
        for (std::size_t i = 0; i + 1 < mesh.lines.size(); i += 2) {
            drawLine(project(mvp, mesh.lines[i].position, mesh.lines[i].color),
                     project(mvp, mesh.lines[i + 1].position, mesh.lines[i + 1].color));
        }
        for (const Vertex& vertex : mesh.points) {
            drawPoint(project(mvp, vertex.position, vertex.color));
        }
    }

    void end() {
        if (!started) return;
        gbxinline::blitScreen(pixels.data(), width, height, width * 4);
    }
};

#endif /* GL3D_WINDOWS_OPENGL */

/* ------------------------------------------------------ scene drawing */

inline Renderer& renderer() { return Renderer::instance(); }

inline void drawCube(Renderer& r, double x, double y, double z, double size, const Rgb& color,
                     double rotX, double rotY, double rotZ) {
    Mesh mesh;
    addBox(mesh, r.model, r.state, x, y, z, size, size, size, color, rotX, rotY, rotZ);
    r.draw(mesh);
}

inline void drawBox(Renderer& r, double x, double y, double z, double sx, double sy, double sz,
                    const Rgb& color, double rotX, double rotY, double rotZ) {
    Mesh mesh;
    addBox(mesh, r.model, r.state, x, y, z, sx * 0.5, sy * 0.5, sz * 0.5, color, rotX, rotY, rotZ);
    r.draw(mesh);
}

inline void drawSphere(Renderer& r, double x, double y, double z, double radius, const Rgb& color,
                       int segments) {
    Mesh mesh;
    addSphere(mesh, r.model, r.state, x, y, z, radius, color, segments);
    r.draw(mesh);
}

inline void drawCylinder(Renderer& r, double x, double y, double z, double radius, double height,
                         const Rgb& color, int segments, double topRadius) {
    Mesh mesh;
    addCylinder(mesh, r.model, r.state, x, y, z, radius, height, color, segments, topRadius);
    r.draw(mesh);
}

inline void drawQuad(Renderer& r, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                     const Rgb& color) {
    Mesh mesh;
    addQuad(mesh, r.model, a, b, c, d, color);
    r.draw(mesh);
}

inline void drawGrid(Renderer& r, double extent, double step, const Rgb& color) {
    Mesh mesh;
    addGrid(mesh, r.model, extent, step, color);
    r.draw(mesh);
}

inline void drawLine(Renderer& r, const Vec3& a, const Vec3& b, const Rgb& color) {
    Mesh mesh;
    addLine(mesh, r.model, a, b, color);
    r.draw(mesh);
}

inline void drawTriangle(Renderer& r, const Vec3& a, const Vec3& b, const Vec3& c,
                         const Rgb& color) {
    Mesh mesh;
    addTriangle(mesh, r.model, a, b, c, color);
    r.draw(mesh);
}

inline void drawPoint(Renderer& r, const Vec3& point, const Rgb& color) {
    Mesh mesh;
    addPoint(mesh, r.model, point, color);
    r.draw(mesh);
}

inline void drawAxis(Renderer& r, double length) {
    Mesh mesh;
    addAxis(mesh, r.model, length);
    r.draw(mesh);
}

inline void drawQuadTex(Renderer& r, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                        const Rgb& tint) {
    Mesh mesh;
    addTexturedQuad(mesh, r.model, a, b, c, d, tint);
    r.draw(mesh);
}

inline void drawCubeTex(Renderer& r, double x, double y, double z, double size, const Rgb& tint,
                        double rotX, double rotY, double rotZ) {
    Mesh mesh;
    addTexturedBox(mesh, r.model, r.state, x, y, z, size, size, size, tint, rotX, rotY, rotZ);
    r.draw(mesh);
}

/* ------------------------------------------------------------ commands */

inline Vec3 point3(const GbxValue* args, int argc, int index) {
    return makeVec(gbxinline::argNum(args, argc, index), gbxinline::argNum(args, argc, index + 1),
                   gbxinline::argNum(args, argc, index + 2));
}

inline void cmdInit(const GbxValue* args, int argc, GbxValue* ret) {
    const int width = static_cast<int>(std::lround(gbxinline::argNum(args, argc, 0, 960)));
    const int height = static_cast<int>(std::lround(gbxinline::argNum(args, argc, 1, 640)));
    gbxinline::retNum(ret, renderer().start(width, height) ? 1 : 0);
}

inline void cmdBegin(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    renderer().begin(unpackColor(gbxinline::argNum(args, argc, 0, 0x0C1018)));
}

inline void cmdPerspective(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    Renderer& r = renderer();
    r.perspective(gbxinline::argNum(args, argc, 0, 60.0),
                  gbxinline::argNum(args, argc, 1,
                                    r.height ? static_cast<double>(r.width) / r.height : 1.5),
                  gbxinline::argNum(args, argc, 2, 0.1), gbxinline::argNum(args, argc, 3, 100.0));
}

inline void cmdOrtho(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    Renderer& r = renderer();
    const double left = gbxinline::argNum(args, argc, 0, 0.0);
    const double right = gbxinline::argNum(args, argc, 1, r.width);
    const double bottom = gbxinline::argNum(args, argc, 2, r.height);
    const double top = gbxinline::argNum(args, argc, 3, 0.0);
    r.ortho(left, right, bottom, top, gbxinline::argNum(args, argc, 4, -1.0),
            gbxinline::argNum(args, argc, 5, 1.0));
}

inline void cmdCamera(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    renderer().camera(gbxinline::argNum(args, argc, 0), gbxinline::argNum(args, argc, 1),
                      gbxinline::argNum(args, argc, 2), gbxinline::argNum(args, argc, 3),
                      gbxinline::argNum(args, argc, 4), gbxinline::argNum(args, argc, 5));
}

inline void cmdIdentity(const GbxValue*, int, GbxValue*) { renderer().resetTransform(); }

inline void cmdPush(const GbxValue*, int, GbxValue*) { renderer().pushTransform(); }

inline void cmdPop(const GbxValue*, int, GbxValue*) { renderer().popTransform(); }

inline void cmdTranslate(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    renderer().translate(gbxinline::argNum(args, argc, 0), gbxinline::argNum(args, argc, 1),
                         gbxinline::argNum(args, argc, 2));
}

inline void cmdRotate(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    renderer().rotate(gbxinline::argNum(args, argc, 0), gbxinline::argNum(args, argc, 1, 0.0),
                      gbxinline::argNum(args, argc, 2, 1.0),
                      gbxinline::argNum(args, argc, 3, 0.0));
}

inline void cmdScale(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    const double sx = gbxinline::argNum(args, argc, 0, 1.0);
    renderer().scale(sx, gbxinline::argNum(args, argc, 1, sx), gbxinline::argNum(args, argc, 2, sx));
}

inline void cmdDepth(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    renderer().setDepthTest(gbxinline::argNum(args, argc, 0, 1.0) != 0.0);
}

inline void cmdCull(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    renderer().setCull(gbxinline::argNum(args, argc, 0, 1.0) != 0.0);
}

inline void cmdBlend(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    renderer().setBlend(gbxinline::argNum(args, argc, 0, 1.0) != 0.0);
}

inline void cmdAlpha(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    renderer().setAlpha(gbxinline::argNum(args, argc, 0, 1.0));
}

inline void cmdLineWidth(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    renderer().setLineWidth(gbxinline::argNum(args, argc, 0, 1.0));
}

inline void cmdPointSize(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    renderer().setPointSize(gbxinline::argNum(args, argc, 0, 3.0));
}

inline void cmdLight(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    Renderer& r = renderer();
    const Vec3 direction = point3(args, argc, 0);
    if (lengthVec(direction) > 1e-6f) r.state.light = normalize(direction);
}

inline void cmdAmbient(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    renderer().state.ambient = clampFloat(static_cast<float>(gbxinline::argNum(args, argc, 0, 0.35)),
                                          0.0f, 1.0f);
}

inline void cmdCube(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    drawCube(renderer(), gbxinline::argNum(args, argc, 0), gbxinline::argNum(args, argc, 1),
             gbxinline::argNum(args, argc, 2), gbxinline::argNum(args, argc, 3, 1.0),
             unpackColor(gbxinline::argNum(args, argc, 4, 0xFFFFFF)),
             gbxinline::argNum(args, argc, 5), gbxinline::argNum(args, argc, 6),
             gbxinline::argNum(args, argc, 7));
}

inline void cmdBox(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    drawBox(renderer(), gbxinline::argNum(args, argc, 0), gbxinline::argNum(args, argc, 1),
            gbxinline::argNum(args, argc, 2), gbxinline::argNum(args, argc, 3),
            gbxinline::argNum(args, argc, 4), gbxinline::argNum(args, argc, 5),
            unpackColor(gbxinline::argNum(args, argc, 6, 0xFFFFFF)),
            gbxinline::argNum(args, argc, 7), gbxinline::argNum(args, argc, 8),
            gbxinline::argNum(args, argc, 9));
}

inline void cmdSphere(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    drawSphere(renderer(), gbxinline::argNum(args, argc, 0), gbxinline::argNum(args, argc, 1),
               gbxinline::argNum(args, argc, 2), gbxinline::argNum(args, argc, 3, 1.0),
               unpackColor(gbxinline::argNum(args, argc, 4, 0xFFFFFF)),
               static_cast<int>(std::lround(gbxinline::argNum(args, argc, 5, 24.0))));
}

inline void cmdCylinder(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    drawCylinder(renderer(), gbxinline::argNum(args, argc, 0), gbxinline::argNum(args, argc, 1),
                 gbxinline::argNum(args, argc, 2), gbxinline::argNum(args, argc, 3, 1.0),
                 gbxinline::argNum(args, argc, 4, 1.0),
                 unpackColor(gbxinline::argNum(args, argc, 5, 0xFFFFFF)),
                 static_cast<int>(std::lround(gbxinline::argNum(args, argc, 6, 24.0))),
                 gbxinline::argNum(args, argc, 7, -1.0));
}

inline void cmdQuad(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    drawQuad(renderer(), point3(args, argc, 0), point3(args, argc, 3), point3(args, argc, 6),
             point3(args, argc, 9), unpackColor(gbxinline::argNum(args, argc, 12, 0xFFFFFF)));
}

inline void cmdTexture(const GbxValue* args, int argc, GbxValue* ret) {
    Renderer& r = renderer();
    const int id = static_cast<int>(std::lround(gbxinline::argNum(args, argc, 0, 0.0)));
    const bool smooth = gbxinline::argNum(args, argc, 1, 0.0) != 0.0;
    if (id <= 0) {
        r.clearTexture();
        gbxinline::retNum(ret, 0);
        return;
    }
    const void* rgba = nullptr;
    int width = 0;
    int height = 0;
    if (!gbxinline::spritePixels(id, &rgba, &width, &height) || !rgba) {
        r.clearTexture();
        gbxinline::retNum(ret, 0);
        return;
    }
    gbxinline::retNum(ret, r.setTexture(rgba, width, height, smooth) ? 1 : 0);
}

inline void cmdQuadTex(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    drawQuadTex(renderer(), point3(args, argc, 0), point3(args, argc, 3), point3(args, argc, 6),
                point3(args, argc, 9),
                unpackColor(gbxinline::argNum(args, argc, 12, 0xFFFFFF)));
}

inline void cmdCubeTex(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    drawCubeTex(renderer(), gbxinline::argNum(args, argc, 0), gbxinline::argNum(args, argc, 1),
                gbxinline::argNum(args, argc, 2), gbxinline::argNum(args, argc, 3, 1.0),
                unpackColor(gbxinline::argNum(args, argc, 4, 0xFFFFFF)),
                gbxinline::argNum(args, argc, 5), gbxinline::argNum(args, argc, 6),
                gbxinline::argNum(args, argc, 7));
}

inline void cmdGrid(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    drawGrid(renderer(), gbxinline::argNum(args, argc, 0, 20.0),
             gbxinline::argNum(args, argc, 1, 1.0),
             unpackColor(gbxinline::argNum(args, argc, 2, 0x2A3340)));
}

inline void cmdLine(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    drawLine(renderer(), point3(args, argc, 0), point3(args, argc, 3),
             unpackColor(gbxinline::argNum(args, argc, 6, 0xFFFFFF)));
}

inline void cmdTriangle(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    drawTriangle(renderer(), point3(args, argc, 0), point3(args, argc, 3), point3(args, argc, 6),
                 unpackColor(gbxinline::argNum(args, argc, 9, 0xFFFFFF)));
}

inline void cmdPoint(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    drawPoint(renderer(), point3(args, argc, 0),
              unpackColor(gbxinline::argNum(args, argc, 3, 0xFFFFFF)));
}

inline void cmdAxis(const GbxValue* args, int argc, GbxValue* ret) {
    (void)ret;
    drawAxis(renderer(), gbxinline::argNum(args, argc, 0, 1.0));
}

inline void cmdEnd(const GbxValue*, int, GbxValue*) { renderer().end(); }

inline void cmdInfo(const GbxValue*, int, GbxValue* ret) { gbxinline::retText(ret, renderer().info); }

inline void cmdWidth(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retNum(ret, renderer().width);
}

inline void cmdHeight(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retNum(ret, renderer().height);
}

inline void cmdShutdown(const GbxValue*, int, GbxValue*) {
    renderer().rendererShutdown();
    renderer().info = "OpenGL 3D was shut down";
}

}  // namespace gl3d

/* The blocks below are Markdown: the IDE shows them as the hover text for the
 * command, OPENGL3D.gbx keeps the longer help in its `@help` block. */

// @tooltip GL3D_INIT
// **GL3D_INIT [width[, height]]**
//
// Creates the renderer and its depth buffer. Without arguments the current GBX
// screen size is used. Returns `1` on success and `0` if no renderer could be
// created.
// @endtooltip
GBX_INLINE_FN("GL3D_INIT", 0, 2, gl3d::cmdInit);

// @tooltip GL3D_BEGIN
// **GL3D_BEGIN [color]**
//
// Starts a frame: clears colour and depth buffer and resets the transform
// stack. Draw the scene, then finish with `GL3D_END`.
//
// ```gbx
// GL3D_BEGIN RGB(12, 16, 24)
// ```
// @endtooltip
GBX_INLINE_FN("GL3D_BEGIN", 0, 1, gl3d::cmdBegin);

// @tooltip GL3D_PERSPECTIVE
// **GL3D_PERSPECTIVE [fov[, aspect[, near[, far]]]]**
//
// Switches back to a perspective projection. Defaults are 60° field of view,
// the screen aspect and a depth range of 0.1 to 100.
// @endtooltip
GBX_INLINE_FN("GL3D_PERSPECTIVE", 0, 4, gl3d::cmdPerspective);

// @tooltip GL3D_ORTHO
// **GL3D_ORTHO [left, right, bottom, top[, near[, far]]]**
//
// Orthographic projection, useful for 2D overlays and HUD panels. Without
// arguments the whole screen is used with y pointing down, so pixel
// coordinates work like `PRINT`. It resets the camera; call `GL3D_CAMERA`
// again before the next 3D pass.
// @endtooltip
GBX_INLINE_FN("GL3D_ORTHO", 0, 6, gl3d::cmdOrtho);

// @tooltip GL3D_CAMERA
// **GL3D_CAMERA eyeX, eyeY, eyeZ, targetX, targetY, targetZ**
//
// Places the camera and points it at a target. Up is +Y.
//
// ```gbx
// GL3D_CAMERA 0, 5, 12, 0, 0, 0
// ```
// @endtooltip
GBX_INLINE_FN("GL3D_CAMERA", 6, 6, gl3d::cmdCamera);

// @tooltip GL3D_IDENTITY
// **GL3D_IDENTITY**
//
// Resets the model transform. The stack stays as it is.
// @endtooltip
GBX_INLINE_FN("GL3D_IDENTITY", 0, 0, gl3d::cmdIdentity);

// @tooltip GL3D_PUSH
// **GL3D_PUSH**
//
// Saves the current model transform on the stack (up to 64 levels). Pair it
// with `GL3D_POP`.
// @endtooltip
GBX_INLINE_FN("GL3D_PUSH", 0, 0, gl3d::cmdPush);

// @tooltip GL3D_POP
// **GL3D_POP**
//
// Restores the transform saved by the matching `GL3D_PUSH`.
// @endtooltip
GBX_INLINE_FN("GL3D_POP", 0, 0, gl3d::cmdPop);

// @tooltip GL3D_TRANSLATE
// **GL3D_TRANSLATE x, y, z**
//
// Moves the current transform. Applies to every following primitive until the
// next frame or the matching `GL3D_POP`.
// @endtooltip
GBX_INLINE_FN("GL3D_TRANSLATE", 3, 3, gl3d::cmdTranslate);

// @tooltip GL3D_ROTATE
// **GL3D_ROTATE degrees[, axisX, axisY, axisZ]**
//
// Rotates the current transform around an axis; the default axis is +Y.
//
// ```gbx
// GL3D_ROTATE 35, 1, 0, 0
// ```
// @endtooltip
GBX_INLINE_FN("GL3D_ROTATE", 1, 4, gl3d::cmdRotate);

// @tooltip GL3D_SCALE
// **GL3D_SCALE sx[, sy[, sz]]**
//
// Scales the current transform. One argument scales all axes evenly.
// @endtooltip
GBX_INLINE_FN("GL3D_SCALE", 1, 3, gl3d::cmdScale);

// @tooltip GL3D_DEPTH
// **GL3D_DEPTH [enabled]**
//
// Turns the depth buffer test on (`1`, default) or off (`0`). Without it, later
// primitives always cover earlier ones.
// @endtooltip
GBX_INLINE_FN("GL3D_DEPTH", 0, 1, gl3d::cmdDepth);

// @tooltip GL3D_CULL
// **GL3D_CULL [enabled]**
//
// Drops back faces. Boxes, spheres and cylinders are wound for culling, so
// closed shapes look unchanged and the fill rate drops.
// @endtooltip
GBX_INLINE_FN("GL3D_CULL", 0, 1, gl3d::cmdCull);

// @tooltip GL3D_BLEND
// **GL3D_BLEND [enabled]**
//
// Enables alpha blending for translucent primitives (`GL3D_ALPHA`).
// @endtooltip
GBX_INLINE_FN("GL3D_BLEND", 0, 1, gl3d::cmdBlend);

// @tooltip GL3D_ALPHA
// **GL3D_ALPHA alpha**
//
// Sets the alpha value for all following primitives, 0 (invisible) to 1
// (opaque, default).
//
// ```gbx
// GL3D_ALPHA 0.4
// GL3D_CUBE 0, 1, 0, 0.8, RGB(56, 189, 248)
// GL3D_ALPHA 1
// ```
// @endtooltip
GBX_INLINE_FN("GL3D_ALPHA", 0, 1, gl3d::cmdAlpha);

// @tooltip GL3D_LINE_WIDTH
// **GL3D_LINE_WIDTH width**
//
// Width of following lines and grids in pixels (1 to 32).
// @endtooltip
GBX_INLINE_FN("GL3D_LINE_WIDTH", 0, 1, gl3d::cmdLineWidth);

// @tooltip GL3D_POINT_SIZE
// **GL3D_POINT_SIZE size**
//
// Size of following `GL3D_POINT` dots in pixels (1 to 64).
// @endtooltip
GBX_INLINE_FN("GL3D_POINT_SIZE", 0, 1, gl3d::cmdPointSize);

// @tooltip GL3D_LIGHT
// **GL3D_LIGHT x, y, z**
//
// Direction towards the light used by boxes, spheres and cylinders. The default
// is above, in front and to the right.
// @endtooltip
GBX_INLINE_FN("GL3D_LIGHT", 3, 3, gl3d::cmdLight);

// @tooltip GL3D_AMBIENT
// **GL3D_AMBIENT value**
//
// Brightness of the unlit side, 0 to 1 (default 0.35).
// @endtooltip
GBX_INLINE_FN("GL3D_AMBIENT", 0, 1, gl3d::cmdAmbient);

// @tooltip GL3D_CUBE
// **GL3D_CUBE x, y, z, size, color[, rotX[, rotY[, rotZ]]]**
//
// Draws a coloured cube. `size` is the half extent, the rotations are given in
// degrees and turn the cube around its own centre.
//
// ```gbx
// GL3D_CUBE 0, 1, 0, 1.2, RGB(163, 230, 53), 20, 35, 10
// ```
// @endtooltip
GBX_INLINE_FN("GL3D_CUBE", 5, 8, gl3d::cmdCube);

// @tooltip GL3D_BOX
// **GL3D_BOX x, y, z, width, height, depth, color[, rotX[, rotY[, rotZ]]]**
//
// Draws a box with full extents, centred on `x, y, z`.
//
// ```gbx
// GL3D_BOX 3, 0.5, 0, 2, 1, 4, RGB(148, 163, 184)
// ```
// @endtooltip
GBX_INLINE_FN("GL3D_BOX", 7, 10, gl3d::cmdBox);

// @tooltip GL3D_SPHERE
// **GL3D_SPHERE x, y, z, radius, color[, segments]**
//
// Draws a sphere with smooth shading. `segments` is the number of slices
// around the axis (default 24).
// @endtooltip
GBX_INLINE_FN("GL3D_SPHERE", 5, 6, gl3d::cmdSphere);

// @tooltip GL3D_CYLINDER
// **GL3D_CYLINDER x, y, z, radius, height, color[, segments[, topRadius]]**
//
// Draws a cylinder standing on `x, y, z`. A `topRadius` smaller than `radius`
// makes a cone; `0` gives a tip.
// @endtooltip
GBX_INLINE_FN("GL3D_CYLINDER", 6, 8, gl3d::cmdCylinder);

// @tooltip GL3D_QUAD
// **GL3D_QUAD x1, y1, z1, x2, y2, z2, x3, y3, z3, x4, y4, z4, color**
//
// Draws a flat quad through four corners in a single colour; the points are
// counter-clockwise seen from the visible side. Lighting does not apply.
// @endtooltip
GBX_INLINE_FN("GL3D_QUAD", 13, 13, gl3d::cmdQuad);

// @tooltip GL3D_GRID
// **GL3D_GRID [size[, step[, color]]]**
//
// Draws a square grid on the X/Z plane through the origin. `size` is the half
// extent, `step` the distance between lines.
// @endtooltip
GBX_INLINE_FN("GL3D_GRID", 0, 3, gl3d::cmdGrid);

// @tooltip GL3D_LINE
// **GL3D_LINE x1, y1, z1, x2, y2, z2, color**
//
// Draws a single line between two points.
// @endtooltip
GBX_INLINE_FN("GL3D_LINE", 7, 7, gl3d::cmdLine);

// @tooltip GL3D_TRIANGLE
// **GL3D_TRIANGLE x1, y1, z1, x2, y2, z2, x3, y3, z3, color**
//
// Draws one filled triangle in a single colour. Triangles that cross the near
// plane are dropped by the HTML5 software renderer.
// @endtooltip
GBX_INLINE_FN("GL3D_TRIANGLE", 10, 10, gl3d::cmdTriangle);

// @tooltip GL3D_POINT
// **GL3D_POINT x, y, z, color**
//
// Draws a dot; its size comes from `GL3D_POINT_SIZE`.
// @endtooltip
GBX_INLINE_FN("GL3D_POINT", 4, 4, gl3d::cmdPoint);

// @tooltip GL3D_AXIS
// **GL3D_AXIS [length]**
//
// Draws the X, Y and Z axes in red, green and blue from the current origin.
// @endtooltip
GBX_INLINE_FN("GL3D_AXIS", 0, 1, gl3d::cmdAxis);

// @tooltip GL3D_TEXTURE
// **GL3D_TEXTURE spriteId[, smooth]**
//
// Uses an image that BASIC already loaded (`SPRITE_LOAD`, `SPRITE_GRAB`,
// `ANIM_LOAD`) as the texture for the following textured primitives. `0`
// clears it, `smooth` 1 enables bilinear filtering. Returns 1 on success.
//
// ```gbx
// SPRITE_LOAD "assets/crate.png", 7
// GL3D_TEXTURE 7, 1
// GL3D_CUBE_TEX 0, 1, 0, 1.2, RGB(255, 255, 255), 20, 35, 10
// ```
// @endtooltip
GBX_INLINE_FN("GL3D_TEXTURE", 1, 2, gl3d::cmdTexture);

// @tooltip GL3D_QUAD_TEX
// **GL3D_QUAD_TEX x1, y1, z1, x2, y2, z2, x3, y3, z3, x4, y4, z4, color**
//
// Quad with the bound texture, corners counter-clockwise, uv 0,0 at the first
// corner. `color` tints the image (`RGB(255,255,255)` keeps it unchanged).
// @endtooltip
GBX_INLINE_FN("GL3D_QUAD_TEX", 13, 13, gl3d::cmdQuadTex);

// @tooltip GL3D_CUBE_TEX
// **GL3D_CUBE_TEX x, y, z, size, color[, rotX[, rotY[, rotZ]]]**
//
// Textured cube; the image is repeated on every face and lit like `GL3D_CUBE`.
// @endtooltip
GBX_INLINE_FN("GL3D_CUBE_TEX", 5, 8, gl3d::cmdCubeTex);

// @tooltip GL3D_END
// **GL3D_END**
//
// Finishes the frame and copies the result into the current GBX screen. Call
// `SCREEN_SHOW` afterwards to present it.
// @endtooltip
GBX_INLINE_FN("GL3D_END", 0, 0, gl3d::cmdEnd);

// @tooltip GL3D_INFO$
// **GL3D_INFO$()**
//
// Returns the active backend, for example `OpenGL 1.1` on Windows or
// `Software rasterizer` in the HTML5 player.
// @endtooltip
GBX_INLINE_FN("GL3D_INFO$", 0, 0, gl3d::cmdInfo);

// @tooltip GL3D_WIDTH
// **GL3D_WIDTH()**
//
// Returns the render resolution width set by `GL3D_INIT`.
// @endtooltip
GBX_INLINE_FN("GL3D_WIDTH", 0, 0, gl3d::cmdWidth);

// @tooltip GL3D_HEIGHT
// **GL3D_HEIGHT()**
//
// Returns the render resolution height set by `GL3D_INIT`.
// @endtooltip
GBX_INLINE_FN("GL3D_HEIGHT", 0, 0, gl3d::cmdHeight);

// @tooltip GL3D_SHUTDOWN
// **GL3D_SHUTDOWN**
//
// Releases the OpenGL context and its resources. The module can be started
// again afterwards with `GL3D_INIT`.
// @endtooltip
GBX_INLINE_FN("GL3D_SHUTDOWN", 0, 0, gl3d::cmdShutdown);

#endif /* GBX_OPENGL3D_H */
