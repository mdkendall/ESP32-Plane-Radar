#include "ui/radar_display.h"

#include <Arduino.h>
#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/runway_overlay.h"

namespace ui {
namespace radar {

uint16_t kColorBackground = 0x0000;
uint16_t kColorGrid = 0x0320;
uint16_t kColorLabel = 0xFFFF;
uint16_t kColorCenter = 0xFFFF;
uint16_t kColorAircraft = 0x001F;
uint16_t kColorTrackVector = 0xFFFF;
uint16_t kColorTagType = 0x5DFF;
uint16_t kColorTagAltitude = 0xFFE0;
uint16_t kColorRunway = 0x4D5F;
uint16_t kColorRunwayLabel = 0x7DFF;

}  // namespace radar

namespace {

bool s_label_metrics_ready = false;
bool s_cardinal_use_vlw = false;
bool s_scale_use_vlw = false;
float s_cardinal_vlw_size = 0.56f;
float s_scale_vlw_size = 0.50f;
float s_tag_vlw_size = 0.56f;
const lgfx::GFXfont* s_cardinal_gfx = &fonts::FreeSansBold12pt7b;
const lgfx::GFXfont* s_scale_gfx = &fonts::FreeSansBold9pt7b;
const lgfx::GFXfont* s_tag_gfx = &fonts::FreeSansBold12pt7b;

bool s_tag_label_metrics_ready = false;
bool s_tag_use_vlw = false;

int s_scale_label_max_w = 0;
int s_scale_label_h = 0;

lgfx::LovyanGFX* s_draw = &tft;
LGFX_Sprite s_frame(&tft);
bool s_frame_ready = false;

class DrawScope {
 public:
  explicit DrawScope(lgfx::LovyanGFX& gfx) : prev_(s_draw) { s_draw = &gfx; }
  ~DrawScope() { s_draw = prev_; }

 private:
  lgfx::LovyanGFX* prev_;
};

int absDiff(int a, int b) { return std::abs(a - b); }

int measureGfxHeight(const lgfx::GFXfont& font) {
  tft.setFont(&font);
  tft.setTextSize(1);
  return tft.fontHeight();
}

int measureVlwHeight(float size) {
  tft.setTextSize(size);
  return tft.fontHeight();
}

float findVlwSizeForHeight(int target_px) {
  float lo = 0.25f;
  float hi = 1.2f;
  for (int i = 0; i < 16; ++i) {
    const float mid = (lo + hi) * 0.5f;
    if (measureVlwHeight(mid) < target_px) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return hi;
}

void applyScaleStyle();
int scaleLabelAnchorX(int cx, int outer_radius);

const lgfx::GFXfont* pickGfxFontClosest(
    int target_px, const lgfx::GFXfont* const* candidates, size_t count) {
  const lgfx::GFXfont* best = candidates[0];
  int best_diff = absDiff(measureGfxHeight(*best), target_px);

  for (size_t i = 1; i < count; ++i) {
    const int diff = absDiff(measureGfxHeight(*candidates[i]), target_px);
    if (diff < best_diff) {
      best_diff = diff;
      best = candidates[i];
    }
  }
  return best;
}

void initLabelMetrics() {
  if (s_label_metrics_ready) {
    return;
  }

  const int cardinal_target = radar::kCardinalLabelHeightPx;

  if (displayFontIsSmooth()) {
    s_cardinal_use_vlw = true;
    s_cardinal_vlw_size = findVlwSizeForHeight(cardinal_target);
    const int cardinal_h = measureVlwHeight(s_cardinal_vlw_size);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    s_scale_use_vlw = true;
    s_scale_vlw_size = findVlwSizeForHeight(scale_target);
  } else {
    const lgfx::GFXfont* cardinal_candidates[] = {&fonts::FreeSansBold12pt7b,
                                                  &fonts::FreeSansBold9pt7b};
    s_cardinal_gfx =
        pickGfxFontClosest(cardinal_target, cardinal_candidates, 2);
    s_cardinal_use_vlw = false;

    const int cardinal_h = measureGfxHeight(*s_cardinal_gfx);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    const lgfx::GFXfont* scale_candidates[] = {&fonts::FreeSansBold9pt7b,
                                               &fonts::FreeSansBold12pt7b};
    s_scale_gfx = pickGfxFontClosest(scale_target, scale_candidates, 2);
    s_scale_use_vlw = false;
  }

  applyScaleStyle();
  s_scale_label_h = tft.fontHeight();
  s_scale_label_max_w = 0;
  char label[12];
  for (size_t i = 0; i < radar::kRangePresetCount; ++i) {
    for (bool miles : {false, true}) {
      radar::formatRing3Label(label, sizeof(label), radar::kRangePresets[i].ring3_km,
                              miles);
      const int w = tft.textWidth(label);
      if (w > s_scale_label_max_w) {
        s_scale_label_max_w = w;
      }
    }
  }

  s_label_metrics_ready = true;
}

void initTagLabelMetrics() {
  if (s_tag_label_metrics_ready) {
    return;
  }

  const int target = radar::kAircraftTagLabelHeightPx;
  if (displayFontIsSmooth()) {
    s_tag_use_vlw = true;
    s_tag_vlw_size = findVlwSizeForHeight(target);
  } else {
    const lgfx::GFXfont* tag_candidates[] = {&fonts::FreeSansBold12pt7b,
                                               &fonts::FreeSansBold9pt7b};
    s_tag_gfx = pickGfxFontClosest(target, tag_candidates, 2);
    s_tag_use_vlw = false;
  }

  s_tag_label_metrics_ready = true;
}

void initPalette() {
  radar::kColorBackground = tft.color565(radar::kBgR, radar::kBgG, radar::kBgB);
  radar::kColorGrid = tft.color565(radar::kGridR, radar::kGridG, radar::kGridB);
  radar::kColorLabel = tft.color565(255, 255, 255);
  radar::kColorCenter = tft.color565(255, 255, 255);
  // GC9A01 BGR panel: swap R/B in color565 so logical red renders red on screen.
  if (config::kDisplayRgbOrder) {
    radar::kColorAircraft =
        tft.color565(radar::kAircraftB, radar::kAircraftG, radar::kAircraftR);
  } else {
    radar::kColorAircraft =
        tft.color565(radar::kAircraftR, radar::kAircraftG, radar::kAircraftB);
  }
  radar::kColorTrackVector =
      tft.color565(radar::kTrackR, radar::kTrackG, radar::kTrackB);
  radar::kColorTagType =
      tft.color565(radar::kTagTypeR, radar::kTagTypeG, radar::kTagTypeB);
  radar::kColorTagAltitude =
      tft.color565(radar::kTagAltR, radar::kTagAltG, radar::kTagAltB);
  radar::kColorRunway =
      tft.color565(radar::kRunwayR, radar::kRunwayG, radar::kRunwayB);
  radar::kColorRunwayLabel = tft.color565(radar::kRunwayLabelR, radar::kRunwayLabelG,
                                          radar::kRunwayLabelB);
}

constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 3.14159265f / 180.0f;

void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* dist_km) {
  // Longitude degrees shrink toward the poles; scale by cos(latitude) so
  // east-west distance isn't overstated away from the equator.
  const float center_lat_rad =
      static_cast<float>(services::location::lat()) * kDegToRad;
  *dx_km = static_cast<float>(lon - services::location::lon()) * kKmPerDeg *
           cosf(center_lat_rad);
  *dy_km =
      static_cast<float>(lat - services::location::lat()) * kKmPerDeg;
  *dist_km = sqrtf((*dx_km) * (*dx_km) + (*dy_km) * (*dy_km));
}

/**
 * Dead-reckon an aircraft's position from its last-fetched fix along its ground
 * track, so it moves smoothly between ADS-B updates. Uses the same flat
 * 1° ≈ 111 km projection as offsetKmFromCenter(), so it round-trips exactly.
 */
void extrapolatedLatLon(const services::adsb::Aircraft& plane,
                        unsigned long base_ms, float* lat, float* lon) {
  *lat = plane.lat;
  *lon = plane.lon;
  if (base_ms == 0 || plane.gs_knots <= 0.0f) {
    return;
  }
  // Elapsed since the fix was measured = time since fetch + the fix's own age.
  const unsigned long elapsed_ms = (millis() - base_ms) + plane.pos_age_ms;
  const float elapsed_h = static_cast<float>(elapsed_ms) / 3600000.0f;
  const float dist_km = plane.gs_knots * 1.852f * elapsed_h;  // knots -> km
  if (dist_km <= 0.0f) {
    return;
  }
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = plane.track_deg * kDegToRad;  // track: 0 = N, 90 = E
  *lat = plane.lat + (dist_km * cosf(rad)) / kKmPerDeg;
  *lon = plane.lon + (dist_km * sinf(rad)) / kKmPerDeg;
}

float innerRingMaxKm() {
  const float outer_km = radar::rangeCurrent().outer_km;
  return outer_km * (static_cast<float>(radar::kGridOuterRadius -
                                       radar::kAircraftInsideRingInsetPx) /
                     static_cast<float>(radar::kGridOuterRadius));
}

/** Flat lat/lon as x/y: 1° ≈ 111 km, north = screen up. */
void latLonToScreen(float lat, float lon, int* out_x, int* out_y) {
  const float outer_km = radar::rangeCurrent().outer_km;
  const float px_per_km = static_cast<float>(radar::kGridOuterRadius) / outer_km;

  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);

  *out_x = radar::kCenterX + static_cast<int>(lroundf(dx_km * px_per_km));
  *out_y = radar::kCenterY - static_cast<int>(lroundf(dy_km * px_per_km));
}

bool isInsideOuterRingKm(float dist_km) { return dist_km <= innerRingMaxKm(); }

int distSqFromCenter(int x, int y) {
  const int dx = x - radar::kCenterX;
  const int dy = y - radar::kCenterY;
  return dx * dx + dy * dy;
}

bool isInsideOuterRing(int x, int y) {
  const int max_r = radar::kGridOuterRadius - radar::kAircraftInsideRingInsetPx;
  return distSqFromCenter(x, y) <= max_r * max_r;
}

/** Rim dot from true bearing; always on screen edge (even if target is 50+ km away). */
bool beyondRingEdgeDotFromLatLon(float lat, float lon, int* out_x, int* out_y) {
  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);
  if (dist_km < 0.01f) {
    return false;
  }
  if (isInsideOuterRingKm(dist_km)) {
    return false;
  }

  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int rim_r = radar::kCenterX - radar::kBeyondRingScreenMarginPx;
  const float angle_rad = atan2f(dx_km, dy_km);

  *out_x = cx + static_cast<int>(lroundf(sinf(angle_rad) * rim_r));
  *out_y = cy - static_cast<int>(lroundf(cosf(angle_rad) * rim_r));
  return true;
}

void drawBeyondRingDot(int x, int y) {
  s_draw->fillSmoothCircle(x, y, radar::kBeyondRingDotRadiusPx,
                           radar::kColorAircraft);
}

void clipPointToOuterRing(int x0, int y0, int* x1, int* y1) {
  const int max_r = radar::kGridOuterRadius;
  const int max_r_sq = max_r * max_r;
  if (distSqFromCenter(*x1, *y1) <= max_r_sq) {
    return;
  }

  const int dx = *x1 - x0;
  const int dy = *y1 - y0;
  float t = 1.0f;
  for (int step = 0; step < 20; ++step) {
    const int px = x0 + static_cast<int>(lroundf(dx * t));
    const int py = y0 + static_cast<int>(lroundf(dy * t));
    if (distSqFromCenter(px, py) <= max_r_sq) {
      *x1 = px;
      *y1 = py;
      return;
    }
    t -= 0.05f;
    if (t <= 0.0f) {
      *x1 = x0;
      *y1 = y0;
      return;
    }
  }
}

int speedLineLengthPx(float gs_knots) {
  if (gs_knots <= 0.0f) {
    return 0;
  }

  // Fixed screen scale: 60 s horizon at gs, not tied to current range zoom.
  constexpr float kKmPerKnotPerHorizon =
      1.852f * radar::kAircraftTrackHorizonSec / 3600.0f;
  const float px =
      gs_knots * kKmPerKnotPerHorizon * radar::kGridOuterRadius /
      radar::kAircraftTrackRefOuterKm * radar::kAircraftTrackLengthScale;

  const int len = static_cast<int>(px + 0.5f);
  if (len < radar::kAircraftSpeedLineMinPx) {
    return radar::kAircraftSpeedLineMinPx;
  }
  return len;
}

void noseTip(int cx, int cy, float heading_deg, int* tip_x, int* tip_y) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  *tip_x = cx + static_cast<int>(lroundf(sinf(rad) * radar::kAircraftNoseLenPx));
  *tip_y = cy - static_cast<int>(lroundf(cosf(rad) * radar::kAircraftNoseLenPx));
}

void drawHeadingTriangle(int cx, int cy, float heading_deg, uint16_t color) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  const float sin_h = sinf(rad);
  const float cos_h = cosf(rad);

  int tip_x = 0;
  int tip_y = 0;
  noseTip(cx, cy, heading_deg, &tip_x, &tip_y);

  const int base_x =
      cx - static_cast<int>(lroundf(sin_h * static_cast<float>(radar::kAircraftTailLenPx)));
  const int base_y =
      cy + static_cast<int>(lroundf(cos_h * static_cast<float>(radar::kAircraftTailLenPx)));

  const int wing_x = static_cast<int>(lroundf(cos_h * radar::kAircraftTailHalfPx));
  const int wing_y = static_cast<int>(lroundf(sin_h * radar::kAircraftTailHalfPx));

  s_draw->fillTriangle(tip_x, tip_y, base_x + wing_x, base_y + wing_y,
                       base_x - wing_x, base_y - wing_y, color);
}

void drawSpeedVector(int cx, int cy, float heading_deg, float track_deg,
                     float gs_knots, uint16_t color) {
  const int len = speedLineLengthPx(gs_knots);
  if (len <= 0) {
    return;
  }

  int tip_x = 0;
  int tip_y = 0;
  noseTip(cx, cy, heading_deg, &tip_x, &tip_y);

  constexpr float kDegToRad = 0.01745329252f;
  const float rad = track_deg * kDegToRad;
  int ex = tip_x + static_cast<int>(lroundf(sinf(rad) * len));
  int ey = tip_y - static_cast<int>(lroundf(cosf(rad) * len));
  clipPointToOuterRing(tip_x, tip_y, &ex, &ey);
  if (ex == tip_x && ey == tip_y) {
    return;
  }
  s_draw->drawWideLine(tip_x, tip_y, ex, ey, radar::kAircraftTrackLineHalfWidth,
                       color);
}

void applyTagStyle() {
  if (s_tag_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_tag_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_tag_gfx);
  }
}

int measureTagBlockWidth(const services::adsb::Aircraft& plane) {
  applyTagStyle();
  int max_w = 0;
  if (plane.callsign[0] != '\0') {
    const int w = s_draw->textWidth(plane.callsign);
    if (w > max_w) {
      max_w = w;
    }
  }
  if (plane.type[0] != '\0') {
    const int w = s_draw->textWidth(plane.type);
    if (w > max_w) {
      max_w = w;
    }
  }
  if (plane.alt[0] != '\0') {
    const int w = s_draw->textWidth(plane.alt);
    if (w > max_w) {
      max_w = w;
    }
  }
  return max_w;
}

bool hasAircraftTag(const services::adsb::Aircraft& plane) {
  return plane.callsign[0] != '\0' || plane.type[0] != '\0' ||
         plane.alt[0] != '\0';
}

struct TagRect {
  TagRect() : x(0), y(0), width(0), height(0) {}
  TagRect(int x_in, int y_in, int width_in, int height_in)
      : x(x_in), y(y_in), width(width_in), height(height_in) {}

  int x;
  int y;
  int width;
  int height;
};

struct TagPlacement {
  TagRect rect;
  uint8_t candidate = 0;
};

constexpr uint8_t kTagCandidateCount = 8;
constexpr uint8_t kTagRight = 0;
constexpr uint8_t kTagLeft = 1;
constexpr uint8_t kTagRightUp = 2;
constexpr uint8_t kTagLeftUp = 3;
constexpr uint8_t kTagRightDown = 4;
constexpr uint8_t kTagLeftDown = 5;
constexpr uint8_t kTagAbove = 6;
constexpr uint8_t kTagBelow = 7;
constexpr uint32_t kTagCacheExpiryFrames = 80;

struct TagCacheEntry {
  char hex[sizeof(services::adsb::Aircraft::hex)] = {};
  uint8_t candidate = 0;
  uint32_t last_seen_frame = 0;
};

TagCacheEntry s_tag_cache[services::adsb::kMaxAircraft];
uint32_t s_tag_layout_frame = 0;
uint8_t s_tag_cache_range_index = 0xff;

bool tagRectsOverlap(const TagRect& a, const TagRect& b) {
  return a.x < b.x + b.width && a.x + a.width > b.x &&
         a.y < b.y + b.height && a.y + a.height > b.y;
}

int tagOverlapArea(const TagRect& a, const TagRect& b) {
  const int left = std::max(a.x, b.x);
  const int top = std::max(a.y, b.y);
  const int right = std::min(a.x + a.width, b.x + b.width);
  const int bottom = std::min(a.y + a.height, b.y + b.height);
  return right > left && bottom > top ? (right - left) * (bottom - top) : 0;
}

bool tagRectOnScreen(const TagRect& rect) {
  return rect.x >= 1 && rect.y >= 1 &&
         rect.x + rect.width < radar::kSize - 1 &&
         rect.y + rect.height < radar::kSize - 1;
}

TagPlacement tagPlacementForCandidate(int x, int y, int width, int height,
                                      uint8_t candidate) {
  const int symbol_half =
      radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx;
  const int side_offset = symbol_half + radar::kAircraftLabelGapPx;
  TagPlacement placement;
  placement.candidate = candidate;
  placement.rect.width = width;
  placement.rect.height = height;

  switch (candidate) {
    case kTagRight:
      placement.rect.x = x + side_offset;
      placement.rect.y = y - height / 2;
      break;
    case kTagLeft:
      placement.rect.x = x - side_offset - width;
      placement.rect.y = y - height / 2;
      break;
    case kTagRightUp:
      placement.rect.x = x + side_offset;
      placement.rect.y = y - side_offset - height;
      break;
    case kTagLeftUp:
      placement.rect.x = x - side_offset - width;
      placement.rect.y = y - side_offset - height;
      break;
    case kTagRightDown:
      placement.rect.x = x + side_offset;
      placement.rect.y = y + side_offset;
      break;
    case kTagLeftDown:
      placement.rect.x = x - side_offset - width;
      placement.rect.y = y + side_offset;
      break;
    case kTagAbove:
      placement.rect.x = x - width / 2;
      placement.rect.y = y - side_offset - height;
      break;
    default:
      placement.rect.x = x - width / 2;
      placement.rect.y = y + side_offset;
      break;
  }
  return placement;
}

bool tagIntersectsPoint(const TagRect& rect, int x, int y) {
  return x >= rect.x && x < rect.x + rect.width && y >= rect.y &&
         y < rect.y + rect.height;
}

int crossProduct(int ax, int ay, int bx, int by, int cx, int cy) {
  return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

bool pointOnSegment(int ax, int ay, int bx, int by, int px, int py) {
  return px >= std::min(ax, bx) && px <= std::max(ax, bx) &&
         py >= std::min(ay, by) && py <= std::max(ay, by);
}

bool segmentsIntersect(int ax, int ay, int bx, int by, int cx, int cy, int dx,
                       int dy) {
  const int ab_c = crossProduct(ax, ay, bx, by, cx, cy);
  const int ab_d = crossProduct(ax, ay, bx, by, dx, dy);
  const int cd_a = crossProduct(cx, cy, dx, dy, ax, ay);
  const int cd_b = crossProduct(cx, cy, dx, dy, bx, by);
  if (((ab_c > 0 && ab_d < 0) || (ab_c < 0 && ab_d > 0)) &&
      ((cd_a > 0 && cd_b < 0) || (cd_a < 0 && cd_b > 0))) {
    return true;
  }
  return (ab_c == 0 && pointOnSegment(ax, ay, bx, by, cx, cy)) ||
         (ab_d == 0 && pointOnSegment(ax, ay, bx, by, dx, dy)) ||
         (cd_a == 0 && pointOnSegment(cx, cy, dx, dy, ax, ay)) ||
         (cd_b == 0 && pointOnSegment(cx, cy, dx, dy, bx, by));
}

bool tagIntersectsSegment(const TagRect& rect, int x0, int y0, int x1, int y1) {
  if (tagIntersectsPoint(rect, x0, y0) || tagIntersectsPoint(rect, x1, y1)) {
    return true;
  }
  const int right = rect.x + rect.width - 1;
  const int bottom = rect.y + rect.height - 1;
  return segmentsIntersect(x0, y0, x1, y1, rect.x, rect.y, right, rect.y) ||
         segmentsIntersect(x0, y0, x1, y1, right, rect.y, right, bottom) ||
         segmentsIntersect(x0, y0, x1, y1, right, bottom, rect.x, bottom) ||
         segmentsIntersect(x0, y0, x1, y1, rect.x, bottom, rect.x, rect.y);
}

bool tagIntersectsStaticUi(const TagRect& rect) {
  const int cardinal_half_w = radar::kCardinalLabelHeightPx;
  const int cardinal_h = radar::kCardinalLabelHeightPx + 2;
  const TagRect reserved[] = {
      {radar::kCenterX - cardinal_half_w, 0, cardinal_half_w * 2, cardinal_h},
      {radar::kCenterX - cardinal_half_w, radar::kSize - cardinal_h,
       cardinal_half_w * 2, cardinal_h},
      {0, radar::kCenterY - cardinal_half_w, cardinal_h, cardinal_half_w * 2},
      {radar::kSize - cardinal_h, radar::kCenterY - cardinal_half_w, cardinal_h,
       cardinal_half_w * 2},
      {scaleLabelAnchorX(radar::kCenterX, radar::kGridOuterRadius) -
           s_scale_label_max_w - 6,
       radar::kCenterY - s_scale_label_h / 2 - 3, s_scale_label_max_w + 9,
       s_scale_label_h + 6},
  };
  for (const TagRect& reserved_rect : reserved) {
    if (tagRectsOverlap(rect, reserved_rect)) {
      return true;
    }
  }
  return false;
}

TagCacheEntry* findTagCacheEntry(const char* hex) {
  if (hex[0] == '\0') {
    return nullptr;
  }
  for (TagCacheEntry& entry : s_tag_cache) {
    if (strcmp(entry.hex, hex) == 0) {
      return &entry;
    }
  }
  return nullptr;
}

TagCacheEntry* claimTagCacheEntry(const char* hex) {
  if (hex[0] == '\0') {
    return nullptr;
  }
  if (TagCacheEntry* existing = findTagCacheEntry(hex)) {
    return existing;
  }

  TagCacheEntry* oldest = &s_tag_cache[0];
  for (TagCacheEntry& entry : s_tag_cache) {
    if (entry.hex[0] == '\0') {
      oldest = &entry;
      break;
    }
    if (entry.last_seen_frame < oldest->last_seen_frame) {
      oldest = &entry;
    }
  }
  strncpy(oldest->hex, hex, sizeof(oldest->hex) - 1);
  oldest->hex[sizeof(oldest->hex) - 1] = '\0';
  oldest->candidate = 0;
  oldest->last_seen_frame = s_tag_layout_frame;
  return oldest;
}

void expireTagCache() {
  for (TagCacheEntry& entry : s_tag_cache) {
    if (entry.hex[0] != '\0' &&
        s_tag_layout_frame - entry.last_seen_frame > kTagCacheExpiryFrames) {
      entry = TagCacheEntry{};
    }
  }
}

void drawAircraftTag(const TagPlacement& placement,
                     const services::adsb::Aircraft& plane) {
  initTagLabelMetrics();
  applyTagStyle();

  const int line_h = s_draw->fontHeight();
  int ly = placement.rect.y;
  s_draw->setTextDatum(textdatum_t::top_left);

  if (plane.callsign[0] != '\0') {
    s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
    s_draw->drawString(plane.callsign, placement.rect.x, ly);
  }
  ly += line_h;

  if (plane.type[0] != '\0') {
    s_draw->setTextColor(radar::kColorTagType, radar::kColorBackground);
    s_draw->drawString(plane.type, placement.rect.x, ly);
  }
  ly += line_h;

  if (plane.alt[0] != '\0') {
    s_draw->setTextColor(radar::kColorTagAltitude, radar::kColorBackground);
    s_draw->drawString(plane.alt, placement.rect.x, ly);
  }
}

struct AircraftDrawItem {
  size_t index = 0;
  int x = 0;
  int y = 0;
  int dist_sq = 0;
  char hex[sizeof(services::adsb::Aircraft::hex)] = {};
};

struct BeyondDotDrawItem {
  int x = 0;
  int y = 0;
  int dist_sq = 0;
};

void sortDrawItemsFarFirst(AircraftDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const AircraftDrawItem key = items[i];
    size_t j = i;
    while (j > 0 &&
           (items[j - 1].dist_sq < key.dist_sq ||
            (items[j - 1].dist_sq == key.dist_sq &&
             strcmp(items[j - 1].hex, key.hex) > 0))) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

void sortBeyondDotsFarFirst(BeyondDotDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const BeyondDotDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

bool tagIntersectsSpeedVector(const TagRect& rect, int x, int y,
                              const services::adsb::Aircraft& plane) {
  const int len = speedLineLengthPx(plane.gs_knots);
  if (len <= 0) {
    return false;
  }

  int tip_x = 0;
  int tip_y = 0;
  noseTip(x, y, plane.nose_deg, &tip_x, &tip_y);
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = plane.track_deg * kDegToRad;
  int end_x = tip_x + static_cast<int>(lroundf(sinf(rad) * len));
  int end_y = tip_y - static_cast<int>(lroundf(cosf(rad) * len));
  clipPointToOuterRing(tip_x, tip_y, &end_x, &end_y);
  return tagIntersectsSegment(rect, tip_x, tip_y, end_x, end_y);
}

int tagPlacementCollisionScore(
    const TagPlacement& placement, const AircraftDrawItem* items,
    size_t draw_count, const services::adsb::Aircraft* planes,
    const TagPlacement* accepted, size_t accepted_count) {
  constexpr int kCollisionPenalty = 100000;
  if (!tagRectOnScreen(placement.rect)) {
    return kCollisionPenalty * 4;
  }

  int score = tagIntersectsStaticUi(placement.rect) ? kCollisionPenalty : 0;
  const int symbol_half =
      radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx;
  for (size_t i = 0; i < draw_count; ++i) {
    const TagRect symbol = {items[i].x - symbol_half, items[i].y - symbol_half,
                            symbol_half * 2, symbol_half * 2};
    if (tagRectsOverlap(placement.rect, symbol)) {
      score += kCollisionPenalty;
    }
    if (tagIntersectsSpeedVector(placement.rect, items[i].x, items[i].y,
                                 planes[items[i].index])) {
      score += kCollisionPenalty;
    }
  }
  for (size_t i = 0; i < accepted_count; ++i) {
    score += tagOverlapArea(placement.rect, accepted[i].rect);
  }
  return score;
}

void tagCandidateOrder(bool prefer_right, uint8_t* candidates) {
  static constexpr uint8_t kPreferRight[kTagCandidateCount] = {
      kTagRight, kTagRightUp, kTagRightDown, kTagAbove,
      kTagBelow, kTagLeft,    kTagLeftUp,    kTagLeftDown,
  };
  static constexpr uint8_t kPreferLeft[kTagCandidateCount] = {
      kTagLeft, kTagLeftUp, kTagLeftDown, kTagAbove,
      kTagBelow, kTagRight, kTagRightUp,  kTagRightDown,
  };
  const uint8_t* order = prefer_right ? kPreferRight : kPreferLeft;
  for (size_t i = 0; i < kTagCandidateCount; ++i) {
    candidates[i] = order[i];
  }
}

void drawTagLeaderLine(int aircraft_x, int aircraft_y,
                       const TagPlacement& placement,
                       uint8_t default_candidate) {
  if (placement.candidate == default_candidate) {
    return;
  }
  const int end_x = std::max(placement.rect.x,
                             std::min(aircraft_x,
                                      placement.rect.x + placement.rect.width - 1));
  const int end_y = std::max(placement.rect.y,
                             std::min(aircraft_y,
                                      placement.rect.y + placement.rect.height - 1));
  s_draw->drawLine(aircraft_x, aircraft_y, end_x, end_y, radar::kColorGrid);
}

void drawAircraft() {
  initLabelMetrics();
  const uint8_t range_index = radar::rangeIndex();
  if (range_index != s_tag_cache_range_index) {
    memset(s_tag_cache, 0, sizeof(s_tag_cache));
    s_tag_cache_range_index = range_index;
  }
  ++s_tag_layout_frame;
  if (s_tag_layout_frame == 0) {
    s_tag_layout_frame = 1;
    memset(s_tag_cache, 0, sizeof(s_tag_cache));
  }
  expireTagCache();

  // Snapshot under the adsb lock (the fetch may run on another thread).
  static services::adsb::Aircraft planes[services::adsb::kMaxAircraft];
  unsigned long base_ms = 0;
  const size_t n = services::adsb::snapshotAircraft(
      planes, services::adsb::kMaxAircraft, &base_ms);

  AircraftDrawItem items[services::adsb::kMaxAircraft];
  BeyondDotDrawItem dots[services::adsb::kMaxAircraft];
  size_t draw_count = 0;
  size_t dot_count = 0;

  for (size_t i = 0; i < n; ++i) {
    // Dead-reckoned position for smooth motion between fetches.
    float lat = 0.0f;
    float lon = 0.0f;
    extrapolatedLatLon(planes[i], base_ms, &lat, &lon);

    float dx_km = 0.0f;
    float dy_km = 0.0f;
    float dist_km = 0.0f;
    offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);

    if (isInsideOuterRingKm(dist_km)) {
      int x = 0;
      int y = 0;
      latLonToScreen(lat, lon, &x, &y);
      items[draw_count].index = i;
      items[draw_count].x = x;
      items[draw_count].y = y;
      items[draw_count].dist_sq = distSqFromCenter(x, y);
      strncpy(items[draw_count].hex, planes[i].hex,
              sizeof(items[draw_count].hex) - 1);
      items[draw_count].hex[sizeof(items[draw_count].hex) - 1] = '\0';
      ++draw_count;
      continue;
    }

    int dot_x = 0;
    int dot_y = 0;
    if (!beyondRingEdgeDotFromLatLon(lat, lon, &dot_x, &dot_y)) {
      continue;
    }
    dots[dot_count].x = dot_x;
    dots[dot_count].y = dot_y;
    dots[dot_count].dist_sq = distSqFromCenter(dot_x, dot_y);
    ++dot_count;
  }

  sortBeyondDotsFarFirst(dots, dot_count);
  for (size_t d = 0; d < dot_count; ++d) {
    drawBeyondRingDot(dots[d].x, dots[d].y);
  }

  sortDrawItemsFarFirst(items, draw_count);
  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    const int x = items[d].x;
    const int y = items[d].y;
    drawSpeedVector(x, y, planes[i].nose_deg, planes[i].track_deg,
                    planes[i].gs_knots, radar::kColorTrackVector);
    drawHeadingTriangle(x, y, planes[i].nose_deg, radar::kColorAircraft);
  }
  TagPlacement accepted[services::adsb::kMaxAircraft];
  size_t accepted_count = 0;
  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    const services::adsb::Aircraft& plane = planes[i];
    if (!hasAircraftTag(plane)) {
      continue;
    }

    initTagLabelMetrics();
    applyTagStyle();
    const int width = measureTagBlockWidth(plane);
    const int height = s_draw->fontHeight() * 3;
    const uint8_t default_candidate =
        items[d].x < radar::kCenterX ? kTagRight : kTagLeft;
    const TagCacheEntry* cached = findTagCacheEntry(plane.hex);
    TagPlacement selected;
    bool found_clear_placement = false;

    if (cached != nullptr) {
      const TagPlacement prior = tagPlacementForCandidate(
          items[d].x, items[d].y, width, height, cached->candidate);
      if (tagPlacementCollisionScore(prior, items, draw_count, planes, accepted,
                                     accepted_count) == 0) {
        selected = prior;
        found_clear_placement = true;
      }
    }

    uint8_t candidates[kTagCandidateCount];
    tagCandidateOrder(default_candidate == kTagRight, candidates);
    int best_score = 0x7fffffff;
    TagPlacement best;
    for (uint8_t candidate : candidates) {
      const TagPlacement placement = tagPlacementForCandidate(
          items[d].x, items[d].y, width, height, candidate);
      const int score = tagPlacementCollisionScore(
          placement, items, draw_count, planes, accepted, accepted_count);
      if (score < best_score) {
        best_score = score;
        best = placement;
      }
      if (!found_clear_placement && score == 0) {
        selected = placement;
        found_clear_placement = true;
        break;
      }
    }
    if (!found_clear_placement) {
      selected = best;
    }

    if (TagCacheEntry* entry = claimTagCacheEntry(plane.hex)) {
      entry->candidate = selected.candidate;
      entry->last_seen_frame = s_tag_layout_frame;
    }
    drawTagLeaderLine(items[d].x, items[d].y, selected, default_candidate);
    drawAircraftTag(selected, plane);
    accepted[accepted_count++] = selected;
  }
}

void applyCardinalStyle() {
  if (s_cardinal_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_cardinal_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_cardinal_gfx);
  }
}

void applyScaleStyle() {
  if (s_scale_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_scale_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_scale_gfx);
  }
}

void drawCardinalLabel(const char* text, int x, int y, textdatum_t datum) {
  applyCardinalStyle();
  s_draw->setTextDatum(datum);
  s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawScaleLabelWithBackground(const char* text, int x, int y) {
  applyScaleStyle();
  s_draw->setTextDatum(textdatum_t::middle_right);

  const int tw = s_draw->textWidth(text);
  const int th = s_draw->fontHeight();
  constexpr int kPadX = 3;
  constexpr int kPadY = 2;

  const int left = x - tw - kPadX;
  const int top = y - th / 2 - kPadY;

  s_draw->fillRect(left, top, tw + kPadX * 2, th + kPadY * 2,
                   radar::kColorBackground);
  s_draw->setTextColor(radar::kColorGrid, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawGridRing(int cx, int cy, int r, uint16_t color) {
  if (r <= 0) {
    return;
  }
  const int thickness =
      std::max(1, static_cast<int>(radar::kGridStrokeHalfWidth * 2.0f));
  for (int i = 0; i < thickness && r - i > 0; ++i) {
    s_draw->drawCircle(cx, cy, r - i, color);
  }
}

void drawRings(int cx, int cy, int outer_radius) {
  for (int i = 1; i <= radar::kRingCount; ++i) {
    const int r = (outer_radius * i) / radar::kRingCount;
    drawGridRing(cx, cy, r, radar::kColorGrid);
  }
}

void drawCrosshairs(int cx, int cy, int radius, uint16_t color) {
  s_draw->drawWideLine(cx, cy - radius, cx, cy + radius,
                       radar::kGridStrokeHalfWidth, color);
  s_draw->drawWideLine(cx - radius, cy, cx + radius, cy,
                       radar::kGridStrokeHalfWidth, color);
}

void drawCenterDot(int cx, int cy) {
  s_draw->fillSmoothCircle(cx, cy, radar::kCenterDotRadius, radar::kColorCenter);
}

void drawCardinalLabels() {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int edge = radar::kSize - 1;

  drawCardinalLabel("N", cx, radar::kCardinalNorthOffsetY, textdatum_t::top_center);
  drawCardinalLabel("S", cx, edge + radar::kCardinalSouthOffsetY,
                    textdatum_t::bottom_center);
  drawCardinalLabel("W", 0, cy, textdatum_t::middle_left);
  drawCardinalLabel("E", edge, cy, textdatum_t::middle_right);
}

int scaleLabelAnchorX(int cx, int outer_radius) {
  return cx + outer_radius - radar::kScaleGapFromOuterRing;
}

void drawScaleLabel(int cx, int cy, int outer_radius) {
  char scale_label[12];
  radar::formatCurrentRing3Label(scale_label, sizeof(scale_label));
  drawScaleLabelWithBackground(scale_label,
                               scaleLabelAnchorX(cx, outer_radius), cy);
}

template <typename Gfx>
void drawStaticGrid(Gfx& gfx) {
  initLabelMetrics();
  const DrawScope scope(gfx);
  displayFontEnsureLoaded(gfx);
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int grid_r = radar::kGridOuterRadius;

  gfx.fillScreen(radar::kColorBackground);
  drawRings(cx, cy, grid_r);
  drawCrosshairs(cx, cy, grid_r, radar::kColorGrid);
  initPalette();
  runway::drawLargeAirportRunways(gfx);
  drawCenterDot(cx, cy);
  drawCardinalLabels();
  drawScaleLabel(cx, cy, grid_r);
  gfx.setTextDatum(textdatum_t::top_left);
}

bool ensureFrameSprite() {
  if (s_frame_ready) {
    return true;
  }
  s_frame.setColorDepth(8);
  if (!s_frame.createSprite(radar::kSize, radar::kSize)) {
    Serial.println("radar: frame sprite alloc failed");
    return false;
  }
  s_frame_ready = true;
  return true;
}

// Double-buffered frame: composite the grid AND aircraft into the off-screen
// sprite, then blit it to the panel in a single pushSprite. Because the panel
// is updated in one pass, labels never show an erase/redraw gap — no flicker.
void renderFrame() {
  drawStaticGrid(s_frame);  // opens its own DrawScope(s_frame)
  {
    const DrawScope scope(s_frame);
    drawAircraft();
  }
  s_frame.pushSprite(0, 0);
  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace

void radarDisplayDraw() {
  initPalette();
  initLabelMetrics();

  if (ensureFrameSprite()) {
    renderFrame();
    return;
  }

  // Fallback when the sprite can't be allocated: draw straight to the panel.
  const DrawScope scope(tft);
  drawStaticGrid(tft);
  drawAircraft();
  tft.setTextDatum(textdatum_t::top_left);
}

void radarDisplayRefreshAircraft() {
  initPalette();

  if (ensureFrameSprite()) {
    renderFrame();
    return;
  }

  radarDisplayDraw();
}

}  // namespace ui
