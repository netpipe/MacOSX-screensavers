// matrix_saver.cpp — Matrix-style OpenGL 4.1 screensaver (single file)
// GLFW window/context + GLEW loading + GLUT clock. No GLM: minimal hand-rolled math.
//
// Linux:   g++ -std=c++11 -O2 matrix_saver.cpp -o matrix_saver $(pkg-config --cflags --libs glfw3 glew glut)
// macOS:   clang++ -std=c++11 -O2 matrix_saver.cpp -framework OpenGL -framework GLUT -lglfw -lGLEW -o matrix_saver
// Windows: cl /std:c++11 /O2 /DMATRIX_NO_GLUT matrix_saver.cpp /link glfw3.lib glew32.lib opengl32.lib
//
// Keys: ESC quit | SPACE next camera shot | P pause

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifndef MATRIX_NO_GLUT
#  ifdef __APPLE__
#    include <GLUT/glut.h>
#  else
#    include <GL/glut.h>
#  endif
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <array>
#include <tuple>
#include <map>
#include <fstream>
#include <sstream>
#include <random>

// --------------------------------------------------------------------------
// tiny math (no GLM)
// --------------------------------------------------------------------------
struct V3 {
    float x, y, z;
    V3() : x(0), y(0), z(0) {}
    V3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    V3 operator+(const V3& o) const { return V3(x + o.x, y + o.y, z + o.z); }
    V3 operator-(const V3& o) const { return V3(x - o.x, y - o.y, z - o.z); }
    V3 operator*(float s)     const { return V3(x * s, y * s, z * s); }
};
static float dot3(const V3& a, const V3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static V3  cross3(const V3& a, const V3& b) {
    return V3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
}
static float len3(const V3& a) { return sqrtf(dot3(a, a)); }
static V3  norm3(const V3& a) { float l = len3(a); return l > 1e-8f ? a * (1.0f/l) : V3(0,0,0); }

struct Mat4 { float m[16]; };   // column-major, OpenGL convention

static Mat4 matIdentity() {
    Mat4 r; memset(r.m, 0, sizeof(r.m));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}
static Mat4 matMul(const Mat4& a, const Mat4& b) {   // a * b
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int ro = 0; ro < 4; ++ro) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k*4 + ro] * b.m[c*4 + k];
            r.m[c*4 + ro] = s;
        }
    return r;
}
static Mat4 matPerspective(float fovyRad, float aspect, float n, float f) {
    Mat4 r; memset(r.m, 0, sizeof(r.m));
    float t = 1.0f / tanf(fovyRad * 0.5f);
    r.m[0]  = t / aspect;
    r.m[5]  = t;
    r.m[10] = (f + n) / (n - f);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * f * n / (n - f);
    return r;
}
static Mat4 matLookAt(const V3& eye, const V3& ctr, const V3& up) {
    V3 f = norm3(ctr - eye);
    V3 s = norm3(cross3(f, up));
    V3 u = cross3(s, f);
    Mat4 r = matIdentity();
    r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;
    r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot3(s, eye);
    r.m[13] = -dot3(u, eye);
    r.m[14] =  dot3(f, eye);
    return r;
}
static Mat4 matTranslate(float x, float y, float z) {
    Mat4 r = matIdentity(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r;
}
static Mat4 matRotY(float a) {
    Mat4 r = matIdentity();
    float c = cosf(a), s = sinf(a);
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
    return r;
}
static float smootherstep01(float x) {
    x = x < 0 ? 0 : (x > 1 ? 1 : x);
    return x * x * x * (x * (x * 6 - 15) + 10);
}

// --------------------------------------------------------------------------
// embedded 5x7 bitmap font (7 rows x 5 chars of ASCII art, bit '1' = pixel)
// converted at runtime into a texture atlas: 8x8 cells of 16px (2x scaled)
// --------------------------------------------------------------------------
static const char* FONT_5X7[] = {
    "01110100011001110101110011000101110", // 0
    "00100011000010000100001000010001110", // 1
    "01110100010000100010001000100011111", // 2
    "01110100010000100110000011000101110", // 3
    "00010001100101010010111110001000010", // 4
    "11111100001111000001000011000101110", // 5
    "00110010001000011110100011000101110", // 6
    "11111000010001000100010000100001000", // 7
    "01110100011000101110100011000101110", // 8
    "01110100011000101111000010001001100", // 9
    "01110100011000111111100011000110001", // A
    "11110100011000111110100011000111110", // B
    "01110100011000010000100001000101110", // C
    "11100100101000110001100011001011100", // D
    "11111100001000011110100001000011111", // E
    "11111100001000011110100001000010000", // F
    "01110100011000010111100011000101111", // G
    "10001100011000111111100011000110001", // H
    "01110001000010000100001000010001110", // I
    "00111000100001000010000101001001100", // J
    "10001100101010011000101001001010001", // K
    "10000100001000010000100001000011111", // L
    "10001110111010110101100011000110001", // M
    "10001110011010110011100011000110001", // N
    "01110100011000110001100011000101110", // O
    "11110100011000111110100001000010000", // P
    "01110100011000110001101011001001101", // Q
    "11110100011000111110101001001010001", // R
    "01111100001000001110000010000111110", // S
    "11111001000010000100001000010000100", // T
    "10001100011000110001100011000101110", // U
    "10001100011000110001100010101000100", // V
    "10001100011000110101101011101110001", // W
    "10001100010101000100010101000110001", // X
    "10001100010101000100001000010000100", // Y
    "11111000010001000100010001000011111", // Z
    "01110100011011110101101101000001110", // @
    "01010010101111101010111110101001010", // #
    "00100011111010001110001011111000100", // $
    "11001110100001000100010000101110011", // %
    "01100100101010001000101011001001101", // &
    "00000001001010101110101010010000000", // *
    "00000001000010011111001000010000000", // +
    "00000000001111100000111110000000000", // =
    "01110100010000100010001000000000100", // ?
    "00000001000000000000001000000000000", // :
    "00000000000000000000000000011000110", // .
    "00000000000000011111000000000000000", // -
    "00001000100001000100010000100010000", // /
    "00100001000010000100001000000000100", // !
    "00010001000100010000010000010000010", // <
    "01000001000001000001000100010001000"  // >
};
static const int FONT_COUNT = (int)(sizeof(FONT_5X7) / sizeof(FONT_5X7[0])); // 52

static GLuint buildAtlasTexture() {
    static unsigned char px[128 * 128];
    memset(px, 0, sizeof(px));
    for (int g = 0; g < FONT_COUNT; ++g) {
        int gx = (g % 8) * 16, gy = (g / 8) * 16;
        for (int b = 0; b < 7; ++b)          // b = row, 0 = top
            for (int c = 0; c < 5; ++c)      // c = column
                if (FONT_5X7[g][b * 5 + c] == '1')
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx) {
                            int X = gx + 3 + c * 2 + dx;
                            int Y = gy + 1 + (6 - b) * 2 + dy;
                            px[Y * 128 + X] = 255;
                        }
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 128, 128, 0, GL_RED, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

// --------------------------------------------------------------------------
// shaders
// --------------------------------------------------------------------------
static const char* RAIN_VS = R"(
#version 410 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec2 a_uv;
uniform mat4 u_vp;
out vec2 v_uv;
out vec3 v_pos;
void main() {
    v_uv = a_uv;
    v_pos = a_pos;
    gl_Position = u_vp * vec4(a_pos, 1.0);
}
)";

static const char* RAIN_FS = R"(
#version 410 core
in vec2 v_uv;
in vec3 v_pos;
uniform sampler2D u_atlas;
uniform vec2  u_grid;    // glyph cells across / down this surface
uniform float u_time, u_dim, u_seed;
uniform vec2  u_res;
uniform vec3  u_eye;
out vec4 outColor;

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

void main() {
    vec2 st = v_uv * u_grid;
    float col = floor(st.x);
    float fx  = fract(st.x);

    float hc1 = hash(vec2(col + u_seed, 7.31));
    float hc2 = hash(vec2(col + u_seed, 3.77));
    float hc3 = hash(vec2(col + u_seed, 9.13));

    float speed = mix(1.2, 4.2, hc1);
    float t     = u_time * speed + hc3 * 64.0;

    float y   = st.y + t;          // scroll space: features fall downward
    float row = floor(y);
    float fy  = fract(y);

    float trail = mix(4.0, 10.0, hc2);
    float head  = mod(t, u_grid.y + trail);
    float depth = head - row;      // 0 at head, grows down the tail
    float lit   = step(0.0, depth) * step(depth, trail);
    float b     = 1.0 - depth / max(trail, 0.001);
    b *= b;
    b *= mix(0.35, 1.0, hash(vec2(col, row) * 1.93));

    // glyph choice, with occasional flicker re-roll
    float flick = step(0.94, hash(vec2(col, row) * 3.71));
    float ci = hash(vec2(col * 1.3 + 17.0, row * 2.9 + floor(u_time * 6.0) * flick));
    float gi = floor(ci * 52.0);
    float gx = mod(gi, 8.0);
    float gy = floor(gi / 8.0);

    vec2 cellpx = vec2(3.0 + fx * 10.0, 1.0 + fy * 14.0);
    vec2 auv = (vec2(gx, gy) * 16.0 + cellpx) / 128.0;
    float glyph = texture(u_atlas, auv).r;

    float headBoost = lit * step(depth, 1.0);
    vec3 textColor = vec3(0.10, 1.00, 0.30) * (b * lit)
                   + vec3(0.75, 1.00, 0.80) * headBoost * 0.8;

    vec3 res = vec3(0.004, 0.014, 0.008);            // dark wall base
    res += textColor * glyph * u_dim;
    res += vec3(0.02, 0.12, 0.05) * glyph * u_dim * 0.25;  // faint ambient glyphs
    res *= 0.96 + 0.04 * sin(u_time * 47.0);         // CRT flicker

    // fog toward darkness
    float d = length(u_eye - v_pos);
    res = mix(res, vec3(0.0, 0.02, 0.01), 1.0 - exp(-d * 0.05));

    // scanlines + vignette
    float scan = 0.88 + 0.12 * sin(gl_FragCoord.y * 2.4);
    vec2 q = gl_FragCoord.xy / u_res * 2.0 - 1.0;
    float vig = 1.0 - 0.35 * dot(q * 0.7, q * 0.7);
    res *= scan * vig;

    outColor = vec4(res, 1.0);
}
)";

static const char* MODEL_VS = R"(
#version 410 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec3 a_nor;
uniform mat4 u_vp;
uniform mat4 u_model;
out vec3 v_n;
out vec3 v_p;
void main() {
    vec4 wp = u_model * vec4(a_pos, 1.0);
    v_p = wp.xyz;
    v_n = mat3(u_model) * a_nor;
    gl_Position = u_vp * wp;
}
)";

static const char* MODEL_FS = R"(
#version 410 core
in vec3 v_n;
in vec3 v_p;
uniform vec3  u_eye;
uniform vec3  u_light;
uniform float u_time;
uniform vec2  u_res;
out vec4 outColor;
void main() {
    vec3 N = normalize(v_n);
    vec3 V = normalize(u_eye - v_p);
    vec3 L = normalize(u_light - v_p);
    vec3 H = normalize(L + V);

    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 48.0);
    float rim  = pow(1.0 - max(dot(N, V), 0.0), 3.0);

    vec3 base = vec3(0.10, 0.16, 0.12);
    vec3 col = base * (vec3(0.16, 0.40, 0.24) + diff * vec3(0.5, 1.0, 0.6) * 1.2);
    col += spec * vec3(0.7, 1.0, 0.8) * 0.5;
    col += rim  * vec3(0.10, 0.90, 0.35) * 0.55;

    // subtle scrolling holo-bands
    float band = smoothstep(0.92, 1.0, sin(v_p.y * 22.0 - u_time * 2.2) * 0.5 + 0.5);
    col += band * vec3(0.05, 0.35, 0.12) * 0.35;

    float d = length(u_eye - v_p);
    col = mix(col, vec3(0.0, 0.03, 0.015), 1.0 - exp(-d * 0.045));

    float scan = 0.88 + 0.12 * sin(gl_FragCoord.y * 2.4);
    vec2 q = gl_FragCoord.xy / u_res * 2.0 - 1.0;
    float vig = 1.0 - 0.35 * dot(q * 0.7, q * 0.7);
    col *= scan * vig;

    outColor = vec4(col, 1.0);
}
)";

static const char* PART_VS = R"(
#version 410 core
uniform mat4  u_vp;
uniform float u_time, u_scale, u_H;
out float v_glyph;
out float v_fade;
float hash11(float n) { return fract(sin(n) * 43758.5453123); }
void main() {
    float id = float(gl_VertexID);
    float h1 = hash11(id + 1.0), h2 = hash11(id + 2.7), h3 = hash11(id + 3.9);
    float h4 = hash11(id + 4.3), h5 = hash11(id + 5.1);
    vec3 p;
    p.x = (h1 * 2.0 - 1.0) * 6.6;
    p.z = (h2 * 2.0 - 1.0) * 6.6;
    float sp = 0.4 + h3 * 1.2;
    p.y = u_H - mod(h4 * 40.0 + u_time * sp, u_H);
    v_glyph = floor(h5 * 52.0);
    v_fade = smoothstep(0.0, 0.6, p.y) * smoothstep(u_H, u_H - 0.8, p.y);
    gl_Position = u_vp * vec4(p, 1.0);
    gl_PointSize = clamp(u_scale / gl_Position.w, 2.0, 48.0);
}
)";

static const char* PART_FS = R"(
#version 410 core
in float v_glyph;
in float v_fade;
uniform sampler2D u_atlas;
out vec4 outColor;
void main() {
    vec2 pc = gl_PointCoord;
    float gx = mod(v_glyph, 8.0);
    float gy = floor(v_glyph / 8.0);
    vec2 cellpx = vec2(3.0 + pc.x * 10.0, 1.0 + (1.0 - pc.y) * 14.0);
    vec2 auv = (vec2(gx, gy) * 16.0 + cellpx) / 128.0;
    float g = texture(u_atlas, auv).r;
    outColor = vec4(vec3(0.2, 1.0, 0.45) * (g * v_fade * 0.5), 1.0);
}
)";

static const char* DISK_VS = R"(
#version 410 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec2 a_uv;
uniform mat4  u_vp;
uniform float u_size;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    vec3 wp = vec3(a_pos.x * u_size, 0.02, a_pos.z * u_size);
    gl_Position = u_vp * vec4(wp, 1.0);
}
)";

static const char* DISK_FS = R"(
#version 410 core
in vec2 v_uv;
uniform float u_time;
out vec4 outColor;
void main() {
    float d = length(v_uv * 2.0 - 1.0);
    float a = exp(-d * 4.0) * (0.30 + 0.08 * sin(u_time * 1.8));
    a += 0.05 * exp(-abs(d - 0.7) * 20.0);
    outColor = vec4(vec3(0.05, 0.85, 0.28) * a, 1.0);
}
)";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "Shader compile error:\n%s\n", log);
    }
    return s;
}
static GLuint makeProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

// --------------------------------------------------------------------------
// OBJ loader (v / vn / vt / f with v, v/vt, v//vn, v/vt/vn, negative indices,
// polygon fan-triangulation; materials ignored)
// --------------------------------------------------------------------------
struct Vertex { float px, py, pz, nx, ny, nz, u, v; };

static void parseVertRef(const std::string& s, int& vi, int& ti, int& ni) {
    vi = ti = ni = 0;
    vi = atoi(s.c_str());
    size_t p1 = s.find('/');
    if (p1 == std::string::npos) return;
    size_t p2 = s.find('/', p1 + 1);
    if (p2 != p1 + 1) ti = atoi(s.c_str() + p1 + 1);
    if (p2 != std::string::npos && p2 + 1 < s.size()) ni = atoi(s.c_str() + p2 + 1);
}

static bool loadObj(const char* path, std::vector<Vertex>& out) {
    std::ifstream f(path);
    if (!f) return false;

    std::vector<std::array<float, 3>> P, N;
    std::vector<std::array<float, 2>> T;
    std::vector<Vertex> verts;
    bool missingNormal = false;

    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() > 1 && line[0] == 'v' && line[1] == 'n') {
            std::array<float, 3> n;
            if (sscanf(line.c_str(), "vn %f %f %f", &n[0], &n[1], &n[2]) == 3) N.push_back(n);
        } else if (line.size() > 1 && line[0] == 'v' && line[1] == 't') {
            std::array<float, 2> t;
            if (sscanf(line.c_str(), "vt %f %f", &t[0], &t[1]) == 2) T.push_back(t);
        } else if (line.size() > 1 && line[0] == 'v' && line[1] == ' ') {
            std::array<float, 3> p;
            if (sscanf(line.c_str(), "v %f %f %f", &p[0], &p[1], &p[2]) == 3) P.push_back(p);
        } else if (line.size() > 1 && line[0] == 'f' && line[1] == ' ') {
            std::istringstream ss(line.substr(2));
            std::vector<std::array<int, 3>> face;
            std::string tok;
            while (ss >> tok) {
                int vi, ti, ni;
                parseVertRef(tok, vi, ti, ni);
                if (vi < 0) vi = (int)P.size() + vi + 1;
                if (ti < 0) ti = (int)T.size() + ti + 1;
                if (ni < 0) ni = (int)N.size() + ni + 1;
                vi = std::max(1, std::min((int)P.size(), vi)) - 1;
                if (ti == 0) missingNormal = true;   // no normal ref: will compute
                ti = ti ? std::max(1, std::min((int)T.size(), ti)) - 1 : -1;
                ni = ni ? std::max(1, std::min((int)N.size(), ni)) - 1 : -1;
                face.push_back({{vi, ti, ni}});
            }
            for (size_t k = 2; k < face.size(); ++k) {
                const std::array<int, 3>* tri[3] = { &face[0], &face[k-1], &face[k] };
                for (int c = 0; c < 3; ++c) {
                    const std::array<int, 3>& r = *tri[c];
                    Vertex vtx;
                    vtx.px = P[r[0]][0]; vtx.py = P[r[0]][1]; vtx.pz = P[r[0]][2];
                    if (r[2] >= 0) { vtx.nx = N[r[2]][0]; vtx.ny = N[r[2]][1]; vtx.nz = N[r[2]][2]; }
                    else           { vtx.nx = vtx.ny = vtx.nz = 0; missingNormal = true; }
                    if (r[1] >= 0) { vtx.u = T[r[1]][0]; vtx.v = T[r[1]][1]; }
                    else           { vtx.u = vtx.v = 0; }
                    verts.push_back(vtx);
                }
            }
        }
    }
    if (verts.empty()) return false;

    // compute smooth normals if the file has none (group by identical position)
    if (missingNormal) {
        std::map<std::tuple<float, float, float>, int> uniq;
        std::vector<V3> acc;
        for (size_t i = 0; i < verts.size(); ++i) {
            auto key = std::make_tuple(verts[i].px, verts[i].py, verts[i].pz);
            auto it = uniq.find(key);
            if (it == uniq.end()) { uniq[key] = (int)acc.size(); acc.push_back(V3()); }
        }
        for (size_t i = 0; i + 2 < verts.size(); i += 3) {
            V3 a(verts[i].px,   verts[i].py,   verts[i].pz);
            V3 b(verts[i+1].px, verts[i+1].py, verts[i+1].pz);
            V3 c(verts[i+2].px, verts[i+2].py, verts[i+2].pz);
            V3 fn = cross3(b - a, c - a);
            for (int k = 0; k < 3; ++k) {
                const Vertex& vtx = verts[i + k];
                int id = uniq[std::make_tuple(vtx.px, vtx.py, vtx.pz)];
                acc[id] = acc[id] + fn;
            }
        }
        for (size_t i = 0; i < verts.size(); ++i) {
            int id = uniq[std::make_tuple(verts[i].px, verts[i].py, verts[i].pz)];
            V3 n = norm3(acc[id]);
            verts[i].nx = n.x; verts[i].ny = n.y; verts[i].nz = n.z;
        }
    }
    out = verts;
    return true;
}

// procedural fallback: trefoil knot with parallel-transport tube frames
static void makeKnot(std::vector<Vertex>& out) {
    const int U = 300, W = 20;
    const float tube = 0.42f, TAU = 6.28318530718f;
    auto kp = [&](float t) {
        float p = 2, q = 3;
        float r = 2.0f + cosf(q * t);
        return V3(r * cosf(p * t), sinf(q * t) * 1.3f, r * sinf(p * t)) * 0.55f;
    };
    std::vector<V3> C(U + 1), Nr(U + 1), Br(U + 1);
    V3 prevN;
    for (int i = 0; i <= U; ++i) {
        float t = i * TAU / U;
        V3 c = kp(t);
        V3 tg = norm3(kp(t + 1e-3f) - kp(t - 1e-3f));
        V3 n = (i == 0) ? norm3(cross3(tg, V3(0, 1, 0)))
                        : norm3(prevN - tg * dot3(prevN, tg));
        C[i] = c; Nr[i] = n; Br[i] = cross3(tg, n);
        prevN = n;
    }
    for (int i = 0; i < U; ++i)
        for (int j = 0; j < W; ++j) {
            float a0 = j * TAU / W, a1 = (j + 1) * TAU / W;
            V3 pt[4], nn[4];
            pt[0] = C[i]   + (Nr[i]   * cosf(a0) + Br[i]   * sinf(a0)) * tube;
            nn[0] =           Nr[i]   * cosf(a0) + Br[i]   * sinf(a0);
            pt[1] = C[i+1] + (Nr[i+1] * cosf(a0) + Br[i+1] * sinf(a0)) * tube;
            nn[1] =           Nr[i+1] * cosf(a0) + Br[i+1] * sinf(a0);
            pt[2] = C[i+1] + (Nr[i+1] * cosf(a1) + Br[i+1] * sinf(a1)) * tube;
            nn[2] =           Nr[i+1] * cosf(a1) + Br[i+1] * sinf(a1);
            pt[3] = C[i]   + (Nr[i]   * cosf(a1) + Br[i]   * sinf(a1)) * tube;
            nn[3] =           Nr[i]   * cosf(a1) + Br[i]   * sinf(a1);
            int idx[6] = { 0, 1, 2, 0, 2, 3 };
            for (int k = 0; k < 6; ++k) {
                Vertex vtx;
                vtx.px = pt[idx[k]].x; vtx.py = pt[idx[k]].y; vtx.pz = pt[idx[k]].z;
                vtx.nx = nn[idx[k]].x; vtx.ny = nn[idx[k]].y; vtx.nz = nn[idx[k]].z;
                vtx.u = vtx.v = 0;
                out.push_back(vtx);
            }
        }
}

// center/scale mesh to fit room, rest it just above the floor
static float g_modelCY = 1.0f;
static void fitMesh(std::vector<Vertex>& verts) {
    V3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
    for (auto& v : verts) {
        mn.x = std::min(mn.x, v.px); mn.y = std::min(mn.y, v.py); mn.z = std::min(mn.z, v.pz);
        mx.x = std::max(mx.x, v.px); mx.y = std::max(mx.y, v.py); mx.z = std::max(mx.z, v.pz);
    }
    V3 c = (mn + mx) * 0.5f;
    float maxDim = std::max(mx.x - mn.x, std::max(mx.y - mn.y, mx.z - mn.z));
    float s = 2.3f / std::max(maxDim, 1e-5f);
    for (auto& v : verts) {
        v.px = (v.px - c.x) * s;
        v.py = (v.py - c.y) * s;
        v.pz = (v.pz - c.z) * s;
    }
    float minY = 1e30f, maxY = -1e30f;
    for (auto& v : verts) { minY = std::min(minY, v.py); maxY = std::max(maxY, v.py); }
    float dy = 0.12f - minY;
    for (auto& v : verts) v.py += dy;
    g_modelCY = (minY + maxY) * 0.5f + dy;
}

// --------------------------------------------------------------------------
// room (walls / floor / ceiling) as 6 quads with UVs
// --------------------------------------------------------------------------
static const float ROOM_W = 7.0f, ROOM_H = 5.0f, GLYPH_H = 0.34f;
struct FaceInfo { float cols, rows, dim, seed; };

static void buildRoom(std::vector<float>& out, std::vector<FaceInfo>& faces) {
    auto quad = [&](V3 a, V3 b, V3 c, V3 d) {   // strip order
        const float uv[4][2] = { {0,0}, {1,0}, {0,1}, {1,1} };
        const V3* pts[4] = { &a, &b, &c, &d };
        for (int i = 0; i < 4; ++i)
            out.insert(out.end(), { pts[i]->x, pts[i]->y, pts[i]->z, uv[i][0], uv[i][1] });
    };
    float colsSq = roundf(2 * ROOM_W / GLYPH_H);        // 41
    float rowsSq = colsSq;
    float colsWd = colsSq;
    float rowsWd = roundf(ROOM_H / GLYPH_H);            // 15

    quad(V3(-ROOM_W, 0, -ROOM_W), V3(ROOM_W, 0, -ROOM_W),                 // floor
         V3(-ROOM_W, 0,  ROOM_W), V3(ROOM_W, 0,  ROOM_W));
    faces.push_back({ colsSq, rowsSq, 0.80f, 0.0f });

    quad(V3(-ROOM_W, ROOM_H, -ROOM_W), V3(ROOM_W, ROOM_H, -ROOM_W),       // ceiling
         V3(-ROOM_W, ROOM_H,  ROOM_W), V3(ROOM_W, ROOM_H,  ROOM_W));
    faces.push_back({ colsSq, rowsSq, 0.55f, 11.0f });

    quad(V3(-ROOM_W, 0, -ROOM_W), V3(ROOM_W, 0, -ROOM_W),                 // back
         V3(-ROOM_W, ROOM_H, -ROOM_W), V3(ROOM_W, ROOM_H, -ROOM_W));
    faces.push_back({ colsWd, rowsWd, 1.0f, 23.0f });

    quad(V3(ROOM_W, 0, ROOM_W), V3(-ROOM_W, 0, ROOM_W),                   // front
         V3(ROOM_W, ROOM_H, ROOM_W), V3(-ROOM_W, ROOM_H, ROOM_W));
    faces.push_back({ colsWd, rowsWd, 1.0f, 47.0f });

    quad(V3(-ROOM_W, 0, ROOM_W), V3(-ROOM_W, 0, -ROOM_W),                 // left
         V3(-ROOM_W, ROOM_H, ROOM_W), V3(-ROOM_W, ROOM_H, -ROOM_W));
    faces.push_back({ colsWd, rowsWd, 1.0f, 71.0f });

    quad(V3(ROOM_W, 0, -ROOM_W), V3(ROOM_W, 0, ROOM_W),                   // right
         V3(ROOM_W, ROOM_H, -ROOM_W), V3(ROOM_W, ROOM_H, ROOM_W));
    faces.push_back({ colsWd, rowsWd, 1.0f, 97.0f });
}

// --------------------------------------------------------------------------
// cinematic camera rig: drifting keyframes, smoothly blended
// --------------------------------------------------------------------------
struct CamKey { float r, h, fov, ty, speed, hold; };
static std::mt19937 g_rng((unsigned)std::chrono::steady_clock::now().time_since_epoch().count());
static float rnd(float a, float b) {
    return std::uniform_real_distribution<float>(a, b)(g_rng);
}
static CamKey randKey() {
    CamKey k;
    k.r     = rnd(2.6f, 5.2f);
    k.h     = rnd(0.8f, 4.2f);
    k.fov   = rnd(44.0f, 70.0f);
    k.ty    = rnd(0.7f, 2.0f);
    k.speed = rnd(0.08f, 0.35f) * (rnd(0, 1) < 0.5f ? -1.0f : 1.0f);
    k.hold  = rnd(10.0f, 18.0f);
    return k;
}

struct CameraRig {
    CamKey kA, kB;
    float age = 100.0f;         // force immediate first key on update
    float theta = rnd(0, 6.28f);
    float fov = 55.0f;
    V3 eye, target;

    void nextShot() { age = kB.hold; }
    void update(float dt, float t) {
        age += dt;
        if (age > kB.hold) { kA = kB; kB = randKey(); age = 0; }
        float u = smootherstep01(age / 3.5f);
        float r   = kA.r   + (kB.r   - kA.r)   * u;
        float h   = kA.h   + (kB.h   - kA.h)   * u;
        float ty  = kA.ty  + (kB.ty  - kA.ty)  * u;
        float sp  = kA.speed + (kB.speed - kA.speed) * u;
        fov = kA.fov + (kB.fov - kA.fov) * u;

        theta += sp * dt;       // azimuth integrates continuously: no snaps
        target = V3(0.30f * sinf(t * 0.23f),
                    ty + 0.10f * sinf(t * 0.31f),
                    0.30f * cosf(t * 0.19f));
        eye = target + V3(cosf(theta) * r, h, sinf(theta) * r);
        eye.x += 0.10f * sinf(t * 0.9f);                  // gentle hand-held sway
        eye.y += 0.08f * sinf(t * 1.3f + 2.0f);
        eye.z += 0.10f * cosf(t * 1.1f);
        eye.y = std::max(0.25f, std::min(4.85f, eye.y));
    }
};

// --------------------------------------------------------------------------
static GLFWwindow* g_win = nullptr;
static bool g_paused = false;

static void keyCb(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, GLFW_TRUE);
    if (key == GLFW_KEY_P)      g_paused = !g_paused;
    if (key == GLFW_KEY_SPACE)  glfwSetWindowUserPointer(w, (void*)1); // signal: next shot
}

static double getTimeSec() {
#ifdef MATRIX_NO_GLUT
    return glfwGetTime();
#else
    return glutGet(GLUT_ELAPSED_TIME) * 0.001;   // GLUT is our clock
#endif
}

int main(int argc, char** argv) {
#ifndef MATRIX_NO_GLUT
    glutInit(&argc, argv);
#endif
    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* vm = glfwGetVideoMode(mon);
    g_win = glfwCreateWindow(vm->width, vm->height, "Matrix Screensaver", mon, nullptr);
    if (!g_win) { fprintf(stderr, "window creation failed\n"); return 1; }
    glfwMakeContextCurrent(g_win);
    glfwSwapInterval(1);
    glfwSetKeyCallback(g_win, keyCb);
    glfwSetWindowUserPointer(g_win, nullptr);
    glfwSetInputMode(g_win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { fprintf(stderr, "glewInit failed\n"); return 1; }
    glGetError();   // swallow spurious error some drivers emit after glewInit on core
    fprintf(stderr, "GL: %s\n", (const char*)glGetString(GL_VERSION));

    // ---- programs
    GLuint progRain  = makeProgram(RAIN_VS,  RAIN_FS);
    GLuint progModel = makeProgram(MODEL_VS, MODEL_FS);
    GLuint progPart  = makeProgram(PART_VS,  PART_FS);
    GLuint progDisk  = makeProgram(DISK_VS,  DISK_FS);
    glProgramUniform1i(progRain, glGetUniformLocation(progRain, "u_atlas"), 0);
    glProgramUniform1i(progPart, glGetUniformLocation(progPart, "u_atlas"), 0);

    // ---- atlas texture
    GLuint atlas = buildAtlasTexture();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas);

    // ---- model
    std::vector<Vertex> mesh;
    const char* objPath = (argc > 1) ? argv[1] : "model.obj";
    if (!loadObj(objPath, mesh)) {
        fprintf(stderr, "Could not load '%s' - using procedural trefoil knot.\n", objPath);
        mesh.clear();
        makeKnot(mesh);
    } else {
        fprintf(stderr, "Loaded %s: %zu triangles\n", objPath, mesh.size() / 3);
    }
    fitMesh(mesh);

    GLuint modelVAO, modelVBO;
    glGenVertexArrays(1, &modelVAO);
    glGenBuffers(1, &modelVBO);
    glBindVertexArray(modelVAO);
    glBindBuffer(GL_ARRAY_BUFFER, modelVBO);
    glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(Vertex), mesh.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    GLsizei modelCount = (GLsizei)mesh.size();

    // ---- room
    std::vector<float> roomVerts;
    std::vector<FaceInfo> faces;
    buildRoom(roomVerts, faces);

    GLuint roomVAO, roomVBO;
    glGenVertexArrays(1, &roomVAO);
    glGenBuffers(1, &roomVBO);
    glBindVertexArray(roomVAO);
    glBindBuffer(GL_ARRAY_BUFFER, roomVBO);
    glBufferData(GL_ARRAY_BUFFER, roomVerts.size() * sizeof(float), roomVerts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, (void*)12);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // ---- glow disk under the model
    static const float diskVerts[] = {
        -1, 0, -1,  0, 0,
         1, 0, -1,  1, 0,
        -1, 0,  1,  0, 1,
         1, 0,  1,  1, 1,
    };
    GLuint diskVAO, diskVBO;
    glGenVertexArrays(1, &diskVAO);
    glGenBuffers(1, &diskVBO);
    glBindVertexArray(diskVAO);
    glBindBuffer(GL_ARRAY_BUFFER, diskVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(diskVerts), diskVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, (void*)12);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // ---- particles: empty VAO, everything derived from gl_VertexID
    GLuint emptyVAO;
    glGenVertexArrays(1, &emptyVAO);
    const int PARTICLES = 700;

    // ---- GL state
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.012f, 0.006f, 1.0f);

    CameraRig rig;
    rig.kA = rig.kB = randKey();
    rig.age = 100.0f;

    double last = getTimeSec();
    float animT = 0.0f;

    while (!glfwWindowShouldClose(g_win)) {
        glfwPollEvents();
        if (glfwGetWindowUserPointer(g_win)) {   // SPACE pressed
            rig.nextShot();
            glfwSetWindowUserPointer(g_win, nullptr);
        }

        double now = getTimeSec();
        float dt = (float)std::min(0.05, std::max(0.0, now - last));
        last = now;
        if (!g_paused) animT += dt;

        int fbW, fbH;
        glfwGetFramebufferSize(g_win, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);

        rig.update(g_paused ? 0.0f : dt, animT);
        float fovRad = rig.fov * 3.14159265f / 180.0f;
        Mat4 proj = matPerspective(fovRad, (float)fbW / (float)std::max(1, fbH), 0.1f, 60.0f);
        Mat4 view = matLookAt(rig.eye, rig.target, V3(0, 1, 0));
        Mat4 vp = matMul(proj, view);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // --- room rain (6 quads)
        glUseProgram(progRain);
        glProgramUniformMatrix4fv(progRain, glGetUniformLocation(progRain, "u_vp"), 1, GL_FALSE, vp.m);
        glProgramUniform1f(progRain, glGetUniformLocation(progRain, "u_time"), animT);
        glProgramUniform2f(progRain, glGetUniformLocation(progRain, "u_res"), (float)fbW, (float)fbH);
        glProgramUniform3f(progRain, glGetUniformLocation(progRain, "u_eye"), rig.eye.x, rig.eye.y, rig.eye.z);
        glBindVertexArray(roomVAO);
        for (int i = 0; i < 6; ++i) {
            glProgramUniform2f(progRain, glGetUniformLocation(progRain, "u_grid"), faces[i].cols, faces[i].rows);
            glProgramUniform1f(progRain, glGetUniformLocation(progRain, "u_dim"), faces[i].dim);
            glProgramUniform1f(progRain, glGetUniformLocation(progRain, "u_seed"), faces[i].seed);
            glDrawArrays(GL_TRIANGLE_STRIP, i * 4, 4);
        }

        // --- model (slow spin about its own center height)
        Mat4 M = matMul(matTranslate(0, g_modelCY, 0),
               matMul(matRotY(animT * 0.15f),
                      matTranslate(0, -g_modelCY, 0)));
        V3 light(cosf(animT * 0.4f) * 4.5f, 4.0f, sinf(animT * 0.4f) * 4.5f);
        glUseProgram(progModel);
        glProgramUniformMatrix4fv(progModel, glGetUniformLocation(progModel, "u_vp"), 1, GL_FALSE, vp.m);
        glProgramUniformMatrix4fv(progModel, glGetUniformLocation(progModel, "u_model"), 1, GL_FALSE, M.m);
        glProgramUniform3f(progModel, glGetUniformLocation(progModel, "u_eye"), rig.eye.x, rig.eye.y, rig.eye.z);
        glProgramUniform3f(progModel, glGetUniformLocation(progModel, "u_light"), light.x, light.y, light.z);
        glProgramUniform1f(progModel, glGetUniformLocation(progModel, "u_time"), animT);
        glProgramUniform2f(progModel, glGetUniformLocation(progModel, "u_res"), (float)fbW, (float)fbH);
        glBindVertexArray(modelVAO);
        glDrawArrays(GL_TRIANGLES, 0, modelCount);

        // --- additive passes: glow disk + falling glyph particles
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);

        glUseProgram(progDisk);
        glProgramUniformMatrix4fv(progDisk, glGetUniformLocation(progDisk, "u_vp"), 1, GL_FALSE, vp.m);
        glProgramUniform1f(progDisk, glGetUniformLocation(progDisk, "u_size"), 2.2f);
        glProgramUniform1f(progDisk, glGetUniformLocation(progDisk, "u_time"), animT);
        glBindVertexArray(diskVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        float pScale = ((float)fbH * 0.5f) / tanf(fovRad * 0.5f) * 0.16f;
        glUseProgram(progPart);
        glProgramUniformMatrix4fv(progPart, glGetUniformLocation(progPart, "u_vp"), 1, GL_FALSE, vp.m);
        glProgramUniform1f(progPart, glGetUniformLocation(progPart, "u_time"), animT);
        glProgramUniform1f(progPart, glGetUniformLocation(progPart, "u_scale"), pScale);
        glProgramUniform1f(progPart, glGetUniformLocation(progPart, "u_H"), ROOM_H);
        glBindVertexArray(emptyVAO);
        glDrawArrays(GL_POINTS, 0, PARTICLES);

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);

        glfwSwapBuffers(g_win);
    }

    glfwDestroyWindow(g_win);
    glfwTerminate();
    return 0;
}