#include <Arduino.h>
#include <U8g2lib.h>

#include "CatFace.h"

// ======================================================================
//  Cat Face (hanya mata, hidung, mulut, kumis) - 12 ekspresi, OLED 128x64
//
//  Ekspresi : normal, happy, love, sleepy, sleeping, surprised, angry,
//             sad, wink, dizzy, smug, blep
//  Animasi  : kedip (kadang dobel), mata melirik,
//             kumis bergoyang, napas/bounce, hati melayang, zZz,
//             gelembung ingus, bintang pusing, air mata, dll.
//
//  API publik tidak berubah: initCatFace() dan updateCatFace().
// ======================================================================

// Hapus tanda komentar untuk mengunci satu ekspresi saat testing:
// #define FORCE_MOOD M_CURIOUS

namespace {

U8G2* display = nullptr;

// ----------------------------------------------------------------------
// Konfigurasi
// ----------------------------------------------------------------------
constexpr unsigned long FRAME_INTERVAL = 33;   // ~30 FPS
constexpr unsigned long BLINK_DURATION = 115;
constexpr uint8_t MOOD_HISTORY_SIZE = 3;

// Geser wajah ke atas supaya pas di tengah layar (tanpa kepala)
constexpr int BASE_OY = -5;

// Efek di luar wajah (hati, zZz, tanda seru, bintang, dll).
// Ubah ke false kalau mau benar-benar HANYA mata, hidung, mulut, kumis.
constexpr bool SHOW_EFFECTS = true;

constexpr int CX = 64;                          // tengah layar
constexpr int EYE_Y = 29, EYE_DX = 20;   // mata sangat besar

enum Mood : uint8_t {
    M_NORMAL,
    M_HAPPY,
    M_LOVE,
    M_SLEEPY,
    M_SLEEPING,
    M_SURPRISED,
    M_ANGRY,
    M_SAD,
    M_WINK,
    M_DIZZY,
    M_SMUG,
    M_BLEP,
    M_CURIOUS      // penasaran: lirik kanan-kiri-atas-bawah
};

// ----------------------------------------------------------------------
// State
// ----------------------------------------------------------------------
Mood mood = M_NORMAL;

unsigned long nowMs = 0;        // waktu frame saat ini
unsigned long t = 0;            // ms sejak ekspresi dimulai
unsigned long lastFrame = 0;
unsigned long moodStart = 0;
unsigned long moodEnd = 0;

bool blinking = false;
unsigned long blinkStart = 0;
unsigned long nextBlink = 0;
bool waitingForDoubleBlink = false;

int lookX = 0, lookY = 0;       // posisi mata sekarang
int lookTX = 0, lookTY = 0;     // target
unsigned long nextLook = 0;
unsigned long lastLookStep = 0;
Mood recentMoods[MOOD_HISTORY_SIZE] = { M_NORMAL, M_NORMAL, M_NORMAL };
uint8_t moodHistoryCount = 0;

int ox = 0, oy = 0;             // offset seluruh wajah (bounce / goyang)

// ----------------------------------------------------------------------
// Pembungkus gambar: semua ikut offset wajah (ox, oy)
// ----------------------------------------------------------------------
inline void color(uint8_t c) { display->setDrawColor(c); }
inline void px(int x, int y) { display->drawPixel(x + ox, y + oy); }
inline void box(int x, int y, int w, int h) { display->drawBox(x + ox, y + oy, w, h); }
inline void rbox(int x, int y, int w, int h, int r) { display->drawRBox(x + ox, y + oy, w, h, r); }
inline void rframe(int x, int y, int w, int h, int r) { display->drawRFrame(x + ox, y + oy, w, h, r); }
inline void line(int x0, int y0, int x1, int y1) { display->drawLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy); }
inline void tri(int x0, int y0, int x1, int y1, int x2, int y2) {
    display->drawTriangle(x0 + ox, y0 + oy, x1 + ox, y1 + oy, x2 + ox, y2 + oy);
}
inline void disc(int x, int y, int r, uint8_t opt = U8G2_DRAW_ALL) { display->drawDisc(x + ox, y + oy, r, opt); }
inline void circle(int x, int y, int r, uint8_t opt = U8G2_DRAW_ALL) { display->drawCircle(x + ox, y + oy, r, opt); }

// Kotak dengan sudut dipotong 1 piksel (aman untuk ukuran kecil)
void softBox(int x, int y, int w, int h) {
    box(x, y, w, h);
    color(0);
    px(x, y);
    px(x + w - 1, y);
    px(x, y + h - 1);
    px(x + w - 1, y + h - 1);
    color(1);
}

// Gambar tanpa offset (untuk efek di luar wajah)
void sparkle(int x, int y, int s) {
    display->drawLine(x - s, y, x + s, y);
    display->drawLine(x, y - s, x, y + s);
}

void tinyHeart(int x, int y) {
    display->drawBox(x, y, 2, 2);
    display->drawBox(x + 3, y, 2, 2);
    display->drawBox(x, y + 1, 5, 2);
    display->drawBox(x + 1, y + 3, 3, 1);
    display->drawPixel(x + 2, y + 4);
}

bool sleepyClosed() { return (t % 2600) < 500; }   // fase nguap / mata tutup

// ----------------------------------------------------------------------
// Skrip lirikan untuk ekspresi "penasaran"
//   {lama (ms), arah X (+ kanan), arah Y (+ bawah)}
// Ubah angkanya untuk mengatur urutan, kecepatan, dan seberapa jauh melirik.
// ----------------------------------------------------------------------
struct GazeKey { uint16_t ms; int8_t x; int8_t y; };

const GazeKey CURIOUS_SCRIPT[] = {
    { 300,  0,  0},   // diam sebentar
    { 800,  5,  0},   // kanan
    { 900, -5,  0},   // kiri
    { 800,  0, -3},   // atas
    { 800,  0,  3},   // bawah
    { 600,  0,  0},   // balik ke tengah
};
constexpr int CURIOUS_STEPS = sizeof(CURIOUS_SCRIPT) / sizeof(CURIOUS_SCRIPT[0]);

unsigned long curiousTotal() {
    unsigned long sum = 0;
    for (int i = 0; i < CURIOUS_STEPS; i++) sum += CURIOUS_SCRIPT[i].ms;
    return sum;
}

int approach(int cur, int target, int step) {
    if (cur < target) return (cur + step > target) ? target : cur + step;
    if (cur > target) return (cur - step < target) ? target : cur - step;
    return cur;
}

// ----------------------------------------------------------------------
// Manajemen ekspresi
// ----------------------------------------------------------------------
Mood pickNextMood(Mood cur) {
#ifdef FORCE_MOOD
    (void)cur;
    return FORCE_MOOD;
#else
    if (cur == M_SLEEPY && random(100) < 58) return M_SLEEPING;
    if (cur == M_SLEEPING) return random(100) < 42 ? M_SURPRISED : M_NORMAL;
    static const Mood pool[] = {
        M_NORMAL, M_NORMAL, M_NORMAL, M_NORMAL, M_NORMAL,
        M_HAPPY, M_HAPPY, M_HAPPY, M_CURIOUS, M_CURIOUS, M_CURIOUS,
        M_SLEEPY, M_SLEEPY, M_LOVE, M_SURPRISED, M_WINK, M_SMUG,
        M_BLEP, M_SAD, M_ANGRY, M_DIZZY
    };
    for (uint8_t attempt = 0; attempt < 12; ++attempt) {
        Mood candidate = pool[random((long)(sizeof(pool) / sizeof(pool[0])))];
        if (candidate == cur) continue;
        bool recent = false;
        for (uint8_t i = 0; i < moodHistoryCount; ++i)
            if (recentMoods[i] == candidate) { recent = true; break; }
        if (recent && candidate != M_NORMAL && random(100) < 82) continue;
        return candidate;
    }
    return M_NORMAL;
#endif
}

void startMood(Mood m, unsigned long now) {
    if (moodHistoryCount < MOOD_HISTORY_SIZE) recentMoods[moodHistoryCount++] = m;
    else {
        recentMoods[0] = recentMoods[1];
        recentMoods[1] = recentMoods[2];
        recentMoods[2] = m;
    }
    mood = m;
    moodStart = now;
    unsigned long dur;
    switch (m) {
        case M_SLEEPING:  dur = random(6500, 11500); break;
        case M_DIZZY:     dur = random(1900, 3300); break;
        case M_SURPRISED: dur = random(900, 1900); break;
        case M_WINK:      dur = random(1200, 2300); break;
        case M_HAPPY:     dur = random(2200, 4800); break;
        case M_CURIOUS:   dur = curiousTotal() + random(250, 1100); break;
        case M_SLEEPY:    dur = random(3000, 6200); break;
        case M_NORMAL:    dur = random(2800, 7200); break;
        default:          dur = random(1700, 5200); break;
    }
    moodEnd = now + dur;
    blinking = false;
    waitingForDoubleBlink = false;
    nextBlink = now + random(900, 2600) + random(0, 900);
    nextLook = now + random(350, 1300);
}

// Update animasi (non-gambar)
// ----------------------------------------------------------------------
void updateBlink() {
    bool canBlink = (mood == M_NORMAL || mood == M_ANGRY || mood == M_SAD ||
                     mood == M_BLEP || mood == M_WINK || mood == M_SMUG ||
                     mood == M_CURIOUS || mood == M_HAPPY);
    if (!canBlink) { blinking = false; return; }
    if (blinking) {
        if (nowMs - blinkStart >= BLINK_DURATION) {
            blinking = false;
            waitingForDoubleBlink = (random(100) < 13);
            nextBlink = nowMs + (waitingForDoubleBlink
                ? random(130, 240)
                : random(1700, 3900) + random(0, 1500));
        }
    } else if ((long)(nowMs - nextBlink) >= 0) {
        blinking = true;
        blinkStart = nowMs;
    }
}

void updateLook() {
    bool roam = (mood == M_NORMAL || mood == M_SMUG || mood == M_SAD ||
                 mood == M_HAPPY || mood == M_BLEP);
    if (mood == M_CURIOUS) {
        lookTX = 0; lookTY = 0;
        unsigned long acc = 0;
        for (int i = 0; i < CURIOUS_STEPS; ++i) {
            acc += CURIOUS_SCRIPT[i].ms;
            if (t < acc) { lookTX = CURIOUS_SCRIPT[i].x; lookTY = CURIOUS_SCRIPT[i].y; break; }
        }
    } else if (roam) {
        if ((long)(nowMs - nextLook) >= 0) {
            int roll = random(100);
            if (roll < 52) { lookTX = random(-1, 2); lookTY = random(-1, 2); }
            else if (roll < 88) { lookTX = random(-3, 4); lookTY = random(-1, 2); }
            else { lookTX = random(-5, 6); lookTY = random(-2, 3); }
            nextLook = nowMs + random(550, 1700) + random(0, 850);
            if (random(100) < 18) nextLook = nowMs + random(220, 480);
        }
    } else {
        lookTX = 0;
        lookTY = (mood == M_BLEP) ? -1 : 0;
    }
    unsigned long interval = (mood == M_CURIOUS) ? 32 : 42;
    if (nowMs - lastLookStep >= interval) {
        lastLookStep = nowMs;
        lookX = approach(lookX, lookTX, 1);
        lookY = approach(lookY, lookTY, 1);
    }
}

void updatePose() {
    ox = 0;
    oy = 0;
    switch (mood) {
        case M_NORMAL:    oy = (sinf(t / 450.0f) > 0.5f) ? 1 : 0; break;               // napas
        case M_HAPPY:     oy = -(int)(fabsf(sinf(t / 140.0f)) * 3.0f + 0.5f); break;   // loncat-loncat
        case M_LOVE:      oy = (int)roundf(sinf(t / 300.0f)); break;                   // melayang
        case M_SLEEPY:    oy = sleepyClosed() ? 2 : 0; break;                          // terkantuk
        case M_SLEEPING:  oy = 1 + (int)roundf(sinf(t / 800.0f)); break;               // naik-turun pelan
        case M_SURPRISED: oy = (t < 200) ? -3 : 0; break;                              // kaget lompat
        case M_ANGRY:     ox = ((t / 50) & 1) ? 1 : -1; break;                         // gemetar
        case M_SAD:       oy = 1; break;                                               // lesu
        case M_DIZZY:     ox = (int)roundf(sinf(t / 110.0f) * 3.0f); break;            // sempoyongan
        case M_BLEP:      oy = -(int)((t / 220) & 1); break;
        case M_CURIOUS:   ox = lookX / 2; oy = lookY / 2; break;                       // kepala ikut menoleh
        default: break;
    }
    oy += BASE_OY;
}

// ----------------------------------------------------------------------
// Mata (besar & berkilau ala kawaii)
// ----------------------------------------------------------------------
void eyeLine(int cx, int y) { box(cx - 9, y + 4, 19, 3); }

void eyeOpen(int cx, int y) {
    if (blinking) { eyeLine(cx, y); return; }
    int x = cx + lookX;
    int yy = y + lookY;
    rbox(x - 9, yy - 10, 19, 21, 8);   // mata oval besar polos
}

void eyeHappy(int cx, int y) {   // ^
    for (int k = 0; k < 3; k++) {
        line(cx - 9, y + 5 + k, cx, y - 4 + k);
        line(cx, y - 4 + k, cx + 9, y + 5 + k);
    }
}

void eyeSleep(int cx, int y) {   // u (tidur)
    for (int k = 0; k < 3; k++) {
        line(cx - 9, y + k, cx - 4, y + 5 + k);
        line(cx - 4, y + 5 + k, cx + 4, y + 5 + k);
        line(cx + 4, y + 5 + k, cx + 9, y + k);
    }
}

void eyeHeart(int cx, int y, bool big) {
    int r = big ? 6 : 5;
    disc(cx - r, y - 2, r);
    disc(cx + r, y - 2, r);
    tri(cx - 2 * r + 1, y - 1, cx + 2 * r - 1, y - 1, cx, y + 2 * r + 2);
}

void eyeHalf(int cx, int lidY) {   // kelopak setengah (smug / sleepy)
    if (blinking) { eyeLine(cx, lidY); return; }
    int x = cx + lookX;
    box(x - 8, lidY + 2, 17, 10);
    color(0);
    px(x - 8, lidY + 11);
    px(x + 8, lidY + 11);
    px(x - 8, lidY + 10);
    px(x + 8, lidY + 10);
    color(1);
    box(cx - 10, lidY, 21, 3);
}

void eyeAngry(int cx, int y, int s) {   // s = -1 mata kiri, +1 mata kanan
    line(cx + s * 10, y - 13, cx - s * 9, y - 4);
    line(cx + s * 10, y - 12, cx - s * 9, y - 3);
    if (blinking) { eyeLine(cx, y); return; }
    softBox(cx - 8, y - 1, 17, 15);
}

void eyeSadBrow(int cx, int y, int s) {
    line(cx + s * 9, y - 11, cx - s * 8, y - 16);
    line(cx + s * 9, y - 10, cx - s * 8, y - 15);
}

void eyeSurprised(int cx, int y) {
    circle(cx, y, 10);
    circle(cx, y, 9);
    disc(cx, y, 4);
}

void eyeSpiral(int cx, int y, int dir) {
    circle(cx, y, 10);
    circle(cx, y, 5);
    float a = (t / 120.0f) * dir;
    for (int i = 0; i < 2; i++) {
        float b = a + i * 3.14159f;
        px(cx + (int)roundf(cosf(b) * 7.5f), y + (int)roundf(sinf(b) * 7.5f));
    }
}

void drawEyes() {
    const int lx = CX - EYE_DX;
    const int rx = CX + EYE_DX;
    const int y = EYE_Y;

    switch (mood) {
        case M_NORMAL:
        case M_BLEP:
        case M_CURIOUS:
            eyeOpen(lx, y);
            eyeOpen(rx, y);
            break;

        case M_HAPPY:
            eyeHappy(lx, y);
            eyeHappy(rx, y);
            break;

        case M_LOVE:
            eyeHeart(lx, y, (t % 600) < 150);
            eyeHeart(rx, y, (t % 600) < 150);
            break;

        case M_SLEEPY:
            if (sleepyClosed()) {
                eyeLine(lx, y);
                eyeLine(rx, y);
            } else {
                eyeHalf(lx, y + 1);
                eyeHalf(rx, y + 1);
            }
            break;

        case M_SLEEPING:
            eyeSleep(lx, y);
            eyeSleep(rx, y);
            break;

        case M_SURPRISED:
            eyeSurprised(lx, y);
            eyeSurprised(rx, y);
            break;

        case M_ANGRY:
            eyeAngry(lx, y, -1);
            eyeAngry(rx, y, +1);
            break;

        case M_SAD: {
            eyeOpen(lx, y);
            eyeOpen(rx, y);
            eyeSadBrow(lx, y, -1);
            eyeSadBrow(rx, y, +1);

            // Air mata menetes dari mata kanan
            float ph = (t % 1600) / 1600.0f;
            if (ph < 0.8f) {
                int ty = y + 13 + (int)(ph * 14.0f);
                int tx = rx + 7;
                disc(tx, ty, 1);
                px(tx, ty - 2);
            }
            break;
        }

        case M_WINK:
            eyeOpen(lx, y);
            if (t < 400) eyeOpen(rx, y);
            else         eyeHappy(rx, y);
            break;

        case M_DIZZY:
            eyeSpiral(lx, y, 1);
            eyeSpiral(rx, y, -1);
            break;

        case M_SMUG:
            eyeHalf(lx, y - 2);
            eyeHalf(rx, y - 2);
            break;
    }
}

// ----------------------------------------------------------------------
// Hidung & mulut
// ----------------------------------------------------------------------
void nose() { tri(61, 42, 67, 42, 64, 45); }

void mouthOmega() {   // mulut kucing  w (besar)
    line(64, 45, 64, 47);
    for (int k = 0; k < 2; k++) {
        line(56, 47 + k, 60, 51 + k);
        line(60, 51 + k, 64, 47 + k);
        line(64, 47 + k, 68, 51 + k);
        line(68, 51 + k, 72, 47 + k);
    }
}

void drawMouth() {
    nose();

    switch (mood) {
        case M_NORMAL:
        case M_LOVE:
        case M_WINK:
        case M_CURIOUS:
            mouthOmega();
            break;

        case M_HAPPY:   // mulut terbuka + lidah
            disc(64, 47, 7, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
            color(0);
            disc(64, 53, 3, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
            color(1);
            break;

        case M_BLEP:    // melet
            mouthOmega();
            box(65, 51, 7, 2);
            disc(68, 53, 3, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
            color(0);
            box(68, 53, 1, 3);
            color(1);
            break;

        case M_SLEEPY:
            if (sleepyClosed()) {          // nguap
                disc(64, 50, 5);
            } else {
                box(60, 49, 9, 2);
            }
            break;

        case M_SLEEPING: {
            line(59, 48, 62, 51);
            line(62, 51, 66, 51);
            line(66, 51, 69, 48);
            line(62, 52, 66, 52);

            // Gelembung ingus
            float b = (sinf(t / 900.0f) + 1.0f) * 0.5f;
            int r = 2 + (int)(b * 6.0f);
            int cx = 70 + r;
            circle(cx, 45, r);
            if (r >= 4) px(cx - r / 2, 45 - r / 2);
            break;
        }

        case M_SURPRISED:
            circle(64, 50, 5);
            circle(64, 50, 4);
            break;

        case M_ANGRY:
        case M_SAD:
            for (int k = 0; k < 2; k++) {
                line(57, 52 + k, 64, 47 + k);
                line(64, 47 + k, 71, 52 + k);
            }
            break;

        case M_DIZZY:   // mulut bergelombang
            for (int k = 0; k < 2; k++) {
                line(56, 51 + k, 60, 47 + k);
                line(60, 47 + k, 64, 51 + k);
                line(64, 51 + k, 68, 47 + k);
                line(68, 47 + k, 72, 51 + k);
            }
            break;

        case M_SMUG:    // senyum miring
            for (int k = 0; k < 2; k++) {
                line(56, 50 + k, 66, 50 + k);
                line(66, 50 + k, 72, 46 + k);
            }
            break;
    }
}

// ----------------------------------------------------------------------
// Kumis
// ----------------------------------------------------------------------
void drawWhiskers() {
    static const int sx[3] = {36, 36, 38};
    static const int sy[3] = {45, 49, 53};
    static const int ex[3] = {29, 27, 29};
    static const int ey[3] = {43, 49, 55};

    int tilt = 0;
    switch (mood) {
        case M_SAD:      tilt = 3; break;    // kumis turun
        case M_SLEEPING: tilt = 2; break;
        case M_HAPPY:
        case M_LOVE:
        case M_BLEP:     tilt = ((t / 140) & 1) ? -3 : -1; break;   // goyang riang
        case M_ANGRY:    tilt = -1; break;
        case M_CURIOUS:  tilt = ((t / 200) & 1) ? -2 : -1; break;   // kumis bergerak waspada
        default: break;
    }

    for (int i = 0; i < 3; i++) {
        line(sx[i], sy[i], ex[i], ey[i] + tilt);
        line(128 - sx[i], sy[i], 128 - ex[i], ey[i] + tilt);
    }
}

// ----------------------------------------------------------------------
// Efek tambahan di luar wajah
// ----------------------------------------------------------------------
void drawOverlay() {
    switch (mood) {
        case M_LOVE:
            for (int i = 0; i < 3; i++) {
                unsigned long ph = (t + i * 500) % 1500;
                int y = 34 - (int)(ph / 50);
                int x = 100 + i * 8 + (int)roundf(sinf(ph / 200.0f) * 2.0f);
                tinyHeart(x, y);
            }
            break;

        case M_SLEEPING:
            for (int i = 0; i < 3; i++) {
                unsigned long ph = (t + i * 900) % 2700;
                float f = ph / 2700.0f;
                if (f > 0.85f) continue;
                int y = 40 - (int)(f * 30.0f);
                int x = 100 + (int)(f * 14.0f);
                if (f < 0.33f)      display->setFont(u8g2_font_5x7_tr);
                else if (f < 0.66f) display->setFont(u8g2_font_6x10_tr);
                else                display->setFont(u8g2_font_7x13B_tr);
                display->drawStr(x, y, "z");
            }
            break;

        case M_SURPRISED:
            if (t < 1400) {
                display->drawBox(103, 6, 3, 8);
                display->drawBox(103, 16, 3, 3);
            }
            break;

        case M_ANGRY: {
            int x = 106, y = 20;
            int L = 3 + (int)((t / 200) & 1);
            for (int sx = -1; sx <= 1; sx += 2) {
                for (int sy = -1; sy <= 1; sy += 2) {
                    display->drawLine(x + sx, y + sy, x + sx * (1 + L), y + sy);
                    display->drawLine(x + sx, y + sy, x + sx, y + sy * (1 + L));
                }
            }
            break;
        }

        case M_DIZZY:
            for (int i = 0; i < 3; i++) {
                float a = t / 200.0f + i * 2.09f;
                int x = 64 + (int)roundf(cosf(a) * 14.0f);
                int y = 7 + (int)roundf(sinf(a) * 3.0f);
                sparkle(x, y, 1);
            }
            break;

        case M_CURIOUS:
            display->setFont(u8g2_font_7x13B_tr);
            display->drawStr(102, 16 + (int)((t / 250) & 1), "?");
            break;

        case M_WINK:
            if (t > 400) sparkle(102, 26, 1 + (int)((t / 200) % 3));
            break;

        case M_HAPPY:
            if ((t / 300) & 1) sparkle(18, 22, 2);
            else               sparkle(108, 30, 2);
            break;

        default:
            break;
    }
}

// ----------------------------------------------------------------------
// Render
// ----------------------------------------------------------------------
void renderCat() {
    display->clearBuffer();
    display->setDrawColor(1);

    drawEyes();
    drawMouth();        // termasuk hidung & gelembung ingus
    drawWhiskers();
    if (SHOW_EFFECTS) drawOverlay();

    display->sendBuffer();
}

} // namespace

// ----------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------

void initCatFace(U8G2& oledDisplay) {
    display = &oledDisplay;

    randomSeed(micros());

    unsigned long now = millis();
    nowMs = now;
    lastFrame = now;
    lastLookStep = now;
    nextLook = now + random(350, 1300);
    moodHistoryCount = 0;
    waitingForDoubleBlink = false;
    lookX = lookY = lookTX = lookTY = 0;

    startMood(M_NORMAL, now);
    t = 0;
    updatePose();
    renderCat();
}

void updateCatFace() {
    if (display == nullptr) {
        return;
    }

    unsigned long now = millis();

    // Batasi frekuensi rendering
    if (now - lastFrame < FRAME_INTERVAL) {
        return;
    }
    lastFrame = now;
    nowMs = now;

    // Ganti ekspresi kalau waktunya sudah habis
    if ((long)(nowMs - moodEnd) >= 0) {
        startMood(pickNextMood(mood), nowMs);
    }
    t = nowMs - moodStart;

    updateBlink();
    updateLook();
    updatePose();

    renderCat();
}
