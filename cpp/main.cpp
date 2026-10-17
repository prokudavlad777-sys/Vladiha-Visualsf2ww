// 2D ВІЙНА на карті (C++17 / raylib 5)
// Ви граєте за СИНІХ (ліворуч), ШІ керує ЧЕРВОНИМИ (праворуч).
#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

static const int W = 1280;
static const int H = 720;
static const float INCOME = 14.0f;
static const float HQ_RANGE = 230.0f;

enum Side { TEAM_BLUE = 0, TEAM_RED = 1 };
enum Dom { GROUND, SEA, AIR };
enum Kind { TANK, PLANE, SHIP, SHAHED };
enum Strike { S_SHAHED, S_MISSILE, S_KAB, S_NUKE, S_NONE };
enum ProjKind { P_MISSILE, P_KAB, P_NUKE };

struct UnitType {
    const char* name;
    int cost;
    float hp, speed, range, dmg, cd;
    Dom dom;
    float sight;
    bool hitsGround, hitsSea, hitsAir;
};

static const UnitType TYPES[4] = {
    {"Танк",     100, 120, 45,  140, 18, 1.2f, GROUND, 260, true, true, false},
    {"Літак",    250, 80,  150, 120, 12, 0.4f, AIR,    500, true, true, true},
    {"Корабель", 300, 400, 30,  260, 30, 1.5f, SEA,    400, true, true, true},
    {"Шахед",    0,   20,  70,  0,   0,  0.0f, AIR,    0,   false, false, false},
};

struct StrikeInfo { const char* name; int cost; float r; };
static const StrikeInfo STRIKES[4] = {
    {"Шахеди x3", 90, 30}, {"Ракета", 200, 70}, {"КАБ", 150, 55}, {"Ядерка", 1500, 230},
};

struct Unit { Kind kind; Side side; float x, y, hp, cd, angle, tx, ty; };
struct Hq { float x, y, hp, maxhp, cd; };
struct Proj { ProjKind kind; Side side; float x, y, tx, ty, speed, r, dmg, dx, dy; bool friendly; };
struct Shot { float x1, y1, x2, y2, t; Side side; };
struct Fx { float x, y, r, t, dur; };
struct Msg { std::string text; float t; };

// Ціль: або юніт, або штаб
struct Target { float* hp; float x, y; bool isUnit; Dom dom; };

struct Game {
    std::vector<Unit> units;
    std::vector<Shot> shots;
    std::vector<Proj> projs;
    std::vector<Fx> fx;
    std::vector<Msg> msgs;
    float money[2];
    Hq hq[2];
    float nukeCd[2];
    Strike armed;
    int over; // 0 - гра, 1 - перемога, 2 - поразка
    bool paused;
    float aiT;
    bool aiSave;
    float t, flash;
};

static Game G;
static Font gFont;

// ---------------------------------------------------------------- допоміжне
static float frand() { return (float)std::rand() / (float)RAND_MAX; }
static float rnd(float a, float b) { return a + frand() * (b - a); }
static float seaY(float x) { return 500.0f + std::sin(x * 0.008f) * 40.0f + std::sin(x * 0.021f) * 15.0f; }
static float dist(float ax, float ay, float bx, float by) { return std::hypot(ax - bx, ay - by); }
static Side enemyOf(Side s) { return s == TEAM_BLUE ? TEAM_RED : TEAM_BLUE; }
static Color sideColor(Side s, unsigned char a = 255) {
    return s == TEAM_BLUE ? Color{64, 153, 255, a} : Color{255, 82, 77, a};
}

static void say(const std::string& text) {
    G.msgs.insert(G.msgs.begin(), Msg{text, 5.0f});
    if (G.msgs.size() > 6) G.msgs.pop_back();
}

static bool canStand(Dom dom, float x, float y) {
    if (x < 5 || x > W - 5 || y < 40 || y > H - 5) return false;
    if (dom == AIR) return true;
    bool sea = y > seaY(x);
    return dom == SEA ? sea : !sea;
}

static void newGame() {
    G.units.clear(); G.shots.clear(); G.projs.clear(); G.fx.clear(); G.msgs.clear();
    G.money[TEAM_BLUE] = G.money[TEAM_RED] = 500;
    G.hq[TEAM_BLUE] = Hq{90, 260, 2500, 2500, 0};
    G.hq[TEAM_RED] = Hq{1190, 260, 2500, 2500, 0};
    G.nukeCd[TEAM_BLUE] = G.nukeCd[TEAM_RED] = 0;
    G.armed = S_NONE; G.over = 0; G.paused = false;
    G.aiT = 3; G.aiSave = false; G.t = 0; G.flash = 0;
    say("Керування: 1-3 купити, 4-7 обрати удар і клікнути по карті");
}

// ---------------------------------------------------------------- юніти та вибухи
static Unit& spawn(Kind kind, Side side, float x, float y) {
    Unit u{kind, side, x, y, TYPES[kind].hp, 0, side == TEAM_BLUE ? 0.0f : (float)PI, x, y};
    G.units.push_back(u);
    return G.units.back();
}

static void spawnPoint(Kind kind, Side side, float& x, float& y) {
    const Hq& h = G.hq[side];
    float dir = side == TEAM_BLUE ? 1.0f : -1.0f;
    if (kind == SHIP) { x = h.x + dir * 50; y = 640 + rnd(-30, 30); return; }
    x = h.x + dir * (50 + rnd(0, 20));
    y = h.y + rnd(-60, 60);
}

static void addFx(float x, float y, float r) { G.fx.push_back(Fx{x, y, r, 0, 0.6f}); }

static void explode(float x, float y, float r, float dmg, Side side, bool friendly) {
    addFx(x, y, r);
    for (auto& u : G.units) {
        if (u.hp > 0 && (friendly || u.side != side)) {
            float d = dist(x, y, u.x, u.y);
            if (d <= r) u.hp -= dmg * (1 - 0.5f * d / r);
        }
    }
    for (int s = 0; s < 2; s++) {
        if (friendly || s != side) {
            Hq& h = G.hq[s];
            float d = dist(x, y, h.x, h.y);
            if (d <= r + 30) h.hp -= dmg * (1 - 0.5f * d / (r + 30));
        }
    }
}

static const Unit* planeNear(Side side, float x, float y, float maxd) {
    const Unit* best = nullptr;
    float bd = maxd;
    for (const auto& u : G.units) {
        if (u.side == side && u.kind == PLANE && u.hp > 0) {
            float d = dist(u.x, u.y, x, y);
            if (d < bd) { best = &u; bd = d; }
        }
    }
    return best;
}

// повертає порожній рядок, якщо успішно, інакше причину відмови
static std::string launch(Strike kind, Side side, float tx, float ty) {
    int cost = STRIKES[kind].cost;
    if (G.money[side] < cost) return "Недостатньо грошей";
    Hq h = G.hq[side];
    if (kind == S_SHAHED) {
        for (int i = 0; i < 3; i++) {
            Unit& u = spawn(SHAHED, side, h.x + rnd(-20, 20), h.y + rnd(-40, 40));
            u.tx = tx + rnd(-30, 30);
            u.ty = ty + rnd(-30, 30);
        }
    } else if (kind == S_MISSILE) {
        G.projs.push_back(Proj{P_MISSILE, side, h.x, h.y, tx, ty, 330, 70, 160, 1, 0, false});
    } else if (kind == S_KAB) {
        const Unit* p = planeNear(side, tx, ty, 400);
        if (!p) return "КАБ: потрібен свій літак ближче 400 до цілі";
        G.projs.push_back(Proj{P_KAB, side, p->x, p->y, tx, ty, 220, 55, 130, 1, 0, false});
    } else {
        if (G.nukeCd[side] > 0) return "Ядерка ще перезаряджається";
        float d = std::max(dist(h.x, h.y, tx, ty), 1.0f);
        G.projs.push_back(Proj{P_NUKE, side, h.x, h.y, tx, ty, d / 3.0f, 230, 800, 1, 0, true});
        G.nukeCd[side] = 45;
        say(std::string(side == TEAM_BLUE ? "Ви запустили" : "Ворог запустив") + " ЯДЕРНУ РАКЕТУ! Приліт за 3 с");
    }
    G.money[side] -= cost;
    return "";
}

// ---------------------------------------------------------------- поведінка юнітів
static void moveToward(Unit& u, const UnitType& T, float tx, float ty, float dt) {
    float a = std::atan2(ty - u.y, tx - u.x);
    static const float offs[7] = {0, 0.6f, -0.6f, 1.2f, -1.2f, 1.8f, -1.8f};
    for (float off : offs) {
        float aa = a + off;
        float nx = u.x + std::cos(aa) * T.speed * dt;
        float ny = u.y + std::sin(aa) * T.speed * dt;
        if (canStand(T.dom, nx, ny)) { u.x = nx; u.y = ny; u.angle = aa; return; }
    }
}

static bool canHit(const UnitType& T, Dom d) {
    return d == GROUND ? T.hitsGround : d == SEA ? T.hitsSea : T.hitsAir;
}

static void updateUnit(Unit& u, float dt) {
    const UnitType& T = TYPES[u.kind];
    u.cd -= dt;

    if (u.kind == SHAHED) {
        float d = dist(u.x, u.y, u.tx, u.ty);
        float step = T.speed * dt;
        u.angle = std::atan2(u.ty - u.y, u.tx - u.x);
        if (d <= step + 3) {
            explode(u.x, u.y, 45, 85, u.side, false);
            u.hp = 0;
        } else {
            u.x += (u.tx - u.x) / d * step;
            u.y += (u.ty - u.y) / d * step;
        }
        return;
    }

    Hq& foe = G.hq[enemyOf(u.side)];
    Target tgt{nullptr, 0, 0, false, GROUND};
    bool have = false;
    float td = T.sight;
    for (auto& e : G.units) {
        if (e.side == u.side || e.hp <= 0) continue;
        if (!canHit(T, TYPES[e.kind].dom)) continue;
        float d = dist(u.x, u.y, e.x, e.y);
        if (d < td) { tgt = Target{&e.hp, e.x, e.y, true, TYPES[e.kind].dom}; td = d; have = true; }
    }
    float dh = dist(u.x, u.y, foe.x, foe.y) - 30;
    if (dh < td) { tgt = Target{&foe.hp, foe.x, foe.y, false, GROUND}; td = dh; have = true; }

    float gx = foe.x, gy = foe.y;
    if (T.dom == SEA) {
        gx = foe.x - (u.side == TEAM_BLUE ? 1.0f : -1.0f) * 40;
        gy = seaY(gx) + 20;
    }

    if (have && td <= T.range) {
        u.angle = std::atan2(tgt.y - u.y, tgt.x - u.x);
        if (u.cd <= 0) {
            *tgt.hp -= T.dmg;
            u.cd = T.cd;
            G.shots.push_back(Shot{u.x, u.y, tgt.x, tgt.y, 0.1f, u.side});
        }
    } else {
        float tx = gx, ty = gy;
        if (have && tgt.isUnit && (T.dom == AIR || tgt.dom == T.dom)) { tx = tgt.x; ty = tgt.y; }
        moveToward(u, T, tx, ty, dt);
    }
}

static void updateHq(Side side, float dt) {
    Hq& h = G.hq[side];
    h.cd -= dt;
    if (h.cd > 0) return;
    Unit* best = nullptr;
    float bd = HQ_RANGE;
    for (auto& e : G.units) {
        if (e.side != side && e.hp > 0) {
            float d = dist(h.x, h.y, e.x, e.y);
            if (d < bd) { best = &e; bd = d; }
        }
    }
    if (best) {
        best->hp -= 8;
        h.cd = 0.4f;
        G.shots.push_back(Shot{h.x, h.y, best->x, best->y, 0.1f, side});
    }
}

// ---------------------------------------------------------------- ШІ (червоні)
static void aiTarget(Side side, float& x, float& y) {
    Side foe = enemyOf(side);
    std::vector<const Unit*> list;
    for (const auto& u : G.units)
        if (u.side == foe && u.hp > 0 && u.kind != SHAHED) list.push_back(&u);
    if (list.empty()) { x = G.hq[foe].x; y = G.hq[foe].y; return; }
    const Unit* u = list[std::rand() % list.size()];
    x = u->x; y = u->y;
}

static int aiCluster(Side side, float& bx, float& by) {
    Side foe = enemyOf(side);
    int bn = 0;
    for (const auto& a : G.units) {
        if (a.side != foe || a.hp <= 0) continue;
        int n = 0;
        for (const auto& b : G.units)
            if (b.side == foe && b.hp > 0 && dist(a.x, a.y, b.x, b.y) < 200) n++;
        if (n > bn) { bx = a.x; by = a.y; bn = n; }
    }
    return bn;
}

static void aiThink(Side side) {
    float m = G.money[side];
    float x = 0, y = 0;
    if (G.aiSave) {
        if (m >= 1500 && G.nukeCd[side] <= 0) {
            int n = aiCluster(side, x, y);
            if (n < 3) { x = G.hq[enemyOf(side)].x; y = G.hq[enemyOf(side)].y; }
            launch(S_NUKE, side, x, y);
            G.aiSave = false;
        }
        return;
    }
    float r = frand();
    if (G.nukeCd[side] <= 0 && m >= 500 && r < 0.08f) { G.aiSave = true; return; }
    if (m >= 200 && r < 0.15f)      { aiTarget(side, x, y); launch(S_MISSILE, side, x, y); }
    else if (m >= 90 && r < 0.28f)  { aiTarget(side, x, y); launch(S_SHAHED, side, x, y); }
    else if (m >= 150 && r < 0.36f) { aiTarget(side, x, y); launch(S_KAB, side, x, y); }
    else {
        float pick = frand();
        Kind kind = pick < 0.55f ? TANK : (pick < 0.80f ? PLANE : SHIP);
        if (m >= TYPES[kind].cost) {
            G.money[side] -= TYPES[kind].cost;
            spawnPoint(kind, side, x, y);
            spawn(kind, side, x, y);
        }
    }
}

// ---------------------------------------------------------------- оновлення
static void update(float dt) {
    for (auto& m : G.msgs) m.t -= dt;
    G.msgs.erase(std::remove_if(G.msgs.begin(), G.msgs.end(), [](const Msg& m) { return m.t <= 0; }), G.msgs.end());
    if (G.paused || G.over) return;

    G.t += dt;
    for (int s = 0; s < 2; s++) {
        G.money[s] += INCOME * dt;
        G.nukeCd[s] = std::max(0.0f, G.nukeCd[s] - dt);
    }
    G.aiT -= dt;
    if (G.aiT <= 0) { G.aiT = rnd(2.5f, 4.5f); aiThink(TEAM_RED); }

    for (size_t i = 0; i < G.units.size(); i++) updateUnit(G.units[i], dt);
    updateHq(TEAM_BLUE, dt);
    updateHq(TEAM_RED, dt);

    for (size_t i = 0; i < G.units.size();) {
        if (G.units[i].hp <= 0) {
            addFx(G.units[i].x, G.units[i].y, 16);
            G.units.erase(G.units.begin() + i);
        } else i++;
    }

    for (size_t i = 0; i < G.projs.size();) {
        Proj p = G.projs[i];
        float d = dist(p.x, p.y, p.tx, p.ty);
        float step = p.speed * dt;
        if (d <= step) {
            explode(p.tx, p.ty, p.r, p.dmg, p.side, p.friendly);
            if (p.kind == P_NUKE) G.flash = 1;
            G.projs.erase(G.projs.begin() + i);
        } else {
            Proj& q = G.projs[i];
            q.dx = (q.tx - q.x) / d;
            q.dy = (q.ty - q.y) / d;
            q.x += q.dx * step;
            q.y += q.dy * step;
            i++;
        }
    }

    for (auto& s : G.shots) s.t -= dt;
    G.shots.erase(std::remove_if(G.shots.begin(), G.shots.end(), [](const Shot& s) { return s.t <= 0; }), G.shots.end());
    for (auto& f : G.fx) f.t += dt;
    G.fx.erase(std::remove_if(G.fx.begin(), G.fx.end(), [](const Fx& f) { return f.t >= f.dur; }), G.fx.end());
    G.flash = std::max(0.0f, G.flash - dt * 1.2f);

    if (G.hq[TEAM_RED].hp <= 0) G.over = 1;
    else if (G.hq[TEAM_BLUE].hp <= 0) G.over = 2;
}

// ---------------------------------------------------------------- малювання
struct Hill { float x, y, r; };
static std::vector<Hill> gHills;

static void buildHills() {
    unsigned seed = 7;
    auto r01 = [&seed]() { seed = seed * 1103515245u + 12345u; return (float)((seed >> 8) & 0xFFFF) / 65535.0f; };
    for (int i = 0; i < 70; i++) gHills.push_back(Hill{r01() * W, r01() * 520, 20 + r01() * 40});
}

// заповнений трикутник незалежно від порядку вершин
static void fillTri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0) std::swap(b, c);
    DrawTriangle(a, b, c, col);
}

static void fillPoly(const std::vector<Vector2>& p, Color col) {
    for (size_t i = 1; i + 1 < p.size(); i++) fillTri(p[0], p[i], p[i + 1], col);
}

static Vector2 rotPt(const Unit& u, float px, float py) {
    float c = std::cos(u.angle), s = std::sin(u.angle);
    return Vector2{u.x + px * c - py * s, u.y + px * s + py * c};
}

static void text(const std::string& s, float x, float y, float size, Color c) {
    DrawTextEx(gFont, s.c_str(), Vector2{x, y}, size, 0, c);
}

static void textCentered(const std::string& s, float cx, float y, float size, Color c) {
    Vector2 m = MeasureTextEx(gFont, s.c_str(), size, 0);
    DrawTextEx(gFont, s.c_str(), Vector2{cx - m.x / 2, y}, size, 0, c);
}

static void textRight(const std::string& s, float rx, float y, float size, Color c) {
    Vector2 m = MeasureTextEx(gFont, s.c_str(), size, 0);
    DrawTextEx(gFont, s.c_str(), Vector2{rx - m.x, y}, size, 0, c);
}

static void drawMap() {
    ClearBackground(Color{59, 90, 56, 255});
    for (const auto& h : gHills) DrawCircle((int)h.x, (int)h.y, h.r, Color{69, 102, 64, 200});
    for (int x = 0; x <= W; x += 8) {
        int sy = (int)seaY((float)x);
        DrawRectangle(x, sy, 8, H - sy, Color{31, 71, 128, 255});
    }
    for (int x = 0; x < W; x += 8)
        DrawLineEx(Vector2{(float)x, seaY((float)x)}, Vector2{(float)x + 8, seaY((float)x + 8)}, 3, Color{209, 194, 128, 255});
    for (int x = 0; x <= W; x += 80) DrawLine(x, 0, x, H, Color{255, 255, 255, 13});
    for (int y = 0; y <= H; y += 80) DrawLine(0, y, W, y, Color{255, 255, 255, 13});
}

static void drawUnit(const Unit& u) {
    Color c = sideColor(u.side);
    if (u.kind == TANK) {
        DrawRectanglePro(Rectangle{u.x, u.y, 18, 12}, Vector2{9, 6}, u.angle * RAD2DEG, c);
        DrawLineEx(Vector2{u.x, u.y}, rotPt(u, 15, 0), 3, Color{255, 255, 255, 230});
    } else if (u.kind == PLANE) {
        fillPoly({rotPt(u, 13, 0), rotPt(u, -8, -9), rotPt(u, -4, 0), rotPt(u, -8, 9)}, c);
    } else if (u.kind == SHIP) {
        fillPoly({rotPt(u, 18, 0), rotPt(u, 10, -7), rotPt(u, -15, -7), rotPt(u, -15, 7), rotPt(u, 10, 7)}, c);
        Vector2 t = rotPt(u, 2, 0);
        DrawCircleV(t, 4, Color{255, 255, 255, 230});
    } else {
        fillTri(rotPt(u, 7, 0), rotPt(u, -6, -6), rotPt(u, -6, 6), c);
    }
    float mx = TYPES[u.kind].hp;
    if (u.hp < mx) {
        DrawRectangle((int)u.x - 12, (int)u.y - 17, 24, 4, Color{0, 0, 0, 180});
        DrawRectangle((int)u.x - 12, (int)u.y - 17, (int)(24 * std::max(u.hp, 0.0f) / mx), 4, Color{77, 255, 77, 255});
    }
}

static void drawHq(Side side) {
    const Hq& h = G.hq[side];
    Color c = sideColor(side);
    DrawCircleLines((int)h.x, (int)h.y, HQ_RANGE, sideColor(side, 38));
    DrawRectangle((int)h.x - 22, (int)h.y - 22, 44, 44, sideColor(side, 140));
    DrawRectangleLinesEx(Rectangle{h.x - 22, h.y - 22, 44, 44}, 3, c);
    DrawLineEx(Vector2{h.x, h.y - 22}, Vector2{h.x, h.y - 40}, 2, c);
    fillTri(Vector2{h.x, h.y - 40}, Vector2{h.x + 16, h.y - 34}, Vector2{h.x, h.y - 28}, c);
    DrawRectangle((int)h.x - 40, (int)h.y + 30, 80, 8, Color{0, 0, 0, 180});
    DrawRectangle((int)h.x - 40, (int)h.y + 30, (int)(80 * std::max(h.hp, 0.0f) / h.maxhp), 8, c);
    textCentered(std::to_string(std::max(0, (int)h.hp)), h.x, h.y + 40, 14, WHITE);
}

static void draw() {
    BeginDrawing();
    drawMap();
    drawHq(TEAM_BLUE);
    drawHq(TEAM_RED);
    for (const auto& u : G.units) drawUnit(u);

    for (const auto& p : G.projs) {
        if (p.kind == P_NUKE) {
            float left = dist(p.x, p.y, p.tx, p.ty) / p.speed;
            float pulse = 0.5f + 0.5f * std::sin(G.t * 10);
            DrawCircle((int)p.tx, (int)p.ty, p.r, Color{255, 26, 26, (unsigned char)(255 * (0.2f + 0.2f * pulse))});
            DrawCircleLines((int)p.tx, (int)p.ty, p.r, Color{255, 64, 64, 255});
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f", left);
            textCentered(buf, p.tx, p.ty - 8, 16, WHITE);
            DrawCircleV(Vector2{p.x, p.y}, 6, Color{255, 250, 204, 255});
        } else {
            DrawLineEx(Vector2{p.x, p.y}, Vector2{p.x - p.dx * 16, p.y - p.dy * 16}, 2, Color{255, 204, 102, 150});
            if (p.kind == P_KAB) DrawRectangle((int)p.x - 3, (int)p.y - 3, 6, 6, sideColor(p.side));
            else DrawCircleV(Vector2{p.x, p.y}, 3.5f, sideColor(p.side));
        }
    }
    for (const auto& s : G.shots) {
        float a = std::min(1.0f, 0.4f + s.t * 5);
        DrawLineV(Vector2{s.x1, s.y1}, Vector2{s.x2, s.y2}, sideColor(s.side, (unsigned char)(255 * a)));
    }
    for (const auto& f : G.fx) {
        float k = f.t / f.dur;
        float r = f.r * (0.3f + 0.7f * k);
        DrawCircle((int)f.x, (int)f.y, r, Color{255, 153, 51, (unsigned char)(255 * (1 - k) * 0.5f)});
        DrawCircleLines((int)f.x, (int)f.y, r, Color{255, 230, 128, (unsigned char)(255 * (1 - k))});
    }

    Vector2 m = GetMousePosition();
    if (G.armed != S_NONE && !G.over) {
        Color y{255, 255, 77, 230};
        DrawCircleLines((int)m.x, (int)m.y, STRIKES[G.armed].r, y);
        DrawLine((int)m.x - 8, (int)m.y, (int)m.x + 8, (int)m.y, y);
        DrawLine((int)m.x, (int)m.y - 8, (int)m.x, (int)m.y + 8, y);
    }

    // інтерфейс
    DrawRectangle(0, 0, W, 34, Color{0, 0, 0, 165});
    text("Гроші: " + std::to_string((int)G.money[TEAM_BLUE]) + " $   (+" + std::to_string((int)INCOME) + "/с)", 14, 8, 16, WHITE);
    text(G.nukeCd[TEAM_BLUE] > 0 ? "Ядерка: " + std::to_string((int)std::ceil(G.nukeCd[TEAM_BLUE])) + "с" : std::string("Ядерка: готова"), 260, 8, 16, WHITE);
    textRight("P — пауза   R — заново   ПКМ/Esc — скасувати удар", W - 14, 8, 16, WHITE);

    DrawRectangle(0, H - 64, W, 64, Color{0, 0, 0, 180});
    float cw = (float)W / 7;
    for (int i = 0; i < 7; i++) {
        bool isUnitKey = i < 3;
        std::string name = isUnitKey ? TYPES[i].name : STRIKES[i - 3].name;
        int cost = isUnitKey ? TYPES[i].cost : STRIKES[i - 3].cost;
        float x = i * cw;
        if (!isUnitKey && G.armed == (Strike)(i - 3))
            DrawRectangle((int)x + 3, H - 61, (int)cw - 6, 58, Color{255, 255, 77, 64});
        Color col = G.money[TEAM_BLUE] >= cost ? WHITE : Color{140, 140, 140, 255};
        text("[" + std::to_string(i + 1) + "] " + name, x + 12, H - 54, 16, col);
        text(std::to_string(cost) + " $" + (isUnitKey ? "" : "  · клік"), x + 12, H - 32, 16, col);
    }
    for (size_t i = 0; i < G.msgs.size(); i++) {
        float a = std::min(1.0f, G.msgs[i].t);
        text(G.msgs[i].text, 14, 44 + i * 18, 16, Color{255, 255, 255, (unsigned char)(255 * a)});
    }

    if (G.flash > 0) DrawRectangle(0, 0, W, H, Color{255, 255, 255, (unsigned char)(255 * std::min(1.0f, G.flash))});
    if (G.over) {
        DrawRectangle(0, 0, W, H, Color{0, 0, 0, 150});
        textCentered(G.over == 1 ? "ПЕРЕМОГА!" : "ПОРАЗКА", W / 2.0f, H / 2.0f - 50, 56,
                     G.over == 1 ? Color{102, 255, 128, 255} : Color{255, 102, 102, 255});
        textCentered("Натисніть R, щоб почати заново", W / 2.0f, H / 2.0f + 20, 20, WHITE);
    } else if (G.paused) {
        textCentered("ПАУЗА", W / 2.0f, H / 2.0f - 30, 56, WHITE);
    }
    EndDrawing();
}

// ---------------------------------------------------------------- ввід
static void handleInput() {
    if (IsKeyPressed(KEY_R)) { newGame(); return; }
    if (IsKeyPressed(KEY_P)) G.paused = !G.paused;
    if (IsKeyPressed(KEY_ESCAPE)) G.armed = S_NONE;
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) G.armed = S_NONE;

    if (!G.over) {
        for (int i = 0; i < 7; i++) {
            if (!IsKeyPressed(KEY_ONE + i)) continue;
            if (i < 3) {
                int cost = TYPES[i].cost;
                if (G.money[TEAM_BLUE] >= cost) {
                    G.money[TEAM_BLUE] -= cost;
                    float x, y;
                    spawnPoint((Kind)i, TEAM_BLUE, x, y);
                    spawn((Kind)i, TEAM_BLUE, x, y);
                } else say("Недостатньо грошей");
            } else {
                Strike s = (Strike)(i - 3);
                G.armed = (G.armed == s) ? S_NONE : s;
            }
        }
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && G.armed != S_NONE && !G.over && !G.paused) {
        Vector2 m = GetMousePosition();
        std::string err = launch(G.armed, TEAM_BLUE, m.x, m.y);
        if (!err.empty()) say(err);
        else if (G.armed == S_NUKE) G.armed = S_NONE;
    }
}

int main() {
    std::srand((unsigned)std::time(nullptr));
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(W, H, "2D Війна — C++ / raylib");
    SetExitKey(KEY_NULL); // Esc лише скасовує удар
    SetTargetFPS(60);

    // шрифт з кирилицею
    std::vector<int> cps;
    for (int c = 32; c < 127; c++) cps.push_back(c);
    for (int c = 0x400; c < 0x500; c++) cps.push_back(c);
    cps.push_back(0xB7); // ·
    std::string fontPath = std::string(GetApplicationDirectory()) + "assets/DejaVuSans.ttf";
    gFont = LoadFontEx(fontPath.c_str(), 32, cps.data(), (int)cps.size());
    SetTextureFilter(gFont.texture, TEXTURE_FILTER_BILINEAR);

    buildHills();
    newGame();

    while (!WindowShouldClose()) {
        float dt = std::min(GetFrameTime(), 0.05f);
        handleInput();
        update(dt);
        draw();
    }
    UnloadFont(gFont);
    CloseWindow();
    return 0;
}
