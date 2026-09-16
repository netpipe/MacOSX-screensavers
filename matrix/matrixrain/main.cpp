// matrix_rain.cpp — Matrix "digital rain" screensaver, true 3D, OpenGL 4.1 core
//
// Streams of glyphs fall at many different depths: some are far away and small,
// some are close and large. The camera drifts slowly to show off the parallax.
// No asset files, no glm — glyph atlas is built procedurally at startup.
//
// Build (Linux):
//   g++ -O2 -std=c++11 matrix_rain.cpp -o matrix_rain -lGLEW -lglfw -lGL
// Build (macOS, brew install glew glfw):
//   g++ -O2 -std=c++11 matrix_rain.cpp -o matrix_rain \
//       -I/opt/homebrew/include -L/opt/homebrew/lib -lGLEW -lglfw
// Run:
//   ./matrix_rain        (windowed 1280x720)
//   ./matrix_rain -f     (fullscreen)
// Keys: ESC or Q quits.

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

// ---------------------------------------------------------------------------
// Small math helpers (no glm)
// ---------------------------------------------------------------------------
struct Vec3 { float x, y, z; };
static Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
static float dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static Vec3 cross(const Vec3& a, const Vec3& b) {
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
static Vec3 normalize(const Vec3& v) {
    float l = std::sqrt(dot(v, v)); if (l < 1e-8f) l = 1e-8f;
    return { v.x/l, v.y/l, v.z/l };
}

struct Mat4 { float m[16]; }; // column-major, OpenGL style

static Mat4 mat4Identity() {
    Mat4 r = {}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r;
}
static Mat4 mat4Perspective(float fovY, float aspect, float zNear, float zFar) {
    Mat4 r = {};
    float t = std::tan(fovY * 0.5f);
    r.m[0]  = 1.0f / (aspect * t);
    r.m[5]  = 1.0f / t;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return r;
}
static Mat4 mat4LookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r = mat4Identity();
    r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;
    r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] =  dot(f, eye);
    return r;
}

// ---------------------------------------------------------------------------
// Embedded 5x7 bitmap font: A-Z, 0-9, then * + - . / : < = > ? # &
// Each glyph = 7 rows, 5 bits each (bit 4 = leftmost pixel).
// ---------------------------------------------------------------------------
static const unsigned char FONT_5x7[48][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // D
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}, // G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // V
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, // W
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // Y
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, // 2
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, // 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
    {0x1F,0x01,0x02,0x04,0x04,0x04,0x04}, // 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
    {0x00,0x0A,0x04,0x1F,0x04,0x0A,0x00}, // *
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // +
    {0x00,0x00,0x00,0x0E,0x00,0x00,0x00}, // -
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // .
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, // /
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, // :
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // <
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // =
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // >
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // ?
    {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}, // #
    {0x04,0x0A,0x0A,0x04,0x15,0x12,0x0D}, // &
};
static const int GLYPH_COUNT = 48;
static const int ATLAS_COLS = 8, ATLAS_ROWS = 6;
static const int CELL = 22;      // cell size in texels
static const int PAD  = 3;       // padding around glyph (room for glow)
static const int GLYPH_PX = 16;  // glyph drawn at 2x (8x8 -> 16x16 area)

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------
static const char* VERT_SRC = R"GLSL(
#version 410 core

layout(location = 0) in vec2 aCorner;   // quad corner, -0.5..0.5
layout(location = 1) in vec4 aInst0;    // x, z, letterIndex, streamLength
layout(location = 2) in vec4 aInst1;    // speed, phase, seed, brightness

uniform mat4  uProj;
uniform mat4  uView;
uniform float uTime;
uniform float uWorldH;
uniform float uGlyphCount;

out vec2  vUV;
out float vFade;
out float vHead;
out float vBright;
out float vDepth;
flat out float vGlyph;

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}
float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    float x     = aInst0.x;
    float z     = aInst0.y;
    float idx   = aInst0.z;
    float len   = aInst0.w;
    float speed = aInst1.x;
    float phase = aInst1.y;
    float seed  = aInst1.z;

    vBright = aInst1.w;
    vUV     = aCorner + 0.5;

    // Stream falls from +uWorldH/2 to -uWorldH/2, then waits (gap) and loops.
    float travel = uWorldH + len + 12.0;
    float prog   = mod(uTime * speed + phase, travel);
    float y      = uWorldH * 0.5 - prog + idx;   // head at idx 0, tail above

    // Brightness along the tail: white head -> fading green tail.
    float t = clamp(1.0 - idx / max(len - 1.0, 1.0), 0.0, 1.0);
    vFade = pow(t, 1.6);
    vHead = step(idx, 0.5);

    // Every letter scrambles to a new random glyph every now and then.
    float rate = mix(4.0, 18.0, hash11(seed + idx * 7.31));
    float slot = floor(uTime * rate + hash11(seed * 1.7 + idx * 3.7) * 23.0);
    vGlyph = floor(hash21(vec2(seed + idx * 13.7, slot)) * uGlyphCount);

    vec4 wpos = vec4(x + aCorner.x, y + aCorner.y, z, 1.0);
    vec4 vpos = uView * wpos;
    vDepth = -vpos.z;
    gl_Position = uProj * vpos;
}
)GLSL";

static const char* FRAG_SRC = R"GLSL(
#version 410 core

in vec2  vUV;
in float vFade;
in float vHead;
in float vBright;
in float vDepth;
flat in float vGlyph;

uniform sampler2D uAtlas;
uniform vec2  uGrid;     // atlas cells: cols, rows
uniform float uCell;     // cell size in texels
uniform float uPad;      // padding in texels
uniform float uGlyphPx;  // glyph area in texels

out vec4 outColor;

void main() {
    float col = mod(vGlyph, uGrid.x);
    float row = floor(vGlyph / uGrid.x);
    vec2 local = vec2(vUV.x, 1.0 - vUV.y);
    vec2 px = vec2(col, row) * uCell + uPad + local * uGlyphPx;
    vec2 uv = px / (uGrid * uCell);
    float g = texture(uAtlas, uv).r;

    // Far-away streams get dimmer, like haze.
    float depthDim = mix(0.15, 1.0, clamp((115.0 - vDepth) / 80.0, 0.0, 1.0));

    vec3 dim  = vec3(0.00, 0.30, 0.07);
    vec3 mid  = vec3(0.05, 0.85, 0.20);
    vec3 head = vec3(0.80, 1.00, 0.82);

    vec3 c = mix(dim, mid, vFade);
    c = mix(c, head, vHead);

    float a = g * (0.12 + 0.88 * vFade) * vBright * depthDim;
    a += vHead * g * 0.5 * vBright * depthDim;

    // Additive blending: rgb is the light we add.
    outColor = vec4(c * a, 1.0);
}
)GLSL";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "Shader compile error:\n%s\n", log);
        exit(1);
    }
    return s;
}
static GLuint linkProgram(const char* vsrc, const char* fsrc) {
    GLuint p = glCreateProgram();
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsrc);
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
        exit(1);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

// ---------------------------------------------------------------------------
// Build the glyph atlas texture (crisp glyphs + soft glow halo)
// ---------------------------------------------------------------------------
static void blurPass(const std::vector<unsigned char>& in,
                     std::vector<unsigned char>& out, int w, int h) {
    out.assign(w * h, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    int xx = x + dx < 0 ? 0 : (x + dx >= w ? w - 1 : x + dx);
                    int yy = y + dy < 0 ? 0 : (y + dy >= h ? h - 1 : y + dy);
                    sum += in[yy * w + xx];
                }
            out[y * w + x] = (unsigned char)(sum / 9);
        }
}

static GLuint buildAtlas() {
    const int W = ATLAS_COLS * CELL, H = ATLAS_ROWS * CELL;
    std::vector<unsigned char> crisp(W * H, 0), tmp, blurred;

    for (int g = 0; g < GLYPH_COUNT; ++g) {
        int cx = g % ATLAS_COLS, cy = g / ATLAS_COLS;
        int ox = cx * CELL + PAD + (GLYPH_PX - 10) / 2; // center 10px glyph
        int oy = cy * CELL + PAD + (GLYPH_PX - 14) / 2; // center 14px glyph
        for (int r = 0; r < 7; ++r) {
            unsigned char bits = FONT_5x7[g][r];
            for (int c = 0; c < 5; ++c) {
                if (bits & (1 << (4 - c))) {
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx) {
                            int px = ox + c * 2 + dx;
                            int py = oy + r * 2 + dy;
                            crisp[py * W + px] = 255;
                        }
                }
            }
        }
    }
    blurPass(crisp, tmp, W, H);
    blurPass(tmp, blurred, W, H);

    std::vector<unsigned char> fin(W * H);
    for (int i = 0; i < W * H; ++i) {
        unsigned char halo = (unsigned char)(blurred[i] * 0.55f);
        fin[i] = crisp[i] > halo ? crisp[i] : halo;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, W, H, 0, GL_RED, GL_UNSIGNED_BYTE, fin.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

// ---------------------------------------------------------------------------
static int fbW = 1280, fbH = 720;
static void framebufferSizeCB(GLFWwindow*, int w, int h) { fbW = w; fbH = h; }
static void keyCB(GLFWwindow* w, int key, int, int action, int) {
    if (action == GLFW_PRESS && (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_Q))
        glfwSetWindowShouldClose(w, GLFW_TRUE);
}

int main(int argc, char** argv) {
    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    bool fullscreen = (argc > 1 && strcmp(argv[1], "-f") == 0);
    GLFWmonitor* monitor = fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    const GLFWvidmode* vm = glfwGetVideoMode(glfwGetPrimaryMonitor());
    int winW = fullscreen ? vm->width : 1280;
    int winH = fullscreen ? vm->height : 720;

    GLFWwindow* win = glfwCreateWindow(winW, winH, "Matrix Rain 3D", monitor, nullptr);
    if (!win) { fprintf(stderr, "window creation failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glfwSetKeyCallback(win, keyCB);
    glfwSetFramebufferSizeCallback(win, framebufferSizeCB);
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    glfwGetFramebufferSize(win, &fbW, &fbH);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { fprintf(stderr, "glewInit failed\n"); return 1; }
    glGetError(); // swallow dummy error some drivers emit after glewInit

    GLuint program = linkProgram(VERT_SRC, FRAG_SRC);
    GLuint atlas   = buildAtlas();

    // ----- geometry: one unit quad, drawn instanced -----
    static const float QUAD[] = {
        -0.5f,-0.5f,  0.5f,-0.5f,  0.5f, 0.5f,
        -0.5f,-0.5f,  0.5f, 0.5f, -0.5f, 0.5f
    };

    // ----- streams of letters at random depths -----
    const int   NUM_STREAMS = 460;
    const float WORLD_H     = 64.0f;

    std::mt19937 rng(1234567u);
    std::uniform_real_distribution<float> uZ(-42.0f, 10.0f);   // depth: far -> near
    std::uniform_real_distribution<float> uX(-36.0f, 36.0f);
    std::uniform_int_distribution<int>    uLen(4, 20);
    std::uniform_real_distribution<float> uSpeed(4.0f, 13.0f);
    std::uniform_real_distribution<float> uPhase(0.0f, 120.0f);
    std::uniform_real_distribution<float> uSeed(0.0f, 1000.0f);
    std::uniform_real_distribution<float> uBright(0.55f, 1.25f);

    std::vector<float> inst; // 8 floats per letter
    int totalLetters = 0;
    for (int s = 0; s < NUM_STREAMS; ++s) {
        float x = uX(rng), z = uZ(rng);
        int   len   = uLen(rng);
        float speed = uSpeed(rng), phase = uPhase(rng);
        float seed  = uSeed(rng),  bright = uBright(rng);
        for (int i = 0; i < len; ++i) {
            inst.push_back(x);        inst.push_back(z);
            inst.push_back((float)i); inst.push_back((float)len);
            inst.push_back(speed);    inst.push_back(phase);
            inst.push_back(seed);     inst.push_back(bright);
            ++totalLetters;
        }
    }

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    GLuint quadVBO;
    glGenBuffers(1, &quadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    GLuint instVBO;
    glGenBuffers(1, &instVBO);
    glBindBuffer(GL_ARRAY_BUFFER, instVBO);
    glBufferData(GL_ARRAY_BUFFER, inst.size() * sizeof(float), inst.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
    glVertexAttribDivisor(1, 1);
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    // ----- render state -----
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);      // additive
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.008f, 0.004f, 1.0f);

    GLint locProj  = glGetUniformLocation(program, "uProj");
    GLint locView  = glGetUniformLocation(program, "uView");
    GLint locTime  = glGetUniformLocation(program, "uTime");
    GLint locWH    = glGetUniformLocation(program, "uWorldH");
    GLint locGC    = glGetUniformLocation(program, "uGlyphCount");
    GLint locAtlas = glGetUniformLocation(program, "uAtlas");
    GLint locGrid  = glGetUniformLocation(program, "uGrid");
    GLint locCell  = glGetUniformLocation(program, "uCell");
    GLint locPad   = glGetUniformLocation(program, "uPad");
    GLint locGPx   = glGetUniformLocation(program, "uGlyphPx");

    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas);
    glUniform1i(locAtlas, 0);
    glUniform1f(locWH, WORLD_H);
    glUniform1f(locGC, (float)GLYPH_COUNT);
    glUniform2f(locGrid, (float)ATLAS_COLS, (float)ATLAS_ROWS);
    glUniform1f(locCell, (float)CELL);
    glUniform1f(locPad,  (float)PAD);
    glUniform1f(locGPx,  (float)GLYPH_PX);

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        float t = (float)glfwGetTime();

        glViewport(0, 0, fbW, fbH);
        glClear(GL_COLOR_BUFFER_BIT);

        // Slowly drifting camera gives the parallax that sells the 3D.
        Vec3 eye    = { 7.0f * std::sin(t * 0.05f),
                        1.5f + 3.0f * std::sin(t * 0.037f),
                        46.0f };
        Vec3 center = { 0.0f, -2.0f, 0.0f };
        Vec3 up     = { 0.0f, 1.0f, 0.0f };
        Mat4 view = mat4LookAt(eye, center, up);
        Mat4 proj = mat4Perspective(55.0f * 3.14159265f / 180.0f,
                                    (float)fbW / (float)(fbH > 0 ? fbH : 1),
                                    0.1f, 300.0f);

        glUniformMatrix4fv(locProj, 1, GL_FALSE, proj.m);
        glUniformMatrix4fv(locView, 1, GL_FALSE, view.m);
        glUniform1f(locTime, t);

        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, totalLetters);

        glfwSwapBuffers(win);
    }

    glDeleteBuffers(1, &quadVBO);
    glDeleteBuffers(1, &instVBO);
    glDeleteVertexArrays(1, &vao);
    glDeleteTextures(1, &atlas);
    glDeleteProgram(program);
    glfwTerminate();
    return 0;
}