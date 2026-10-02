#pragma once

#include <LovyanGFX.hpp>

namespace ui::runway {

void drawLargeAirportRunways(lgfx::LGFXBase& gfx);

/** True when a screen-space rectangle overlaps a rendered airport label. */
bool airportLabelOverlaps(int x, int y, int width, int height);

}  // namespace ui::runway
