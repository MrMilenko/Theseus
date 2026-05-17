// imgui_impl_bgfx.h
//
// Dear ImGui renderer backend for bgfx. Replaces imgui_impl_opengl3
// when the desktop build is configured with THESEUS_USE_BGFX. Mirrors
// imgui_impl_opengl3's API surface so the call sites in sdl_main.cpp
// only need to be #ifdef-swapped.
//
// Init/Shutdown/NewFrame/RenderDrawData are the lifecycle hooks called
// from PreSwapOverlays. CreateFontsTexture is run lazily on first
// NewFrame (or explicitly here at Init time for clean error reporting).

#pragma once

struct ImDrawData;

// View 0 is the dashboard's main view; ImGui draws are submitted into
// the same view alongside everything else so submission order (which
// view 0 is set to Sequential for) determines layering.
bool ImGui_ImplBgfx_Init(unsigned char viewId);
void ImGui_ImplBgfx_Shutdown();
void ImGui_ImplBgfx_NewFrame();
void ImGui_ImplBgfx_RenderDrawData(ImDrawData* draw_data);
bool ImGui_ImplBgfx_CreateFontsTexture();
void ImGui_ImplBgfx_DestroyFontsTexture();
