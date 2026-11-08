#pragma once
#include "camera.hpp"
#include "input.hpp"

namespace bb {
RawInput capturePlatformInput(const Viewport& viewport, bool recording, bool edges = true);
}
