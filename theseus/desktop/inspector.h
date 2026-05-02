// inspector.h: scene graph inspector public API. Companion to
// desktop/inspector.cpp.

#pragma once

struct IDirect3DDevice8;
struct CNode;
struct CInstance;

// Render the inspector panel and selection highlight
void RenderInspectorPanel(IDirect3DDevice8* dev);
void DrawSelectionHighlight(IDirect3DDevice8* dev);

// Inspector helpers used by other tools
const char* FindNodeDefName(CNode* pNode, CInstance* pRoot);
void CountNodes(CNode* pNode, int& total, int& visible, int& groups, int depth = 0);
void SetVisibilityRecursive(CNode* pNode, bool visible, int depth = 0);
