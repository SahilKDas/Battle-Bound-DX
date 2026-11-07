#pragma once
#include "coordinates.hpp"

namespace bb {
struct Viewport {
    double pixelWidth{1280}, pixelHeight{720};
    double logicalWidth{640}, logicalHeight{360};

    StagePoint pixelToStage(PixelPoint point, bool clampToViewport = false) const;
    PixelPoint stageToPixel(StagePoint point) const;
    PixelPoint stageToLogical(StagePoint point) const;
};

struct CameraState {
    WorldPoint position;
    WorldPoint gridOrigin;
    double shakeMagnitude{};
};
struct CameraTuning {
    double horizontalSmoothing{5};
    double verticalSmoothing{10};
    double fastFallSmoothing{4};
    double vehicleSmoothing{4};
    double lowAltitudeOffset{-60};
};

class CameraSystem {
public:
    void reset(CameraState state);
    void configure(CameraTuning tuning);
    const CameraState& state() const { return current; }
    StagePoint worldToStage(WorldPoint point) const;
    WorldPoint stageToWorld(StagePoint point) const;

    void followHorizontal(double playerX, double aimLead = 0);
    void followVertical(double playerY, double verticalSpeed, double aimLead = 0);
    void followHelicopterHorizontal(double playerX);
    void followHelicopterVertical(double playerY, double airborneTicks);
    void followPassengerHorizontal(double playerX, double heading, double facing);
    void followPassengerVertical(double playerY);
    void panHorizontal(double velocity);
    void panVertical(double velocity);
    void shakeHorizontal(double amplitude, double signedNoise);
    void shakeVertical(double amplitude, double signedNoise);
    void snapGridHorizontal(double cellWidth);
    void snapGridVertical(double cellHeight);
    void clampHorizontal(double minimum, double maximum);
    void clampVerticalMinimum(double minimum);
    void clampVerticalMaximum(double maximum);

    // Compatibility setters are the only bridge from remaining Scratch writes.
    void setHorizontal(double value);
    void setVertical(double value);
    void setGridHorizontal(double value);
    void setGridVertical(double value);
    void setShakeMagnitude(double value);

private:
    CameraState current;
    CameraTuning tuning;
};
void testCameraSystem();
}
