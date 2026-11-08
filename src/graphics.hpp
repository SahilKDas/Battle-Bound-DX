#pragma once
#include <raylib.h>

namespace bb::gfx {
void initialize(int width, int height, const char* title, bool hidden);
void shutdown();
bool shouldClose();
void beginFrame();
void endFrame();
void setFrameRate(int fps);
void setVsync(bool enabled);
bool keyDown(int key);
bool keyPressed(int key);
bool mouseDown(int button);
bool mousePressed(int button);
float mouseX();
float mouseY();
float mouseWheel();
int nextCharacter();
int width();
int height();
Texture2D upload(Image image);
void release(Texture2D texture);
void sprite(Texture2D texture, Rectangle source, Rectangle destination, Vector2 pivot,
            float angle, Color tint, float hue = 0, float brightness = 0);
void rectangle(float x, float y, float width, float height, Color color);
void text(const char* text, float x, float y, float size, Color color);
void requestCapture();
Image capture();
}
