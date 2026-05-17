// inspector.cpp: scene graph inspector panel. Floating ImGui window
// that walks the live CNode tree and exposes node properties for
// the XAP authoring tools. Desktop-only.

#include "std.h"
#include "dashapp.h"
#include "node.h"
#include "runner.h"
#include "shape_render.h"
#include "asset_loader.h"
#include "panel_shared.h"
#include "inspector.h"
#include "imgui.h"

extern bool g_bWireframe;

// ============================================================================
// Inspector state
// ============================================================================

bool g_inspectorOpen = false; // tracks g_debugMode (extern'd in d3d8_sdl.h)

static char g_treeFilter[128] = "";  // scene tree search filter

// ============================================================================
// Helper functions
// ============================================================================

// Count nodes in a subtree
void CountNodes(CNode* pNode, int& total, int& visible, int& groups, int depth) {
    if (!pNode || depth > 32) return;
    total++;
    if (pNode->m_visible) visible++;
    CGroup* grp = dynamic_cast<CGroup*>(pNode);
    if (grp) {
        groups++;
        for (int i = 0; i < grp->m_children.GetLength(); i++) {
            CNode* child = grp->m_children.GetNode(i);
            if (child) CountNodes(child, total, visible, groups, depth + 1);
        }
    }
}

// Set visibility for a node and all descendants
void SetVisibilityRecursive(CNode* pNode, bool visible, int depth) {
    if (!pNode || depth > 32) return;
    pNode->m_visible = visible;
    CGroup* grp = dynamic_cast<CGroup*>(pNode);
    if (grp) {
        for (int i = 0; i < grp->m_children.GetLength(); i++) {
            CNode* child = grp->m_children.GetNode(i);
            if (child) SetVisibilityRecursive(child, visible, depth + 1);
        }
    }
}

// Find the DEF name for a node by searching an instance's class members
static const TCHAR* FindNodeDefNameInInstance(CNode* pNode, CInstance* pInst) {
    if (!pNode || !pInst || !pInst->m_class || !pInst->m_class->m_members)
        return NULL;
    for (CDefine* d = pInst->m_class->m_members->m_firstDefine; d; d = d->m_next) {
        if (d->m_node && d->m_node->m_obj == objMember) {
            CMember* mem = (CMember*)d->m_node;
            int idx = mem->m_memberIndex;
            if (idx >= 0 && idx < pInst->m_vars.GetLength()) {
                CNode* varNode = pInst->m_vars.GetNode(idx);
                if (varNode == pNode)
                    return d->m_name;
            }
        }
    }
    return NULL;
}

// Walk up the parent chain to find the owning CInstance and resolve the DEF name
const char* FindNodeDefName(CNode* pNode, CInstance* pRoot) {
    if (!pNode) return NULL;
    // First try the root instance
    const TCHAR* name = FindNodeDefNameInInstance(pNode, pRoot);
    if (name) return name;
    // Walk parent chain looking for a CInstance
    for (CObject* p = pNode->m_parent; p; p = p->m_parent) {
        CInstance* inst = dynamic_cast<CInstance*>(p);
        if (inst) {
            name = FindNodeDefNameInInstance(pNode, inst);
            if (name) return name;
        }
    }
    return NULL;
}

// Case-insensitive substring match
static bool MatchesFilter(const char* text, const char* filter) {
    if (!filter[0]) return true;
    if (!text) return false;
    for (const char* t = text; *t; t++) {
        const char* a = t;
        const char* b = filter;
        while (*a && *b && (tolower(*a) == tolower(*b))) { a++; b++; }
        if (!*b) return true;
    }
    return false;
}

// Check if a node or any descendant matches the filter
static bool SubtreeMatchesFilter(CNode* pNode, CInstance* pRoot, const char* filter) {
    if (!filter[0]) return true;
    if (!pNode) return false;

    const char* className = "?";
    CNodeClass* nc = pNode->GetNodeClass();
    if (nc && nc->m_className) className = nc->m_className;
    const TCHAR* defName = FindNodeDefName(pNode, pRoot);

    if (MatchesFilter(className, filter)) return true;
    if (defName && MatchesFilter(defName, filter)) return true;

    CGroup* pGroup = dynamic_cast<CGroup*>(pNode);
    if (pGroup) {
        for (int i = 0; i < pGroup->m_children.GetLength(); i++) {
            CNode* child = pGroup->m_children.GetNode(i);
            if (child && SubtreeMatchesFilter(child, pRoot, filter))
                return true;
        }
    }
    return false;
}

// ============================================================================
// DrawSelectionHighlight
// ============================================================================

// Draw selection highlight into the current ImGui frame (called from PreSwapOverlays)
void DrawSelectionHighlight(IDirect3DDevice8* dev) {
    if (!dev || !dev->m_inspectorSelectedNode || dev->m_drawRecordCount == 0) return;

    // Find the combined AABB of all draw calls from the selected node
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    bool found = false;
    for (int i = 0; i < dev->m_drawRecordCount; i++) {
        if (dev->m_drawRecords[i].sceneNode == dev->m_inspectorSelectedNode) {
            const auto& r = dev->m_drawRecords[i];
            if (r.screenMinX < minX) minX = r.screenMinX;
            if (r.screenMinY < minY) minY = r.screenMinY;
            if (r.screenMaxX > maxX) maxX = r.screenMaxX;
            if (r.screenMaxY > maxY) maxY = r.screenMaxY;
            found = true;
        }
    }
    if (!found || (maxX - minX) < 2 || (maxY - minY) < 2) return;

    ImVec2 p1(minX, minY);
    ImVec2 p2(maxX, maxY);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    // Pulsing green border
    float t = (float)fmod(ImGui::GetTime() * 3.0, 6.283185);
    float alpha = 0.5f + 0.3f * sinf(t);
    dl->AddRect(p1, p2, IM_COL32(100, 255, 100, (int)(alpha * 255)), 0.0f, 0, 2.0f);
    // Semi-transparent fill
    dl->AddRectFilled(p1, p2, IM_COL32(100, 255, 100, 20));

    // Hover tooltip at cursor
    if (dev->m_inspectorEnabled && dev->m_inspectorHitID >= 0) {
        int globalMX, globalMY;
        SDL_GetGlobalMouseState(&globalMX, &globalMY);
        int mainX, mainY;
        SDL_GetWindowPosition(g_pSDLWindow, &mainX, &mainY);
        int winW, winH;
        SDL_GetWindowSize(g_pSDLWindow, &winW, &winH);
        float mx = (float)(globalMX - mainX);
        float my = (float)(globalMY - mainY);
        // Only show tooltip if mouse is in the main window
        if (mx >= 0 && mx < winW && my >= 0 && my < winH) {
            const auto& r = dev->m_drawRecords[dev->m_inspectorHitID];
            char tip[256];
            snprintf(tip, sizeof(tip), "%s\n%s %ux%u",
                r.matName ? r.matName : "",
                r.tex0Name[0] ? r.tex0Name : "",
                r.tex0W, r.tex0H);
            ImVec2 pos(mx + 16, my + 16);
            ImVec2 textSize = ImGui::CalcTextSize(tip);
            ImVec2 padding(6, 4);
            dl->AddRectFilled(
                ImVec2(pos.x - padding.x, pos.y - padding.y),
                ImVec2(pos.x + textSize.x + padding.x, pos.y + textSize.y + padding.y),
                IM_COL32(10, 20, 10, 220), 4.0f);
            dl->AddRect(
                ImVec2(pos.x - padding.x, pos.y - padding.y),
                ImVec2(pos.x + textSize.x + padding.x, pos.y + textSize.y + padding.y),
                IM_COL32(80, 200, 80, 180), 4.0f);
            dl->AddText(pos, IM_COL32(200, 255, 200, 255), tip);
        }
    }
}

// ============================================================================
// RenderNodeTree
// ============================================================================

// Recursively render the scene graph as an ImGui tree
static void RenderNodeTree(CNode* pNode, CInstance* pRoot, IDirect3DDevice8* dev, const char* filter, int depth = 0) {
    if (!pNode || depth > 32) return;

    // Get class name
    const char* className = "?";
    CNodeClass* nc = pNode->GetNodeClass();
    if (nc && nc->m_className)
        className = nc->m_className;

    // Get DEF name
    const TCHAR* defName = FindNodeDefName(pNode, pRoot);

    // Filter: skip nodes (and subtrees) that don't match
    if (filter[0] && !SubtreeMatchesFilter(pNode, pRoot, filter))
        return;

    // Does this specific node match the filter?
    bool thisMatches = !filter[0] || MatchesFilter(className, filter) || (defName && MatchesFilter(defName, filter));

    // Build label (unique ID for ImGui)
    char label[256];
    if (defName)
        snprintf(label, sizeof(label), "%s (%s)##%p", defName, className, (void*)pNode);
    else
        snprintf(label, sizeof(label), "%s##%p", className, (void*)pNode);

    // Use dynamic_cast because IsKindOf walks XAP class chain, not C++ hierarchy
    CGroup* pGroup = dynamic_cast<CGroup*>(pNode);
    bool isGroup = (pGroup != NULL);
    bool isSelected = (dev && dev->m_inspectorSelectedNode == (void*)pNode);

    // Visibility toggle. small button before the tree node
    ImGui::PushID((void*)pNode);
    if (!pNode->m_visible) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        if (ImGui::SmallButton("o")) pNode->m_visible = true;
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
        if (ImGui::SmallButton("*")) pNode->m_visible = false;
        ImGui::PopStyleColor();
    }
    ImGui::PopID();
    ImGui::SameLine();

    // Determine text color: hidden nodes always grey, otherwise priority order
    ImVec4 textColor;
    if (!pNode->m_visible && !isSelected)
        textColor = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);  // hidden
    else if (isSelected)
        textColor = ImVec4(1.0f, 1.0f, 0.2f, 1.0f);   // selected
    else if (!thisMatches && filter[0])
        textColor = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);   // filtered out
    else if (defName)
        textColor = ImVec4(0.4f, 1.0f, 0.8f, 1.0f);   // named
    else if (isGroup)
        textColor = ImVec4(0.6f, 0.9f, 0.6f, 1.0f);   // group
    else
        textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text); // default

    ImGui::PushStyleColor(ImGuiCol_Text, textColor);

    if (isGroup) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (pGroup->m_children.GetLength() == 0)
            flags |= ImGuiTreeNodeFlags_Leaf;
        // Auto-open: first 2 levels normally, or all when filtering
        if (depth < 2 || filter[0])
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        if (isSelected)
            flags |= ImGuiTreeNodeFlags_Selected;

        bool open = ImGui::TreeNodeEx(label, flags);

        // Scroll to selected node when triggered by 3D click
        if (isSelected && g_scrollToSelected) {
            ImGui::SetScrollHereY(0.5f);
            g_scrollToSelected = false;
        }

        // Click to select (but not on arrow)
        if (ImGui::IsItemClicked(0) && !ImGui::IsItemToggledOpen()) {
            if (dev) dev->m_inspectorSelectedNode = (void*)pNode;
        }

        // Right-click context menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Select")) {
                if (dev) dev->m_inspectorSelectedNode = (void*)pNode;
            }
            if (ImGui::MenuItem(pNode->m_visible ? "Hide" : "Show")) {
                pNode->m_visible = !pNode->m_visible;
            }
            if (isGroup) {
                if (ImGui::MenuItem("Show All Children"))
                    SetVisibilityRecursive(pNode, true);
                if (ImGui::MenuItem("Hide All Children"))
                    SetVisibilityRecursive(pNode, false);
                if (ImGui::MenuItem("Solo (hide siblings)")) {
                    CGroup* parent = dynamic_cast<CGroup*>(pNode->m_parent);
                    if (parent) {
                        for (int j = 0; j < parent->m_children.GetLength(); j++) {
                            CNode* sib = parent->m_children.GetNode(j);
                            if (sib) sib->m_visible = (sib == pNode);
                        }
                    }
                }
                if (ImGui::MenuItem("Unsolo (show siblings)")) {
                    CGroup* parent = dynamic_cast<CGroup*>(pNode->m_parent);
                    if (parent) {
                        for (int j = 0; j < parent->m_children.GetLength(); j++) {
                            CNode* sib = parent->m_children.GetNode(j);
                            if (sib) sib->m_visible = true;
                        }
                    }
                }
            }
            ImGui::EndPopup();
        }

        ImGui::PopStyleColor();

        if (open) {
            for (int i = 0; i < pGroup->m_children.GetLength(); i++) {
                CNode* child = pGroup->m_children.GetNode(i);
                if (child)
                    RenderNodeTree(child, pRoot, dev, filter, depth + 1);
            }
            ImGui::TreePop();
        }
    } else {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (isSelected)
            flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::TreeNodeEx(label, flags);

        // Scroll to selected node when triggered by 3D click
        if (isSelected && g_scrollToSelected) {
            ImGui::SetScrollHereY(0.5f);
            g_scrollToSelected = false;
        }

        if (ImGui::IsItemClicked(0)) {
            if (dev) dev->m_inspectorSelectedNode = (void*)pNode;
        }

        // Right-click context menu for leaf nodes
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Select")) {
                if (dev) dev->m_inspectorSelectedNode = (void*)pNode;
            }
            if (ImGui::MenuItem(pNode->m_visible ? "Hide" : "Show")) {
                pNode->m_visible = !pNode->m_visible;
            }
            ImGui::EndPopup();
        }

        ImGui::PopStyleColor();
    }
}

// ============================================================================
// RenderInspectorPanel
// ============================================================================

void RenderInspectorPanel(IDirect3DDevice8* dev) {
    if (!dev || !g_inspectorOpen) return;

    ImGui::SetNextWindowSize(ImVec2(420, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(300, 200), ImVec2(800, FLT_MAX));

    if (!ImGui::Begin("Inspector", &g_inspectorOpen)) {
        ImGui::End();
        // Sync debug mode when closed via X button
        if (!g_inspectorOpen) {
            g_debugMode = false;
            if (g_bWireframe) {
                g_bWireframe = false;
#ifndef THESEUS_USE_BGFX
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
            }
            dev->m_inspectorEnabled = false;
            dev->m_inspectorSelectedNode = NULL;
            dev->m_inspectorHitID = -1;
        }
        return;
    }

    // Stats bar
    int totalNodes = 0, visibleNodes = 0, groupNodes = 0;
    if (g_pObject)
        CountNodes((CNode*)g_pObject, totalNodes, visibleNodes, groupNodes);
    ImGui::Text("Draw: %d | Nodes: %d/%d | Groups: %d",
        dev->m_drawRecordCount, visibleNodes, totalNodes, groupNodes);

    // Wireframe toggle
    ImGui::SameLine();
    if (ImGui::SmallButton(g_bWireframe ? "Wire:ON" : "Wire:OFF"))
        g_bWireframe = !g_bWireframe;

    ImGui::Separator();

    // Hovered element info
    if (dev->m_inspectorEnabled && dev->m_inspectorHitID >= 0) {
        const auto& r = dev->m_drawRecords[dev->m_inspectorHitID];
        ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "%s", r.matName ? r.matName : "(none)");
        if (r.tex0Name[0])
            ImGui::Text("  %s %ux%u", r.tex0Name, r.tex0W, r.tex0H);
        ImGui::Separator();
    }

    // Selected node info
    if (dev->m_inspectorSelectedNode && g_pObject) {
        CNode* sel = (CNode*)dev->m_inspectorSelectedNode;
        CNodeClass* snc = sel->GetNodeClass();
        const TCHAR* sDefName = FindNodeDefName(sel, g_pObject);

        // Header with DEF name and class
        const char* className = (snc && snc->m_className) ? snc->m_className : "?";
        if (sDefName)
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "%s (%s)", sDefName, className);
        else
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "%s", className);

        // Action buttons
        if (ImGui::SmallButton(sel->m_visible ? "Hide" : "Show"))
            sel->m_visible = !sel->m_visible;
        ImGui::SameLine();
        if (ImGui::SmallButton("Deselect"))
            dev->m_inspectorSelectedNode = NULL;

        // Count draw calls from this node
        int drawCount = 0;
        for (int i = 0; i < dev->m_drawRecordCount; i++) {
            if (dev->m_drawRecords[i].sceneNode == dev->m_inspectorSelectedNode)
                drawCount++;
        }
        if (drawCount > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(%d draws)", drawCount);
        }

        // Transform properties (read-only)
        CTransform* xform = dynamic_cast<CTransform*>(sel);
        if (xform) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Transform");
            ImGui::Text("  Pos:   %.2f, %.2f, %.2f", xform->m_translation.x, xform->m_translation.y, xform->m_translation.z);
            ImGui::Text("  Scale: %.2f, %.2f, %.2f", xform->m_scale.x, xform->m_scale.y, xform->m_scale.z);
            ImGui::Text("  Rot:   %.2f, %.2f, %.2f, %.2f", xform->m_rotation.x, xform->m_rotation.y, xform->m_rotation.z, xform->m_rotation.w);
            if (xform->m_alpha != 1.0f)
                ImGui::Text("  Alpha: %.2f", xform->m_alpha);
        }

        // Shape properties (read-only)
        CShape* shape = dynamic_cast<CShape*>(sel);
        if (shape) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Shape");
            if (shape->m_appearance) {
                CAppearance* app = dynamic_cast<CAppearance*>(shape->m_appearance);
                if (app) {
                    if (app->m_material) {
                        CMaterial* mat = dynamic_cast<CMaterial*>(app->m_material);
                        if (mat) {
                            ImGui::Text("  Diffuse:  %.2f, %.2f, %.2f", mat->m_diffuseColor.x, mat->m_diffuseColor.y, mat->m_diffuseColor.z);
                            ImGui::Text("  Emissive: %.2f, %.2f, %.2f", mat->m_emissiveColor.x, mat->m_emissiveColor.y, mat->m_emissiveColor.z);
                            if (mat->m_transparency > 0.0f)
                                ImGui::Text("  Transparency: %.2f", mat->m_transparency);
                        }
                    }
                    if (app->m_texture) {
                        CImageTexture* imgTex = dynamic_cast<CImageTexture*>(app->m_texture);
                        CTexture* baseTex = dynamic_cast<CTexture*>(app->m_texture);
                        if (imgTex && imgTex->m_url)
                            ImGui::Text("  Texture: %s", imgTex->m_url);
                        else if (baseTex) {
                            CNodeClass* tnc = baseTex->GetNodeClass();
                            ImGui::Text("  Texture: %s", (tnc && tnc->m_className) ? tnc->m_className : "?");
                        }
                        if (baseTex && baseTex->m_surface) {
#ifndef THESEUS_USE_BGFX
                            uintptr_t imguiTex = (uintptr_t)baseTex->m_surface->m_glTexture;
#else
                            uintptr_t imguiTex = bgfx::isValid(baseTex->m_surface->m_bgfxTex)
                                ? (uintptr_t)baseTex->m_surface->m_bgfxTex.idx : 0;
#endif
                            if (imguiTex) {
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%dx%d", baseTex->m_nImageWidth, baseTex->m_nImageHeight);
                                float thumbH = 64.0f;
                                float aspect = baseTex->m_nImageWidth > 0 ? (float)baseTex->m_nImageWidth / baseTex->m_nImageHeight : 1.0f;
                                float thumbW = thumbH * aspect;
                                if (thumbW > ImGui::GetContentRegionAvail().x)
                                    thumbW = ImGui::GetContentRegionAvail().x;
                                ImGui::Image((ImTextureID)imguiTex, ImVec2(thumbW, thumbH));
                            }
                        }
                    }
                }
            }
            if (shape->m_geometry) {
                CNodeClass* gnc = shape->m_geometry->GetNodeClass();
                ImGui::Text("  Geometry: %s", (gnc && gnc->m_className) ? gnc->m_className : "?");
                CMeshNode* mesh = dynamic_cast<CMeshNode*>(shape->m_geometry);
                if (mesh && mesh->m_url)
                    ImGui::Text("  Mesh: %s", mesh->m_url);
            }
        }

        // Instance: show script variables
        CInstance* inst = dynamic_cast<CInstance*>(sel);
        if (inst && inst->m_class && inst->m_class->m_members) {
            int varCount = inst->m_vars.GetLength();
            if (varCount > 0) {
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Script Variables", ImGuiTreeNodeFlags_DefaultOpen)) {
                    for (CDefine* d = inst->m_class->m_members->m_firstDefine; d; d = d->m_next) {
                        if (!d->m_node || d->m_node->m_obj != objMember) continue;
                        CMember* mem = (CMember*)d->m_node;
                        int idx = mem->m_memberIndex;
                        if (idx < 0 || idx >= varCount) continue;
                        CNode* varNode = inst->m_vars.GetNode(idx);
                        if (!varNode) {
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  %s: null", d->m_name);
                            continue;
                        }
                        // Show value based on object type
                        if (varNode->m_obj == objNumber) {
                            CNumObject* num = (CNumObject*)varNode;
                            ImGui::Text("  %s: %.4g", d->m_name, num->m_value);
                        } else if (varNode->m_obj == objString) {
                            CStrObject* str = (CStrObject*)varNode;
                            const TCHAR* sv = str->GetSz();
                            // Truncate long strings in display
                            if (sv && _tcslen(sv) > 40) {
                                char trunc[48];
                                strncpy(trunc, sv, 40); trunc[40] = '\0';
                                ImGui::Text("  %s: \"%s...\"", d->m_name, trunc);
                            } else {
                                ImGui::Text("  %s: \"%s\"", d->m_name, sv ? sv : "");
                            }
                        } else if (varNode->m_obj == objNode) {
                            CNodeClass* vnc = varNode->GetNodeClass();
                            ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "  %s: [%s]", d->m_name,
                                (vnc && vnc->m_className) ? vnc->m_className : "node");
                        } else {
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  %s: (type %d)", d->m_name, varNode->m_obj);
                        }
                    }
                }
            }
        }

        // Group child count (only if not a Transform, which already shows)
        CGroup* grp = dynamic_cast<CGroup*>(sel);
        if (grp && !xform && !inst) {
            ImGui::Spacing();
            ImGui::Text("Children: %d", grp->m_children.GetLength());
        }

        ImGui::Separator();
    }

    // Scene Graph tree
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Scene Graph");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(* hide, o show)");

    // Search filter
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Search nodes...", g_treeFilter, sizeof(g_treeFilter));

    if (g_pObject) {
        ImGui::BeginChild("SceneTree", ImVec2(0, 0), false);
        RenderNodeTree((CNode*)g_pObject, g_pObject, dev, g_treeFilter, 0);
        ImGui::EndChild();
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No scene loaded");
    }

    ImGui::End();
}
