#include <pebble.h>

// ============================================================================
// CONSTANTS
// ============================================================================

// Layout constants scaled per platform.
// Emery (200x228) is the reference; others scaled proportionally.

#if defined(PBL_PLATFORM_BASALT) || defined(PBL_PLATFORM_APLITE) || defined(PBL_PLATFORM_DIORITE)
// 144x168 — scale ~0.72x
#define SCREEN_W 144
#define SCREEN_H 168
#define EARTH_CENTER_X    0
#define EARTH_CENTER_Y  162
#define EARTH_RADIUS     72
#define MOON_CENTER_X   119
#define MOON_CENTER_Y    67
#define MOON_RADIUS      16
#define ISS_ORBIT_RADIUS          80
#define CSS_ORBIT_RADIUS          82
#define CREW_DRAGON_ORBIT_RADIUS  92
#define SHENZHOU_ORBIT_RADIUS     86
#define ARTEMIS_ORBIT_RADIUS      23
#define MISSION_LABEL_Y 145
#define HELIOCENTRIC_X   18
#define HELIOCENTRIC_Y   43

#elif defined(PBL_PLATFORM_CHALK)
// 180x180 — scale ~0.85x
#define SCREEN_W 180
#define SCREEN_H 180
#define EARTH_CENTER_X    0
#define EARTH_CENTER_Y  170
#define EARTH_RADIUS     80
#define MOON_CENTER_X   148
#define MOON_CENTER_Y    71
#define MOON_RADIUS      19
#define ISS_ORBIT_RADIUS          90
#define CSS_ORBIT_RADIUS          94
#define CREW_DRAGON_ORBIT_RADIUS 105
#define SHENZHOU_ORBIT_RADIUS    100
#define ARTEMIS_ORBIT_RADIUS      27
#define MISSION_LABEL_Y 68
#define HELIOCENTRIC_X   22
#define HELIOCENTRIC_Y   46

#elif defined(PBL_PLATFORM_GABBRO)
// 260x260 — scale ~1.2x
#define SCREEN_W 260
#define SCREEN_H 260
#define EARTH_CENTER_X    0
#define EARTH_CENTER_Y  250
#define EARTH_RADIUS    115
#define MOON_CENTER_X   215
#define MOON_CENTER_Y    88
#define MOON_RADIUS      26
#define ISS_ORBIT_RADIUS         130
#define CSS_ORBIT_RADIUS         133
#define CREW_DRAGON_ORBIT_RADIUS 150
#define SHENZHOU_ORBIT_RADIUS    144
#define ARTEMIS_ORBIT_RADIUS      38
#define MISSION_LABEL_Y 245
#define HELIOCENTRIC_X   30
#define HELIOCENTRIC_Y   68

#else
// Emery 200x228 — reference layout
#define SCREEN_W 200
#define SCREEN_H 228
#define EARTH_CENTER_X    0
#define EARTH_CENTER_Y  220
#define EARTH_RADIUS    100
#define MOON_CENTER_X   165
#define MOON_CENTER_Y    77
#define MOON_RADIUS      22
#define ISS_ORBIT_RADIUS          110
#define CSS_ORBIT_RADIUS          114
#define CREW_DRAGON_ORBIT_RADIUS  128
#define SHENZHOU_ORBIT_RADIUS     120
#define ARTEMIS_ORBIT_RADIUS       32
#define MISSION_LABEL_Y 210
#define HELIOCENTRIC_X   25
#define HELIOCENTRIC_Y   58
#endif

// Text layers — font sizes are fixed, same across all platforms
#define TIME_LAYER_Y    2
#define TIME_LAYER_H    40
#define DATE_LAYER_Y    44
#define DATE_LAYER_H    22
#define MISSION_LABEL_H 16

// Animation
#define ANIMATION_INTERVAL          50
#define ANIMATION_INTERVAL_LOW_BAT 100
#define LOW_BATTERY_THRESHOLD       20

#define NUM_MISSIONS 10
#define NUM_STARS    20

// Set DEBUG_STATIONS to freeze ISS/CSS at fixed angles for layout testing.
// Comment out (or set to 0) for production — real angles from JS longitude data.
// #define DEBUG_STATIONS
#ifdef DEBUG_STATIONS
  #define ISS_DEBUG_ANGLE DEG_TO_TRIGANGLE(35)
  #define CSS_DEBUG_ANGLE DEG_TO_TRIGANGLE(70)
#endif

// ============================================================================
// ENUMERATIONS & STRUCTS
// ============================================================================

typedef enum {
    COUNTRY_US = 0,
    COUNTRY_CN,
    COUNTRY_EU,
    COUNTRY_RU,
} Country;

typedef enum {
    ORBIT_EARTH = 0,
    ORBIT_MOON,
    TRANSIT_TO_MOON,
    TRANSIT_TO_EARTH,
    ORBIT_DOCKED,        // docked to ISS
    ORBIT_HELIOCENTRIC,  // deep space / heliocentric orbit
    ORBIT_DOCKED_CSS,    // docked to CSS (Tiangong)
} OrbitType;

// Continent region shown on Earth face, based on user longitude
typedef enum {
    REGION_AMERICAS = 0,
    REGION_EUROPE_AFRICA = 1,
    REGION_ASIA = 2,
} ContinentRegion;

typedef struct {
    char        name[15];  // mission/spacecraft name, updated from LL2 (up to 14 chars)
    Country     country;
    OrbitType   orbit;
    int32_t     angle;
    int32_t     angle_speed;
    int16_t     orbit_radius;
    bool        active;
} MissionState;

// ============================================================================
// GLOBAL STATE
// ============================================================================

static Window    *s_main_window  = NULL;
static Layer     *s_canvas_layer = NULL;
static TextLayer *s_time_layer   = NULL;
static TextLayer *s_date_layer   = NULL;
static TextLayer *s_mission_layer = NULL;
static AppTimer  *s_anim_timer   = NULL;

static int       s_battery_level  = 100;
#ifdef DEBUG_STATIONS
static int32_t   s_iss_angle      = ISS_DEBUG_ANGLE;
static int32_t   s_css_angle      = CSS_DEBUG_ANGLE;
#else
static int32_t   s_iss_angle      = DEG_TO_TRIGANGLE(180);
static int32_t   s_css_angle      = DEG_TO_TRIGANGLE(180);
#endif
static int       s_anim_tick      = 0;
static int       s_user_lon       = 5;   // default: Europe (Paris area)

static MissionState s_missions[NUM_MISSIONS];

// ============================================================================
// GPATHS — pre-allocated
// ============================================================================

// Rocket: nose points UP (angle=0), 4-point silhouette
static GPoint s_rocket_pts[] = {
    { 0, -7},
    { 4,  7},
    { 0,  4},
    {-4,  7},
};
static GPathInfo s_rocket_info = { .num_points = 4, .points = s_rocket_pts };
static GPath *s_rocket_path = NULL;

// ---- CONTINENT GPATHS — derived from simplified SVG outlines ----
// All points: ×1.5 scale + dy+80 shift, then rotated 15° clockwise.
// CS() scales each coordinate proportionally to EARTH_RADIUS (designed for radius=100).
#define CS(v) ((int16_t)((int32_t)(v) * EARTH_RADIUS / 100))

// REGION_AMERICAS (shifted +20 dy upward total)
static GPoint s_am_north_pts[] = {
    { CS( 58), CS(-74)},  { CS( 58), CS(-55)},  { CS( 50), CS(-54)},  { CS( 61), CS(-50)},
    { CS( 43), CS(-52)},  { CS( 45), CS(-49)},  { CS( 29), CS(-60)},  { CS( 23), CS(-58)},
    { CS( 20), CS(-52)},  { CS( 22), CS(-56)},  { CS( 13), CS(-58)},  { CS( 15), CS(-64)},
    { CS( 35), CS(-71)},  { CS( 41), CS(-74)},  { CS( 39), CS(-76)},  { CS( 45), CS(-76)},
    { CS( 42), CS(-72)},  { CS( 44), CS(-68)},  { CS( 50), CS(-69)},  { CS( 49), CS(-75)},
};
static GPoint s_am_south_pts[] = {
    { CS( 56), CS(-33)},  { CS( 54), CS(-37)},  { CS( 60), CS(-39)},  { CS( 69), CS(-36)},
    { CS( 76), CS(-38)},  { CS( 80), CS(-31)},  { CS( 73), CS(-14)},  { CS( 76), CS( -4)},
    { CS( 74), CS( -4)},  { CS( 66), CS(-22)},  { CS( 57), CS(-27)},
};

// REGION_EUROPE_AFRICA (shifted +20 dy upward total)
static GPoint s_eu_europe_pts[] = {
    { CS( 64), CS(-66)},  { CS( 66), CS(-61)},  { CS( 61), CS(-54)},  { CS( 62), CS(-53)},
    { CS( 46), CS(-57)},  { CS( 46), CS(-54)},  { CS( 55), CS(-53)},  { CS( 55), CS(-50)},
    { CS( 43), CS(-52)},  { CS( 38), CS(-47)},  { CS( 36), CS(-36)},  { CS( 33), CS(-33)},
    { CS( 28), CS(-34)},  { CS( 24), CS(-41)},  { CS( 32), CS(-46)},  { CS( 30), CS(-49)},
    { CS( 26), CS(-50)},  { CS( 34), CS(-65)},  { CS( 32), CS(-70)},  { CS( 33), CS(-73)},
    { CS( 37), CS(-69)},  { CS( 50), CS(-73)},  { CS( 55), CS(-65)},  { CS( 59), CS(-70)},
};
static GPoint s_eu_africa_pts[] = {
    { CS( 64), CS(-41)},  { CS( 71), CS(-46)},  { CS( 79), CS(-39)},  { CS( 86), CS(-39)},
    { CS( 86), CS(-28)},  { CS( 88), CS(-15)},  { CS( 85), CS( -6)},  { CS( 68), CS(-24)},
    { CS( 59), CS(-20)},  { CS( 52), CS(-22)},  { CS( 51), CS(-37)},
};

// REGION_ASIA + AUSTRALIA (shifted +20 dy upward total)
static GPoint s_as_asia_pts[] = {
    { CS( 37), CS(-69)},  { CS( 59), CS(-70)},  { CS( 56), CS(-71)},  { CS( 56), CS(-67)},
    { CS( 49), CS(-66)},  { CS( 50), CS(-56)},  { CS( 51), CS(-53)},  { CS( 49), CS(-44)},
    { CS( 49), CS(-42)},  { CS( 43), CS(-46)},  { CS( 42), CS(-40)},  { CS( 30), CS(-43)},
    { CS( 31), CS(-37)},  { CS( 25), CS(-42)},  { CS( 21), CS(-43)},  { CS( 17), CS(-47)},
    { CS( 28), CS(-59)},  { CS( 34), CS(-70)},
};
static GPoint s_as_australia_pts[] = {
    { CS( 95), CS(-29)},  { CS( 97), CS(-19)},  { CS( 91), CS(-17)},  { CS( 80), CS( -6)},
    { CS( 73), CS(-14)},  { CS( 77), CS(-32)},  { CS( 86), CS(-39)},  { CS( 93), CS(-35)},
    { CS( 89), CS(-44)},
};

// GPath storage: [region][shape_a, shape_b]
static GPath *s_continent_paths[3][2];
static GPathInfo s_continent_infos[3][2] = {
    {   // AMERICAS
        { .num_points = 20, .points = s_am_north_pts },
        { .num_points = 11, .points = s_am_south_pts },
    },
    {   // EUROPE-AFRICA
        { .num_points = 24, .points = s_eu_europe_pts },
        { .num_points = 11, .points = s_eu_africa_pts },
    },
    {   // ASIA + AUSTRALIA
        { .num_points = 18, .points = s_as_asia_pts },
        { .num_points = 9,  .points = s_as_australia_pts },
    },
};

// ============================================================================
// STAR POSITIONS
// ============================================================================

static const GPoint STAR_POSITIONS[NUM_STARS] = {
    { 15,  72 }, { 42,  85 }, { 88,  68 }, {118,  80 },
    { 30, 108 }, { 65, 115 }, {105, 105 }, {130,  92 },
    { 18, 135 }, { 55, 142 }, { 90, 130 }, {115, 118 },
    {145, 130 }, {175, 108 }, {183,  72 }, { 72, 165 },
    {100, 158 }, {135, 155 }, {160, 165 }, {188, 140 },
};

// ============================================================================
// TEXT BUFFERS
// ============================================================================

static char s_time_buf[8];
static char s_date_buf[16];

// ============================================================================
// HELPERS
// ============================================================================

static GPoint compute_orbit_pos(GPoint center, int radius, int32_t angle) {
    GPoint p;
    p.x = (int16_t)(center.x + (sin_lookup(angle) * radius) / TRIG_MAX_RATIO);
    p.y = (int16_t)(center.y - (cos_lookup(angle) * radius) / TRIG_MAX_RATIO);
    return p;
}

static bool is_on_screen(GPoint p, int margin) {
    return p.x >= -margin && p.x < SCREEN_W + margin &&
           p.y >= -margin && p.y < SCREEN_H + margin;
}

static ContinentRegion get_continent_region(int lon) {
    if (lon < -30) return REGION_AMERICAS;
    if (lon <  60) return REGION_EUROPE_AFRICA;
    return REGION_ASIA;
}

// Convert a station's longitude to a trig angle relative to the user's longitude
static int32_t lon_to_orbit_angle(int station_lon, int user_lon) {
    int diff = ((station_lon - user_lon) % 360 + 360) % 360;
    return (int32_t)diff * TRIG_MAX_ANGLE / 360;
}

// ============================================================================
// DRAW FUNCTIONS
// ============================================================================

static void draw_stars(GContext *ctx) {
    graphics_context_set_stroke_color(ctx, GColorWhite);
    for (int i = 0; i < NUM_STARS; i++) {
        graphics_draw_pixel(ctx, STAR_POSITIONS[i]);
        if (i % 4 == 0) {
            graphics_draw_pixel(ctx,
                GPoint(STAR_POSITIONS[i].x + 1, STAR_POSITIONS[i].y));
        }
    }
}

static void draw_earth(GContext *ctx) {
    GPoint ec = GPoint(EARTH_CENTER_X, EARTH_CENTER_Y);

    // Ocean base
    graphics_context_set_fill_color(ctx, GColorOxfordBlue);
    graphics_fill_circle(ctx, ec, EARTH_RADIUS);

    // Continents for user's region — two distinct greens to separate shapes
    ContinentRegion region = get_continent_region(s_user_lon);

    // Shape 0: brighter green (Iberia / N.America / China)
    graphics_context_set_fill_color(ctx, GColorGreen);
    gpath_draw_filled(ctx, s_continent_paths[region][0]);
    graphics_context_set_stroke_color(ctx, GColorIslamicGreen);
    graphics_context_set_stroke_width(ctx, 1);
    gpath_draw_outline(ctx, s_continent_paths[region][0]);

    // Shape 1: darker green (Africa / S.America / India)
    graphics_context_set_fill_color(ctx, GColorIslamicGreen);
    gpath_draw_filled(ctx, s_continent_paths[region][1]);
    graphics_context_set_stroke_color(ctx, GColorDarkGreen);
    graphics_context_set_stroke_width(ctx, 1);
    gpath_draw_outline(ctx, s_continent_paths[region][1]);


    // Equator — 45° diagonal chord separating northern and southern continent groups
    graphics_draw_line(ctx,
        GPoint(ec.x + CS(80), ec.y + CS(-60)),
        GPoint(ec.x + CS(-60), ec.y + CS(80)));

    // Earth rim highlight
    graphics_context_set_stroke_color(ctx, GColorCeleste);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_circle(ctx, ec, EARTH_RADIUS);
}

static void draw_orbit_paths(GContext *ctx) {
    GPoint ec = GPoint(EARTH_CENTER_X, EARTH_CENTER_Y);

    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_context_set_stroke_width(ctx, 1);

    int radii[2] = { CREW_DRAGON_ORBIT_RADIUS, SHENZHOU_ORBIT_RADIUS };
    for (int r = 0; r < 2; r++) {
        for (int i = 0; i < 32; i += 2) {
            int32_t a = (i * TRIG_MAX_ANGLE) / 32;
            GPoint p = compute_orbit_pos(ec, radii[r], a);
            if (is_on_screen(p, 0)) {
                graphics_draw_pixel(ctx, p);
            }
        }
    }

    // ISS orbit (lighter dashes)
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    for (int i = 0; i < 32; i += 2) {
        int32_t a = (i * TRIG_MAX_ANGLE) / 32;
        GPoint p = compute_orbit_pos(ec, ISS_ORBIT_RADIUS, a);
        if (is_on_screen(p, 0)) {
            graphics_draw_pixel(ctx, p);
        }
    }

}

static void draw_moon(GContext *ctx) {
    GPoint c = GPoint(MOON_CENTER_X, MOON_CENTER_Y);
    int r = MOON_RADIUS;

    // White disc
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, c, r);

    // Craters — grey filled circles of varying sizes
    graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_circle(ctx, GPoint(c.x - 8, c.y - 6),  5);   // large
    graphics_fill_circle(ctx, GPoint(c.x + 7, c.y + 5),  3);   // medium
    graphics_fill_circle(ctx, GPoint(c.x - 2, c.y + 10), 2);   // small
    graphics_fill_circle(ctx, GPoint(c.x + 4, c.y - 11), 2);   // small
    graphics_fill_circle(ctx, GPoint(c.x + 11, c.y - 3), 2);   // small
    graphics_fill_circle(ctx, GPoint(c.x - 11, c.y + 5), 2);   // small

    // Crater rims — slightly darker ring around the two largest craters
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, GPoint(c.x - 8, c.y - 6), 6);
    graphics_draw_circle(ctx, GPoint(c.x + 7, c.y + 5), 4);

    // Moon rim
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, c, r);
}

static void draw_iss(GContext *ctx, GPoint pos, int32_t angle, int bar_half) {
    if (!is_on_screen(pos, 12)) return;

    // Docking axis: white line tangent to orbit — widens with number of docked ships
    int32_t tx = cos_lookup(angle);
    int32_t ty = sin_lookup(angle);
    GPoint a1 = GPoint(pos.x - (int16_t)(bar_half * tx / TRIG_MAX_RATIO),
                       pos.y - (int16_t)(bar_half * ty / TRIG_MAX_RATIO));
    GPoint a2 = GPoint(pos.x + (int16_t)(bar_half * tx / TRIG_MAX_RATIO),
                       pos.y + (int16_t)(bar_half * ty / TRIG_MAX_RATIO));
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, a1, a2);

    // Flagpole: 8px radially outward (away from Earth)
    int32_t rx = sin_lookup(angle);
    int32_t ry = -cos_lookup(angle);
    GPoint tip = GPoint(pos.x + (int16_t)(8 * rx / TRIG_MAX_RATIO),
                        pos.y + (int16_t)(8 * ry / TRIG_MAX_RATIO));
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, pos, tip);

    // White flag rectangle (to the right of pole tip, axis-aligned)
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(tip.x, tip.y - 5, 8, 5), 0, GCornerNone);

    // Blue dot — Earth/international
    graphics_context_set_fill_color(ctx, GColorBlue);
    graphics_fill_circle(ctx, GPoint(tip.x + 4, tip.y - 3), 2);

    // Station label below
    GFont tiny = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "ISS", tiny,
                       GRect(pos.x - 15, pos.y + 10, 30, 16),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// Chinese Space Station — white body bar + flagpole + red flag (mirrors ISS style)
static void draw_css(GContext *ctx, GPoint pos, int32_t angle, int bar_half) {
    if (!is_on_screen(pos, 12)) return;

    // Body: white line tangent to orbit — widens with number of docked ships
    int32_t tx = cos_lookup(angle);
    int32_t ty = sin_lookup(angle);
    GPoint a1 = GPoint(pos.x - (int16_t)(bar_half * tx / TRIG_MAX_RATIO),
                       pos.y - (int16_t)(bar_half * ty / TRIG_MAX_RATIO));
    GPoint a2 = GPoint(pos.x + (int16_t)(bar_half * tx / TRIG_MAX_RATIO),
                       pos.y + (int16_t)(bar_half * ty / TRIG_MAX_RATIO));
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, a1, a2);

    // Flagpole: 8px radially outward
    int32_t rx = sin_lookup(angle);
    int32_t ry = -cos_lookup(angle);
    GPoint tip = GPoint(pos.x + (int16_t)(8 * rx / TRIG_MAX_RATIO),
                        pos.y + (int16_t)(8 * ry / TRIG_MAX_RATIO));
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, pos, tip);

    // Red flag with yellow star (China)
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_rect(ctx, GRect(tip.x, tip.y - 5, 8, 5), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, GColorYellow);
    graphics_fill_circle(ctx, GPoint(tip.x + 4, tip.y - 3), 2);

    // Station label below
    GFont tiny = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "TIANGONG", tiny,
                       GRect(pos.x - 30, pos.y + 10, 60, 16),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void draw_flag(GContext *ctx, GPoint pos, Country country) {
    GRect flag = GRect(pos.x + 3, pos.y - 5, 7, 5);

    switch (country) {
        case COUNTRY_US:
            // Red base, then blue canton (top-left quarter)
            graphics_context_set_fill_color(ctx, GColorRed);
            graphics_fill_rect(ctx, flag, 0, GCornerNone);
            graphics_context_set_fill_color(ctx, GColorBlue);
            graphics_fill_rect(ctx, GRect(flag.origin.x, flag.origin.y, 3, 2), 0, GCornerNone);
            break;
        case COUNTRY_CN:
            graphics_context_set_fill_color(ctx, GColorRed);
            graphics_fill_rect(ctx, flag, 0, GCornerNone);
            graphics_context_set_fill_color(ctx, GColorYellow);
            graphics_fill_circle(ctx, GPoint(flag.origin.x + 2, flag.origin.y + 2), 1);
            break;
        case COUNTRY_EU:
            graphics_context_set_fill_color(ctx, GColorCobaltBlue);
            graphics_fill_rect(ctx, flag, 0, GCornerNone);
            graphics_context_set_fill_color(ctx, GColorYellow);
            graphics_fill_circle(ctx, GPoint(flag.origin.x + 3, flag.origin.y + 2), 1);
            break;
        case COUNTRY_RU:
            // Russian tricolor: white / blue / red (top to bottom)
            graphics_context_set_fill_color(ctx, GColorWhite);
            graphics_fill_rect(ctx, GRect(flag.origin.x, flag.origin.y,     flag.size.w, 2), 0, GCornerNone);
            graphics_context_set_fill_color(ctx, GColorBlue);
            graphics_fill_rect(ctx, GRect(flag.origin.x, flag.origin.y + 2, flag.size.w, 2), 0, GCornerNone);
            graphics_context_set_fill_color(ctx, GColorRed);
            graphics_fill_rect(ctx, GRect(flag.origin.x, flag.origin.y + 4, flag.size.w, 1), 0, GCornerNone);
            break;
    }

    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_rect(ctx, flag);
}

static void draw_mission_rocket(GContext *ctx, const MissionState *m, int idx, int docked_offset) {
    GPoint pos;
    int32_t travel;
    bool label_above = false;

    switch (m->orbit) {
        case ORBIT_MOON:
            pos    = compute_orbit_pos(GPoint(MOON_CENTER_X, MOON_CENTER_Y),
                                       m->orbit_radius, m->angle);
            travel = (m->angle + TRIG_MAX_ANGLE / 4) & (TRIG_MAX_ANGLE - 1);
            break;
        case TRANSIT_TO_MOON:
            // Linear path from Earth orbit edge (102,169) toward Moon (165,77)
            pos.x  = (int16_t)(102 + (int32_t)(m->angle * 63) / TRIG_MAX_ANGLE);
            pos.y  = (int16_t)(169 - (int32_t)(m->angle * 92) / TRIG_MAX_ANGLE);
            travel = TRIG_MAX_ANGLE / 8;
            break;
        case TRANSIT_TO_EARTH:
            // Linear path from Moon (165,77) back to Earth orbit edge (102,169)
            pos.x  = (int16_t)(165 - (int32_t)(m->angle * 63) / TRIG_MAX_ANGLE);
            pos.y  = (int16_t)(77  + (int32_t)(m->angle * 92) / TRIG_MAX_ANGLE);
            travel = TRIG_MAX_ANGLE * 5 / 8;
            break;
        case ORBIT_HELIOCENTRIC: {
            // Tiny static dot in top-left corner — deep space, no orbit shown
            GPoint p = GPoint(HELIOCENTRIC_X, HELIOCENTRIC_Y);
            graphics_context_set_fill_color(ctx, GColorYellow);
            graphics_fill_circle(ctx, p, 2);
            if (s_missions[idx].name[0] != '\0') {
                graphics_context_set_text_color(ctx, GColorDarkGray);
                graphics_draw_text(ctx, s_missions[idx].name,
                    fonts_get_system_font(FONT_KEY_GOTHIC_14),
                    GRect(p.x + 4, p.y - 6, 80, 20),
                    GTextOverflowModeWordWrap,
                    GTextAlignmentLeft, NULL);
            }
            return;  // skip rocket drawing below
        }
        case ORBIT_DOCKED:
        case ORBIT_DOCKED_CSS: {
            int32_t ref = (m->orbit == ORBIT_DOCKED_CSS) ? s_css_angle : s_iss_angle;
            int ref_r  = (m->orbit == ORBIT_DOCKED_CSS) ? CSS_ORBIT_RADIUS : ISS_ORBIT_RADIUS;
            GPoint station = compute_orbit_pos(GPoint(EARTH_CENTER_X, EARTH_CENTER_Y),
                                               ref_r, ref);
            int32_t tx = cos_lookup(ref);
            int32_t ty = sin_lookup(ref);
            int32_t rx = sin_lookup(ref);
            int32_t ry = -cos_lookup(ref);
            int16_t dock_x = station.x + (int16_t)(docked_offset * tx / TRIG_MAX_RATIO);
            int16_t dock_y = station.y + (int16_t)(docked_offset * ty / TRIG_MAX_RATIO);
            pos.x = dock_x + (int16_t)(7 * rx / TRIG_MAX_RATIO);
            pos.y = dock_y + (int16_t)(7 * ry / TRIG_MAX_RATIO);
            travel = (ref + TRIG_MAX_ANGLE / 2) & (TRIG_MAX_ANGLE - 1);
            label_above = true;
            break;
        }
        default:  // ORBIT_EARTH
            pos    = compute_orbit_pos(GPoint(EARTH_CENTER_X, EARTH_CENTER_Y),
                                       m->orbit_radius, m->angle);
            travel = (m->angle + TRIG_MAX_ANGLE / 4) & (TRIG_MAX_ANGLE - 1);
            break;
    }

    if (!is_on_screen(pos, 10)) return;

    GColor body_color;
    switch (m->country) {
        case COUNTRY_CN: body_color = GColorRed;        break;
        case COUNTRY_EU: body_color = GColorCobaltBlue; break;
        default:         body_color = GColorCobaltBlue; break;
    }

    gpath_move_to(s_rocket_path, pos);
    gpath_rotate_to(s_rocket_path, travel);

    graphics_context_set_fill_color(ctx, body_color);
    gpath_draw_filled(ctx, s_rocket_path);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 1);
    gpath_draw_outline(ctx, s_rocket_path);

    draw_flag(ctx, pos, m->country);

    // Mission name label — right of rocket for docked ships, below for others
    if (s_missions[idx].name[0] != '\0') {
        int label_w = 100;
        int label_h = 28;
        int label_x, label_y;
        label_h = 16;
        label_y = pos.y - label_h / 2;
        GTextAlignment label_align = GTextAlignmentLeft;
        if (label_above) {  // docked: always to the right, 5px higher
            label_w = 80;
            label_x = pos.x + 15;
            if (label_x + label_w > SCREEN_W) label_x = SCREEN_W - label_w;
            label_y -= 5;
        } else {
            // Moon/Earth orbit: 15px gap, flip side based on screen half
            label_w = 60;
            if (pos.x >= SCREEN_W / 2) {
                // Label box is LEFT of rocket — right-align so text ends near rocket
                label_x = pos.x - label_w - 15;
                if (label_x < 0) label_x = 0;
                label_align = GTextAlignmentRight;
            } else {
                label_x = pos.x + 15;
                if (label_x + label_w > SCREEN_W) label_x = SCREEN_W - label_w;
            }
        }
        graphics_context_set_text_color(ctx, GColorLightGray);
        graphics_draw_text(ctx, s_missions[idx].name,
            fonts_get_system_font(FONT_KEY_GOTHIC_14),
            GRect(label_x, label_y, label_w, label_h),
            GTextOverflowModeWordWrap,
            label_align, NULL);
    }
}

// ============================================================================
// CANVAS UPDATE PROC
// ============================================================================

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

    draw_stars(ctx);
    draw_orbit_paths(ctx);
    draw_earth(ctx);
    draw_moon(ctx);

    GPoint ec = GPoint(EARTH_CENTER_X, EARTH_CENTER_Y);

    // Count docked ships per station to size the body bar
    int iss_docked = 0, css_docked = 0;
    for (int i = 0; i < NUM_MISSIONS; i++) {
        if (!s_missions[i].active) continue;
        if (s_missions[i].orbit == ORBIT_DOCKED)     iss_docked++;
        if (s_missions[i].orbit == ORBIT_DOCKED_CSS) css_docked++;
    }
    int iss_bar = (iss_docked > 2) ? (iss_docked - 1) * 10 + 4 : 14;
    int css_bar = (css_docked > 2) ? (css_docked - 1) * 10 + 4 : 14;

    GPoint iss_pos = compute_orbit_pos(ec, ISS_ORBIT_RADIUS, s_iss_angle);
    draw_iss(ctx, iss_pos, s_iss_angle, iss_bar);

    GPoint css_pos = compute_orbit_pos(ec, CSS_ORBIT_RADIUS, s_css_angle);
    draw_css(ctx, css_pos, s_css_angle, css_bar);

    // Compute tangential offsets — ISS and CSS groups independently centred
    int docked_offsets[NUM_MISSIONS];
    memset(docked_offsets, 0, sizeof(docked_offsets));
    for (int pass = 0; pass < 2; pass++) {
        OrbitType ot = (pass == 0) ? ORBIT_DOCKED : ORBIT_DOCKED_CSS;
        int total = 0, rank = 0;
        for (int i = 0; i < NUM_MISSIONS; i++)
            if (s_missions[i].active && s_missions[i].orbit == ot) total++;
        for (int i = 0; i < NUM_MISSIONS; i++) {
            if (!s_missions[i].active || s_missions[i].orbit != ot) continue;
            docked_offsets[i] = rank * 20 - (total - 1) * 10;
            rank++;
        }
    }

    for (int i = 0; i < NUM_MISSIONS; i++) {
        if (s_missions[i].active) {
            draw_mission_rocket(ctx, &s_missions[i], i, docked_offsets[i]);
        }
    }
}

// ============================================================================
// ANIMATION TIMER
// ============================================================================

static void animation_update(void) {
#ifndef DEBUG_STATIONS
    // Real-speed orbital drift: 1 unit per 2 ticks ≈ 109 min/orbit
    s_anim_tick++;
    if ((s_anim_tick & 1) == 0) s_iss_angle = (s_iss_angle + 1) & (TRIG_MAX_ANGLE - 1);
    else                         s_css_angle = (s_css_angle + 1) & (TRIG_MAX_ANGLE - 1);
#endif

    for (int i = 0; i < NUM_MISSIONS; i++) {
        if (s_missions[i].active &&
            s_missions[i].orbit != ORBIT_DOCKED &&
            s_missions[i].orbit != ORBIT_DOCKED_CSS &&
            s_missions[i].orbit != ORBIT_HELIOCENTRIC) {
            s_missions[i].angle =
                (s_missions[i].angle + s_missions[i].angle_speed) & (TRIG_MAX_ANGLE - 1);
        }
    }

    if (s_canvas_layer) {
        layer_mark_dirty(s_canvas_layer);
    }
}

static void anim_timer_callback(void *data) {
    animation_update();
    uint32_t interval = (s_battery_level <= LOW_BATTERY_THRESHOLD)
                        ? ANIMATION_INTERVAL_LOW_BAT : ANIMATION_INTERVAL;
    s_anim_timer = app_timer_register(interval, anim_timer_callback, NULL);
}

// ============================================================================
// TICK HANDLER
// ============================================================================

static void update_time_display(struct tm *t) {
    if (clock_is_24h_style()) {
        strftime(s_time_buf, sizeof(s_time_buf), "%H:%M", t);
    } else {
        strftime(s_time_buf, sizeof(s_time_buf), "%I:%M", t);
    }
    text_layer_set_text(s_time_layer, s_time_buf);

    // Use locale-appropriate day/month order:
    //   US (12h): MON APR 07
    //   non-US (24h): MON 07 APR
    if (clock_is_24h_style()) {
        strftime(s_date_buf, sizeof(s_date_buf), "%a %d %b", t);
    } else {
        strftime(s_date_buf, sizeof(s_date_buf), "%a %b %d", t);
    }
    for (int i = 0; s_date_buf[i]; i++) {
        if (s_date_buf[i] >= 'a' && s_date_buf[i] <= 'z') {
            s_date_buf[i] = (char)(s_date_buf[i] - 32);
        }
    }
    text_layer_set_text(s_date_layer, s_date_buf);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    update_time_display(tick_time);

    if (tick_time->tm_min % 30 == 0) {
        DictionaryIterator *iter;
        AppMessageResult result = app_message_outbox_begin(&iter);
        if (result == APP_MSG_OK) {
            dict_write_uint8(iter, MESSAGE_KEY_REQUEST_UPDATE, 1);
            app_message_outbox_send();
        }
    }
}

// ============================================================================
// BATTERY CALLBACK
// ============================================================================

static void battery_callback(BatteryChargeState state) {
    s_battery_level = state.charge_percent;
}

// ============================================================================
// APPMESSAGE CALLBACKS
// ============================================================================

static void update_mission_from_message(int idx, Country country, OrbitType orbit,
                                        const char *name) {
    if (idx < 0 || idx >= NUM_MISSIONS) return;
    s_missions[idx].country = country;
    s_missions[idx].orbit   = orbit;
    s_missions[idx].active  = true;
    if (orbit == ORBIT_MOON) {
        s_missions[idx].orbit_radius = ARTEMIS_ORBIT_RADIUS;
        if (s_missions[idx].angle_speed == 0)
            s_missions[idx].angle_speed = 300 + (idx * 37);
    } else if (orbit == ORBIT_DOCKED || orbit == ORBIT_DOCKED_CSS) {
        s_missions[idx].orbit_radius = (orbit == ORBIT_DOCKED_CSS) ? CSS_ORBIT_RADIUS : ISS_ORBIT_RADIUS;
    } else {
        if (s_missions[idx].orbit_radius == 0)
            s_missions[idx].orbit_radius = CREW_DRAGON_ORBIT_RADIUS;
        if (s_missions[idx].angle_speed == 0)
            s_missions[idx].angle_speed = 300 + (idx * 20);
    }
    if (name && name[0] != '\0') {
        strncpy(s_missions[idx].name, name, sizeof(s_missions[idx].name) - 1);
        s_missions[idx].name[sizeof(s_missions[idx].name) - 1] = '\0';
    }
    APP_LOG(APP_LOG_LEVEL_INFO, "Mission %d: %s country=%d orbit=%d",
            idx, s_missions[idx].name, (int)country, (int)orbit);
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
    // User location — process first so lon_to_orbit_angle is correct below
    Tuple *lon_tuple = dict_find(iterator, MESSAGE_KEY_USER_LON);
    if (lon_tuple) {
        s_user_lon = (int)lon_tuple->value->int32;
        APP_LOG(APP_LOG_LEVEL_INFO, "User lon: %d -> region %d",
                s_user_lon, (int)get_continent_region(s_user_lon));
    }

    // ISS
    Tuple *iss_lon = dict_find(iterator, MESSAGE_KEY_ISS_LON);
    if (iss_lon) {
        APP_LOG(APP_LOG_LEVEL_INFO, "ISS lon=%d", (int)iss_lon->value->int32);
#ifndef DEBUG_STATIONS
        s_iss_angle = lon_to_orbit_angle((int)iss_lon->value->int32, s_user_lon);
#endif
    }

    // CSS (Chinese Space Station)
    Tuple *css_lon = dict_find(iterator, MESSAGE_KEY_CSS_LON);
    if (css_lon) {
        APP_LOG(APP_LOG_LEVEL_INFO, "CSS lon=%d", (int)css_lon->value->int32);
#ifndef DEBUG_STATIONS
        s_css_angle = lon_to_orbit_angle((int)css_lon->value->int32, s_user_lon);
#endif
    }

    // Missions (country, orbit, name, optional lon for docked positioning)
    const uint32_t mission_key_country[NUM_MISSIONS] = {
        MESSAGE_KEY_MISSION_0_COUNTRY, MESSAGE_KEY_MISSION_1_COUNTRY,
        MESSAGE_KEY_MISSION_2_COUNTRY, MESSAGE_KEY_MISSION_3_COUNTRY,
        MESSAGE_KEY_MISSION_4_COUNTRY, MESSAGE_KEY_MISSION_5_COUNTRY,
        MESSAGE_KEY_MISSION_6_COUNTRY, MESSAGE_KEY_MISSION_7_COUNTRY,
        MESSAGE_KEY_MISSION_8_COUNTRY, MESSAGE_KEY_MISSION_9_COUNTRY
    };
    const uint32_t mission_key_orbit[NUM_MISSIONS] = {
        MESSAGE_KEY_MISSION_0_ORBIT, MESSAGE_KEY_MISSION_1_ORBIT,
        MESSAGE_KEY_MISSION_2_ORBIT, MESSAGE_KEY_MISSION_3_ORBIT,
        MESSAGE_KEY_MISSION_4_ORBIT, MESSAGE_KEY_MISSION_5_ORBIT,
        MESSAGE_KEY_MISSION_6_ORBIT, MESSAGE_KEY_MISSION_7_ORBIT,
        MESSAGE_KEY_MISSION_8_ORBIT, MESSAGE_KEY_MISSION_9_ORBIT
    };
    const uint32_t mission_key_name[NUM_MISSIONS] = {
        MESSAGE_KEY_MISSION_0_NAME, MESSAGE_KEY_MISSION_1_NAME,
        MESSAGE_KEY_MISSION_2_NAME, MESSAGE_KEY_MISSION_3_NAME,
        MESSAGE_KEY_MISSION_4_NAME, MESSAGE_KEY_MISSION_5_NAME,
        MESSAGE_KEY_MISSION_6_NAME, MESSAGE_KEY_MISSION_7_NAME,
        MESSAGE_KEY_MISSION_8_NAME, MESSAGE_KEY_MISSION_9_NAME
    };
    const uint32_t mission_key_lon[NUM_MISSIONS] = {
        MESSAGE_KEY_MISSION_0_LON, MESSAGE_KEY_MISSION_1_LON,
        MESSAGE_KEY_MISSION_2_LON, MESSAGE_KEY_MISSION_3_LON,
        MESSAGE_KEY_MISSION_4_LON, MESSAGE_KEY_MISSION_5_LON,
        MESSAGE_KEY_MISSION_6_LON, MESSAGE_KEY_MISSION_7_LON,
        MESSAGE_KEY_MISSION_8_LON, MESSAGE_KEY_MISSION_9_LON
    };
    for (int i = 0; i < NUM_MISSIONS; i++) {
        Tuple *mc = dict_find(iterator, mission_key_country[i]);
        Tuple *mo = dict_find(iterator, mission_key_orbit[i]);
        Tuple *mn = dict_find(iterator, mission_key_name[i]);
        Tuple *ml = dict_find(iterator, mission_key_lon[i]);
        if (mc && mo) {
            update_mission_from_message(i, (Country)mc->value->int32,
                                           (OrbitType)mo->value->int32,
                                           mn ? mn->value->cstring : NULL);
            if (ml) s_missions[i].angle = lon_to_orbit_angle((int)ml->value->int32, s_user_lon);
        }
    }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "AppMessage inbox dropped: %d", (int)reason);
}

// ============================================================================
// WINDOW LIFECYCLE
// ============================================================================

static void main_window_load(Window *window) {
    window_set_background_color(window, GColorBlack);
    Layer *root = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root);

    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(root, s_canvas_layer);

    s_time_layer = text_layer_create(
        GRect(0, TIME_LAYER_Y, bounds.size.w, TIME_LAYER_H));
    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_text_color(s_time_layer, GColorWhite);
    text_layer_set_font(s_time_layer,
        fonts_get_system_font(FONT_KEY_LECO_38_BOLD_NUMBERS));
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_time_layer));

    s_date_layer = text_layer_create(
        GRect(0, DATE_LAYER_Y, bounds.size.w, DATE_LAYER_H));
    text_layer_set_background_color(s_date_layer, GColorClear);
    text_layer_set_text_color(s_date_layer, GColorCadetBlue);
    text_layer_set_font(s_date_layer,
        fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_date_layer));

    s_mission_layer = text_layer_create(
        GRect(0, MISSION_LABEL_Y, bounds.size.w, MISSION_LABEL_H));
    text_layer_set_background_color(s_mission_layer, GColorClear);
    text_layer_set_text_color(s_mission_layer, GColorDarkGray);
    text_layer_set_font(s_mission_layer,
        fonts_get_system_font(FONT_KEY_GOTHIC_14));
    text_layer_set_text_alignment(s_mission_layer, GTextAlignmentCenter);
    text_layer_set_text(s_mission_layer, "LIVE SPACE MISSIONS");
    layer_add_child(root, text_layer_get_layer(s_mission_layer));

    // Rocket GPath
    s_rocket_path = gpath_create(&s_rocket_info);

    // Continent GPaths — all 3 regions, 2 shapes each
    GPoint earth_anchor = GPoint(EARTH_CENTER_X, EARTH_CENTER_Y);
    for (int reg = 0; reg < 3; reg++) {
        for (int shape = 0; shape < 2; shape++) {
            s_continent_paths[reg][shape] =
                gpath_create(&s_continent_infos[reg][shape]);
            gpath_move_to(s_continent_paths[reg][shape], earth_anchor);
        }
    }

    // Initial time and earth rotation
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t) {
        update_time_display(t);
    }

    s_anim_timer = app_timer_register(ANIMATION_INTERVAL, anim_timer_callback, NULL);
}

static void main_window_unload(Window *window) {
    if (s_anim_timer) {
        app_timer_cancel(s_anim_timer);
        s_anim_timer = NULL;
    }

    if (s_rocket_path) {
        gpath_destroy(s_rocket_path);
        s_rocket_path = NULL;
    }

    for (int reg = 0; reg < 3; reg++) {
        for (int shape = 0; shape < 2; shape++) {
            if (s_continent_paths[reg][shape]) {
                gpath_destroy(s_continent_paths[reg][shape]);
                s_continent_paths[reg][shape] = NULL;
            }
        }
    }

    if (s_canvas_layer)   { layer_destroy(s_canvas_layer);        s_canvas_layer   = NULL; }
    if (s_time_layer)     { text_layer_destroy(s_time_layer);     s_time_layer     = NULL; }
    if (s_date_layer)     { text_layer_destroy(s_date_layer);     s_date_layer     = NULL; }
    if (s_mission_layer)  { text_layer_destroy(s_mission_layer);  s_mission_layer  = NULL; }
}

// ============================================================================
// APP LIFECYCLE
// ============================================================================

static void init(void) {
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_open(512, 64);

    battery_state_service_subscribe(battery_callback);
    BatteryChargeState bstate = battery_state_service_peek();
    s_battery_level = bstate.charge_percent;

    s_main_window = window_create();
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load   = main_window_load,
        .unload = main_window_unload,
    });
    window_stack_push(s_main_window, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit(void) {
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    app_message_deregister_callbacks();
    window_destroy(s_main_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}
