// xap_editor.h: XAP script editor public API. Companion to
// desktop/xap_editor.cpp.

#pragma once

struct CNode;
struct CInstance;

// Inline node discovery info, used by both XAP editor and inspector.
struct InlineInfo {
    CNode*      node;
    const char* defName;
    const char* url;
};

// CollectInlineNodes and ReloadInlineNode are extern "C" in Group.cpp
// Include their declarations from xap_editor.cpp after full type definitions

// XAP editor state (read by sdl_main for window management / key handling)
extern bool g_xapEditorOpen;
extern bool g_extractedMode;

// Load a .xap file into the editor buffer
void XapEditor_LoadFile(const char* path);

// Returns true if the editor has a buffer loaded
bool XapEditor_HasBuffer();

// Render XAP editor content (called from main loop in the XAP editor SDL window)
void RenderXAPEditor();

// Reload the scene from the currently edited XAP text
void ReloadSceneFromEditor();

// Check and consume the reload-requested flag (set by editor hotkeys)
bool XapEditor_ConsumeReloadRequest();

// Free editor resources (called at shutdown)
void XapEditor_Cleanup();
