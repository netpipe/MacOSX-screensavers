// matrix_semantic_rain.cpp
//
// Matrix-style digital rain, but with a readable semantic layer:
//
//   Each 5-letter block is one state sample:
//
//     field 0 = agent / person / identity   A-Z          slow
//     field 1 = action                      0-9          medium
//     field 2 = emotion                     * + - . / :  faster
//     field 3 = location / context          < = > ? # &  medium/fast
//     field 4 = observer / simulation meta  symbols      fast
//
//   The bottom/head block is the newest sample. Blocks above it are older.
//
// Build (Linux):
//   g++ -O2 -std=c++11 matrix_semantic_rain.cpp -o matrix_semantic_rain -lGLEW -lglfw -lGL
//
// Build (macOS, Homebrew glew/glfw):
//   g++ -O2 -std=c++11 matrix_semantic_rain.cpp -o matrix_semantic_rain \
//       -I/opt/homebrew/include -L/opt/homebrew/lib -lGLEW -lglfw
//
// Run:
//   ./matrix_semantic_rain        windowed
//   ./matrix_semantic_rain -f     fullscreen
//
// Keys:
//   ESC / Q  quit
//   V        toggle semantic mode / original random Matrix mode
//   C        toggle field colors
//   F        cycle field filter
//   0        show all fields
//   1..5     isolate one field

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal math (no glm)
// ---------------------------------------------------------------------------

struct Vec3 {
    float x, y, z;
};

static Vec3 operator-(const Vec3& a, const Vec3& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

static float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static Vec3 normalize(const Vec3& v) {
    float len = std::sqrt(dot(v, v));
    if (len < 1e-8f) len = 1e-8f;
    return { v.x / len, v.y / len, v.z / len };
}

struct Mat4 {
    float m[16]; // column-major
};

static Mat4 mat4Identity() {
    Mat4 r = {};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

static Mat4 mat4Perspective(float fovYRadians, float aspect, float zNear, float zFar) {
    Mat4 r = {};
    float tf = std::tan(fovYRadians * 0.5f);

    r.m[0]  = 1.0f / (aspect * tf);
    r.m[5]  = 1.0f / tf;
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

    r.m[0] = s.x;
    r.m[4] = s.y;
    r.m[8] = s.z;

    r.m[1] = u.x;
    r.m[5] = u.y;
    r.m[9] = u.z;

    r.m[2]  = -f.x;
    r.m[6]  = -f.y;
    r.m[10] = -f.z;

    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] =  dot(f, eye);

    return r;
}

// ---------------------------------------------------------------------------
// Embedded 5x7 bitmap font.
// Order:
//   0..25  A-Z
//   26..35 0-9
//   36..47 * + - . / : < = > ? # &
// ---------------------------------------------------------------------------

static const unsigned char FONT_5x7[48][7] = {
    { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, // A
    { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, // B
    { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, // C
    { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E }, // D
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, // E
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }, // F
    { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E }, // G
    { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, // H
    { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, // I
    { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }, // J
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, // K
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, // L
    { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, // M
    { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 }, // N
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, // O
    { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }, // P
    { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, // Q
    { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, // R
    { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, // S
    { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, // T
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, // U
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }, // V
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A }, // W
    { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }, // X
    { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, // Y
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }, // Z

    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, // 0
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, // 1
    { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F }, // 2
    { 0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E }, // 3
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, // 4
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }, // 5
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, // 6
    { 0x1F, 0x01, 0x02, 0x04, 0x04, 0x04, 0x04 }, // 7
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, // 8
    { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }, // 9

    { 0x00, 0x0A, 0x04, 0x1F, 0x04, 0x0A, 0x00 }, // *
    { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 }, // +
    { 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00 }, // -
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C }, // .
    { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 }, // /
    { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 }, // :
    { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 }, // <
    { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 }, // =
    { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 }, // >
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 }, // ?
    { 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A }, // #
    { 0x04, 0x0A, 0x0A, 0x04, 0x15, 0x12, 0x0D }, // &
};

static const int GLYPH_COUNT = 48;
static const int ATLAS_COLS  = 8;
static const int ATLAS_ROWS  = 6;

static const int CELL     = 22; // texels per atlas cell
static const int PAD      = 3;  // padding around glyph
static const int GLYPH_PX = 16; // glyph area inside cell, 8x8 font at 2x scale

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

static const char* VERT_SRC = R"GLSL(
#version 410 core

layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aInst0; // x, z, letterIndex, streamLength
layout(location = 2) in vec4 aInst1; // speed, phase, seed, brightness

uniform mat4  uProj;
uniform mat4  uView;
uniform float uTime;
uniform float uWorldH;
uniform float uGlyphCount;
uniform float uSemantic;

out vec2  vUV;
out float vFade;
out float vHead;
out float vBright;
out float vDepth;

flat out float vGlyph;
flat out float vField;

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

    float raw    = uTime * speed + phase;
    float travel = uWorldH + len + 12.0;
    float prog   = mod(raw, travel);

    float y = uWorldH * 0.5 - prog + idx;

    float t = clamp(1.0 - idx / max(len - 1.0, 1.0), 0.0, 1.0);
    vFade = pow(t, 1.6);
    vHead = step(idx, 0.5);

    float glyphIndex;

    if (uSemantic < 0.5) {
        // --------------------------------------------------------------------
        // Original random Matrix rain.
        // --------------------------------------------------------------------
        vField = -1.0;

        float rate = mix(4.0, 18.0, hash11(seed + idx * 7.31));
        float slot = floor(uTime * rate + hash11(seed * 1.7 + idx * 3.7) * 23.0);

        glyphIndex = floor(hash21(vec2(seed + idx * 13.7, slot)) * uGlyphCount);
    } else {
        // --------------------------------------------------------------------
        // Semantic mode.
        //
        // Each stream is a scrolling log. Every 5 letters is one state block:
        //
        //   field 0 = agent / person / identity
        //   field 1 = action
        //   field 2 = emotion
        //   field 3 = location / context
        //   field 4 = observer / simulation metadata
        //
        // The bottom/head block is current. Blocks above are older.
        // --------------------------------------------------------------------

        int field = int(mod(idx, 5.0));
        vField = float(field);

        float block = floor(idx / 5.0);
        float secondsPerBlock = 5.0 / max(speed, 0.001);
        float past = max(0.0, uTime - block * secondsPerBlock);

        float offset = 0.0;
        float count  = 1.0;
        float epoch  = 0.0;

        if (field == 0) {
            // Agent / person / identity.
            // Very stable: changes when the stream lifecycle wraps.
            offset = 0.0;   // A-Z
            count  = 26.0;
            epoch  = floor((past * speed + phase) / travel + 1000.0);
        } else if (field == 1) {
            // Action.
            offset = 26.0;  // 0-9
            count  = 10.0;
            epoch  = floor(past * mix(0.6, 1.8, hash11(seed + 17.0))
                           + hash11(seed + 71.0) * 19.0
                           + 1000.0);
        } else if (field == 2) {
            // Emotion.
            offset = 36.0;  // * + - . / :
            count  = 6.0;
            epoch  = floor(past * mix(2.0, 6.0, hash11(seed + 29.0))
                           + hash11(seed + 83.0) * 19.0
                           + 1000.0);
        } else if (field == 3) {
            // Location / context.
            offset = 42.0;  // < = > ? # &
            count  = 6.0;
            epoch  = floor(past * mix(0.8, 3.0, hash11(seed + 37.0))
                           + hash11(seed + 97.0) * 19.0
                           + 1000.0);
        } else {
            // Observer / simulation metadata.
            offset = 36.0;  // symbols
            count  = 12.0;
            epoch  = floor(past * mix(7.0, 16.0, hash11(seed + 43.0))
                           + hash11(seed + 113.0) * 19.0
                           + 1000.0);
        }

        float h = hash21(vec2(seed + float(field) * 131.0, epoch));
        glyphIndex = offset + min(count - 1.0, floor(h * count));
    }

    vGlyph = glyphIndex;

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
flat in float vField;

uniform sampler2D uAtlas;
uniform vec2  uGrid;
uniform float uCell;
uniform float uPad;
uniform float uGlyphPx;

uniform int uFieldColors;
uniform int uFieldFilter;

out vec4 outColor;

void main() {
    float col = mod(vGlyph, uGrid.x);
    float row = floor(vGlyph / uGrid.x);

    vec2 local = vec2(vUV.x, 1.0 - vUV.y);
    vec2 px = vec2(col, row) * uCell + uPad + local * uGlyphPx;
    vec2 uv = px / (uGrid * uCell);

    float g = texture(uAtlas, uv).r;

    int f = int(vField);

    float filterMul = 1.0;
    if (f >= 0 && uFieldFilter > 0 && f != uFieldFilter - 1)
        filterMul = 0.06;

    float depthDim = mix(0.15, 1.0, clamp((115.0 - vDepth) / 80.0, 0.0, 1.0));

    vec3 c;

    if (uFieldColors == 1 && f >= 0) {
        // Color-coded semantic fields.
        if (f == 0) {
            // Agent / person.
            c = vec3(0.80, 1.00, 0.82);
        } else if (f == 1) {
            // Action.
            c = vec3(0.12, 0.90, 0.25);
        } else if (f == 2) {
            // Emotion.
            c = vec3(0.85, 0.90, 0.20);
        } else if (f == 3) {
            // Location / context.
            c = vec3(0.20, 0.85, 0.75);
        } else {
            // Observer / simulation metadata.
            c = vec3(0.35, 0.55, 0.95);
        }

        c *= mix(0.35, 1.0, vFade);
        c = mix(c, vec3(1.0), vHead * 0.35);
    } else {
        // Classic Matrix green.
        vec3 dim  = vec3(0.00, 0.30, 0.07);
        vec3 mid  = vec3(0.05, 0.85, 0.20);
        vec3 head = vec3(0.80, 1.00, 0.82);

        c = mix(dim, mid, vFade);
        c = mix(c, head, vHead);
    }

    float a = g * (0.12 + 0.88 * vFade) * vBright * depthDim * filterMul;
    a += vHead * g * 0.5 * vBright * depthDim * filterMul;

    outColor = vec4(c * a, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
// Shader helpers
// ---------------------------------------------------------------------------

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
// Atlas generation
// ---------------------------------------------------------------------------

static void blurPass(const std::vector<unsigned char>& in,
                     std::vector<unsigned char>& out,
                     int w, int h) {
    out.assign(w * h, 0);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sum = 0;

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int xx = x + dx;
                    int yy = y + dy;

                    if (xx < 0) xx = 0;
                    if (xx >= w) xx = w - 1;
                    if (yy < 0) yy = 0;
                    if (yy >= h) yy = h - 1;

                    sum += in[yy * w + xx];
                }
            }

            out[y * w + x] = (unsigned char)(sum / 9);
        }
    }
}

static GLuint buildAtlas() {
    const int W = ATLAS_COLS * CELL;
    const int H = ATLAS_ROWS * CELL;

    std::vector<unsigned char> crisp(W * H, 0);
    std::vector<unsigned char> tmp;
    std::vector<unsigned char> blurred;

    for (int g = 0; g < GLYPH_COUNT; ++g) {
        int cx = g % ATLAS_COLS;
        int cy = g / ATLAS_COLS;

        int ox = cx * CELL + PAD + (GLYPH_PX - 10) / 2;
        int oy = cy * CELL + PAD + (GLYPH_PX - 14) / 2;

        for (int r = 0; r < 7; ++r) {
            unsigned char bits = FONT_5x7[g][r];

            for (int c = 0; c < 5; ++c) {
                if (bits & (1 << (4 - c))) {
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            int px = ox + c * 2 + dx;
                            int py = oy + r * 2 + dy;
                            crisp[py * W + px] = 255;
                        }
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

    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, W, H, 0,
                 GL_RED, GL_UNSIGNED_BYTE, fin.data());

    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return tex;
}

// ---------------------------------------------------------------------------
// App state / callbacks
// ---------------------------------------------------------------------------

static int fbW = 1280;
static int fbH = 720;

static int gSemantic    = 1;
static int gFieldColors = 1;
static int gFieldFilter = 0; // 0=all, 1..5 isolate fields

static void framebufferSizeCB(GLFWwindow*, int w, int h) {
    fbW = w;
    fbH = h;
}

static void keyCB(GLFWwindow* w, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;

    if (action != GLFW_PRESS)
        return;

    if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_Q) {
        glfwSetWindowShouldClose(w, GLFW_TRUE);
    } else if (key == GLFW_KEY_V) {
        gSemantic ^= 1;
    } else if (key == GLFW_KEY_C) {
        gFieldColors ^= 1;
    } else if (key == GLFW_KEY_F) {
        gFieldFilter = (gFieldFilter + 1) % 6;
    } else if (key == GLFW_KEY_0) {
        gFieldFilter = 0;
    } else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_5) {
        gFieldFilter = key - GLFW_KEY_1 + 1;
    }
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    if (!glfwInit()) {
        fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    bool fullscreen = (argc > 1 && strcmp(argv[1], "-f") == 0);

    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    const GLFWvidmode* vm = primary ? glfwGetVideoMode(primary) : nullptr;

    GLFWmonitor* monitor = nullptr;
    int winW = 1280;
    int winH = 720;

    if (fullscreen && vm) {
        monitor = primary;
        winW = vm->width;
        winH = vm->height;
    } else {
        fullscreen = false;
    }

    GLFWwindow* win = glfwCreateWindow(winW, winH, "Semantic Matrix Rain", monitor, nullptr);
    if (!win) {
        fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    glfwSetKeyCallback(win, keyCB);
    glfwSetFramebufferSizeCallback(win, framebufferSizeCB);
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

    glfwGetFramebufferSize(win, &fbW, &fbH);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "glewInit failed\n");
        glfwTerminate();
        return 1;
    }

    // Some drivers emit a harmless error after glewInit with core profile.
    glGetError();

    printf(
        "Semantic Matrix rain:\n"
        "  field 1 = agent/person   A-Z          slow\n"
        "  field 2 = action         0-9          medium\n"
        "  field 3 = emotion        * + - . / :  fast\n"
        "  field 4 = location       < = > ? # &  medium/fast\n"
        "  field 5 = observer/meta  symbols      fast\n"
        "\n"
        "Keys:\n"
        "  ESC/Q quit\n"
        "  V toggle semantic/random\n"
        "  C toggle field colors\n"
        "  F cycle field filter\n"
        "  0 all fields\n"
        "  1..5 isolate field\n"
    );

    GLuint program = linkProgram(VERT_SRC, FRAG_SRC);
    GLuint atlas   = buildAtlas();

    // Fullscreen-ish quad, drawn instanced.
    static const float QUAD[] = {
        -0.5f, -0.5f,
         0.5f, -0.5f,
         0.5f,  0.5f,

        -0.5f, -0.5f,
         0.5f,  0.5f,
        -0.5f,  0.5f
    };

    // -----------------------------------------------------------------------
    // Generate streams.
    //
    // Each stream acts like one observer/simulation timeline.
    // Stream seed is its identity.
    //
    // Set USE_OBSERVER_LANES to true if you want more column-like lanes.
    // -----------------------------------------------------------------------

    const int   NUM_STREAMS = 460;
    const float WORLD_H     = 64.0f;

    const bool USE_OBSERVER_LANES = false;
    const int  LANES = 40;

    std::mt19937 rng(987654321u);

    std::uniform_real_distribution<float> uZ(-42.0f, 10.0f);
    std::uniform_real_distribution<float> uX(-36.0f, 36.0f);
    std::uniform_int_distribution<int>    uLenBlocks(1, 4);       // 5, 10, 15, 20
    std::uniform_real_distribution<float> uSpeed(2.5f, 7.0f);
    std::uniform_real_distribution<float> uPhase(0.0f, 120.0f);
    std::uniform_real_distribution<float> uBright(0.55f, 1.25f);
    std::uniform_real_distribution<float> uJitter(-0.35f, 0.35f);

    std::vector<float> inst;
    inst.reserve(NUM_STREAMS * 20 * 8);

    int totalLetters = 0;

    for (int s = 0; s < NUM_STREAMS; ++s) {
        float x;

        if (USE_OBSERVER_LANES) {
            int lane = s % LANES;
            x = -36.0f + 72.0f * (float)lane / (float)(LANES - 1) + uJitter(rng);
        } else {
            x = uX(rng);
        }

        float z = uZ(rng);

        int   len   = 5 * uLenBlocks(rng);
        float speed = uSpeed(rng);
        float phase = uPhase(rng);

        // Seed is effectively the observer / simulation ID.
        float seed = 1000.0f + (float)s * 137.0f;

        float bright = uBright(rng);

        for (int i = 0; i < len; ++i) {
            inst.push_back(x);
            inst.push_back(z);
            inst.push_back((float)i);
            inst.push_back((float)len);

            inst.push_back(speed);
            inst.push_back(phase);
            inst.push_back(seed);
            inst.push_back(bright);

            ++totalLetters;
        }
    }

    // -----------------------------------------------------------------------
    // GL objects
    // -----------------------------------------------------------------------

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
    glBufferData(GL_ARRAY_BUFFER,
                 inst.size() * sizeof(float),
                 inst.data(),
                 GL_STATIC_DRAW);

    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                          8 * sizeof(float), (void*)0);

    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE,
                          8 * sizeof(float), (void*)(4 * sizeof(float)));

    glVertexAttribDivisor(1, 1);
    glVertexAttribDivisor(2, 1);

    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    // -----------------------------------------------------------------------
    // Render state
    // -----------------------------------------------------------------------

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDisable(GL_DEPTH_TEST);

    glClearColor(0.0f, 0.008f, 0.004f, 1.0f);

    glUseProgram(program);

    GLint locProj        = glGetUniformLocation(program, "uProj");
    GLint locView        = glGetUniformLocation(program, "uView");
    GLint locTime        = glGetUniformLocation(program, "uTime");
    GLint locWorldH      = glGetUniformLocation(program, "uWorldH");
    GLint locGlyphCount  = glGetUniformLocation(program, "uGlyphCount");
    GLint locSemantic    = glGetUniformLocation(program, "uSemantic");
    GLint locAtlas       = glGetUniformLocation(program, "uAtlas");
    GLint locGrid        = glGetUniformLocation(program, "uGrid");
    GLint locCell        = glGetUniformLocation(program, "uCell");
    GLint locPad         = glGetUniformLocation(program, "uPad");
    GLint locGlyphPx     = glGetUniformLocation(program, "uGlyphPx");
    GLint locFieldColors = glGetUniformLocation(program, "uFieldColors");
    GLint locFieldFilter = glGetUniformLocation(program, "uFieldFilter");

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas);

    glUniform1i(locAtlas, 0);
    glUniform1f(locWorldH, WORLD_H);
    glUniform1f(locGlyphCount, (float)GLYPH_COUNT);
    glUniform2f(locGrid, (float)ATLAS_COLS, (float)ATLAS_ROWS);
    glUniform1f(locCell, (float)CELL);
    glUniform1f(locPad, (float)PAD);
    glUniform1f(locGlyphPx, (float)GLYPH_PX);

    const float FOV = 55.0f * 3.14159265358979323846f / 180.0f;

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        float t = (float)glfwGetTime();

        glViewport(0, 0, fbW, fbH);
        glClear(GL_COLOR_BUFFER_BIT);

        float aspect = (float)fbW / (float)(fbH > 0 ? fbH : 1);

        // Slow camera drift gives parallax and reinforces the 3D depth.
        Vec3 eye = {
            7.0f * std::sin(t * 0.05f),
            1.5f + 3.0f * std::sin(t * 0.037f),
            46.0f
        };

        Vec3 center = { 0.0f, -2.0f, 0.0f };
        Vec3 up     = { 0.0f,  1.0f, 0.0f };

        Mat4 view = mat4LookAt(eye, center, up);
        Mat4 proj = mat4Perspective(FOV, aspect, 0.1f, 300.0f);

        glUniformMatrix4fv(locProj, 1, GL_FALSE, proj.m);
        glUniformMatrix4fv(locView, 1, GL_FALSE, view.m);

        glUniform1f(locTime, t);
        glUniform1f(locSemantic, gSemantic ? 1.0f : 0.0f);
        glUniform1i(locFieldColors, gFieldColors);
        glUniform1i(locFieldFilter, gFieldFilter);

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