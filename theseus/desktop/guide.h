// guide.h: the guide, a native overlay over the dashboard (guide.cpp).
// Opens with the Guide button, LT+B or F7; B backs out a level and closes
// it from the top. While it's open it has the pad and the keyboard.
#pragma once

union SDL_Event;

// What the guide reads each frame (joystick.cpp's Joy_GuideButtons).
enum {
    GUIDE_UP = 1 << 0, GUIDE_DOWN = 1 << 1, GUIDE_LEFT = 1 << 2, GUIDE_RIGHT = 1 << 3,
    GUIDE_A = 1 << 4, GUIDE_B = 1 << 5, GUIDE_X = 1 << 6, GUIDE_Y = 1 << 7,
    GUIDE_LB = 1 << 8, GUIDE_RB = 1 << 9, GUIDE_START = 1 << 10, GUIDE_BACK = 1 << 11,
};

// Pad and keyboard as guide buttons. device becomes 1 for the pad, 0 for
// the keyboard, and is left alone when nothing is held.
unsigned Joy_GuideButtons(int* device);

extern bool g_guideRequested;   // set by the Guide button, LT+B or F7
extern bool g_welcomeDone;      // desktop.ini WelcomeDone

void Guide_Frame();                       // once a frame, before the dashboard advances
void Guide_Render(int width, int height); // backbuffer pixels; draws on view 2
void Guide_HandleEvent(const SDL_Event* event);
bool Guide_IsOpen();
// True while the guide has the input, and after it closes until the
// buttons that closed it are let go of.
bool Guide_TakesInput();
// True while its keyboard is up: keys type, they aren't buttons.
bool Guide_Typing();
void Guide_Shutdown();
