// NeonClockView.m — OpenGL clock screensaver for macOS
#import <AppKit/AppKit.h>
#import <OpenGL/gl3.h>

// ---------------------------------------------------------------------------
// Minimal declaration of the ScreenSaver API we need. Some recent macOS SDKs
// don't provide <ScreenSaver/ScreenSaver.h> correctly; the actual classes
// are supplied at runtime by ScreenSaver.framework (linked via the Makefile).
// ---------------------------------------------------------------------------
@interface ScreenSaverView : NSView
- (instancetype)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview;
- (void)startAnimation;
- (void)stopAnimation;
- (void)animateOneFrame;
- (BOOL)hasConfigureSheet;
- (NSWindow *)configureSheet;
- (NSOpenGLContext *)openGLContext;
- (void)setOpenGLContext:(NSOpenGLContext *)context;
- (NSOpenGLPixelFormat *)openGLPixelFormat;
- (void)setOpenGLPixelFormat:(NSOpenGLPixelFormat *)pixelFormat;
@end
#import <Foundation/Foundation.h>

#pragma mark - Shaders

static const GLchar *kVertexSource =
"#version 150 core\n"
"in vec2 aPos;\n"
"void main() {\n"
"    gl_Position = vec4(aPos, 0.0, 1.0);\n"
"}\n";

static const GLchar *kFragmentSource =
"#version 150 core\n"
"uniform vec2  u_resolution;\n"
"uniform float u_seconds;      // seconds since local midnight\n"
"out vec4 outColor;\n"
"\n"
"const float PI = 3.141592653589793;\n"
"\n"
"float cover(float d) {\n"                                  // anti-aliased fill of an SDF
"    float aa = fwidth(d) * 1.4 + 1e-5;\n"
"    return 1.0 - smoothstep(0.0, aa, d);\n"
"}\n"
"\n"
"float sdSegment(vec2 p, vec2 a, vec2 b) {\n"
"    vec2 pa = p - a;\n"
"    vec2 ba = b - a;\n"
"    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);\n"
"    return length(pa - ba * h);\n"
"}\n"
"\n"
"int segMask(int d) {\n"                                    // classic 7-segment masks
"    if (d == 0) return 63;\n"
"    if (d == 1) return 6;\n"
"    if (d == 2) return 91;\n"
"    if (d == 3) return 79;\n"
"    if (d == 4) return 102;\n"
"    if (d == 5) return 109;\n"
"    if (d == 6) return 125;\n"
"    if (d == 7) return 7;\n"
"    if (d == 8) return 127;\n"
"    return 111;\n"
"}\n"
"\n"
"float digitSDF(vec2 p, int digit) {\n"
"    int m = segMask(digit);\n"
"    float d = 1e5;\n"
"    if (((m >> 0) & 1) != 0) d = min(d, sdSegment(p, vec2(-0.25,  0.80), vec2( 0.25,  0.80)));\n"
"    if (((m >> 1) & 1) != 0) d = min(d, sdSegment(p, vec2( 0.30,  0.70), vec2( 0.30,  0.10)));\n"
"    if (((m >> 2) & 1) != 0) d = min(d, sdSegment(p, vec2( 0.30, -0.10), vec2( 0.30, -0.70)));\n"
"    if (((m >> 3) & 1) != 0) d = min(d, sdSegment(p, vec2(-0.25, -0.80), vec2( 0.25, -0.80)));\n"
"    if (((m >> 4) & 1) != 0) d = min(d, sdSegment(p, vec2(-0.30, -0.10), vec2(-0.30, -0.70)));\n"
"    if (((m >> 5) & 1) != 0) d = min(d, sdSegment(p, vec2(-0.30,  0.70), vec2(-0.30,  0.10)));\n"
"    if (((m >> 6) & 1) != 0) d = min(d, sdSegment(p, vec2(-0.25,  0.00), vec2( 0.25,  0.00)));\n"
"    return d;\n"
"}\n"
"\n"
"float drawDigit(vec2 p, int digit, float r) {\n"
"    return cover(digitSDF(p, digit) - r);\n"
"}\n"
"\n"
"float drawDigitalTime(vec2 p, float scale) {\n"
"    vec2 q = p / scale;\n"
"    int h24 = int(u_seconds / 3600.0);\n"
"    int mi  = int(mod(u_seconds, 3600.0) / 60.0);\n"
"    float r = 0.115;\n"
"    float v = 0.0;\n"
"    v = max(v, drawDigit(q - vec2(-1.95, 0.0), h24 / 10, r));\n"
"    v = max(v, drawDigit(q - vec2(-0.90, 0.0), h24 % 10, r));\n"
"    v = max(v, drawDigit(q - vec2( 0.90, 0.0), mi  / 10, r));\n"
"    v = max(v, drawDigit(q - vec2( 1.95, 0.0), mi  % 10, r));\n"
"    v = max(v, cover(length(q - vec2(0.0,  0.40)) - r));\n"  // colon dots
"    v = max(v, cover(length(q - vec2(0.0, -0.40)) - r));\n"
"    return v;\n"
"}\n"
"\n"
"float ticks(vec2 p, float count, float r0, float r1, float w) {\n"
"    float an  = atan(p.x, p.y);\n"                          // 0 at 12 o'clock
"    float seg = (2.0 * PI) / count;\n"
"    float a   = mod(an, seg) - 0.5 * seg;\n"                // fold onto nearest tick
"    vec2  q   = vec2(sin(a), cos(a)) * length(p);\n"
"    return sdSegment(q, vec2(0.0, r0), vec2(0.0, r1)) - w;\n"
"}\n"
"\n"
"float handSDF(vec2 p, float angle, float tail, float len, float w) {\n"
"    vec2 dir = vec2(sin(angle), cos(angle));\n"
"    float t = clamp(dot(p, dir), -tail, len);\n"
"    return length(p - dir * t) - w;\n"
"}\n"
"\n"
"void main() {\n"
"    vec2 p = gl_FragCoord.xy - 0.5 * u_resolution;\n"
"    float S = min(u_resolution.x, u_resolution.y);\n"
"\n"
"    // background with vignette\n"
"    float vig = length(p / S);\n"
"    vec3 col = mix(vec3(0.050, 0.055, 0.095), vec3(0.008, 0.010, 0.022),\n"
"                   smoothstep(0.1, 0.95, vig));\n"
"\n"
"    // analog face\n"
"    float R = 0.36 * S;\n"
"    vec2 cp = p - vec2(0.0, 0.10 * S);\n"
"    col += vec3(0.020, 0.026, 0.050) * cover(length(cp) - R);\n"
"    col += vec3(0.72, 0.78, 0.90) * cover(abs(length(cp) - R) - max(1.5, 0.004 * S)) * 0.85;\n"
"    col += vec3(0.30, 0.34, 0.42) * cover(ticks(cp, 60.0, R * 0.92, R * 0.965, max(0.7, 0.0018 * S)));\n"
"    col += vec3(0.80, 0.84, 0.92) * cover(ticks(cp, 12.0, R * 0.86, R * 0.965, max(1.4, 0.0050 * S)));\n"
"\n"
"    // smooth-sweeping hands\n"
"    float aS = mod(u_seconds, 60.0)    * (2.0 * PI / 60.0);\n"
"    float aM = mod(u_seconds, 3600.0)  * (2.0 * PI / 3600.0);\n"
"    float aH = mod(u_seconds, 43200.0) * (2.0 * PI / 43200.0);\n"
"\n"
"    float dH = handSDF(cp, aH, R * 0.07, R * 0.50, max(2.0, 0.0120 * S));\n"
"    col = mix(col, vec3(0.90, 0.92, 0.97), cover(dH));\n"
"    float dM = handSDF(cp, aM, R * 0.07, R * 0.74, max(1.5, 0.0085 * S));\n"
"    col = mix(col, vec3(0.95, 0.96, 1.00), cover(dM));\n"
"    float dS = handSDF(cp, aS, R * 0.18, R * 0.92, max(1.0, 0.0035 * S));\n"
"    col += vec3(1.0, 0.22, 0.15) * 0.30 * exp(-max(dS, 0.0) / (0.012 * S));\n"
"    col = mix(col, vec3(1.00, 0.30, 0.20), cover(dS));\n"
"    col = mix(col, vec3(1.0, 0.35, 0.25), cover(length(cp) - max(2.5, 0.013 * S)));\n"
"\n"
"    // digital HH:MM below\n"
"    float dv = drawDigitalTime(p - vec2(0.0, -0.43 * S), 0.055 * S);\n"
"    vec3 dcol = vec3(0.30, 0.85, 1.00);\n"
"    col += dcol * dv * 0.20;\n"
"    col = mix(col, dcol, dv * 0.95);\n"
"\n"
"    outColor = vec4(pow(col, vec3(0.92)), 1.0);\n"
"}\n";

#pragma mark - View

static GLuint CompileShader(GLenum type, const GLchar *source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        NSLog(@"NeonClock shader compile error: %s", log);
    }
    return s;
}

@interface NeonClockView : ScreenSaverView {
@private
    NSOpenGLPixelFormat *_pixelFormat;
    NSOpenGLContext     *_glContext;
    GLuint _program, _vao, _vbo;
    GLint  _locResolution, _locSeconds;
    NSSize _lastSize;
}
@end

@implementation NeonClockView

- (instancetype)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview {
self = [super initWithFrame:frame isPreview:isPreview];
    if (self) {
        NSOpenGLPixelFormatAttribute attrs[] = {
            NSOpenGLPFADoubleBuffer,
            NSOpenGLPFAColorSize, 24,
            NSOpenGLPFAAlphaSize, 8,
            NSOpenGLPFAOpenGLProfile, (NSOpenGLPixelFormatAttribute)NSOpenGLProfileVersion3_2Core,
            0
        };
        _pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
        if (_pixelFormat) {
            _glContext = [[NSOpenGLContext alloc] initWithFormat:_pixelFormat shareContext:nil];
        }
        [self setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    }
    return self;
}

- (void)startAnimation {
    [super startAnimation];
    if (!_glContext || _program) return;

    [_glContext setView:self];
    [_glContext update];
    [_glContext makeCurrentContext];

    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexSource);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentSource);
    _program = glCreateProgram();
    glAttachShader(_program, vs);
    glAttachShader(_program, fs);
    glBindAttribLocation(_program, 0, "aPos");   // <-- add this line
    glLinkProgram(_program);
    GLint ok = 0;
    glGetProgramiv(_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        glGetProgramInfoLog(_program, sizeof(log), NULL, log);
        NSLog(@"NeonClock program link error: %s", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &_vao);
    glBindVertexArray(_vao);
    glGenBuffers(1, &_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    static const GLfloat tri[] = { -1.f, -1.f,  3.f, -1.f,  -1.f, 3.f };  // one fullscreen triangle
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    _locResolution = glGetUniformLocation(_program, "u_resolution");
    _locSeconds    = glGetUniformLocation(_program, "u_seconds");
    glDisable(GL_DEPTH_TEST);
}

- (void)animateOneFrame {
    if (!_glContext || !_program) return;
    [_glContext makeCurrentContext];

    NSSize px = [self convertSizeToBacking:self.bounds.size];   // pixels (Retina-aware)
    if (px.width < 2 || px.height < 2) return;
if (px.width != _lastSize.width || px.height != _lastSize.height) {
    _lastSize = px;
    [_glContext update];
}

    NSDate *now = [NSDate date];
    NSTimeInterval sinceMidnight =
        [now timeIntervalSinceDate:[[NSCalendar currentCalendar] startOfDayForDate:now]];

    glViewport(0, 0, (GLsizei)px.width, (GLsizei)px.height);
    glClearColor(0.0f, 0.0f, 0.02f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(_program);
    glUniform2f(_locResolution, (GLfloat)px.width, (GLfloat)px.height);
    glUniform1f(_locSeconds, (GLfloat)sinceMidnight);

    glBindVertexArray(_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    [_glContext flushBuffer];
}

- (void)stopAnimation {
    if (_glContext) {
        [_glContext makeCurrentContext];
        if (_program) { glDeleteProgram(_program); _program = 0; }
        if (_vbo) { glDeleteBuffers(1, &_vbo); _vbo = 0; }
        if (_vao) { glDeleteVertexArrays(1, &_vao); _vao = 0; }
        [NSOpenGLContext clearCurrentContext];
    }
    [super stopAnimation];
}

- (BOOL)hasConfigureSheet { return NO; }
- (NSWindow *)configureSheet { return nil; }

@end