// skin_editor.h: end-user authoring tool for dashboard skins. Lists
// subdirectories of Data/Skins/, applies the chosen skin via ReloadSkin(),
// and clones Stock to seed a new skin. Desktop-only.

#pragma once

extern bool g_skinEditorOpen;

void RenderSkinEditor();
