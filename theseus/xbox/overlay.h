// overlay.h: modal overlay UI public API. Init / Draw / Update,
// input routing entry points, and alert popups. Companion to
// xbox/overlay.cpp.

#pragma once

void InitCubeOverlay();
void DrawCubeOverlay();
void UpdateCubeOverlay();

void OverlayOnUp();
void OverlayOnDown();
void OverlayOnLeft();
void OverlayOnRight();
void OverlayOnA();
void OverlayOnB();

// Alert popup (callable from anywhere in the XBE)
void OverlayAlert(const TCHAR* msg);
extern bool g_bOverlayAlertActive;

// State
extern bool g_bShowOverlay;
extern bool g_bOverlayInputCapture;
