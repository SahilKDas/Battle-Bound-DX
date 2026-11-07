#include "camera.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bb {
namespace {
double scratchRound(double value) { return std::floor(value + 0.5); }
constexpr double radians = 3.14159265358979323846 / 180.0;
}

StagePoint Viewport::pixelToStage(PixelPoint point, bool clampToViewport) const {
    if (pixelWidth <= 0 || pixelHeight <= 0 || logicalWidth <= 0 || logicalHeight <= 0) {
        throw std::invalid_argument("Viewport dimensions must be positive");
    }
    StagePoint result{point.x * logicalWidth / pixelWidth - logicalWidth / 2,
                      logicalHeight / 2 - point.y * logicalHeight / pixelHeight};
    if (clampToViewport) {
        result.x = std::clamp(result.x, -logicalWidth / 2, logicalWidth / 2);
        result.y = std::clamp(result.y, -logicalHeight / 2, logicalHeight / 2);
    }
    return result;
}

PixelPoint Viewport::stageToPixel(StagePoint point) const {
    const auto logical = stageToLogical(point);
    return {logical.x * pixelWidth / logicalWidth, logical.y * pixelHeight / logicalHeight};
}

PixelPoint Viewport::stageToLogical(StagePoint point) const {
    return {logicalWidth / 2 + point.x, logicalHeight / 2 - point.y};
}

void CameraSystem::reset(CameraState state) { current = state; }
void CameraSystem::configure(CameraTuning value) {
    for (double smoothing : {value.horizontalSmoothing, value.verticalSmoothing,
                             value.fastFallSmoothing, value.vehicleSmoothing}) {
        if (!std::isfinite(smoothing) || smoothing <= 0) {
            throw std::invalid_argument("Camera smoothing must be finite and positive");
        }
    }
    if (!std::isfinite(value.lowAltitudeOffset)) throw std::invalid_argument("Invalid camera offset");
    tuning = value;
}
StagePoint CameraSystem::worldToStage(WorldPoint point) const {
    return {point.x - current.position.x, point.y - current.position.y};
}
WorldPoint CameraSystem::stageToWorld(StagePoint point) const {
    return {point.x + current.position.x, point.y + current.position.y};
}

void CameraSystem::followHorizontal(double playerX, double aimLead) {
    current.position.x += scratchRound((playerX + aimLead - current.position.x) / tuning.horizontalSmoothing);
}
void CameraSystem::followVertical(double playerY, double verticalSpeed, double aimLead) {
    const double smoothing = verticalSpeed < -20 ? tuning.fastFallSmoothing : tuning.verticalSmoothing;
    current.position.y += scratchRound(((playerY + aimLead) + 30 - current.position.y) / smoothing);
}
void CameraSystem::followHelicopterHorizontal(double playerX) {
    current.position.x += scratchRound((playerX - current.position.x) / tuning.vehicleSmoothing);
}
void CameraSystem::followHelicopterVertical(double playerY, double airborneTicks) {
    const double offset = tuning.lowAltitudeOffset * (playerY < 700 && airborneTicks > 1);
    current.position.y += scratchRound((playerY + offset - current.position.y) / tuning.vehicleSmoothing);
}
void CameraSystem::followPassengerHorizontal(double playerX, double heading, double facing) {
    const double offset = (std::sin(heading * radians) * 40) * facing;
    current.position.x += scratchRound((playerX - offset - current.position.x) / tuning.vehicleSmoothing);
}
void CameraSystem::followPassengerVertical(double playerY) {
    current.position.y += scratchRound((playerY + tuning.lowAltitudeOffset * (playerY < 700) - current.position.y) / tuning.vehicleSmoothing);
}
void CameraSystem::panHorizontal(double velocity) { current.position.x += scratchRound(velocity); }
void CameraSystem::panVertical(double velocity) { current.position.y += scratchRound(velocity); }
void CameraSystem::shakeHorizontal(double amplitude, double signedNoise) {
    current.position.x += amplitude * signedNoise;
}
void CameraSystem::shakeVertical(double amplitude, double signedNoise) {
    current.position.y += amplitude * signedNoise;
}
void CameraSystem::snapGridHorizontal(double cellWidth) {
    if (cellWidth <= 0) throw std::invalid_argument("Grid cell width must be positive");
    current.gridOrigin.x = scratchRound(current.position.x / cellWidth) * cellWidth;
}
void CameraSystem::snapGridVertical(double cellHeight) {
    if (cellHeight <= 0) throw std::invalid_argument("Grid cell height must be positive");
    current.gridOrigin.y = scratchRound(current.position.y / cellHeight) * cellHeight;
}
void CameraSystem::clampHorizontal(double minimum, double maximum) {
    current.position.x = std::clamp(current.position.x, minimum, maximum);
}
void CameraSystem::clampVerticalMinimum(double minimum) {
    current.position.y = std::max(current.position.y, minimum);
}
void CameraSystem::clampVerticalMaximum(double maximum) {
    current.position.y = std::min(current.position.y, maximum);
}
void CameraSystem::setHorizontal(double value) { current.position.x = value; }
void CameraSystem::setVertical(double value) { current.position.y = value; }
void CameraSystem::setGridHorizontal(double value) { current.gridOrigin.x = value; }
void CameraSystem::setGridVertical(double value) { current.gridOrigin.y = value; }
void CameraSystem::setShakeMagnitude(double value) { current.shakeMagnitude = value; }

void testCameraSystem() {
    auto require = [](bool condition, const char* label) {
        if (!condition) throw std::runtime_error(label);
    };
    const Viewport viewport{1920, 1080};
    auto stage = viewport.pixelToStage({960.75, 539.25});
    auto pixel = viewport.stageToPixel(stage);
    require(std::abs(pixel.x - 960.75) < 1e-10 && std::abs(pixel.y - 539.25) < 1e-10,
            "Fractional viewport coordinates must round-trip");
    require(viewport.pixelToStage({0, 0}).x == -320 &&
            viewport.pixelToStage({1920, 1080}).y == -180, "Viewport corners");
    CameraSystem camera;
    camera.reset({{100.25, -20.5}, {}, 0});
    auto world = camera.stageToWorld({3.5, 4.25});
    auto back = camera.worldToStage(world);
    require(back.x == 3.5 && back.y == 4.25, "World/stage coordinate round-trip");
    camera.reset({});
    camera.panHorizontal(-0.5);
    camera.panVertical(-1.5);
    require(camera.state().position.x == 0 && camera.state().position.y == -1,
            "Scratch negative-half rounding must be retained");
    camera.reset({});
    camera.followVertical(10, -21);
    require(camera.state().position.y == 10, "Fast fall camera smoothing");
    camera.reset({});
    camera.followVertical(10, -20);
    require(camera.state().position.y == 4, "Fall threshold is strictly below -20");
    camera.reset({});
    camera.followHelicopterVertical(680, 2);
    require(camera.state().position.y == 155, "Airborne helicopter framing");
    camera.reset({});
    camera.followHelicopterVertical(680, 1);
    require(camera.state().position.y == 170, "Grounded helicopter framing");
    camera.reset({});
    camera.followPassengerHorizontal(100, 90, -1);
    require(camera.state().position.x == 35, "Passenger heading/facing offset");
    camera.setHorizontal(-360);
    camera.snapGridHorizontal(720);
    require(camera.state().gridOrigin.x == 0, "Grid snapping at negative half-cell");
    camera.clampHorizontal(-114, 8755);
    require(camera.state().position.x == -114, "Legacy camera bounds");
    camera.shakeHorizontal(5, -1);
    require(camera.state().position.x == -119, "Shake uses supplied noise, no hidden RNG");
}
}
