// MinecraftC++ -- clean-room C++ voxel engine.
// Portable math used by the world, the player and the renderer.
#pragma once

#include <cmath>
#include <cstdint>

namespace mcc {

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const {
        const float l = length();
        return l > 1e-6f ? Vec3{x / l, y / l, z / l} : Vec3{};
    }
};

// Column-major 4x4 matrix, the layout OpenGL expects.
struct Mat4 {
    float m[16]{};

    static Mat4 identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static Mat4 multiply(const Mat4& a, const Mat4& b) {
        Mat4 r;
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                float v = 0.0f;
                for (int k = 0; k < 4; ++k) v += a.m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = v;
            }
        }
        return r;
    }

    static Mat4 perspective(float fovYRad, float aspect, float near, float far) {
        Mat4 r;
        const float f = 1.0f / std::tan(fovYRad * 0.5f);
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (far + near) / (near - far);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * far * near) / (near - far);
        return r;
    }

    static Mat4 ortho(float left, float right, float bottom, float top) {
        Mat4 r = identity();
        r.m[0] = 2.0f / (right - left);
        r.m[5] = 2.0f / (top - bottom);
        r.m[12] = -(right + left) / (right - left);
        r.m[13] = -(top + bottom) / (top - bottom);
        r.m[10] = -1.0f;
        return r;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        const Vec3 f = (center - eye).normalized();
        const Vec3 s = f.cross(up).normalized();
        const Vec3 u = s.cross(f);
        Mat4 r = identity();
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -s.dot(eye);
        r.m[13] = -u.dot(eye);
        r.m[14] = f.dot(eye);
        return r;
    }
};

// Six frustum planes extracted from a view-projection matrix.
struct Frustum {
    float p[6][4]{};

    void extract(const Mat4& vp) {
        const float* m = vp.m;
        const float rows[4][4] = {
            {m[0], m[4], m[8], m[12]},
            {m[1], m[5], m[9], m[13]},
            {m[2], m[6], m[10], m[14]},
            {m[3], m[7], m[11], m[15]},
        };
        auto setPlane = [&](int i, float a, float b, float c, float d) {
            const float len = std::sqrt(a * a + b * b + c * c);
            if (len < 1e-6f) {
                p[i][0] = p[i][1] = p[i][2] = p[i][3] = 0.0f;
                return;
            }
            p[i][0] = a / len; p[i][1] = b / len;
            p[i][2] = c / len; p[i][3] = d / len;
        };
        setPlane(0, rows[3][0] + rows[0][0], rows[3][1] + rows[0][1],
                 rows[3][2] + rows[0][2], rows[3][3] + rows[0][3]);
        setPlane(1, rows[3][0] - rows[0][0], rows[3][1] - rows[0][1],
                 rows[3][2] - rows[0][2], rows[3][3] - rows[0][3]);
        setPlane(2, rows[3][0] + rows[1][0], rows[3][1] + rows[1][1],
                 rows[3][2] + rows[1][2], rows[3][3] + rows[1][3]);
        setPlane(3, rows[3][0] - rows[1][0], rows[3][1] - rows[1][1],
                 rows[3][2] - rows[1][2], rows[3][3] - rows[1][3]);
        setPlane(4, rows[3][0] + rows[2][0], rows[3][1] + rows[2][1],
                 rows[3][2] + rows[2][2], rows[3][3] + rows[2][3]);
        setPlane(5, rows[3][0] - rows[2][0], rows[3][1] - rows[2][1],
                 rows[3][2] - rows[2][2], rows[3][3] - rows[2][3]);
    }

    bool boxVisible(const Vec3& mn, const Vec3& mx) const {
        for (int i = 0; i < 6; ++i) {
            const float a = p[i][0], b = p[i][1], c = p[i][2], d = p[i][3];
            // The corner furthest along the plane normal decides the whole box.
            const float x = a >= 0.0f ? mx.x : mn.x;
            const float y = b >= 0.0f ? mx.y : mn.y;
            const float z = c >= 0.0f ? mx.z : mn.z;
            if (a * x + b * y + c * z + d < 0.0f) return false;
        }
        return true;
    }
};

struct AABB {
    Vec3 mn, mx;
    bool intersects(const AABB& o) const {
        return mn.x < o.mx.x && mx.x > o.mn.x &&
               mn.y < o.mx.y && mx.y > o.mn.y &&
               mn.z < o.mx.z && mx.z > o.mn.z;
    }
};

struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    Color() = default;
    Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
    static Color rgb(uint32_t hex, float alpha = 1.0f) {
        return Color{((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f,
                     (hex & 0xFF) / 255.0f, alpha};
    }
    Color withAlpha(float alpha) const { return Color{r, g, b, alpha}; }
    Color scaled(float s) const { return Color{r * s, g * s, b * s, a}; }
};

}  // namespace mcc
