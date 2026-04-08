// =============================================================================
//  Scrolling Scenery Demo  –  OpenGL 3.3 Core
//  Controls:
//    LMB         → capture mouse / enter look mode
//    WASD        → move  (W=forward  S=back  A=left  D=right)
//    Q / E       → fly up / down
//    Mouse       → look (yaw / pitch)
//    Scroll      → zoom FOV
//    N           → toggle Night <-> Day
//    R           → toggle Rain <-> Clear
//    ESC (1st)   → release mouse
//    ESC (2nd)   → quit
// =============================================================================
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <Windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")


#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


// ── Constants ─────────────────────────────────────────────────────────────────
const int   SCR_W = 1280, SCR_H = 720;
const float PI = 3.14159265f;

// ── Camera ────────────────────────────────────────────────────────────────────
struct Camera
{
    glm::vec3 pos{ 0.f, 1.7f, 8.f };
    glm::vec3 front{ 0.f,-0.04f,-1.f };
    glm::vec3 worldUp{ 0,1,0 };
    float yaw = -90.f, pitch = -2.f;
    float fov = 65.f;
    float lastX = SCR_W / 2.f, lastY = SCR_H / 2.f;
    bool  firstMouse = true;
    float speed = 5.5f, sensitivity = 0.10f;
    void updateDir()
    {
        float r = glm::radians(yaw), p = glm::radians(pitch);
        front = glm::normalize(glm::vec3(cosf(r) * cosf(p), sinf(p), sinf(r) * cosf(p)));
    }
    glm::vec3 right() { return glm::normalize(glm::cross(front, worldUp)); }
    glm::mat4 view() { return glm::lookAt(pos, pos + front, worldUp); }
} cam;

// ── Collision ─────────────────────────────────────────────────────────────────
struct Cylinder {
    glm::vec3 pos;
    float radius;
};
static std::vector<Cylinder> g_obstacles;
static void addObs(glm::vec3 p, float r) { g_obstacles.push_back({ p, r }); }
static bool checkObs(glm::vec3 p) {
    const float CAM_R = 0.4f; // Camera "body" radius
    for (auto& ob : g_obstacles) {
        float dx = p.x - ob.pos.x, dz = p.z - ob.pos.z;
        if (sqrtf(dx * dx + dz * dz) < (ob.radius + CAM_R)) return true;
    }
    return false;
}

// ── Globals ───────────────────────────────────────────────────────────────────

static GLuint g_prog;
static GLuint texGrass;
static GLuint texMushroom;
static bool   g_captured = false;
static bool   g_day = false;
static bool   g_rain = false;
static float  g_fogDensity = 0.042f; // Restored to previous level
static float  g_thunderTimer = 0.0f; // Thunder flash duration


// Interaction & Animation State
static bool  g_barnDoorOpen = false;
static float g_barnDoorFactor = 0.f; // 0=closed, 1=open
static int   g_heldObjIdx = -1;      // -1 means not holding anything

// ── Scene offsets – controls the S-curve layout ───────────────────────────────
static const glm::vec3 OFF_FOREST = { 0.f, 0.f, -15.f };  // সামনে মাঝখানে
static const glm::vec3 OFF_CAMP = { -38.f, 0.f, +10.f };  // বাম দিকে - রাস্তা থেকে দূরে
static const glm::vec3 OFF_VILLAGE = { +35.f, 0.f, +30.f };  // ডান দিকে
static const glm::vec3 OFF_BARN = { -45.f, 0.f, +25.f };  // দূর বাম
static const glm::vec3 OFF_TOWER = { +45.f, 0.f, +45.f };  // দূর ডান
static const glm::vec3 OFF_MED = { 0.f, 0.f, +40.f };  // সোজা উত্তরে

// Fire position derived from campsite offset
static const glm::vec3 FIRE_BASE{ -2.8f, 0.f, -37.f };
#define FIRE_P (FIRE_BASE + OFF_CAMP)

// Lamp world positions (medieval town stays at centre, off≈0)
static const glm::vec3 LAMP_WORLD[] =
{
    // Medieval town (আগের ৫টা)
    {-4.f + OFF_MED.x, 5.15f, -198.f + OFF_MED.z},
    { 4.f + OFF_MED.x, 5.15f, -198.f + OFF_MED.z},
    {-4.f + OFF_MED.x, 5.15f, -186.f + OFF_MED.z},
    { 4.f + OFF_MED.x, 5.15f, -186.f + OFF_MED.z},
    { 0.f + OFF_MED.x, 5.15f, -174.f + OFF_MED.z},

    // Castle gate lamps (নতুন ২টা)
    {-2.5f + OFF_VILLAGE.x, 5.15f, -99.f + OFF_VILLAGE.z},
    { 6.5f + OFF_VILLAGE.x, 5.15f, -99.f + OFF_VILLAGE.z},
};
static const int NUM_LAMPS = 7;  // ছিল 5

// ── Mesh type ─────────────────────────────────────────────────────────────────
using Mesh = std::vector<float>;

static void pv(Mesh& m, float x, float y, float z,
    float nx, float ny, float nz,
    float s = 0.f, float t = 0.f)
{
    m.push_back(x); m.push_back(y); m.push_back(z);
    m.push_back(nx); m.push_back(ny); m.push_back(nz);
    m.push_back(s); m.push_back(t);
}

static void quad(Mesh& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n)
{
    pv(m, a.x, a.y, a.z, n.x, n.y, n.z); pv(m, b.x, b.y, b.z, n.x, n.y, n.z);
    pv(m, c.x, c.y, c.z, n.x, n.y, n.z); pv(m, a.x, a.y, a.z, n.x, n.y, n.z);
    pv(m, c.x, c.y, c.z, n.x, n.y, n.z); pv(m, d.x, d.y, d.z, n.x, n.y, n.z);
}

// ── Primitive builders ────────────────────────────────────────────────────────
static Mesh mkBox()
{
    Mesh m;
    // প্রতিটি face এ UV coordinate যোগ করা হলো
    // সামনের face
    pv(m, -1, -1, 1, 0, 0, 1, 0, 0); pv(m, 1, -1, 1, 0, 0, 1, 1, 0);
    pv(m, 1, 1, 1, 0, 0, 1, 1, 1); pv(m, -1, -1, 1, 0, 0, 1, 0, 0);
    pv(m, 1, 1, 1, 0, 0, 1, 1, 1); pv(m, -1, 1, 1, 0, 0, 1, 0, 1);
    // পেছনের face
    pv(m, 1, -1, -1, 0, 0, -1, 0, 0); pv(m, -1, -1, -1, 0, 0, -1, 1, 0);
    pv(m, -1, 1, -1, 0, 0, -1, 1, 1); pv(m, 1, -1, -1, 0, 0, -1, 0, 0);
    pv(m, -1, 1, -1, 0, 0, -1, 1, 1); pv(m, 1, 1, -1, 0, 0, -1, 0, 1);
    // বাকি face গুলো একইভাবে...
    // (বাম, ডান, উপর, নিচ — প্রতিটিতে UV 0-1 রেঞ্জে)
    pv(m, -1, -1, -1, -1, 0, 0, 0, 0); pv(m, -1, -1, 1, -1, 0, 0, 1, 0);
    pv(m, -1, 1, 1, -1, 0, 0, 1, 1); pv(m, -1, -1, -1, -1, 0, 0, 0, 0);
    pv(m, -1, 1, 1, -1, 0, 0, 1, 1); pv(m, -1, 1, -1, -1, 0, 0, 0, 1);
    pv(m, 1, -1, 1, 1, 0, 0, 0, 0); pv(m, 1, -1, -1, 1, 0, 0, 1, 0);
    pv(m, 1, 1, -1, 1, 0, 0, 1, 1); pv(m, 1, -1, 1, 1, 0, 0, 0, 0);
    pv(m, 1, 1, -1, 1, 0, 0, 1, 1); pv(m, 1, 1, 1, 1, 0, 0, 0, 1);
    pv(m, -1, 1, 1, 0, 1, 0, 0, 0); pv(m, 1, 1, 1, 0, 1, 0, 1, 0);
    pv(m, 1, 1, -1, 0, 1, 0, 1, 1); pv(m, -1, 1, 1, 0, 1, 0, 0, 0);
    pv(m, 1, 1, -1, 0, 1, 0, 1, 1); pv(m, -1, 1, -1, 0, 1, 0, 0, 1);
    pv(m, -1, -1, -1, 0, -1, 0, 0, 0); pv(m, 1, -1, -1, 0, -1, 0, 1, 0);
    pv(m, 1, -1, 1, 0, -1, 0, 1, 1); pv(m, -1, -1, -1, 0, -1, 0, 0, 0);
    pv(m, 1, -1, 1, 0, -1, 0, 1, 1); pv(m, -1, -1, 1, 0, -1, 0, 0, 1);
    return m;
}
static Mesh mkCyl(int segs = 60)
{
    Mesh m; float st = 2 * PI / segs;
    for (int i = 0; i < segs; i++) {
        float a0 = i * st, a1 = (i + 1) * st;
        float u0 = (float)i / segs, u1 = (float)(i + 1) / segs;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        pv(m, c0, -1, s0, c0, 0, s0, u0, 0.f); pv(m, c1, -1, s1, c1, 0, s1, u1, 0.f); pv(m, c1, 1, s1, c1, 0, s1, u1, 1.f);
        pv(m, c0, -1, s0, c0, 0, s0, u0, 0.f); pv(m, c1, 1, s1, c1, 0, s1, u1, 1.f); pv(m, c0, 1, s0, c0, 0, s0, u0, 1.f);
        pv(m, 0, 1, 0, 0, 1, 0, 0.5f, 0.5f); pv(m, c0, 1, s0, 0, 1, 0, u0, 1.f); pv(m, c1, 1, s1, 0, 1, 0, u1, 1.f);
        pv(m, 0, -1, 0, 0, -1, 0, 0.5f, 0.5f); pv(m, c1, -1, s1, 0, -1, 0, u1, 1.f); pv(m, c0, -1, s0, 0, -1, 0, u0, 1.f);
    }
    return m;
}
static Mesh mkSph(int segs = 16)
{
    Mesh m; float ps = PI / segs, ts = 2 * PI / segs;
    for (int i = 0; i < segs; i++) {
        float p0 = i * ps - PI / 2.f, p1 = (i + 1) * ps - PI / 2.f;
        for (int j = 0; j < segs; j++) {
            float t0 = j * ts, t1 = (j + 1) * ts;
            auto P = [](float p, float t)->glm::vec3 {
                return{ cosf(p) * cosf(t), sinf(p), cosf(p) * sinf(t) };
                };
            glm::vec3 v00 = P(p0, t0), v10 = P(p1, t0), v01 = P(p0, t1), v11 = P(p1, t1);
            // UV: u = j/segs, v = i/segs
            float u0 = (float)j / segs, u1 = (float)(j + 1) / segs;
            float vv0 = (float)i / segs, vv1 = (float)(i + 1) / segs;
            pv(m, v00.x, v00.y, v00.z, v00.x, v00.y, v00.z, u0, vv0);
            pv(m, v10.x, v10.y, v10.z, v10.x, v10.y, v10.z, u0, vv1);
            pv(m, v11.x, v11.y, v11.z, v11.x, v11.y, v11.z, u1, vv1);
            pv(m, v00.x, v00.y, v00.z, v00.x, v00.y, v00.z, u0, vv0);
            pv(m, v11.x, v11.y, v11.z, v11.x, v11.y, v11.z, u1, vv1);
            pv(m, v01.x, v01.y, v01.z, v01.x, v01.y, v01.z, u1, vv0);
        }
    }
    return m;
}
static Mesh mkCone(int segs = 18)
{
    Mesh m; float st = 2 * PI / segs;
    for (int i = 0; i < segs; i++) {
        float a0 = i * st, a1 = (i + 1) * st;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        glm::vec3 n0 = glm::normalize(glm::vec3(c0, 0.45f, s0));
        glm::vec3 n1 = glm::normalize(glm::vec3(c1, 0.45f, s1));
        glm::vec3 nt = glm::normalize(glm::vec3((c0 + c1) * .5f, 0.45f, (s0 + s1) * .5f));
        pv(m, c0, 0, s0, n0.x, n0.y, n0.z); pv(m, c1, 0, s1, n1.x, n1.y, n1.z); pv(m, 0, 1, 0, nt.x, nt.y, nt.z);
        pv(m, 0, 0, 0, 0, -1, 0); pv(m, c1, 0, s1, 0, -1, 0); pv(m, c0, 0, s0, 0, -1, 0);
    }
    return m;
}
static Mesh mkRoof()
{
    Mesh m;
    glm::vec3 lN = glm::normalize(glm::vec3(-1, 1, 0)), rN = glm::normalize(glm::vec3(1, 1, 0));
    quad(m, { -1,0,-1 }, { 0,1,-1 }, { 0,1,1 }, { -1,0,1 }, lN);
    quad(m, { 0,1,-1 }, { 1,0,-1 }, { 1,0,1 }, { 0,1,1 }, rN);
    pv(m, -1, 0, -1, 0, 0, -1); pv(m, 1, 0, -1, 0, 0, -1); pv(m, 0, 1, -1, 0, 0, -1);
    pv(m, 1, 0, 1, 0, 0, 1);   pv(m, -1, 0, 1, 0, 0, 1);  pv(m, 0, 1, 1, 0, 0, 1);
    return m;
}
static Mesh mkStars(int n = 500)
{
    Mesh m; srand(101);
    for (int i = 0; i < n; i++) {
        float th = ((float)rand() / RAND_MAX) * 2 * PI;
        float ph = ((float)rand() / RAND_MAX) * PI - PI * 0.5f;
        float r = 60.f;
        pv(m, r * cosf(ph) * cosf(th), r * sinf(ph) + 2.f, r * cosf(ph) * sinf(th), 0, 1, 0);
    }
    return m;
}

static Mesh mkSkyDome(int segs = 32, float tileX = 4.0f)
{
    Mesh m; float ps = PI / segs, ts = 2 * PI / segs;
    for (int i = 0; i < segs; i++) {
        float p0 = i * ps - PI / 2.f, p1 = (i + 1) * ps - PI / 2.f;
        for (int j = 0; j < segs; j++) {
            float t0 = j * ts, t1 = (j + 1) * ts;
            auto P = [](float p, float t)->glm::vec3 {
                return{ cosf(p) * cosf(t), sinf(p), cosf(p) * sinf(t) };
            };
            glm::vec3 v00 = P(p0, t0), v10 = P(p1, t0), v01 = P(p0, t1), v11 = P(p1, t1);
            
            // Tiled UV mapped horizontally, standard linearly vertically
            float u0 = ((float)j / segs) * tileX;
            float u1 = ((float)(j + 1) / segs) * tileX;
            float vv0 = (float)i / segs;
            float vv1 = (float)(i + 1) / segs;
            
            pv(m, v00.x, v00.y, v00.z, v00.x, v00.y, v00.z, u0, vv0);
            pv(m, v10.x, v10.y, v10.z, v10.x, v10.y, v10.z, u0, vv1);
            pv(m, v11.x, v11.y, v11.z, v11.x, v11.y, v11.z, u1, vv1);
            
            pv(m, v00.x, v00.y, v00.z, v00.x, v00.y, v00.z, u0, vv0);
            pv(m, v11.x, v11.y, v11.z, v11.x, v11.y, v11.z, u1, vv1);
            pv(m, v01.x, v01.y, v01.z, v01.x, v01.y, v01.z, u1, vv0);
        }
    }
    return m;
}

// ── VAO builder ───────────────────────────────────────────────────────────────
static GLuint mkVAO(const Mesh& mesh, int& cnt)
{
    cnt = (int)mesh.size() / 8;   // ← 6 থেকে 8 করো
    GLuint va, vb;
    glGenVertexArrays(1, &va); glGenBuffers(1, &vb);
    glBindVertexArray(va);
    glBindBuffer(GL_ARRAY_BUFFER, vb);
    glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(float), mesh.data(), GL_STATIC_DRAW);

    // attr 0: position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // attr 1: normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // attr 2: texCoord  ← নতুন
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    return va;
}

// ── Shader loader ─────────────────────────────────────────────────────────────
static std::string readFile(const char* path)
{
    std::ifstream f(path);
    if (!f) { std::cerr << "Cannot open: " << path << "\n"; return ""; }
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
static GLuint mkProg(const char* vp, const char* fp)
{
    auto vs_str = readFile(vp); auto fs_str = readFile(fp);
    const char* vc = vs_str.c_str(); const char* fc = fs_str.c_str();
    int ok; char log[1024];
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vc, nullptr); glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(vs, 1024, nullptr, log); std::cerr << "VS:\n" << log << "\n"; }
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fc, nullptr); glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(fs, 1024, nullptr, log); std::cerr << "FS:\n" << log << "\n"; }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(prog, 1024, nullptr, log); std::cerr << "LINK:\n" << log << "\n"; }
    glDeleteShader(vs); glDeleteShader(fs);
    return prog;
}

// ── Uniform helpers ───────────────────────────────────────────────────────────
static void u(const char* n, const glm::mat4& v) { glUniformMatrix4fv(glGetUniformLocation(g_prog, n), 1, GL_FALSE, glm::value_ptr(v)); }
static void u(const char* n, const glm::vec3& v) { glUniform3fv(glGetUniformLocation(g_prog, n), 1, glm::value_ptr(v)); }
static void u(const char* n, float v) { glUniform1f(glGetUniformLocation(g_prog, n), v); }
static void u(const char* n, int v) { glUniform1i(glGetUniformLocation(g_prog, n), v); }
static void uv3a(const char* nm, int idx, const glm::vec3& v)
{
    char buf[64]; snprintf(buf, 64, "%s[%d]", nm, idx);
    glUniform3fv(glGetUniformLocation(g_prog, buf), 1, glm::value_ptr(v));
}

// ── Mesh VAOs ─────────────────────────────────────────────────────────────────
static GLuint bxV, cyV, spV, cnV, rfV, stV, sdV;
static int    bxC, cyC, spC, cnC, rfC, stC, sdC;

// ── Globals for Textures ──────────────────────────────────────────────────────
static GLuint texWall, texWindows, texRoof, texWood, texStone, texPlanks, texWindowArch;
static GLuint texCastle;
static GLuint texFrog;
static GLuint texBarnWall, texBarnInner, texBarnFloor, texBarnRoof, texBarnDoor;
static GLuint texHorizon;

static GLuint loadTexture(const char* path) {
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    int w, h, nc;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &w, &h, &nc, 0);
    if (data) {
        GLenum f = (nc == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, f, w, h, 0, f, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else {
        std::cerr << "Failed to load: " << path << "\n";
    }
    stbi_image_free(data);
    return texID;
}

static void drwTex(GLuint va, int cnt, const glm::mat4& m, GLuint texID, int matType = 7, bool emissive = false, GLenum mode = GL_TRIANGLES)
{
    u("model", m);
    u("objectColor", glm::vec3(1.0f));
    u("matType", matType); u("isEmissive", (int)emissive);
    if (texID > 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texID);
        u("tex0", 0);
    }
    glBindVertexArray(va);
    glDrawArrays(mode, 0, cnt);
}

// ── Core draw call ────────────────────────────────────────────────────────────
static void drw(GLuint va, int cnt, const glm::mat4& m, glm::vec3 col,
    int matType = 0, bool emissive = false, GLenum mode = GL_TRIANGLES)
{
    u("model", m); u("objectColor", col);
    u("matType", matType); u("isEmissive", (int)emissive);
    glBindVertexArray(va);
    glDrawArrays(mode, 0, cnt);
}

// ── Bezier & Fractal Logic ──────────────────────────────────────────────────
static void drawBezierArch(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float thickness) {
    const int segments = 20;
    for (int i = 0; i < segments; i++) {
        float t = (float)i / segments;
        float nt = (float)(i + 1) / segments;
        auto bezier = [&](float bt) {
            return powf(1 - bt, 3) * p0 + 3.f * powf(1 - bt, 2) * bt * p1 + 3.f * (1 - bt) * bt * bt * p2 + bt * bt * bt * p3;
            };
        glm::vec3 a = bezier(t);
        glm::vec3 b = bezier(nt);
        glm::vec3 mid = (a + b) * 0.5f;
        float len = glm::length(b - a);
        glm::mat4 m = glm::translate(glm::mat4(1), mid);
        glm::vec3 dir = glm::normalize(b - a);
        glm::vec3 up = { 0, 1, 0 };
        glm::vec3 axis = glm::cross(up, dir);
        if (glm::length(axis) > 0.001f) {
            float angle = acosf(glm::dot(up, dir));
            m = glm::rotate(m, angle, axis);
        }
        m = glm::scale(m, { thickness, len * 0.5f, thickness });
        drw(cyV, cyC, m, { 0.4f, 0.35f, 0.3f });
    }
}

static void drawFractalTree(glm::vec3 p, glm::vec3 dir, float len, float thick, int depth) {
    if (depth <= 0) return;
    glm::vec3 end = p + dir * len;
    glm::mat4 m = glm::translate(glm::mat4(1), (p + end) * 0.5f);
    glm::vec3 up = { 0, 1, 0 };
    glm::vec3 axis = glm::cross(up, dir);
    if (glm::length(axis) > 0.001f) {
        float angle = acosf(glm::dot(up, dir));
        m = glm::rotate(m, angle, axis);
    }
    m = glm::scale(m, { thick, len * 0.5f, thick });
    drw(cyV, cyC, m, { 0.35f - depth * 0.05f, 0.2f, 0.1f });

    if (depth > 1) {
        float ang = 0.5f;
        glm::vec3 d1 = glm::normalize(dir + glm::vec3(sinf(ang), cosf(ang), 0) * 0.5f);
        glm::vec3 d2 = glm::normalize(dir + glm::vec3(-sinf(ang), cosf(ang), 0) * 0.5f);
        drawFractalTree(end, d1, len * 0.7f, thick * 0.6f, depth - 1);
        drawFractalTree(end, d2, len * 0.7f, thick * 0.6f, depth - 1);
    }
}

// =============================================================================
// ── Object builders (unchanged) ───────────────────────────────────────────────
// =============================================================================

static void drawTree(glm::vec3 p, float sc, int seed = 0)
{
    srand(seed);

    float hue = (rand() % 20) * 0.005f;
    glm::vec3 woodColor = { 0.30f + hue, 0.18f, 0.10f }; // ডার্ক ব্রাউন
    glm::vec3 leafBaseColor = { 0.10f, 0.32f + hue, 0.05f };

    auto drawBranch = [&](auto self, glm::vec3 start, glm::vec3 dir, float length, float thickness, int depth) -> void {
        glm::vec3 end = start + dir * length;

        // শাখা (Branch) ড্রয়িং
        glm::vec3 center = (start + end) * 0.5f;
        glm::mat4 m = glm::translate(glm::mat4(1), center);

        glm::vec3 up = glm::vec3(0, 1, 0);
        glm::vec3 axis = glm::cross(up, dir);
        if (glm::length(axis) > 0.001f) {
            float angle = acos(glm::dot(up, dir) / (glm::length(up) * glm::length(dir)));
            m = glm::rotate(m, angle, axis);
        }

        m = glm::scale(m, { thickness, length * 0.5f, thickness });
        drw(cyV, cyC, m, woodColor);

        // পাতার লজিক (Leaves)
        // depth ১ বা ২ হলে আমরা কিছু পাতা যোগ করব যাতে গাছটি ন্যাড়া না লাগে
        if (depth <= 2) {
            int leafCount = (depth == 0) ? 12 : 5; // একদম মাথায় বেশি পাতা থাকবে
            for (int i = 0; i < leafCount; i++) {
                // র্যান্ডম অফসেট যাতে পাতাগুলো গুচ্ছ আকারে থাকে
                float ox = ((rand() % 100) / 100.f - 0.5f) * sc * 0.8f;
                float oy = ((rand() % 100) / 100.f - 0.2f) * sc * 0.6f;
                float oz = ((rand() % 100) / 100.f - 0.5f) * sc * 0.8f;

                glm::mat4 lm = glm::translate(glm::mat4(1), end + glm::vec3(ox, oy, oz));

                // পাতার সাইজ এবং শেডিং এ বৈচিত্র্য আনা
                float s = sc * (0.25f + (rand() % 50) / 100.f);
                lm = glm::scale(lm, glm::vec3(s));

                // নিচের পাতা গাঢ়, উপরের পাতা হালকা (Fake AO)
                float brightness = 0.7f + (oy / sc);
                drw(spV, spC, lm, leafBaseColor * brightness);
            }
        }

        if (depth <= 0) return;

        // নতুন শাখা তৈরি (Recursion)
        int numSplits = (depth == 1) ? 3 : 2;
        for (int i = 0; i < numSplits; i++) {
            glm::vec3 newDir = glm::normalize(dir + glm::vec3(
                ((rand() % 100) / 50.f - 1.f) * 0.7f,
                ((rand() % 100) / 100.f) * 0.4f,
                ((rand() % 100) / 50.f - 1.f) * 0.7f
            ));
            self(self, end, newDir, length * 0.75f, thickness * 0.6f, depth - 1);
        }
        };

    // গাছের মূল কান্ড শুরু
    drawBranch(drawBranch, p, glm::vec3(0, 1, 0), sc * 1.3f, sc * 0.15f, 4);

    // গ্রাউন্ড শ্যাডো
    glm::mat4 ao = glm::translate(glm::mat4(1), p + glm::vec3(0, 0.01f, 0));
    ao = glm::scale(ao, { sc * 1.2f, 0.01f, sc * 1.2f });
    drw(cyV, cyC, ao, { 0.05f, 0.05f, 0.05f });
}
static void drawMushroom(glm::vec3 p, float sc, glm::vec3 cap)
{
    glm::mat4 m = glm::translate(glm::mat4(1), p + glm::vec3(0, sc * 0.42f, 0));
    m = glm::scale(m, { sc * 0.11f,sc * 0.42f,sc * 0.11f });
    drw(cyV, cyC, m, { 0.76f,0.72f,0.66f }); // Stem color improved slightly

    // Top cap with texture
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, sc * 0.88f, 0));
    m = glm::scale(m, { sc * 0.42f, sc * 0.24f, sc * 0.42f });
    drwTex(spV, spC, m, texMushroom, 7);
}
static void drawHuman(glm::vec3 p, float t)
{
    glm::mat4 m = glm::translate(glm::mat4(1), p);

    // Body (Torso)
    glm::mat4 torso = glm::translate(m, { 0, 1.45f, 0 });
    torso = glm::scale(torso, { 0.35f, 0.55f, 0.22f });
    drw(bxV, bxC, torso, { 0.15f, 0.25f, 0.45f }); // Blue-ish shirt

    // Head
    glm::mat4 head = glm::translate(m, { 0, 2.25f, 0 });
    head = glm::scale(head, glm::vec3(0.24f));
    drw(spV, spC, head, { 0.92f, 0.72f, 0.55f }); // Skin tone

    // Legs
    for (float lx : {-0.15f, 0.15f}) {
        float sw = sinf(t * 5.0f + (lx > 0 ? PI : 0.f)) * 0.35f;
        glm::mat4 leg = glm::translate(m, { lx, 0.65f, sw * 0.15f });
        leg = glm::scale(leg, { 0.08f, 0.65f, 0.08f });
        drw(cyV, cyC, leg, { 0.1f, 0.1f, 0.1f }); // Dark pants
    }
    // Arms
    for (float ax : {-0.48f, 0.48f}) {
        float sw = sinf(t * 5.0f + (ax > 0 ? 0.f : PI)) * 0.45f;
        glm::mat4 arm = glm::translate(m, { ax, 1.65f, sw * 0.25f });
        arm = glm::scale(arm, { 0.06f, 0.45f, 0.06f });
        drw(cyV, cyC, arm, { 0.92f, 0.72f, 0.55f }); // Skin tone
    }
}
static void drawFrog(glm::vec3 p, float sc, float t, int id)
{
    // Interaction: Progress: 0 to 10 units forward, then reset
    float cycleLen = 8.0f;
    float totalT = t + (float)id * 2.2f;
    float progress = fmodf(totalT * 0.85f, 10.0f);

    // Leap Animation (Parabolic)
    float leapFreq = 3.5f;
    float jump = max(0.0f, sinf(progress * leapFreq)) * 0.65f;

    // Fade out as it reaches the end (scaling to 0)
    float fade = 1.0f;
    if (progress > 8.0f) fade = 1.0f - (progress - 8.0f) / 2.0f;

    glm::mat4 m = glm::translate(glm::mat4(1), p + glm::vec3(0, jump * sc, progress * sc));
    m = glm::scale(m, glm::vec3(fade * sc)); // Include scale in the fade

    // Body
    glm::mat4 body = glm::scale(m, { 0.35f, 0.22f, 0.45f });
    drwTex(spV, spC, body, texFrog, 7);


    // Eyes
    for (float ex : {-0.15f, 0.15f}) {
        glm::mat4 eye = glm::translate(m, { ex * sc, 0.18f * sc, 0.18f * sc });
        eye = glm::scale(eye, glm::vec3(sc * 0.08f));
        drw(spV, spC, eye, { 0, 0, 0 }, 0, true);
    }
}
static void drawHelpSign(glm::vec3 p)
{
    glm::mat4 b = glm::translate(glm::mat4(1), p);

    // Post
    glm::mat4 post = glm::translate(b, { 0, 1.2f, 0 });
    post = glm::scale(post, { 0.08f, 1.20f, 0.08f });
    drw(bxV, bxC, post, { 0.4f, 0.3f, 0.15f });

    // Instruction Planks
    glm::vec3 plankCol = { 0.35f, 0.28f, 0.18f };
    for (int i = 0; i < 2; i++) {
        glm::mat4 plank = glm::translate(b, { 0, 1.85f - i * 0.55f, 0.08f });
        plank = glm::rotate(plank, sinf(glfwGetTime() * 0.8f + i) * 0.04f, { 0,0,1 });
        plank = glm::scale(plank, { 1.25f, 0.24f, 0.05f });
        drw(bxV, bxC, plank, plankCol);

        // Small "Button" labels
        glm::mat4 key = glm::translate(b, { -0.95f, 1.85f - i * 0.55f, 0.12f });
        key = glm::scale(key, glm::vec3(0.12f));
        drw(bxV, bxC, key, { 0.7f, 0.7f, 0.7f });
    }
}
static void drawRoadSideStones(const glm::vec3 WP[], int NWP)
{
    srand(42);
    for (int seg = 0; seg < NWP - 1; seg++)
    {
        glm::vec3 from = WP[seg], to = WP[seg + 1];
        float len = glm::length(to - from);
        glm::vec3 dir = glm::normalize(to - from);
        glm::vec3 perp = glm::normalize(glm::vec3(-dir.z, 0.f, dir.x));

        float spacing = 0.22f; // ঘনত্ব বাড়ানো হলো (আগে ০.১৮ ছিল)
        int count = (int)(len / spacing);

        for (int i = 0; i < count; i++)
        {
            float t = (i + 0.5f) / (float)count;
            glm::vec3 center = from + dir * (t * len);

            for (int side : {-1, 1})
            {
                // রাস্তার এজ ৩.৪ (width=3.4f means half-width=3.4)
                float borderDist = 3.4f + ((float)(rand() % 100)) / 100.f * 0.12f;
                glm::vec3 pos = center + perp * (float(side) * borderDist);

                // CROSSROADS (0, -5) এর কাছে পাথর ড্র না করা (যাতে ইন্টারসেকশন পরিষ্কার থাকে)
                if (glm::distance(pos, glm::vec3(0.f, 0.013f, -5.f)) < 7.5f) continue;

                float sx = 0.11f + (rand() % 100) / 750.f;
                float sy = 0.08f + (rand() % 100) / 550.f;
                float sz = sx * (0.65f + (rand() % 100) / 250.f);
                float angle = ((float)(rand() % 628)) / 100.f;

                glm::mat4 m = glm::translate(glm::mat4(1), pos + glm::vec3(0, sy, 0));
                m = glm::rotate(m, angle, { 0, 1, 0 });
                m = glm::scale(m, { sx, sy, sz });

                float bri = 0.28f + (rand() % 100) / 400.f;
                drw(spV, spC, m, { bri, bri * 0.94f, bri * 0.88f });
            }
        }
    }
}
static void drawStump(glm::vec3 p, float r, float h)
{
    glm::mat4 m = glm::translate(glm::mat4(1), p + glm::vec3(0, h, 0));
    m = glm::scale(m, { r,h,r });
    drw(cyV, cyC, m, { 0.26f,0.16f,0.09f });
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, h * 2.f + 0.01f, 0));
    m = glm::scale(m, { r,0.04f,r });
    drw(cyV, cyC, m, { 0.36f,0.24f,0.14f });
}
static void drawFire(glm::vec3 p, float t)
{
    // ১. আগুনের কাঠ (Logs)
    for (int i = 0; i < 4; i++) {
        float ang = i * (PI / 2.f);
        glm::mat4 m = glm::translate(glm::mat4(1), p + glm::vec3(0, 0.05f, 0));
        m = glm::rotate(m, ang, { 0, 1, 0 });
        m = glm::rotate(m, glm::radians(80.f), { 1, 0, 0 });
        m = glm::scale(m, { 0.04f, 0.22f, 0.04f });
        drw(cyV, cyC, m, { 0.25f, 0.12f, 0.05f });
    }

    // ২. আগুনের একাধিক শিখা (Flame Tongues)
    // অনেকগুলো পাতলা কোণ (Cone) বিভিন্ন দিকে বাঁকিয়ে এই আঁকাবাঁকা লুক তৈরি করা হয়
    int numTongues = 8;
    for (int i = 0; i < numTongues; i++) {
        float ang = (i * 2.0f * PI / numTongues) + (t * 2.0f); // সময়ের সাথে ঘুরবে
        float flicker = sinf(t * 12.0f + i) * 0.1f + 0.9f; // কাঁপুনি

        // শিখার দূরত্ব কেন্দ্র থেকে
        float dist = 0.08f * sinf(t * 3.0f + i);
        glm::vec3 fPos = p + glm::vec3(cosf(ang) * dist, 0.1f, sinf(ang) * dist);

        // বড় শিখা (বাইরের স্তর - কমলা/লাল)
        glm::mat4 m1 = glm::translate(glm::mat4(1), fPos + glm::vec3(0, 0.3f * flicker, 0));
        m1 = glm::rotate(m1, sinf(t * 5.f + i) * 0.3f, { 0,0,1 }); // বাতাসে দোলা
        m1 = glm::scale(m1, { 0.15f, 0.6f * flicker, 0.15f });
        drw(cnV, cnC, m1, { 1.0f, 0.35f, 0.0f }, 0, true);

        // ছোট উজ্জ্বল শিখা (ভেতরের স্তর - হলুদ/সাদা)
        glm::mat4 m2 = glm::translate(glm::mat4(1), fPos + glm::vec3(0, 0.2f * flicker, 0));
        m2 = glm::scale(m2, { 0.08f, 0.35f * flicker, 0.08f });
        drw(cnV, cnC, m2, { 1.0f, 0.9f, 0.3f }, 0, true);
    }

    // ৩. স্ফুলিঙ্গ (Sparks)
    for (int i = 0; i < 6; i++) {
        float life = fmod(t * 0.7f + i * 0.2f, 1.2f);
        glm::mat4 sm = glm::translate(glm::mat4(1), p + glm::vec3(sinf(t * 5.f + i) * 0.1f, 0.4f + life, cosf(t * 4.f + i) * 0.1f));
        sm = glm::scale(sm, glm::vec3(0.02f * (1.2f - life)));
        drw(spV, spC, sm, { 1.0f, 0.7f, 0.2f }, 0, true);
    }
}
static void drawClouds(float t)
{
    if (!g_day) return; // রাতে মেঘ দেখাবে না

    struct CloudDef { float x, y, z, spd; int seed; };
    static const CloudDef clouds[] = {
        {  10.f, 22.f,  -30.f, 0.8f, 1 },
        { -15.f, 25.f,  -60.f, 0.6f, 2 },
        {  25.f, 20.f,  -90.f, 1.0f, 3 },
        { -20.f, 24.f, -120.f, 0.7f, 4 },
        {   5.f, 23.f, -150.f, 0.9f, 5 },
        {  30.f, 21.f, -180.f, 0.5f, 6 },
        { -30.f, 26.f,  -50.f, 0.8f, 7 },
        {  18.f, 22.f, -100.f, 0.6f, 8 },
    };

    for (auto& c : clouds)
    {
        // মেঘ ধীরে ধীরে X দিকে ভাসে
        float cx = c.x + fmodf(t * c.spd, 80.f) - 40.f;

        srand(c.seed);
        // প্রতিটি মেঘ = কয়েকটি overlapping sphere
        int blobs = 4 + (c.seed % 3);
        for (int b = 0; b < blobs; b++) {
            float bx = cx + (rand() % 100) / 20.f - 2.5f;
            float by = c.y + (rand() % 100) / 50.f - 1.f;
            float bz = c.z + (rand() % 100) / 15.f - 3.f;
            float bs = 1.8f + (rand() % 100) / 40.f;

            glm::mat4 m = glm::translate(glm::mat4(1), { bx, by, bz });
            m = glm::scale(m, { bs * 1.6f, bs * 0.7f, bs });
            // মেঘের রং — রেইনে গাঢ় ধূসর, পরিষ্কারে সাদাটে
            glm::vec3 cc = g_rain
                ? glm::vec3(0.45f, 0.48f, 0.52f)
                : glm::vec3(0.96f, 0.97f, 1.00f);
            drw(spV, spC, m, cc);
        }
    }
}
static void drawBirds(float t)
{
    // রাতে পাখি নেই
    if (!g_day) return;

    struct Flock { float cx, cy, cz, radius, speed, height; int count; };
    static const Flock flocks[] = {
        {  5.f, 14.f,  -40.f, 8.f, 0.4f, 0.f, 5 },
        { -10.f, 18.f, -90.f, 6.f, 0.6f, 0.f, 4 },
        {  15.f, 16.f,-140.f, 7.f, 0.5f, 0.f, 6 },
    };

    for (auto& fl : flocks)
    {
        for (int i = 0; i < fl.count; i++)
        {
            // প্রতিটি পাখি circle এ ওড়ে, phase offset দিয়ে ছড়ানো
            float phase = t * fl.speed + i * (2.f * PI / fl.count);
            float bx = fl.cx + cosf(phase) * fl.radius;
            float bz = fl.cz + sinf(phase) * fl.radius * 0.5f;
            float by = fl.cy + sinf(phase * 2.f + i) * 0.8f; // উঁচু-নিচু

            // ডানার ঝাপটা
            float wingFlap = sinf(t * 6.f + i * 1.3f) * 0.3f;

            // Body (ছোট্ট elongated sphere)
            glm::mat4 body = glm::translate(glm::mat4(1), { bx, by, bz });
            // পাখি উড়ার দিকে মুখ করে
            body = glm::rotate(body, phase + PI / 2.f, { 0, 1, 0 });
            body = glm::scale(body, { 0.08f, 0.06f, 0.22f });
            drw(spV, spC, body, { 0.12f, 0.10f, 0.10f });

            // বাম ডানা
            glm::mat4 wingL = glm::translate(glm::mat4(1), { bx, by, bz });
            wingL = glm::rotate(wingL, phase + PI / 2.f, { 0, 1, 0 });
            wingL = glm::rotate(wingL, wingFlap, { 0, 0, 1 });
            wingL = glm::translate(wingL, { -0.25f, 0.f, 0.f });
            wingL = glm::scale(wingL, { 0.25f, 0.02f, 0.10f });
            drw(bxV, bxC, wingL, { 0.12f, 0.10f, 0.10f });

            // ডান ডানা
            glm::mat4 wingR = glm::translate(glm::mat4(1), { bx, by, bz });
            wingR = glm::rotate(wingR, phase + PI / 2.f, { 0, 1, 0 });
            wingR = glm::rotate(wingR, -wingFlap, { 0, 0, 1 });
            wingR = glm::translate(wingR, { 0.25f, 0.f, 0.f });
            wingR = glm::scale(wingR, { 0.25f, 0.02f, 0.10f });
            drw(bxV, bxC, wingR, { 0.12f, 0.10f, 0.10f });
        }
    }
}
static void drawMedievalHall(glm::vec3 p, float facing)
{
    glm::mat4 b = glm::translate(glm::mat4(1), p);
    b = glm::rotate(b, facing, { 0, 1, 0 });
    glm::mat4 m;

    // ── Color Palette ──────────────────────────────────────────────────────
    glm::vec3 wallDark = { 0.16f, 0.18f, 0.14f };  // প্রায় কালো সবুজাভ দেয়াল
    glm::vec3 beamCol = { 0.20f, 0.13f, 0.07f };  // গাঢ় কাঠের রঙ
    glm::vec3 plasterC = { 0.72f, 0.70f, 0.60f };  // সাদাটে plaster

    // ══════════════════════════════════════════════════════════════════════
    // 1. STONE FOUNDATION
    // ══════════════════════════════════════════════════════════════════════
    m = glm::translate(glm::mat4(1), { 0.f, 0.013f, -5.f });
    m = glm::scale(m, { 16.f, 0.012f, 16.f });
    drwTex(bxV, bxC, m, texStone, 8);

    // ══════════════════════════════════════════════════════════════════════
    // 2. MAIN WALLS — dark timber-frame, full perimeter
    // ══════════════════════════════════════════════════════════════════════
    // Upper plaster band
    m = glm::translate(b, { 0.f, 1.8f, 0.f });
    m = glm::scale(m, { 5.0f, 0.9f, 3.0f });
    drwTex(bxV, bxC, m, texWall, 8);

    // Lower dark panel (below mid-beam)
    m = glm::translate(b, { 0.f, 0.75f, 0.f });
    m = glm::scale(m, { 5.0f, 0.55f, 3.0f });
    drw(bxV, bxC, m, wallDark);

    // Mid horizontal beam
    m = glm::translate(b, { 0.f, 1.32f, 0.f });
    m = glm::scale(m, { 5.1f, 0.10f, 3.1f });
    drwTex(bxV, bxC, m, texWood, 8);

    // Top beam (wall cap)
    m = glm::translate(b, { 0.f, 2.72f, 0.f });
    m = glm::scale(m, { 5.1f, 0.10f, 3.1f });
    drwTex(bxV, bxC, m, texWood, 8);

    // Bottom base beam
    m = glm::translate(b, { 0.f, 0.40f, 0.f });
    m = glm::scale(m, { 5.1f, 0.10f, 3.1f });
    drwTex(bxV, bxC, m, texWood, 8);

    // ══════════════════════════════════════════════════════════════════════
    // 3. VERTICAL BEAMS — front and back
    // ══════════════════════════════════════════════════════════════════════
    for (float x : {-2.5f, -1.25f, 0.f, 1.25f, 2.5f}) {
        // Front face beams
        m = glm::translate(b, { x, 1.55f, 3.02f });
        m = glm::scale(m, { 0.09f, 1.35f, 0.06f });
        drwTex(bxV, bxC, m, texWood, 8);
        // Back face beams
        m = glm::translate(b, { x, 1.55f, -3.02f });
        m = glm::scale(m, { 0.09f, 1.35f, 0.06f });
        drwTex(bxV, bxC, m, texWood, 8);
    }
    // Side vertical beams (left & right walls)
    for (float z : {-1.5f, 0.f, 1.5f}) {
        m = glm::translate(b, { 5.02f, 1.55f, z });
        m = glm::scale(m, { 0.06f, 1.35f, 0.09f });
        drwTex(bxV, bxC, m, texWood, 8);
        m = glm::translate(b, { -5.02f, 1.55f, z });
        m = glm::scale(m, { 0.06f, 1.35f, 0.09f });
        drwTex(bxV, bxC, m, texWood, 8);
    }

    // ══════════════════════════════════════════════════════════════════════
    // 4. DIAGONAL X-BEAMS — front gable (এটাই ছবির সবচেয়ে বড় feature)
    // ══════════════════════════════════════════════════════════════════════
    // Left panel X
    m = glm::translate(b, { -1.88f, 1.55f, 3.03f });
    m = glm::rotate(m, glm::radians(38.f), { 0,0,1 });
    m = glm::scale(m, { 0.07f, 1.5f, 0.055f });
    drwTex(bxV, bxC, m, texWood, 8);
    m = glm::translate(b, { -1.88f, 1.55f, 3.03f });
    m = glm::rotate(m, glm::radians(-38.f), { 0,0,1 });
    m = glm::scale(m, { 0.07f, 1.5f, 0.055f });
    drwTex(bxV, bxC, m, texWood, 8);
    // Right panel X
    m = glm::translate(b, { 1.88f, 1.55f, 3.03f });
    m = glm::rotate(m, glm::radians(38.f), { 0,0,1 });
    m = glm::scale(m, { 0.07f, 1.5f, 0.055f });
    drwTex(bxV, bxC, m, texWood, 8);
    m = glm::translate(b, { 1.88f, 1.55f, 3.03f });
    m = glm::rotate(m, glm::radians(-38.f), { 0,0,1 });
    m = glm::scale(m, { 0.07f, 1.5f, 0.055f });
    drwTex(bxV, bxC, m, texWood, 8);
    // Back gable X-beams (same pattern)
    m = glm::translate(b, { -1.88f, 1.55f, -3.03f });
    m = glm::rotate(m, glm::radians(38.f), { 0,0,1 });
    m = glm::scale(m, { 0.07f, 1.5f, 0.055f });
    drwTex(bxV, bxC, m, texWood, 8);
    m = glm::translate(b, { -1.88f, 1.55f, -3.03f });
    m = glm::rotate(m, glm::radians(-38.f), { 0,0,1 });
    m = glm::scale(m, { 0.07f, 1.5f, 0.055f });
    drwTex(bxV, bxC, m, texWood, 8);
    m = glm::translate(b, { 1.88f, 1.55f, -3.03f });
    m = glm::rotate(m, glm::radians(38.f), { 0,0,1 });
    m = glm::scale(m, { 0.07f, 1.5f, 0.055f });
    drwTex(bxV, bxC, m, texWood, 8);
    m = glm::translate(b, { 1.88f, 1.55f, -3.03f });
    m = glm::rotate(m, glm::radians(-38.f), { 0,0,1 });
    m = glm::scale(m, { 0.07f, 1.5f, 0.055f });
    drwTex(bxV, bxC, m, texWood, 8);

    // ══════════════════════════════════════════════════════════════════════
    // 5. MAIN ROOF — খুব steep, চওড়া overhang (ছবির dominant feature)
    // ══════════════════════════════════════════════════════════════════════
    m = glm::translate(b, { 0.f, 2.82f, 0.f });
    m = glm::scale(m, { 6.2f, 3.8f, 4.0f });  // বড় overhang দিতে scale বাড়ানো
    drwTex(rfV, rfC, m, texRoof, 8);

    // Roof ridge beam (মাথার লম্বা বিম)
    m = glm::translate(b, { 0.f, 6.62f, 0.f });
    m = glm::scale(m, { 6.3f, 0.10f, 0.15f });
    drwTex(bxV, bxC, m, texWood, 8);

    // Roof edge trim — front & back gable edges
    for (float rz : {-4.02f, 4.02f}) {
        m = glm::translate(b, { -1.55f, 4.75f, rz });
        m = glm::rotate(m, glm::radians(32.5f), { 0,0,1 });
        m = glm::scale(m, { 3.6f, 0.10f, 0.10f });
        drwTex(bxV, bxC, m, texWood, 8);
        m = glm::translate(b, { 1.55f, 4.75f, rz });
        m = glm::rotate(m, glm::radians(-32.5f), { 0,0,1 });
        m = glm::scale(m, { 3.6f, 0.10f, 0.10f });
        drwTex(bxV, bxC, m, texWood, 8);
    }

    // ══════════════════════════════════════════════════════════════════════
    // 6. FRONT GABLE WINDOW (বড় ত্রিভুজাকার গর্ত-এ)
    // ══════════════════════════════════════════════════════════════════════
    m = glm::translate(b, { 0.f, 3.5f, 3.04f });
    m = glm::scale(m, { 0.80f, 0.65f, 0.04f });
    drwTex(bxV, bxC, m, texWindows, 7);
    // Window arch top
    m = glm::translate(b, { 0.f, 4.15f, 3.04f });
    m = glm::scale(m, { 0.80f, 0.35f, 0.04f });
    drwTex(spV, spC, m, texWindowArch, 7);

    // ══════════════════════════════════════════════════════════════════════
    // 7. FRONT WINDOWS (ground floor)
    // ══════════════════════════════════════════════════════════════════════
    for (float wx : {-1.5f, 1.0f}) {
        m = glm::translate(b, { wx, 1.55f, 3.02f });
        m = glm::scale(m, { 0.30f, 0.38f, 0.04f });
        drwTex(bxV, bxC, m, texWindows, 7);
    }

    // ══════════════════════════════════════════════════════════════════════
    // 8. LEFT PORCH (খোলা verandah — ছবিতে বাম দিকে দেখা যাচ্ছে)
    // ══════════════════════════════════════════════════════════════════════
    // Porch posts (4টি)
    for (float pz : {-1.4f, 1.4f}) {
        m = glm::translate(b, { -5.9f, 1.3f, pz });
        m = glm::scale(m, { 0.12f, 1.3f, 0.12f });
        drwTex(cyV, cyC, m, texWood, 8);
    }
    // Porch beam (horizontal, posts এর উপরে)
    m = glm::translate(b, { -5.9f, 2.65f, 0.f });
    m = glm::scale(m, { 0.12f, 0.10f, 3.0f });
    drwTex(bxV, bxC, m, texWood, 8);
    // Connection beam (main wall থেকে posts পর্যন্ত)
    m = glm::translate(b, { -5.5f, 2.65f, 0.f });
    m = glm::scale(m, { 1.0f, 0.10f, 0.10f });
    drwTex(bxV, bxC, m, texWood, 8);
    // Porch lean-to roof
    m = glm::translate(b, { -5.55f, 2.55f, 0.f });
    m = glm::rotate(m, glm::radians(12.f), { 0, 0, 1 });
    m = glm::scale(m, { 1.4f, 0.95f, 3.2f });
    drwTex(rfV, rfC, m, texRoof, 8);
    // Porch floor
    m = glm::translate(b, { -5.55f, 0.37f, 0.f });
    m = glm::scale(m, { 1.2f, 0.06f, 2.8f });
    drwTex(bxV, bxC, m, texWood, 8);

    // ══════════════════════════════════════════════════════════════════════
    // 9. RIGHT SIDE EXTENSION (ছবিতে ডানদিকে ছোট অংশ)
    // ══════════════════════════════════════════════════════════════════════
    // Small side wall
    m = glm::translate(b, { 5.9f, 1.1f, 0.f });
    m = glm::scale(m, { 0.85f, 1.1f, 2.2f });
    drwTex(bxV, bxC, m, texWall, 8);
    // Side lean-to roof
    m = glm::translate(b, { 5.8f, 2.25f, 0.f });
    m = glm::rotate(m, glm::radians(-10.f), { 0, 0, 1 });
    m = glm::scale(m, { 1.2f, 0.9f, 2.6f });
    drwTex(rfV, rfC, m, texRoof, 8);

    // ══════════════════════════════════════════════════════════════════════
    // 10. DOOR (সামনের মাঝখানে)
    // ══════════════════════════════════════════════════════════════════════
    m = glm::translate(b, { -2.5f, 1.0f, 3.02f });
    m = glm::scale(m, { 0.38f, 0.80f, 0.04f });
    drwTex(bxV, bxC, m, texWood, 7);
    // Door arch
    m = glm::translate(b, { -2.5f, 1.82f, 3.02f });
    m = glm::scale(m, { 0.38f, 0.28f, 0.04f });
    drwTex(spV, spC, m, texWindowArch, 7);
    // Door frame
    m = glm::translate(b, { -2.5f, 1.0f, 3.03f });
    m = glm::scale(m, { 0.44f, 0.85f, 0.03f });
    drw(bxV, bxC, m, beamCol);

    // ══════════════════════════════════════════════════════════════════════
    // 11. STONE STEPS (দরজার সামনে)
    // ══════════════════════════════════════════════════════════════════════
    for (int si = 0; si < 3; si++) {
        m = glm::translate(b, { -2.5f, 0.08f + si * 0.13f, 3.12f + si * 0.18f });
        m = glm::scale(m, { 0.50f, 0.07f, 0.18f });
        drwTex(bxV, bxC, m, texStone, 8);
    }

    // ══════════════════════════════════════════════════════════════════════
    // 12. DECORATIVE ROOF SUPPORT BEAMS (eave brackets)
    // ══════════════════════════════════════════════════════════════════════
    for (float bx : {-2.5f, 0.f, 2.5f}) {
        // Front eave
        m = glm::translate(b, { bx, 2.75f, 3.55f });
        m = glm::rotate(m, glm::radians(-35.f), { 1,0,0 });
        m = glm::scale(m, { 0.09f, 0.10f, 0.55f });
        drwTex(bxV, bxC, m, texWood, 8);
        // Back eave
        m = glm::translate(b, { bx, 2.75f, -3.55f });
        m = glm::rotate(m, glm::radians(35.f), { 1,0,0 });
        m = glm::scale(m, { 0.09f, 0.10f, 0.55f });
        drwTex(bxV, bxC, m, texWood, 8);
    }
}

static void drawTent(glm::vec3 p, float facing)
{
    glm::mat4 base = glm::translate(glm::mat4(1), p);
    base = glm::rotate(base, facing, { 0, 1, 0 });

    // কালার প্যালেট
    glm::vec3 fabricLight = { 0.45f, 0.35f, 0.25f };  // হালকা ব্রাউন
    glm::vec3 fabricMid = { 0.35f, 0.25f, 0.15f };  // মাঝারি ব্রাউন
    glm::vec3 fabricDark = { 0.25f, 0.15f, 0.08f };  // গাঢ় ব্রাউন
    glm::vec3 poleColor = { 0.32f, 0.21f, 0.10f };

    glm::vec3 interiorDark = { 0.01f, 0.01f, 0.015f }; // ভেতরের অন্ধকার শেড

    // ── ১. মেইন বডি (Body) ──
    glm::mat4 m = glm::translate(base, { 0.0f, 0.0f, 0.0f });
    m = glm::scale(m, { 1.4f, 1.6f, 2.2f });
    drw(rfV, rfC, m, fabricMid);

    // ── ২. ভেতরের অন্ধকার রুম (Interior Room) ──
    // মেইন বডির ভেতরে এটা একটু ছোট সাইজে থাকবে যেন বাইরের দেয়াল একে ঢেকে রাখে
    glm::mat4 interior = glm::translate(base, { 0.0f, 0.01f, 0.0f });
    interior = glm::scale(interior, { 1.35f, 1.55f, 2.15f });
    drw(rfV, rfC, interior, interiorDark);

    // ── ৩. উপরের রিজ স্ট্রিপ (Ridge Strip) ──
    glm::mat4 ridgeStrip = glm::translate(base, { 0.0f, 1.58f, 0.0f });
    ridgeStrip = glm::scale(ridgeStrip, { 0.12f, 0.12f, 2.25f });
    drw(bxV, bxC, ridgeStrip, fabricLight);

    // ── ৪. বাম পাশের প্যানেল (Left Slope) ──
    glm::mat4 leftPanel = glm::translate(base, { -0.72f, 0.78f, 0.0f });
    leftPanel = glm::rotate(leftPanel, glm::radians(-32.0f), { 0, 0, 1 });
    leftPanel = glm::scale(leftPanel, { 0.04f, 1.55f, 2.18f });
    drw(bxV, bxC, leftPanel, fabricDark);

    // ── ৫. ডান পাশের প্যানেল (Right Slope) ──
    glm::mat4 rightPanel = glm::translate(base, { 0.72f, 0.78f, 0.0f });
    rightPanel = glm::rotate(rightPanel, glm::radians(32.0f), { 0, 0, 1 });
    rightPanel = glm::scale(rightPanel, { 0.04f, 1.55f, 2.18f });
    drw(bxV, bxC, rightPanel, fabricLight);

    // ── ৬. পেছনের দেয়াল (Back Wall) ──
    glm::mat4 back = glm::translate(base, { 0.0f, 0.0f, -2.21f });
    back = glm::scale(back, { 1.4f, 1.6f, 0.05f });
    drw(rfV, rfC, back, fabricDark);

    // ── ৭. সামনের এন্ট্রেন্স/দরজা (The Door Gap) ──
    // সামনের ওয়ালে একটি কালো ত্রিভুজ যা ভেতরের রুমের গভীরতা বোঝাবে
    glm::mat4 door = glm::translate(base, { 0.0f, 0.02f, 2.21f });
    door = glm::scale(door, { 0.65f, 0.95f, 0.02f }); // দরজার সাইজ
    drw(rfV, rfC, door, { 0.005f, 0.005f, 0.008f }); // নিরেট কালো

    // ── ৮. গ্রাউন্ড শিট (Floor) ──
    glm::mat4 floor = glm::translate(base, { 0.0f, 0.018f, 0.0f });
    floor = glm::scale(floor, { 1.15f, 0.02f, 2.05f });
    drw(bxV, bxC, floor, { 0.14f, 0.11f, 0.06f });

    // ── ৯. মাঝখানের ফ্যাব্রিক ব্যান্ড (Horizontal Bands) ──
    for (float zb : {-0.6f, 0.6f}) {
        glm::mat4 band = glm::translate(base, { 0.0f, 0.75f, zb });
        band = glm::scale(band, { 1.42f, 0.08f, 0.22f });
        drw(rfV, rfC, band, fabricDark);
    }

    // ── ১০. দড়ি এবং খুঁটি (Ropes & Pegs) ──
    for (float z : {-0.7f, 0.7f}) {
        for (float xs : {-1.0f, 1.0f}) {
            glm::mat4 rope = glm::translate(base, { xs * 0.60f, 0.52f, z });
            rope = glm::rotate(rope, glm::radians(xs > 0 ? 38.0f : -38.0f), { 0, 0, 1 });
            rope = glm::scale(rope, { 0.012f, 0.80f, 0.012f });
            drw(cyV, cyC, rope, { 0.78f, 0.74f, 0.56f });

            glm::mat4 peg = glm::translate(base, { xs * 1.08f, 0.07f, z });
            peg = glm::rotate(peg, glm::radians(xs > 0 ? 15.0f : -15.0f), { 0, 0, 1 });
            peg = glm::scale(peg, { 0.025f, 0.14f, 0.025f });
            drw(cyV, cyC, peg, poleColor);
        }
    }
}
static void drawBench(glm::vec3 p, float angle)
{
    glm::mat4 base = glm::translate(glm::mat4(1), p);
    base = glm::rotate(base, angle, { 0,1,0 });
    glm::mat4 m = glm::translate(base, { 0,0.46f,0 });
    m = glm::scale(m, { 0.82f,0.06f,0.24f });
    drw(bxV, bxC, m, { 0.30f,0.18f,0.09f });
    m = glm::translate(base, { 0,0.78f,-0.20f });
    m = glm::scale(m, { 0.82f,0.24f,0.05f });
    drw(bxV, bxC, m, { 0.30f,0.18f,0.09f });
    for (int xi : {-1, 1}) for (int zi : {-1, 1}) {
        m = glm::translate(base, { xi * 0.70f,0.22f,zi * 0.16f });
        m = glm::scale(m, { 0.05f,0.22f,0.05f });
        drw(bxV, bxC, m, { 0.24f,0.14f,0.07f });
    }
}
static void drawMoon(glm::vec3 p)
{
    // মূল চাঁদ - একে Full Moon হিসেবে রাখতে শুধু একটি গোলক ড্র হবে
    glm::mat4 m = glm::translate(glm::mat4(1), p);
    m = glm::scale(m, { 2.8f, 2.8f, 2.8f });

    // চাঁদের কালার কিছুটা ব্রাইট (সাদাটে) রাখা হয়েছে
    drw(spV, spC, m, { 0.98f, 0.98f, 0.92f }, 0, true);

    // দ্বিতীয় স্ফিয়ারটি (যা ছায়া তৈরি করত) মুছে ফেলা হয়েছে।
}
static void drawLampPost(glm::vec3 p)
{
    glm::mat4 m = glm::translate(glm::mat4(1), p + glm::vec3(0, 0.08f, 0));
    m = glm::scale(m, { 0.28f,0.08f,0.28f });
    drw(bxV, bxC, m, { 0.20f,0.18f,0.16f });
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 0.35f, 0));
    m = glm::scale(m, { 0.18f,0.28f,0.18f });
    drw(cyV, cyC, m, { 0.18f,0.16f,0.14f });
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 2.8f, 0));
    m = glm::scale(m, { 0.055f,2.5f,0.055f });
    drw(cyV, cyC, m, { 0.16f,0.15f,0.13f });
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 5.0f, 0));
    m = glm::scale(m, { 0.6f,0.05f,0.05f });
    drw(bxV, bxC, m, { 0.16f,0.15f,0.13f });
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 5.15f, 0));
    m = glm::scale(m, { 0.24f,0.28f,0.24f });
    drw(bxV, bxC, m, { 0.14f,0.13f,0.11f });
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 5.48f, 0));
    m = glm::scale(m, { 0.26f,0.08f,0.26f });
    drw(bxV, bxC, m, { 0.14f,0.13f,0.11f });
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 5.15f, 0));
    m = glm::scale(m, { 0.10f,0.14f,0.10f });
    glm::vec3 lampGlow = g_day ? glm::vec3(0.f) : glm::vec3(1.f, 0.95f, 0.65f);
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 5.15f, 0));
    m = glm::scale(m, { 0.38f,0.38f,0.38f });
    drw(spV, spC, m, lampGlow, 0, !g_day);
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 4.72f, 0));
    m = glm::scale(m, { 0.70f,0.55f,0.70f });
    drw(cnV, cnC, m, glm::vec3(1.f, 0.85f, 0.38f) * 0.055f, 0, true);
}
static void drawBarrel(glm::vec3 p)
{
    glm::mat4 m = glm::translate(glm::mat4(1), p + glm::vec3(0, 0.52f, 0));
    m = glm::scale(m, { 0.36f,0.52f,0.36f });
    drw(cyV, cyC, m, { 0.30f,0.19f,0.09f });
    for (float ry : {0.22f, -0.12f}) {
        m = glm::translate(glm::mat4(1), p + glm::vec3(0, 0.52f + ry, 0));
        m = glm::scale(m, { 0.40f,0.05f,0.40f });
        drw(cyV, cyC, m, { 0.50f,0.36f,0.16f });
    }
}
static void drawHalfTimberHouse(glm::vec3 p, float facing, float sx = 1.f, bool flipDoor = false, glm::vec3 wallTint = { 1.f,1.f,1.f })
{
    glm::mat4 b = glm::translate(glm::mat4(1), p);
    b = glm::rotate(b, facing, { 0,1,0 });

    // --- GROUND FLOOR ---
    // Stone Foundation
    glm::mat4 m = glm::translate(b, { 0.0f, 0.2f, 0.0f });
    m = glm::scale(m, { sx * 2.4f, 0.2f, 2.0f });
    drwTex(bxV, bxC, m, texStone, 8);

    // Ground Floor Wall (Plaster)
    m = glm::translate(b, { 0.0f, 1.25f, 0.0f });
    m = glm::scale(m, { sx * 2.3f, 0.85f, 1.9f });
    u("objectColor", wallTint); // Use tint
    drwTex(bxV, bxC, m, texWall, 8);
    u("objectColor", glm::vec3(1.f)); // Reset

    // Vertical Wooden Planks on ground floor (Lower half)
    m = glm::translate(b, { 0.0f, 0.8f, 0.0f });
    m = glm::scale(m, { sx * 2.33f, 0.4f, 1.93f });
    drwTex(bxV, bxC, m, texPlanks, 8);

    // Vertical Beams for Ground floor
    for (float x : {-2.3f, -1.15f, 0.f, 1.15f, 2.3f}) {
        m = glm::translate(b, { x * sx, 1.25f, 1.93f });
        m = glm::scale(m, { 0.08f, 0.85f, 0.05f });
        drwTex(bxV, bxC, m, texWood, 8);
        m = glm::translate(b, { x * sx, 1.25f, -1.93f });
        m = glm::scale(m, { 0.08f, 0.85f, 0.05f });
        drwTex(bxV, bxC, m, texWood, 8);
    }

    // Floor Divider Beam (Jetting out slightly to support overhang)
    m = glm::translate(b, { 0.0f, 2.2f, 0.0f });
    m = glm::scale(m, { sx * 2.6f, 0.12f, 2.3f });
    drwTex(bxV, bxC, m, texWood, 8);

    // --- SECOND FLOOR (OVERHANG) ---
    // Second Floor Wall (Plaster)
    m = glm::translate(b, { 0.0f, 3.4f, 0.0f });
    m = glm::scale(m, { sx * 2.5f, 1.08f, 2.2f }); // Wider than ground floor!
    u("objectColor", wallTint * 0.95f); // Slightly darker for fake AO
    drwTex(bxV, bxC, m, texWall, 8);
    u("objectColor", glm::vec3(1.f));

    // Second Floor Horizontal Beams (Top and Bottom borders)
    m = glm::translate(b, { 0.0f, 2.4f, 0.0f });
    m = glm::scale(m, { sx * 2.55f, 0.08f, 2.25f });
    drwTex(bxV, bxC, m, texWood, 8);
    m = glm::translate(b, { 0.0f, 4.4f, 0.0f });
    m = glm::scale(m, { sx * 2.55f, 0.08f, 2.25f });
    drwTex(bxV, bxC, m, texWood, 8);

    // Second Floor Vertical Beams
    for (float x : {-2.5f, -1.25f, 0.f, 1.25f, 2.5f}) {
        m = glm::translate(b, { x * sx, 3.4f, 2.22f });
        m = glm::scale(m, { 0.08f, 1.0f, 0.05f });
        drwTex(bxV, bxC, m, texWood, 8);
        m = glm::translate(b, { x * sx, 3.4f, -2.22f });
        m = glm::scale(m, { 0.08f, 1.0f, 0.05f });
        drwTex(bxV, bxC, m, texWood, 8);
    }

    // Second Floor Diagonal Beams (X shapes on the far sides)
    for (float rsx : {-1.0f, 1.0f}) {
        float cx = 1.875f * sx * rsx;
        // Front diagonals
        m = glm::translate(b, { cx, 3.4f, 2.22f });
        m = glm::rotate(m, glm::radians(rsx * 35.f), { 0,0,1 });
        m = glm::scale(m, { 0.06f, 1.2f, 0.05f });
        drwTex(bxV, bxC, m, texWood, 8);
        m = glm::translate(b, { cx, 3.4f, 2.22f });
        m = glm::rotate(m, glm::radians(-rsx * 35.f), { 0,0,1 });
        m = glm::scale(m, { 0.06f, 1.2f, 0.05f });
        drwTex(bxV, bxC, m, texWood, 8);
        // Back diagonals
        m = glm::translate(b, { cx, 3.4f, -2.22f });
        m = glm::rotate(m, glm::radians(rsx * 35.f), { 0,0,1 });
        m = glm::scale(m, { 0.06f, 1.2f, 0.05f });
        drwTex(bxV, bxC, m, texWood, 8);
        m = glm::translate(b, { cx, 3.4f, -2.22f });
        m = glm::rotate(m, glm::radians(-rsx * 35.f), { 0,0,1 });
        m = glm::scale(m, { 0.06f, 1.2f, 0.05f });
        drwTex(bxV, bxC, m, texWood, 8);
    }

    // --- ROOF ---
    m = glm::translate(b, { 0.0f, 4.45f, 0.0f });
    m = glm::scale(m, { sx * 2.7f, 1.8f, 2.45f });
    drwTex(rfV, rfC, m, texRoof, 8);

    // Roof Edges (Wooden trim along the triangles)
    for (float rz : {-2.46f, 2.46f}) {
        m = glm::translate(b, { -1.35f * sx, 5.35f, rz });
        m = glm::rotate(m, glm::radians(33.6f), { 0,0,1 });
        m = glm::scale(m, { 1.63f * sx, 0.08f, 0.08f });
        drwTex(bxV, bxC, m, texWood, 8);
        m = glm::translate(b, { 1.35f * sx, 5.35f, rz });
        m = glm::rotate(m, glm::radians(-33.6f), { 0,0,1 });
        m = glm::scale(m, { 1.63f * sx, 0.08f, 0.08f });
        drwTex(bxV, bxC, m, texWood, 8);
    }

    // Chimney
    m = glm::translate(b, { sx * 1.5f, 6.2f, -0.8f });
    m = glm::scale(m, { 0.22f, 1.2f, 0.22f });
    drwTex(bxV, bxC, m, texStone, 8);
    m = glm::translate(b, { sx * 1.5f, 7.4f, -0.8f });
    m = glm::scale(m, { 0.26f, 0.12f, 0.26f });
    drwTex(bxV, bxC, m, texStone, 8);

    // --- DOORS & WINDOWS ---
    float dx = flipDoor ? sx * 1.15f : -sx * 1.15f;

    // Stone Steps (3 steps in front of door)
    for (int si = 0; si < 3; si++) {
        m = glm::translate(b, { dx, 0.08f + si * 0.13f, 1.96f + si * 0.18f });
        m = glm::scale(m, { 0.50f, 0.07f, 0.18f });
        drwTex(bxV, bxC, m, texStone, 8);
    }



    // Door (Ground Floor)
    m = glm::translate(b, { dx, 0.95f, 1.93f });
    m = glm::scale(m, { 0.38f, 0.75f, 0.04f });
    drwTex(bxV, bxC, m, texWood, 7);
    m = glm::translate(b, { dx, 1.7f, 1.93f });
    m = glm::scale(m, { 0.38f, 0.28f, 0.04f });
    drwTex(spV, spC, m, texWindowArch, 7);

    // Windows (Second Floor)
    for (float wx : {-0.6f * sx, 0.6f * sx}) {
        m = glm::translate(b, { wx, 3.4f, 2.22f });
        m = glm::scale(m, { 0.3f, 0.45f, 0.03f });
        drwTex(bxV, bxC, m, texWindows, 7);
        m = glm::translate(b, { wx, 3.85f, 2.22f });
        m = glm::scale(m, { 0.3f, 0.25f, 0.03f });
        drwTex(spV, spC, m, texWindowArch, 7);
    }
}
// ── Simple Wooden Cabin (Image 3 style) ──────────────────────────────────
static void drawWoodenCabin(glm::vec3 p, float facing, float sx = 1.f, glm::vec3 woodTint = { 1.f,1.f,1.f })
{
    glm::mat4 b = glm::translate(glm::mat4(1), p);
    b = glm::rotate(b, facing, { 0, 1, 0 });
    glm::mat4 m;

    // Stone foundation
    m = glm::translate(b, { 0.f, 0.18f, 0.f });
    m = glm::scale(m, { sx * 2.2f, 0.18f, 1.8f });
    drwTex(bxV, bxC, m, texStone, 8);

    // Main wooden walls (planks all around)
    m = glm::translate(b, { 0.f, 1.2f, 0.f });
    m = glm::scale(m, { sx * 2.1f, 1.0f, 1.7f });
    u("objectColor", woodTint);
    drwTex(bxV, bxC, m, texPlanks, 8);
    u("objectColor", glm::vec3(1.f));

    // Corner posts (4 vertical beams)
    for (int xs : {-1, 1}) for (int zs : {-1, 1}) {
        m = glm::translate(b, { xs * sx * 2.05f, 1.2f, zs * 1.68f });
        m = glm::scale(m, { 0.09f, 1.0f, 0.09f });
        drwTex(bxV, bxC, m, texWood, 8);
    }

    // Top beam (horizontal around)
    m = glm::translate(b, { 0.f, 2.28f, 0.f });
    m = glm::scale(m, { sx * 2.2f, 0.09f, 1.82f });
    drwTex(bxV, bxC, m, texWood, 8);

    // Bottom beam
    m = glm::translate(b, { 0.f, 0.38f, 0.f });
    m = glm::scale(m, { sx * 2.2f, 0.09f, 1.82f });
    drwTex(bxV, bxC, m, texWood, 8);

    // Roof (simple, steep)
    m = glm::translate(b, { 0.f, 2.38f, 0.f });
    m = glm::scale(m, { sx * 2.4f, 1.5f, 2.0f });
    drwTex(rfV, rfC, m, texWood, 8);  // dark wood roof like image 3

    // Roof overhang beams (front & back)
    for (float rz : {-2.01f, 2.01f}) {
        for (float rx : {-sx * 1.8f, -sx * 0.6f, sx * 0.6f, sx * 1.8f}) {
            m = glm::translate(b, { rx, 2.8f, rz });
            m = glm::scale(m, { 0.07f, 0.6f, 0.07f });
            drwTex(bxV, bxC, m, texWood, 8);
        }
    }

    // Door (front, ground level)
    m = glm::translate(b, { -sx * 0.9f, 1.1f, 1.72f });
    m = glm::scale(m, { 0.35f, 0.72f, 0.04f });
    drwTex(bxV, bxC, m, texWood, 7);

    // Door frame
    m = glm::translate(b, { -sx * 0.9f, 1.83f, 1.72f });
    m = glm::scale(m, { 0.38f, 0.12f, 0.05f });
    drwTex(bxV, bxC, m, texWood, 8);

    // Small window
    m = glm::translate(b, { sx * 0.7f, 1.4f, 1.72f });
    m = glm::scale(m, { 0.28f, 0.28f, 0.04f });
    drwTex(bxV, bxC, m, texWindows, 7);

    // Steps (2 step staircase in front)
    for (int si = 0; si < 2; si++) {
        m = glm::translate(b, { -sx * 0.9f, 0.10f + si * 0.14f, 1.92f + si * 0.14f });
        m = glm::scale(m, { 0.38f, 0.07f, 0.14f });
        drwTex(bxV, bxC, m, texWood, 8);
    }
}

static void drawBridge(glm::vec3 p)
{
    // Bridge deck — Z দিকে লম্বা (নদী পার করছে X দিকে)
    glm::mat4 m = glm::translate(glm::mat4(1), p + glm::vec3(0, 0.32f, 0));
    m = glm::scale(m, { 9.0f, 0.22f, 3.2f }); // X দিকে লম্বা = নদী পার
    drw(bxV, bxC, m, { 0.44f, 0.40f, 0.36f });

    // Railings (X দিকে)
    for (float zi : {-1.6f, 1.6f}) {
        m = glm::translate(glm::mat4(1), p + glm::vec3(0, 0.72f, zi));
        m = glm::scale(m, { 8.6f, 0.42f, 0.10f });
        drw(bxV, bxC, m, { 0.42f, 0.38f, 0.34f });
    }

    // Railing posts
    for (float xi : {-3.f, -1.f, 1.f, 3.f}) for (float zi : {-1.6f, 1.6f}) {
        m = glm::translate(glm::mat4(1), p + glm::vec3(xi, 0.60f, zi));
        m = glm::scale(m, { 0.14f, 0.60f, 0.14f });
        drw(bxV, bxC, m, { 0.40f, 0.36f, 0.32f });
    }

    // Stone arch pillars (নদীর মধ্যে দাঁড়িয়ে)
    for (float xi : {-2.8f, 0.f, 2.8f}) {
        for (float zi : {-1.1f, 1.1f}) {
            m = glm::translate(glm::mat4(1), p + glm::vec3(xi, -0.40f, zi));
            m = glm::scale(m, { 0.20f, 0.80f, 0.20f });
            drw(cyV, cyC, m, { 0.42f, 0.38f, 0.34f });
        }
        // Arch crossbar
        m = glm::translate(glm::mat4(1), p + glm::vec3(xi, 0.05f, 0));
        m = glm::scale(m, { 0.24f, 0.12f, 2.6f });
        drw(bxV, bxC, m, { 0.40f, 0.36f, 0.32f });
    }
}
static void drawCoastalTower(glm::vec3 p)
{
    glm::mat4 m = glm::translate(glm::mat4(1), p + glm::vec3(0, 5.f, 0));
    m = glm::scale(m, { 2.5f,5.0f,2.5f });
    drw(bxV, bxC, m, { 0.42f,0.38f,0.34f });
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 10.1f, 0));
    m = glm::scale(m, { 2.8f,0.18f,2.8f });
    drw(bxV, bxC, m, { 0.40f,0.36f,0.32f });
    for (int side = 0; side < 4; side++) {
        float sa = side * PI / 2.f;
        float cx = cosf(sa) * 2.55f, cz = sinf(sa) * 2.55f;
        float tx = -sinf(sa), tz = cosf(sa);
        for (int k = -1; k <= 1; k++) {
            m = glm::translate(glm::mat4(1), p + glm::vec3(cx + tx * k * 1.15f, 10.85f, cz + tz * k * 1.15f));
            m = glm::scale(m, { 0.50f,0.80f,0.50f });
            drw(bxV, bxC, m, { 0.42f,0.38f,0.34f });
        }
    }
    for (int xs : {-1, 1}) for (int zs : {-1, 1}) {
        m = glm::translate(glm::mat4(1), p + glm::vec3(xs * 2.55f, 10.85f, zs * 2.55f));
        m = glm::scale(m, { 0.50f,0.80f,0.50f });
        drw(bxV, bxC, m, { 0.42f,0.38f,0.34f });
    }
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 1.1f, 2.51f));
    m = glm::scale(m, { 0.50f,1.1f,0.08f });
    drw(bxV, bxC, m, { 0.20f,0.12f,0.06f });
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 2.25f, 2.51f));
    m = glm::scale(m, { 0.50f,0.50f,0.08f });
    drw(spV, spC, m, { 0.20f,0.12f,0.06f });
    glm::vec3 wc = g_day ? glm::vec3(0.65f, 0.75f, 0.95f) : glm::vec3(1.0f, 0.80f, 0.25f);
    for (float wy : {3.5f, 6.5f, 9.0f}) {
        m = glm::translate(glm::mat4(1), p + glm::vec3(0, wy, 2.52f));
        m = glm::scale(m, { 0.14f,0.42f,0.04f });
        drw(bxV, bxC, m, wc, 0, true);
    }
    m = glm::translate(glm::mat4(1), p + glm::vec3(2.52f, 6.5f, 0));
    m = glm::scale(m, { 0.04f,0.42f,0.14f });
    drw(bxV, bxC, m, wc, 0, true);
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 0.18f, 0));
    m = glm::scale(m, { 2.8f,0.18f,2.8f });
    drw(bxV, bxC, m, { 0.38f,0.34f,0.30f });
}
static void drawBarn(glm::vec3 p, float doorAngle)
{
    glm::mat4 m;

    // ── 1. EXTERIOR SHELL ──────────────────────────────────────────────────
    // Back wall
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 3.2f, -5.5f));
    m = glm::scale(m, { 5.2f, 3.2f, 0.20f });
    drwTex(bxV, bxC, m, texBarnWall, 8);
    // Side walls
    for (int xi : {-1, 1}) {
        m = glm::translate(glm::mat4(1), p + glm::vec3(xi * 5.2f, 3.2f, 0));
        m = glm::scale(m, { 0.20f, 3.2f, 5.5f });
        drwTex(bxV, bxC, m, texBarnWall, 8);
    }
    // Front Portal (Left and Right halves)
    for (int xi : {-1, 1}) {
        m = glm::translate(glm::mat4(1), p + glm::vec3(xi * 4.3f, 3.2f, 5.5f));
        m = glm::scale(m, { 0.9f, 3.2f, 0.20f });
        drwTex(bxV, bxC, m, texBarnWall, 8);
    }

    // Interactive Door (Pivot at left side of door opening)
    glm::mat4 door = glm::translate(glm::mat4(1), p + glm::vec3(-3.4f, 3.2f, 5.55f)); // Pivot
    door = glm::rotate(door, glm::radians(doorAngle), { 0, 1, 0 });
    door = glm::translate(door, { 3.4f, 0, 0 }); // Offset to center
    door = glm::scale(door, { 3.4f, 3.2f, 0.15f });
    drwTex(bxV, bxC, door, texBarnDoor, 8);

    // Header above door
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 5.8f, 5.5f));
    m = glm::scale(m, { 5.2f, 0.55f, 0.20f });
    drwTex(bxV, bxC, m, texWood, 8);

    // ── 2. INTERIOR SKELETON ───────────────────────────────────────────
    // Floor
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 0.05f, 0));
    m = glm::scale(m, { 5.1f, 0.05f, 5.4f });
    drwTex(bxV, bxC, m, texBarnFloor, 8);

    // Vertical Pillars
    for (float xi : {-4.8f, 4.8f}) {
        for (float zi : {-4.5f, 0.f, 4.5f}) {
            m = glm::translate(glm::mat4(1), p + glm::vec3(xi, 3.2f, zi));
            m = glm::scale(m, { 0.15f, 3.2f, 0.15f });
            drwTex(cyV, cyC, m, texBarnInner, 8);
        }
    }
    // Horizontal support beams
    for (float hy : {2.5f, 5.5f}) {
        m = glm::translate(glm::mat4(1), p + glm::vec3(0, hy, -5.3f));
        m = glm::scale(m, { 5.1f, 0.12f, 0.12f });
        drwTex(bxV, bxC, m, texBarnInner, 8);
    }

    // ── 3. ROOF ──────────────────────────────────────────────────────────
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 6.8f, 0));
    m = glm::scale(m, { 5.8f, 2.8f, 6.0f });
    drwTex(rfV, rfC, m, texBarnRoof, 8);

    // Some hay bales
    for (int i = 0; i < 3; i++) {
        m = glm::translate(glm::mat4(1), p + glm::vec3(-3.5f, 0.55f + i * 0.85f, -3.8f));
        m = glm::scale(m, { 0.85f, 0.52f, 0.85f });
        drw(bxV, bxC, m, { 0.75f, 0.65f, 0.20f });
    }
}
static void drawPillar(glm::vec3 p, float h, bool broken = false)
{
    float ph = broken ? h * 0.6f : h;
    glm::mat4 m = glm::translate(glm::mat4(1), p + glm::vec3(0, ph, 0));
    m = glm::scale(m, { 0.4f,ph,0.4f });
    drw(cyV, cyC, m, { 0.44f,0.41f,0.38f });
    if (!broken) {
        m = glm::translate(glm::mat4(1), p + glm::vec3(0, h * 2.05f, 0));
        m = glm::scale(m, { 0.52f,0.12f,0.52f });
        drw(bxV, bxC, m, { 0.42f,0.38f,0.35f });
    }
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 0.12f, 0));
    m = glm::scale(m, { 0.52f,0.12f,0.52f });
    drw(bxV, bxC, m, { 0.42f,0.38f,0.35f });
}
static void drawWallSegment(glm::vec3 p, float len, float rot = 0.f)
{
    glm::mat4 m = glm::translate(glm::mat4(1), p + glm::vec3(0, 0.6f, 0));
    m = glm::rotate(m, rot, { 0,1,0 });
    m = glm::scale(m, { len,0.60f,0.28f });
    drw(bxV, bxC, m, { 0.44f,0.41f,0.37f });
    m = glm::translate(glm::mat4(1), p + glm::vec3(0, 1.1f, 0));
    m = glm::rotate(m, rot, { 0,1,0 });
    m = glm::scale(m, { len * 0.9f,0.15f,0.30f });
    drw(bxV, bxC, m, { 0.42f,0.38f,0.34f });
}

// ── Beautification Helpers ──────────────────────────────────────────────────
static void drawFence(glm::vec3 p, float angle, float totalLen)
{
    glm::mat4 b = glm::translate(glm::mat4(1), p);
    b = glm::rotate(b, angle, { 0, 1, 0 });

    int posts = (int)(totalLen / 1.8f) + 1;
    if (posts < 2) posts = 2;
    float step = totalLen / (posts - 1);
    glm::vec3 woodCol = { 0.32f, 0.25f, 0.18f };

    for (int i = 0; i < posts; i++) {
        float x = -totalLen * 0.5f + i * step;
        // Vertical post
        glm::mat4 m = glm::translate(b, { x, 0.45f, 0.f });
        m = glm::scale(m, { 0.08f, 0.45f, 0.08f });
        drw(cyV, cyC, m, woodCol);

        // Horizontal rails
        if (i < posts - 1) {
            float rx = x + step * 0.5f;
            for (float ry : {0.35f, 0.65f}) {
                glm::mat4 r = glm::translate(b, { rx, ry, 0.f });
                r = glm::scale(r, { step * 0.5f, 0.035f, 0.035f });
                drw(bxV, bxC, r, woodCol * 0.85f);
            }
        }
    }
}
static void drawStonePath(glm::vec3 from, glm::vec3 to, float width = 1.0f)
{
    glm::vec3 mid = (from + to) * 0.5f;
    float len = glm::length(to - from);
    float angle = atan2f(to.x - from.x, to.z - from.z);

    glm::mat4 m = glm::translate(glm::mat4(1), { mid.x, 0.015f, mid.z });
    m = glm::rotate(m, angle, { 0, 1, 0 });
    m = glm::scale(m, { width * 0.5f, 0.012f, len * 0.5f });
    drwTex(bxV, bxC, m, texStone, 8);
}

// =============================================================================
// ── Castle ────────────────────────────────────────────────────────────────────
// =============================================================================
static void drawCastle(glm::vec3 pos)
{
    glm::mat4 b = glm::translate(glm::mat4(1), pos);
    glm::mat4 m;

    // ── Color fallback (used when no texture) ─────────────────────────────
    glm::vec3 stoneCol = { 0.50f, 0.46f, 0.40f };
    glm::vec3 roofCol = { 0.20f, 0.28f, 0.18f }; // dark slate green
    glm::vec3 gateCol = { 0.22f, 0.18f, 0.12f };

    // ── Half-sizes of the courtyard ────────────────────────────────────────
    // Outer keep: 14 wide x 14 deep.  Towers at each corner, radius 1.8
    const float TW = 7.0f;   // half-width of keep
    const float TH = 12.0f;  // tower height half (cylinder goes +-TH)
    const float TR = 1.8f;   // tower radius
    const float WH = 8.0f;   // curtain-wall half-height
    const float WT = 0.7f;   // curtain-wall half-thickness

    // ══════════════════════════════════════════════════════════════════════
    // 1. FOUR CORNER TOWERS (cylinders)
    // ══════════════════════════════════════════════════════════════════════
    for (int xs : {-1, 1})
        for (int zs : {-1, 1})
        {
            // Tower shaft
            m = glm::translate(b, { xs * TW, TH, zs * TW });
            m = glm::scale(m, { TR, TH, TR });
            drwTex(cyV, cyC, m, texCastle, 8); // Changed from 0 to 8 to enable triplanar texture

            // Battlements ring on top of each tower
            for (int k = 0; k < 8; k++)
            {
                float ang = k * (PI / 4.f);
                float bx2 = cosf(ang) * (TR * 0.85f);
                float bz2 = sinf(ang) * (TR * 0.85f);
                m = glm::translate(b, { xs * TW + bx2, TH * 2.f + 0.6f, zs * TW + bz2 });
                m = glm::scale(m, { 0.28f, 0.55f, 0.28f });
                drwTex(bxV, bxC, m, texCastle, 8); // Changed from 0 to 8
            }

            // Cone roof on each tower
            m = glm::translate(b, { xs * TW, TH * 2.f + 0.4f, zs * TW });
            m = glm::scale(m, { TR * 1.25f, TR * 2.5f, TR * 1.25f });
            drw(cnV, cnC, m, roofCol);
        }

    // ══════════════════════════════════════════════════════════════════════
    // 2. CURTAIN WALLS (boxes connecting the towers)
    // ══════════════════════════════════════════════════════════════════════
    // Front wall  (+Z side)  — has the gate gap
    //   Left half
    m = glm::translate(b, { -(TW * 0.5f + TR * 0.5f), WH, TW });
    m = glm::scale(m, { TW * 0.5f - TR - 1.5f, WH, WT });
    drwTex(bxV, bxC, m, texCastle, 8);
    //   Right half
    m = glm::translate(b, { (TW * 0.5f + TR * 0.5f), WH, TW });
    m = glm::scale(m, { TW * 0.5f - TR - 1.5f, WH, WT });
    drwTex(bxV, bxC, m, texCastle, 8);

    // Back wall   (-Z side)
    m = glm::translate(b, { 0.f, WH, -TW });
    m = glm::scale(m, { TW - TR, WH, WT });
    drwTex(bxV, bxC, m, texCastle, 8);

    // Left wall   (-X side)
    m = glm::translate(b, { -TW, WH, 0.f });
    m = glm::scale(m, { WT, WH, TW - TR });
    drwTex(bxV, bxC, m, texCastle, 8);

    // Right wall  (+X side)
    m = glm::translate(b, { TW, WH, 0.f });
    m = glm::scale(m, { WT, WH, TW - TR });
    drwTex(bxV, bxC, m, texCastle, 8);

    // ══════════════════════════════════════════════════════════════════════
    // 3. WALL CRENELLATIONS (merlons along the top of each curtain wall)
    // ══════════════════════════════════════════════════════════════════════
    // Front wall merlons (left & right halves, skip gate area)
    for (int s : {-1, 1})
        for (int k = 0; k < 4; k++)
        {
            float wx = s * (TR + 1.6f + k * 1.4f);
            m = glm::translate(b, { wx, WH * 2.f + 0.45f, TW });
            m = glm::scale(m, { 0.35f, 0.45f, 0.35f });
            drwTex(bxV, bxC, m, texCastle, 8);
        }
    // Back wall merlons
    for (int k = -3; k <= 3; k++)
    {
        m = glm::translate(b, { k * 1.6f, WH * 2.f + 0.45f, -TW });
        m = glm::scale(m, { 0.35f, 0.45f, 0.35f });
        drwTex(bxV, bxC, m, texCastle, 8);
    }
    // Side wall merlons
    for (int sx : {-1, 1})
        for (int k = -3; k <= 3; k++)
        {
            m = glm::translate(b, { (float)sx * TW, WH * 2.f + 0.45f, k * 1.6f });
            m = glm::scale(m, { 0.35f, 0.45f, 0.35f });
            drwTex(bxV, bxC, m, texCastle, 8);
        }

    // ══════════════════════════════════════════════════════════════════════
    // 4. GATEHOUSE (front centre, flanking the opening)
    // ══════════════════════════════════════════════════════════════════════
    // Gate side pillars
    for (int xs : {-1, 1})
    {
        m = glm::translate(b, { xs * 1.5f, WH * 0.7f, TW });
        m = glm::scale(m, { 0.55f, WH * 0.7f, WT * 1.8f });
        drwTex(bxV, bxC, m, texCastle, 8);
    }
    // Gate lintel (crossbar above the arch)
    m = glm::translate(b, { 0.f, WH * 1.35f, TW });
    m = glm::scale(m, { 1.6f, 0.35f, WT * 1.8f });
    drwTex(bxV, bxC, m, texCastle, 8);
    // Gate archway (dark wood door)
    m = glm::translate(b, { 0.f, WH * 0.52f, TW + WT * 0.5f });
    m = glm::scale(m, { 1.05f, WH * 0.52f, 0.12f });
    drw(bxV, bxC, m, gateCol);
    // Portcullis bars (vertical)
    for (int k = -1; k <= 1; k++)
    {
        m = glm::translate(b, { k * 0.36f, WH * 0.52f, TW + WT * 0.5f + 0.05f });
        m = glm::scale(m, { 0.06f, WH * 0.52f, 0.06f });
        drw(bxV, bxC, m, { 0.25f, 0.22f, 0.18f });
    }

    // ══════════════════════════════════════════════════════════════════════
    // 5. INNER KEEP (central tall tower)
    // ══════════════════════════════════════════════════════════════════════
    const float KH = 16.f; // keep half-height
    const float KW = 2.8f; // keep half-width
    m = glm::translate(b, { 0.f, KH, 0.f });
    m = glm::scale(m, { KW, KH, KW });
    drwTex(bxV, bxC, m, texCastle, 8);

    // Keep battlements
    for (int xs : {-1, 0, 1})
        for (int zs : {-1, 0, 1})
        {
            if (xs == 0 && zs == 0) continue;
            m = glm::translate(b, { xs * (KW - 0.4f), KH * 2.f + 0.5f, zs * (KW - 0.4f) });
            m = glm::scale(m, { 0.38f, 0.55f, 0.38f });
            drwTex(bxV, bxC, m, texCastle, 8);
        }

    // Keep cone roof
    m = glm::translate(b, { 0.f, KH * 2.f + 0.3f, 0.f });
    m = glm::scale(m, { KW * 1.3f, KW * 3.5f, KW * 1.3f });
    drw(cnV, cnC, m, roofCol);

    // Keep windows (glowing)
    glm::vec3 wc = g_day ? glm::vec3(0.65f, 0.75f, 0.95f)
        : glm::vec3(1.0f, 0.85f, 0.30f);
    for (float wy : { 6.f, 14.f, 22.f })
        for (int xs : {-1, 1})
        {
            m = glm::translate(b, { xs * (KW + 0.01f), wy, 0.f });
            m = glm::scale(m, { 0.04f, 0.50f, 0.18f });
            drw(bxV, bxC, m, wc, 0, !g_day);
        }

    // ══════════════════════════════════════════════════════════════════════
    // 6. COURTYARD GROUND
    // ══════════════════════════════════════════════════════════════════════
    m = glm::translate(b, { 0.f, 0.02f, 0.f });
    m = glm::scale(m, { TW - TR, 0.02f, TW - TR });
    drwTex(bxV, bxC, m, texCastle, 8);
}

// =============================================================================
// ── Rain ──────────────────────────────────────────────────────────────────────
// =============================================================================
static const int N_DROPS = 750;
struct Drop { float dx, dz, y, spd; };
static Drop   g_drops[N_DROPS];
static GLuint g_rainVAO, g_rainVBO;

static void initRain()
{
    srand(2024);
    for (auto& d : g_drops) {
        d.dx = ((rand() % 560) - 280) / 10.f;
        d.dz = ((rand() % 560) - 280) / 10.f;
        d.y = ((rand() % 230)) / 10.f;
        d.spd = 8.f + (rand() % 80) / 10.f;
    }
    glGenVertexArrays(1, &g_rainVAO);
    glGenBuffers(1, &g_rainVBO);
    glBindVertexArray(g_rainVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_rainVBO);
    // ← 2 vertices per drop, each vertex = pos(3) + normal(3) = 6 floats
    glBufferData(GL_ARRAY_BUFFER, N_DROPS * 2 * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}
static void updateRain(float dt)
{
    for (auto& d : g_drops) {
        d.y -= d.spd * dt;
        if (d.y < -0.4f) {
            d.y = 18.f + (rand() % 50) / 10.f;
            d.dx = ((rand() % 560) - 280) / 10.f;
            d.dz = ((rand() % 560) - 280) / 10.f;
        }
    }
    static std::vector<float> rv;
    rv.clear(); rv.reserve(N_DROPS * 12);
    float nx[3] = { 0,1,0 };
    for (auto& d : g_drops) {
        float wx = cam.pos.x + d.dx, wz = cam.pos.z + d.dz;
        rv.push_back(wx);       rv.push_back(d.y + 0.28f); rv.push_back(wz);
        rv.push_back(nx[0]); rv.push_back(nx[1]); rv.push_back(nx[2]);
        rv.push_back(wx + 0.05f); rv.push_back(d.y);       rv.push_back(wz);
        rv.push_back(nx[0]); rv.push_back(nx[1]); rv.push_back(nx[2]);
    }
    glBindBuffer(GL_ARRAY_BUFFER, g_rainVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, rv.size() * sizeof(float), rv.data());
}
static void drawRainParticles()
{
    if (!g_rain) return;
    // ← Restore view/projection before drawing rain (compass block changes them)
    int fbW, fbH;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &fbW, &fbH);
    extern Camera cam;
    glm::mat4 view = cam.view();
    glm::mat4 proj = glm::perspective(glm::radians(cam.fov), (float)fbW / (float)fbH, 0.05f, 280.f);
    u("projection", proj);
    u("view", view);
    glEnable(GL_DEPTH_TEST);

    u("matType", 0); u("isEmissive", 1);
    u("objectColor", glm::vec3(0.60f, 0.72f, 0.88f));
    u("model", glm::mat4(1));
    glBindVertexArray(g_rainVAO);
    glDrawArrays(GL_LINES, 0, N_DROPS * 2);
}

// =============================================================================
// ── Road helper ───────────────────────────────────────────────────────────────
// Draws a single cobblestone road segment between two world-space points
// =============================================================================
static void drawRoadSegment(glm::vec3 from, glm::vec3 to, float width = 3.0f)
{
    glm::vec3 mid = (from + to) * 0.5f;
    float len = glm::length(to - from);
    float angle = atan2f(to.x - from.x, to.z - from.z);

    glm::mat4 m = glm::translate(glm::mat4(1), { mid.x, 0.014f, mid.z });
    m = glm::rotate(m, angle, { 0, 1, 0 });
    m = glm::scale(m, { width, 0.018f, len * 0.5f });
    drw(bxV, bxC, m, {}, 2);
}
// =============================================================================
// ── Scenes – each now takes a glm::vec3 offset ────────────────────────────────
// =============================================================================

static void drawSkyDome()
{
    glDepthMask(GL_FALSE); 
    u("isBackground", 1);
    
    // Use a huge sphere around the camera, negative X scale flips UVs and winding order
    // We drop the dome slightly so the equator (where distant mountains are) sits exactly on the horizon line
    glm::mat4 m = glm::translate(glm::mat4(1), cam.pos + glm::vec3(0, -10.f, 0));
    m = glm::scale(m, { -240.f, 240.f, 240.f });
    u("model", m);
    
    // Smooth blending color with day/night and rain states
    glm::vec3 baseCol = g_day ? glm::vec3(1.0f) : glm::vec3(0.08f, 0.10f, 0.20f);
    if(g_rain) baseCol *= 0.45f;
    
    u("objectColor", baseCol);
    u("matType", 7); // Using basic texture matType
    u("isEmissive", 1); // Rendering self-illuminated to not have awkward normals
    
    if (texHorizon > 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texHorizon);
        u("tex0", 0);
    }
    
    glBindVertexArray(sdV);
    glDrawArrays(GL_TRIANGLES, 0, sdC);
    
    u("isBackground", 0);
    glDepthMask(GL_TRUE);
}

static void drawRoadEntrance()
{
    drawPillar({ -2.0f,0,7.f }, 2.2f, false);
    drawPillar({ 2.0f,0,7.f }, 2.2f, false);
    glm::mat4 m = glm::translate(glm::mat4(1), { 0,5.0f,7.f });
    m = glm::scale(m, { 2.5f,0.18f,0.25f });
    drw(bxV, bxC, m, { 0.44f,0.40f,0.36f });
    drawTree({ -4.f,0,6.5f }, 1.0f, 90);
    drawTree({ 4.f,0,6.5f }, 1.0f, 91);
    drawTree({ -5.5f,0,4.f }, 1.1f, 92);
    drawTree({ 5.5f,0,4.f }, 1.1f, 93);
}

static void drawRoadEnd(glm::vec3 off)
{
    float z = -205.f + off.z;
    drawWallSegment({ -5.f + off.x,0,z }, 4.0f, 0.0f);
    drawWallSegment({ 5.f + off.x,0,z }, 4.0f, 0.0f);
    drawPillar({ -9.f + off.x,0,z }, 2.0f, true);
    drawPillar({ 9.f + off.x,0,z }, 2.0f, true);
    for (int i = 0; i < 5; i++) {
        drawTree({ -6.f - i * 2.2f + off.x,0,z - 1.f + i * 0.5f }, 1.1f + i * 0.06f, 95 + i);
        drawTree({ 6.f + i * 2.2f + off.x,0,z - 1.f + i * 0.5f }, 1.1f + i * 0.06f, 100 + i);
    }
    for (int i = 0; i < 8; i++)
        drawTree({ -14.f + i * 4.f + off.x,0,z - 4.f }, 1.3f + i * 0.04f, 105 + i);
}

static void drawSceneForestEntrance(glm::vec3 off)
{
    float fx = off.x, fz = off.z;

    // ── 1. Entrances & Ruins ──
    drawPillar({ -3.5f + fx, 0, 1.f + fz }, 2.5f, false);
    drawPillar({ 3.5f + fx, 0, 1.f + fz }, 2.5f, false);
    drawWallSegment({ -6.f + fx, 0, -3.f + fz }, 3.0f, 0.22f);
    drawWallSegment({ 5.5f + fx, 0, -5.f + fz }, 2.5f, -0.15f);
    drawPillar({ -8.f + fx, 0, -4.f + fz }, 2.0f, true);
    drawPillar({ 7.5f + fx, 0, -6.f + fz }, 2.2f, true);
    drawPillar({ -5.f + fx, 0, -10.f + fz }, 1.8f, true);

    // ── 2. Mushroom Park ──
    // Stone path into the forest park
    drawStonePath({ fx, 0, 1.f + fz }, { fx - 4.f, 0, -8.f + fz }, 1.5f);

    // Benches (Stumps)
    drawStump({ fx - 6.f, 0, -8.f + fz }, 0.45f, 0.48f);
    drawStump({ fx - 3.5f, 0, -10.5f + fz }, 0.38f, 0.42f);

    // Fairy Ring of mushrooms
    for (int i = 0; i < 10; i++) {
        float a = i * (PI * 2.f / 10.f);
        float r = 2.2f;
        glm::vec3 mPos = { fx - 5.f + cosf(a) * r, 0.f, -12.f + fz + sinf(a) * r };
        drawMushroom(mPos, 0.72f, (i % 2 == 0) ? glm::vec3(1, 0.2, 0.2) : glm::vec3(0.2, 0.6, 1));
    }

    // Dense scattered mushrooms
    for (int i = 0; i < 15; i++) {
        // Use a simple deterministic "hash" for scattering
        float mx = glm::fract(sin(float(i) * 12.9898f) * 43758.5453f) * 12.f - 6.f;
        float mz = glm::fract(sin(float(i) * 78.233f) * 43758.5453f) * 12.f - 12.f;

        // রাস্তার ওপরে কোনো মাশরুম ড্র না করা (x axis is road)
        if (abs(mx) < 4.0f) continue;

        drawMushroom({ fx + mx, 0.f, fz + mz }, 0.65f, { 0.4f, 0.1f, 0.9f });
    }

    // ── 3. Trees (Groves) ──
    for (int i = 0; i < 6; i++) {
        drawTree({ -5.f - i * 1.8f + fx, 0.f, -3.f - i * 2.2f + fz }, 1.2f + i * 0.05f, i + 20);
        drawTree({ 5.f + i * 1.8f + fx, 0.f, -3.f - i * 2.2f + fz }, 1.2f + i * 0.05f, i + 30);
    }
}

static void drawSceneCampsite(float t, glm::vec3 off)
{
    float z = -37.f + off.z;
    drawFire(FIRE_P, t);   // FIRE_P already incorporates OFF_CAMP
    drawStump({ -4.2f + off.x,0.f,z + 1.5f }, 0.30f, 0.32f);
    drawStump({ -2.5f + off.x,0.f,z + 2.2f }, 0.25f, 0.26f);
    drawStump({ -1.8f + off.x,0.f,z + 0.0f }, 0.28f, 0.30f);
    drawStump({ 0.0f + off.x,0.f,z - 1.0f }, 0.22f, 0.23f);
    drawTent({ -5.8f + off.x, 0.f, z + 3.0f }, glm::radians(90.0f));
    drawBench({ 2.2f + off.x,0.f,z + 1.5f }, 0.85f);
    drawBarrel({ -0.8f + off.x,0.f,z - 2.5f });
    drawBarrel({ -1.6f + off.x,0.f,z - 2.8f });
    drawMushroom({ -5.2f + off.x,0,z + 0.5f }, 0.92f, { 0.08f,0.45f,1.f });
    drawMushroom({ -4.4f + off.x,0,z - 2.0f }, 0.74f, { 0.08f,0.45f,1.f });
    drawMushroom({ 1.2f + off.x,0,z + 6.8f }, 0.82f, { 0.62f,0.08f,1.f }); // Pushed away from road
    drawMushroom({ 7.2f + off.x,0,z + 4.5f }, 0.76f, { 0.08f,0.45f,1.f }); // Pushed away from road
    drawMushroom({ -7.0f + off.x,0,z - 3.5f }, 0.68f, { 0.05f,0.8f,0.8f });
    drawMushroom({ 7.0f + off.x,0,z - 4.0f }, 0.88f, { 0.62f,0.08f,1.f });  // Pushed away from road
    drawMushroom({ -2.5f + off.x,0,z - 5.0f }, 0.70f, { 0.08f,0.45f,1.f }); // Pushed toward grass
    drawTree({ -9.0f + off.x,0,z - 5.0f }, 1.30f, 40);
    drawTree({ -6.0f + off.x,0,z - 8.5f }, 1.15f, 41);
    drawTree({ -1.5f + off.x,0,z - 9.5f }, 1.22f, 42);
    drawTree({ 3.0f + off.x,0,z - 9.0f }, 1.38f, 43);
    drawTree({ 8.0f + off.x,0,z - 6.5f }, 1.20f, 44);
    drawTree({ 9.8f + off.x,0,z - 1.0f }, 1.32f, 45);
    drawTree({ 9.2f + off.x,0,z + 5.0f }, 1.10f, 46);
    drawTree({ -8.5f + off.x,0,z + 4.0f }, 1.28f, 47);
    drawTree({ -10.f + off.x,0,z + 1.0f }, 1.18f, 48);

    // Relocated frogs near campsite
    for (int i = 0; i < 3; i++) {
        drawFrog({ -5.0f + i * 2.5f + off.x, 0, z + 6.0f }, 0.85f, (float)glfwGetTime(), i);
    }
}

static void drawSceneVillage(glm::vec3 off)
{
    float z = -77.f + off.z;
    const float RX = off.x; // Road X

    // ── 1. Castle — dominant landmark ──
    drawCastle({ RX + 2.f, 0.f, z - 30.f });

    // Castle gate lamps
    drawLampPost({ RX - 2.5f, 0.f, z - 22.f });
    drawLampPost({ RX + 6.5f, 0.f, z - 22.f });

    // ── 2. Houses (The Settlement) ──
    // A: Wealthy Manor (Large Half-Timber)
    glm::vec3 hPosA = { RX - 10.f, 0.f, z - 5.f };
    drawHalfTimberHouse(hPosA, glm::radians(-15.f), 1.45f, false, { 1.0f, 0.98f, 0.92f });
    drawStonePath({ RX - 1.7f, 0, z - 5.f }, hPosA + glm::vec3(2.5f, 0, 0), 1.2f);
    // Boundary fences for manor
    drawFence(hPosA + glm::vec3(0, 0, 4.5f), 0.f, 8.0f);
    drawFence(hPosA + glm::vec3(-4.5f, 0, 0), glm::radians(90.f), 9.0f);
    drawFence(hPosA + glm::vec3(4.5f, 0, 0), glm::radians(90.f), 9.0f);

    // B: Standard Townhouse (Half-Timber)
    glm::vec3 hPosB = { RX + 14.f, 0.f, z + 8.f };
    drawHalfTimberHouse(hPosB, glm::radians(160.f), 1.1f, true, { 0.95f, 0.95f, 1.0f });
    drawStonePath({ RX + 1.7f, 0, z + 8.f }, hPosB + glm::vec3(-2.8f, 0, -1.8f), 1.0f);
    // Boundary fences for farmhouse
    drawFence(hPosB + glm::vec3(0, 0, -3.8f), 0.f, 7.5f);
    drawFence(hPosB + glm::vec3(-3.8f, 0, 0), glm::radians(90.f), 7.5f);

    // C: Rustic Cabin (Wooden)
    glm::vec3 hPosC = { RX - 12.f, 0.f, z + 18.f };
    drawWoodenCabin(hPosC, glm::radians(45.f), 1.05f, { 0.45f, 0.35f, 0.25f });
    drawStonePath({ RX - 1.7f, 0, z + 12.f }, hPosC + glm::vec3(2.0f, 0, -1.0f), 0.8f);
    // Simple yard fence
    drawFence(hPosC + glm::vec3(-1.0f, 0, 3.2f), glm::radians(25.f), 6.5f);

    // D: Village Barn (Large utility)
    glm::vec3 hPosD = { RX + 18.f, 0.f, z - 15.f };
    drawBarn(hPosD, g_barnDoorFactor * -90.f);
    drawFence(hPosD + glm::vec3(0, 0, 6.5f), 0.f, 12.0f);

    // Cute frogs near the barn (DELETED - MOVED TO CAMPSITE)


    // Human NPC walking near barn
    drawHuman(hPosD + glm::vec3(-4.5f, 0, 15.0f), (float)glfwGetTime());

    // ── 3. Environmental Scattering (Micro-scenes) ──
    // Well/Gathering area near house A
    drawStump(hPosA + glm::vec3(5.f, 0, 2.f), 0.35f, 0.38f);
    drawBarrel(hPosA + glm::vec3(4.5f, 0, 3.2f));
    drawMushroom(hPosA + glm::vec3(3.f, 0, 4.f), 0.82f, { 0.1f, 0.5f, 1.0f });

    // Messy utility area near the barn
    drawBarrel({ RX + 12.f, 0, z - 12.f });
    drawBarrel({ RX + 13.2f, 0, z - 11.5f });
    drawStump({ RX + 15.f, 0, z - 18.f }, 0.4f, 0.42f);

    // ── 4. Natural Elements (Groves) ──
    // Western Grove
    drawTree({ RX - 14.f, 0, z - 20.f }, 1.3f, 50);
    drawTree({ RX - 18.f, 0, z - 15.f }, 1.1f, 51);
    drawTree({ RX - 16.f, 0, z - 25.f }, 1.2f, 52);

    // Eastern Grove
    drawTree({ RX + 22.f, 0, z + 5.f }, 1.25f, 53);
    drawTree({ RX + 20.f, 0, z + 12.f }, 1.05f, 54);
    drawTree({ RX + 24.f, 0, z + 2.f }, 1.15f, 55);

    // Castle backdrop trees
    drawTree({ RX - 8.f,  0, z - 45.f }, 1.1f, 56);
    drawTree({ RX + 12.f, 0, z - 45.f }, 1.2f, 57);
}
static void drawSceneBarn(glm::vec3 off)
{
    float z = -113.f + off.z;
    drawBarn({ 0.f + off.x,0.f,z }, 0.f); // Exterior only barn static
    drawTree({ -12.f + off.x,0,z - 5.f }, 1.2f, 60);
    drawTree({ 12.f + off.x,0,z - 5.f }, 1.15f, 61);
    drawTree({ -10.f + off.x,0,z + 8.f }, 1.0f, 62);
    drawTree({ 11.f + off.x,0,z + 9.f }, 1.1f, 63);
    drawStump({ -8.f + off.x,0,z + 2.f }, 0.35f, 0.38f);
    drawStump({ 8.f + off.x,0,z - 2.f }, 0.28f, 0.30f);
    drawBarrel({ 7.f + off.x,0,z + 3.f });
    drawMushroom({ -7.5f + off.x,0,z - 1.f }, 0.8f, { 0.62f,0.08f,1.f });
    drawMushroom({ 6.5f + off.x,0,z + 4.f }, 0.72f, { 0.08f,0.45f,1.f });
    for (int i = 0; i < 3; i++) {
        glm::mat4 m = glm::translate(glm::mat4(1), { -6.5f + i * 0.2f + off.x,0.55f + i * 0.2f,z + 2.f + i * 0.1f });
        m = glm::scale(m, { 0.9f,0.5f,0.7f });
        drw(bxV, bxC, m, { 0.68f,0.52f,0.16f });
    }
}

static void drawSceneCoastalTower(glm::vec3 off)
{
    float z = -148.f + off.z;
    drawCoastalTower({ 1.f + off.x,0.f,z });
    for (int i = 0; i < 8; i++) {
        float rx = cosf(i * PI / 4.f) * 5.f, rz = sinf(i * PI / 4.f) * 5.f;
        glm::mat4 m = glm::translate(glm::mat4(1), { 1.f + rx + off.x,0.2f,z + rz });
        m = glm::scale(m, { 0.6f + i * 0.15f,0.28f + i * 0.05f,0.6f + i * 0.15f });
        drw(spV, spC, m, { 0.44f,0.40f,0.36f });
    }
    drawWallSegment({ -6.f + off.x,0,z - 4.f }, 4.f, 0.4f);
    drawWallSegment({ 8.f + off.x,0,z - 3.f }, 3.5f, -0.3f);
    drawTree({ -8.f + off.x,0,z - 6.f }, 0.95f, 70);
    drawTree({ -6.f + off.x,0,z - 9.f }, 0.80f, 71);
    drawTree({ 9.f + off.x,0,z - 5.f }, 0.90f, 72);
    drawTree({ -9.f + off.x,0,z + 4.f }, 1.00f, 73);
    drawTree({ 8.f + off.x,0,z + 6.f }, 0.85f, 74);
    for (int i = 0; i < 6; i++) {
        glm::mat4 m = glm::translate(glm::mat4(1), { 6.f + i * 2.f + off.x,0.18f,z - 4.f + i * 0.5f });
        m = glm::scale(m, { 0.45f,0.22f,0.38f });
        drw(bxV, bxC, m, { 0.40f,0.37f,0.34f });
    }
}

static void drawSceneMedievalTown(glm::vec3 off)
{
    float z = -183.f + off.z;
    glm::mat4 m = glm::translate(glm::mat4(1), { 0 + off.x,0.01f,z });
    m = glm::scale(m, { 12.f,0.015f,22.f });
    drw(bxV, bxC, m, {}, 2);

    drawLampPost({ -4.f + off.x,0,z - 15.f });
    drawLampPost({ 4.f + off.x,0,z - 15.f });
    drawLampPost({ -4.f + off.x,0,z - 3.f });
    drawLampPost({ 4.f + off.x,0,z - 3.f });
    drawLampPost({ 0.f + off.x,0,z + 9.f });

    drawHalfTimberHouse({ -9.f + off.x,0,z - 10.f }, 0.f, 1.2f, false);
    drawFence({ -9.f + off.x, 0, z - 6.5f }, 0.f, 7.5f);
    drawWoodenCabin({ off.x - 10.f, 0, z + 13.f }, 0.1f, 0.9f);
    drawFence({ off.x - 10.f, 0, z + 16.f }, 0.1f, 6.0f);
    drawHalfTimberHouse({ -8.f + off.x,0,z + 8.f }, 0.f, 1.0f, false);
    drawFence({ -8.f + off.x, 0, z + 11.5f }, 0.f, 7.0f);
    drawWoodenCabin({ off.x - 10.f, 0, z + 13.f }, 0.1f, 0.9f);
    drawHalfTimberHouse({ 0.f + off.x,0,z - 18.f }, 0.f, 1.3f, false);
    drawMedievalHall({ 12.f + off.x, 0.f, z - 5.f }, glm::radians(90.f));

    // Barn barrels (Interaction targets)
    drawBarrel({ 5.f + off.x,0,z - 2.f });
    drawBarrel({ 5.6f + off.x,0,z - 1.5f });

    m = glm::translate(glm::mat4(1), { -3.f + off.x,1.1f,z + 5.f });
    m = glm::scale(m, { 1.6f,0.06f,1.2f });
    drw(bxV, bxC, m, { 0.62f,0.22f,0.12f });
    for (int ci : {-1, 1}) for (int ri : {-1, 1}) {
        m = glm::translate(glm::mat4(1), { -3.f + ci * 1.3f + off.x,0.55f,z + 5.f + ri * 1.0f });
        m = glm::scale(m, { 0.06f,0.55f,0.06f });
        drw(cyV, cyC, m, { 0.28f,0.16f,0.08f });
    }
    m = glm::translate(glm::mat4(1), { -3.f + off.x,1.55f,z + 5.f });
    m = glm::scale(m, { 1.8f,0.04f,1.4f });
    drw(bxV, bxC, m, { 0.22f,0.40f,0.18f });

    m = glm::translate(glm::mat4(1), { 2.5f + off.x,0.6f,z + 2.f });
    m = glm::scale(m, { 0.7f,0.6f,0.7f });
    drw(cyV, cyC, m, { 0.44f,0.40f,0.36f });
    m = glm::translate(glm::mat4(1), { 2.5f + off.x,1.25f,z + 2.f });
    m = glm::scale(m, { 0.82f,0.06f,0.82f });
    drw(cyV, cyC, m, { 0.42f,0.38f,0.34f });

    for (int i = 0; i < 5; i++) {
        drawTree({ -14.f + off.x,0,z - 18.f + i * 8.f }, 1.1f + i * 0.05f, 80 + i);
        drawTree({ 14.f + off.x,0,z - 18.f + i * 8.f }, 1.1f + i * 0.05f, 85 + i);
    }
    drawBarrel({ 5.f + off.x,0,z - 2.f });
    drawBarrel({ 5.6f + off.x,0,z - 1.5f });
    drawBench({ -6.f + off.x,0,z + 1.f }, 1.0f);
}

// =============================================================================
// ── Input ─────────────────────────────────────────────────────────────────────
// =============================================================================
void cbScroll(GLFWwindow*, double, double yo) {
    cam.fov -= (float)yo * 2.5f;
    cam.fov = glm::clamp(cam.fov, 12.f, 105.f);
}
void cbMouse(GLFWwindow*, double xp, double yp) {
    if (!g_captured) return;
    if (cam.firstMouse) { cam.lastX = (float)xp; cam.lastY = (float)yp; cam.firstMouse = false; }
    float dx = (float)xp - cam.lastX, dy = cam.lastY - (float)yp;
    cam.lastX = (float)xp; cam.lastY = (float)yp;
    cam.yaw += dx * cam.sensitivity;
    cam.pitch += dy * cam.sensitivity;
    cam.pitch = glm::clamp(cam.pitch, -89.f, 89.f);
    cam.updateDir();
}
void cbKey(GLFWwindow* win, int key, int, int action, int) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        if (g_captured) { glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL); g_captured = false; cam.firstMouse = true; }
        else glfwSetWindowShouldClose(win, true);
    }
    if (key == GLFW_KEY_N && action == GLFW_PRESS) g_day = !g_day;
    if (key == GLFW_KEY_R && action == GLFW_PRESS) g_rain = !g_rain;

    // Interaction: E to toggle Barn Door
    if (key == GLFW_KEY_E && action == GLFW_PRESS) {
        float dist = glm::distance(cam.pos, glm::vec3(35.f + 18.f, 0, -77.f + 30.f - 15.f + 5.5f));
        if (dist < 8.f) g_barnDoorOpen = !g_barnDoorOpen;
    }

    // Interaction: G to Grab/Release Barrel
    if (key == GLFW_KEY_G && action == GLFW_PRESS) {
        if (g_heldObjIdx >= 0) {
            g_heldObjIdx = -1; // Drop
        }
        else {
            // Check distance to center of potential barrels
            glm::vec3 barrelP = { 35.f + 5.f, 0, -77.f + 30.f - 2.f }; // Village barrel
            if (glm::distance(cam.pos, barrelP) < 4.f) g_heldObjIdx = 1;
        }
    }

    if (key == GLFW_KEY_EQUAL && (action == GLFW_PRESS || action == GLFW_REPEAT))
        g_fogDensity = glm::clamp(g_fogDensity + 0.002f, 0.0f, 0.15f);
    if (key == GLFW_KEY_MINUS && (action == GLFW_PRESS || action == GLFW_REPEAT))
        g_fogDensity = glm::clamp(g_fogDensity - 0.002f, 0.0f, 0.15f);
}
void cbClick(GLFWwindow* win, int btn, int action, int) {
    if (btn == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if (!g_captured) {
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            g_captured = true; cam.firstMouse = true;
        }
        // Thunder: captured হোক বা না হোক, rain থাকলেই কাজ করবে
        if (g_rain) {
            g_thunderTimer = 0.6f;
            PlaySoundA(NULL, NULL, 0);
            if (!PlaySoundA("textures/thunder.wav", NULL, SND_ASYNC | SND_FILENAME | SND_NODEFAULT))
                PlaySoundA("SystemExclamation", NULL, SND_ASYNC | SND_ALIAS);
        }
    }
}
void handleKeys(GLFWwindow* win, float dt) {
    if (!g_captured) return;
    float spd = cam.speed * dt;
    glm::vec3 r = cam.right();
    glm::vec3 f = cam.front;
    // We only want XZ movement for collision normally, but let's keep it generic

    glm::vec3 move(0.f);
    if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) move += f;
    if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) move -= f;
    if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) move -= r;
    if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) move += r;
    if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) spd *= 1.8f;

    if (glm::length(move) > 0.001f) {
        move = glm::normalize(move) * spd;

        // Target position
        glm::vec3 nextP = cam.pos + move;
        if (!checkObs(nextP)) {
            cam.pos = nextP;
        }
        else {
            // Slide: try X movement
            glm::vec3 nextX = cam.pos + glm::vec3(move.x, 0, 0);
            if (!checkObs(nextX)) cam.pos.x = nextX.x;
            // Slide: try Z movement
            glm::vec3 nextZ = cam.pos + glm::vec3(0, 0, move.z);
            if (!checkObs(nextZ)) cam.pos.z = nextZ.z;
        }
    }

    // Door animation
    float doorTarget = g_barnDoorOpen ? 1.f : 0.f;
    g_barnDoorFactor += (doorTarget - g_barnDoorFactor) * 5.f * dt;

    // Vertical movement (ignored by collision for now as it's primarily ground-based)
    if (glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS) cam.pos.y += spd;
    if (glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS) cam.pos.y -= spd;
}

// ── Collision registration ──────────────────────────────────────────────────
static void initObstacles()
{
    g_obstacles.clear();

    // 1. Entrance pillars
    addObs({ -2.0f, 0, 7.f }, 0.5f);
    addObs({ 2.0f, 0, 7.f }, 0.5f);

    // 2. Forest Entrance (Mushroom Park)
    float fX = OFF_FOREST.x, fZ = OFF_FOREST.z;
    addObs({ -3.5f + fX, 0, 1.f + fZ }, 0.6f); // Pillar
    addObs({ 3.5f + fX, 0, 1.f + fZ }, 0.6f);  // Pillar
    addObs({ -6.f + fX, 0, -3.f + fZ }, 2.5f);  // Wall
    addObs({ 5.5f + fX, 0, -5.f + fZ }, 2.0f);  // Wall
    addObs({ fX - 6.f, 0, -8.f + fZ }, 0.6f);   // Park Bench (Stump)
    addObs({ fX - 3.5f, 0, -10.5f + fZ }, 0.5f); // Park Bench (Stump)

    // 3. Campsite
    float cZ = -37.f + OFF_CAMP.z;
    addObs({ -5.8f + OFF_CAMP.x, 0.f, cZ + 3.0f }, 2.5f); // Tent

    // 4. Village (Settlement)
    float vZ = -77.f + OFF_VILLAGE.z;
    float vX = OFF_VILLAGE.x;
    addObs({ vX + 2.f, 0.f, vZ - 30.f }, 10.0f); // Castle
    addObs({ vX - 10.f, 0.f, vZ - 5.f }, 4.5f);   // Manor
    addObs({ vX + 14.f, 0.f, vZ + 8.f }, 3.5f);   // Townhouse
    addObs({ vX - 12.f, 0.f, vZ + 18.f }, 2.8f);  // Cabin
    addObs({ vX + 18.f, 0.f, vZ - 15.f }, 7.0f);  // Barn

    // 5. Farm Barn
    float bZ = -113.f + OFF_BARN.z;
    addObs({ 0.f + OFF_BARN.x, 0.f, bZ }, 6.5f);

    // 6. Coastal Tower
    float tZ = -148.f + OFF_TOWER.z;
    addObs({ 1.f + OFF_TOWER.x, 0.f, tZ }, 3.5f);

    // 7. Medieval Town
    float mZ = -183.f + OFF_MED.z;
    float mX = OFF_MED.x;
    addObs({ -9.f + mX, 0, mZ - 10.f }, 3.5f);
    addObs({ -8.f + mX, 0, mZ + 8.f }, 3.0f);
    addObs({ 0.f + mX, 0, mZ - 18.f }, 3.5f);
    addObs({ 12.f + mX, 0.f, mZ - 5.f }, 5.5f);
    addObs({ mX - 10.f, 0, mZ + 13.f }, 2.5f);

    // 8. General Trees (Forest entrance groves)
    for (int i = 0; i < 4; i++) {
        addObs({ -5.f - i * 1.8f + fX, 0.f, -3.f - i * 2.2f + fZ }, 0.7f);
        addObs({ 5.f + i * 1.8f + fX, 0.f, -3.f - i * 2.2f + fZ }, 0.7f);
    }
}

// =============================================================================
// ── Main ──────────────────────────────────────────────────────────────────────
// =============================================================================
int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* win = glfwCreateWindow(SCR_W, SCR_H,
        "WASD=Move | Q/E=Fly | LMB(Rain)=Thunder | Shift=Sprint | N=Night | R=Rain | +/-=Fog | ESC x2=Quit",
        nullptr, nullptr);

    if (!win) { std::cerr << "Window failed\n"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glfwSetScrollCallback(win, cbScroll);
    glfwSetCursorPosCallback(win, cbMouse);
    glfwSetKeyCallback(win, cbKey);
    glfwSetMouseButtonCallback(win, cbClick);
    glfwSetFramebufferSizeCallback(win, [](GLFWwindow*, int w, int h) {
        glViewport(0, 0, w, h);
        });

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::cerr << "GLAD failed\n"; return -1; }
    glViewport(0, 0, SCR_W, SCR_H);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_PROGRAM_POINT_SIZE);
    cam.updateDir();

    g_prog = mkProg("vertex.vert", "fragment.frag");
    { auto m = mkBox();  bxV = mkVAO(m, bxC); }
    { auto m = mkCyl();  cyV = mkVAO(m, cyC); }
    { auto m = mkSph();  spV = mkVAO(m, spC); }
    { auto m = mkCone(); cnV = mkVAO(m, cnC); }
    { auto m = mkRoof(); rfV = mkVAO(m, rfC); }
    { auto m = mkStars(); stV = mkVAO(m, stC); }
    { auto m = mkSkyDome(); sdV = mkVAO(m, sdC); }
    initRain();

    // ── Load Digitized Image Texture ──────────────────────────────────────
    texWall = loadTexture("textures/house wall.png");
    texWindows = loadTexture("textures/house windows.png");
    texRoof = loadTexture("textures/medieval_roof_tiles_1775394068495.png");
    texWood = loadTexture("textures/medieval_wood_beams_1775394143335.png");
    texStone = loadTexture("textures/media__1775393870034.png");
    texPlanks = loadTexture("textures/Screenshot 2026-04-05 191944.png");

    texWindowArch = loadTexture("textures/Screenshot 2026-04-05 191956.png");
    texCastle = loadTexture("textures/castle.png");
    texMushroom = loadTexture("textures/mushroom .png");
    texFrog = loadTexture("textures/Screenshot 2026-04-06 220835.png");
    texBarnWall = loadTexture("textures/Screenshot 2026-04-06 214047.png");
    texBarnInner = loadTexture("textures/Screenshot 2026-04-06 214058.png");
    texBarnFloor = loadTexture("textures/Screenshot 2026-04-06 214109.png");
    texBarnRoof = loadTexture("textures/Screenshot 2026-04-06 214117.png");
    texBarnDoor = loadTexture("textures/Screenshot 2026-04-06 214127.png");
    texHorizon = loadTexture("textures/horizon.png");
    glBindTexture(GL_TEXTURE_2D, texHorizon);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    // ── Grass ground texture (আলাদা — house wall এর সাথে মিলবে না) ──────
    texGrass = loadTexture("textures/grass.png");
    // fallback: যদি grass.png না থাকে তাহলে sample_texture.png
    if (texGrass == 0 || texGrass == (GLuint)-1) texGrass = 0;

    // Default sample for other usage
    GLuint myTex = loadTexture("sample_texture.png");
    // grass ground এ আলাদা texture না থাকলে procedural matType=1 use করো
    if (texGrass == 0) texGrass = myTex;

    glUseProgram(g_prog);
    u("tex0", 0);

    initObstacles();

    // ── S-curve road waypoints (world XZ) ─────────────────────────────────
    // Centre → LEFT(camp) → RIGHT(village) → LEFT(barn) → RIGHT(tower) → Centre(medieval)
    // ── উত্তর রাস্তা: প্রবেশ → বন → মধ্যযুগীয় শহর ────────────────────────
    const glm::vec3 WP_MAIN[] = {
        {  0.f, 0,   8.f },
        {  0.f, 0,  -5.f },
        {  0.f, 0, -20.f },
        {  0.f, 0, -60.f },
        {  0.f, 0,-100.f },
        {  0.f, 0,-125.f }
    };
    const int NWP_MAIN = 6;

    // ── পশ্চিম রাস্তা: crossroads → campsite → barn ──────────────────────────
    const glm::vec3 WP_WEST[] = {
        {   0.f, 0,  -5.f },
        { -20.f, 0, -10.f },
        { -35.f, 0, -25.f },
        { -40.f, 0, -45.f },
        { -45.f, 0, -60.f },
        { -45.f, 0, -80.f }
    };
    const int NWP_WEST = 6;

    // ── পূর্ব রাস্তা: crossroads → village → tower ───────────────────────────
    const glm::vec3 WP_EAST[] = {
        {  0.f, 0,  -5.f },
        { 20.f, 0, -10.f },
        { 35.f, 0, -25.f },
        { 40.f, 0, -45.f },
        { 45.f, 0, -60.f },
        { 45.f, 0, -80.f }
    };
    const int NWP_EAST = 6;


    float prevT = (float)glfwGetTime();

    while (!glfwWindowShouldClose(win))
    {
        float now = (float)glfwGetTime();
        float dt = now - prevT; prevT = now;
        handleKeys(win, dt);
        if (g_rain) updateRain(dt);
        else { // rain band থাকলেও যদি বন্ধ থাকে VAO reset করার দরকার নেই
        }

        // Thunder Decay
        if (g_thunderTimer > 0) g_thunderTimer -= dt;


        glm::vec3 skyDay{ 0.70f, 0.82f, 0.95f };    // Lighter, hazier sky
        glm::vec3 skyNight{ 0.05f, 0.06f, 0.15f };  // Less pitch black, more night-mist
        glm::vec3 skyRainD{ 0.52f, 0.56f, 0.62f };
        glm::vec3 skyRainN{ 0.04f, 0.06f, 0.12f };
        float df = g_day ? 1.f : 0.f;
        glm::vec3 skyC = g_rain ? (g_day ? skyRainD : skyRainN) : (g_day ? skyDay : skyNight);

        // Thunder Flash Effect
        if (g_thunderTimer > 0) {
            float flash = (g_thunderTimer / 0.5f) * 0.8f;
            skyC += glm::vec3(flash * 0.6f, flash * 0.7f, flash * 1.0f);
        }


        glClearColor(skyC.r, skyC.g, skyC.b, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = cam.view();
        int fbW, fbH;
        glfwGetFramebufferSize(win, &fbW, &fbH);
        glm::mat4 proj = glm::perspective(glm::radians(cam.fov), (float)fbW / (float)fbH, 0.05f, 280.f);

        float flk = 1.f + 0.14f * sinf(now * 8.1f) + 0.09f * sinf(now * 13.4f) * sinf(now * 3.3f);
        glm::vec3 fireLPos = FIRE_P + glm::vec3(0, 0.88f, 0);

        glUseProgram(g_prog);
        u("view", view);
        u("projection", proj);
        u("viewPos", cam.pos);
        u("dayFactor", df);
        u("isRaining", (int)g_rain);
        u("time", now);
        u("skyColor", skyC);

        u("sunDir", glm::normalize(glm::vec3(0.55f, 1.0f, -0.4f)));
        u("sunColor", glm::vec3(1.0f, 0.96f, 0.82f));
        u("sunIntensity", 1.80f);

        u("moonDir", glm::normalize(glm::vec3(0.35f, 1.0f, -0.6f)));
        u("moonColor", glm::vec3(0.55f, 0.64f, 0.92f));

        u("firePos", fireLPos);
        u("fireColor", glm::vec3(1.f, 0.50f, 0.08f));
        u("fireIntensity", g_day ? 0.0f : flk * 8.0f);

        u("fogDensity", g_fogDensity);
        u("numLamps", NUM_LAMPS);
        u("lampIntensity", g_day ? 0.0f : 3.5f);
        for (int i = 0; i < NUM_LAMPS; i++) uv3a("lampPos", i, LAMP_WORLD[i]);

        // ── Drawing Horizon ──
        drawSkyDome();

        // ── Large grass ground (wide enough to cover S-curve spread) ──────
        {
            // Left half (covers camp/barn at x≈-18)
            glm::mat4 m = glm::translate(glm::mat4(1), { 0.f, 0.f, -75.f });
            m = glm::scale(m, { 110.f, 0.01f, 85.f });
            drwTex(bxV, bxC, m, texGrass, 1);
        }

        // ── তিনটি রাস্তা আঁকো ────────────────────────────────────────────────
        for (int i = 0; i < NWP_MAIN - 1; i++) drawRoadSegment(WP_MAIN[i], WP_MAIN[i + 1], 3.4f);
        for (int i = 0; i < NWP_WEST - 1; i++) drawRoadSegment(WP_WEST[i], WP_WEST[i + 1], 3.4f);
        for (int i = 0; i < NWP_EAST - 1; i++) drawRoadSegment(WP_EAST[i], WP_EAST[i + 1], 3.4f);
        drawRoadSideStones(WP_MAIN, NWP_MAIN);
        drawRoadSideStones(WP_WEST, NWP_WEST);
        drawRoadSideStones(WP_EAST, NWP_EAST);

        // ── Crossroads ও scene-এর কাছে চওড়া pad ─────────────────────────────
        {
            glm::mat4 m;
            // আক্ষরিক ইন্টারসেকশন প্যাচ (Cobblestone inside pebbles)
            m = glm::translate(glm::mat4(1), { 0.f, 0.014f,  -5.f });
            m = glm::scale(m, { 3.6f, 0.013f, 3.6f });
            drw(bxV, bxC, m, {}, 2);
        }

        // ── All scenes with their offsets ──────────────────────────────────
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, myTex);

        drawRoadEntrance();
        drawHelpSign({ 0.f, 0, -5.f }); // Helpful UI Billboard near start

        drawSceneForestEntrance(OFF_FOREST);

        // Fractal Grove near forest
        drawFractalTree(OFF_FOREST + glm::vec3(-8.f, 0, -15.f), { 0,1,0 }, 2.5f, 0.3f, 6);
        drawFractalTree(OFF_FOREST + glm::vec3(8.f, 0, -18.f), { 0,1,0 }, 2.2f, 0.25f, 5);

        // Bezier Arch at Village Entrance
        drawBezierArch({ OFF_VILLAGE.x - 4.f, 0, OFF_VILLAGE.z + 5.f },
            { OFF_VILLAGE.x - 4.f, 8.f, OFF_VILLAGE.z + 5.f },
            { OFF_VILLAGE.x + 4.f, 8.f, OFF_VILLAGE.z + 5.f },
            { OFF_VILLAGE.x + 4.f, 0, OFF_VILLAGE.z + 5.f }, 0.25f);

        drawSceneCampsite(now, OFF_CAMP);

        drawSceneVillage(OFF_VILLAGE);
        drawSceneBarn(OFF_BARN);
        drawSceneCoastalTower(OFF_TOWER);
        drawSceneMedievalTown(OFF_MED);

        // Held Object (Barrel)
        if (g_heldObjIdx >= 0) {
            glm::vec3 holdP = cam.pos + cam.front * 2.5f + glm::vec3(0, -0.4f, 0);
            drawBarrel(holdP);
        }

        drawRoadEnd(OFF_MED);
        // পশ্চিম রাস্তার শেষে গাছ
        for (int i = 0; i < 6; i++) {
            drawTree({ -74.f - i * 2.2f, 0, -102.f - i * 1.5f }, 1.1f + i * 0.05f, 110 + i);
            drawTree({ -86.f + i * 1.5f, 0, -104.f - i * 1.0f }, 1.0f + i * 0.04f, 116 + i);
        }
        // পূর্ব রাস্তার শেষে গাছ
        for (int i = 0; i < 6; i++) {
            drawTree({ 74.f + i * 2.2f, 0, -102.f - i * 1.5f }, 1.1f + i * 0.05f, 122 + i);
            drawTree({ 86.f - i * 1.5f, 0, -104.f - i * 1.0f }, 1.0f + i * 0.04f, 128 + i);
        }

        // ── Direction Indicator (Compass) ──
        // Only draw when captured
        // ── Direction Indicator (Compass) ──
        // Only draw when captured
        if (g_captured) {
            glDisable(GL_DEPTH_TEST);
            glm::mat4 compassProj = glm::ortho(0.f, (float)fbW, 0.f, (float)fbH);
            u("projection", compassProj);
            u("view", glm::mat4(1));

            glm::mat4 m = glm::translate(glm::mat4(1), { 70.f, 70.f, 0.f });
            m = glm::rotate(m, glm::radians(cam.yaw + 90.f), { 0, 0, 1 });
            m = glm::scale(m, { 30.f, 40.f, 1.f });
            drw(cnV, cnC, m, { 1.f, 0.1f, 0.1f }, 0, true); // Red needle
            glEnable(GL_DEPTH_TEST);

            // ← CRITICAL: Restore 3D projection & view after compass HUD
            u("projection", proj);
            u("view", view);
        }

        // ── Night-only elements ────────────────────────────────────────────
        if (!g_day) {
            drawMoon({ -18.f,28.f,-40.f });
            u("matType", 0); u("isEmissive", 1);
            u("objectColor", glm::vec3(0.82f, 0.86f, 1.0f));
            glm::mat4 starModel = glm::translate(glm::mat4(1), cam.pos);
            u("model", starModel);
            glBindVertexArray(stV);
            glDrawArrays(GL_POINTS, 0, stC);
        }
        drawClouds(now);
        drawBirds(now);
        drawRainParticles(); // একবারই call
        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}