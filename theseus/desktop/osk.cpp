// On screen keyboard for pad mode, up whenever ImGui has a text field active.
//
// It never takes ImGui focus: that would deactivate the field being typed
// into, WantTextInput would drop, and the keyboard would close itself. We run
// the cursor ourselves and feed characters into ImGui's input queue.

#include "std.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "pads.h"

extern bool g_padModeActive;

// Three layers. Paths need the symbol page; without it half the fields in
// the app are untypeable.
static const char* kRowsLower[] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm",
};
static const char* kRowsUpper[] = {
    "!@#$%^&*()",
    "QWERTYUIOP",
    "ASDFGHJKL",
    "ZXCVBNM",
};
static const char* kRowsSym[] = {
    "1234567890",
    "/\\:~.-_+=",
    "()[]{}<>|",
    "\'\"`,;?!",
};
static const int kRowCount = 4;

// Bottom row of actions, addressed as row kRowCount.
enum { ACT_SHIFT = 0, ACT_SYM, ACT_SPACE, ACT_BACK, ACT_DONE, ACT_COUNT };
static const char* kActLabels[ACT_COUNT] = { "Shift", "#+=", "Space", "Back", "Done" };

static int  s_row = 1, s_col = 0;
static bool s_shift = false;
static bool s_sym   = false;

static const char* RowChars(int row) {
    if (s_sym)   return kRowsSym[row];
    return s_shift ? kRowsUpper[row] : kRowsLower[row];
}

static int RowLen(int row) {
    if (row >= kRowCount) return ACT_COUNT;
    return (int)strlen(RowChars(row));
}

static void Commit(ImGuiIO& io) {
    if (s_row < kRowCount) {
        io.AddInputCharacter((unsigned int)(unsigned char)RowChars(s_row)[s_col]);
        s_shift = false;   // sticky for one character, like a phone keyboard
        return;
    }
    switch (s_col) {
        case ACT_SHIFT: s_shift = !s_shift; s_sym = false; break;
        case ACT_SYM:   s_sym   = !s_sym;   s_shift = false; break;
        case ACT_SPACE: io.AddInputCharacter(' '); break;
        case ACT_BACK:
            io.AddKeyEvent(ImGuiKey_Backspace, true);
            io.AddKeyEvent(ImGuiKey_Backspace, false);
            break;
        case ACT_DONE:
            io.AddKeyEvent(ImGuiKey_Enter, true);
            io.AddKeyEvent(ImGuiKey_Enter, false);
            break;
    }
}

// sdl_main reads this before NewFrame to decide if ImGui nav gets the pad.
static bool s_oskUp = false;
bool OSK_IsActive() { return s_oskUp; }

// Read the pads straight from SDL: ImGui's key state is not fed while we
// hold it, so we edge detect ourselves.
struct PadEdge {
    bool prev[SDL_CONTROLLER_BUTTON_MAX];
    bool Pressed(SDL_GameControllerButton b) {
        const bool now = Pads_Button(b);
        const bool hit = now && !prev[b];
        prev[b] = now;
        return hit;
    }
};
static PadEdge s_pad;

void RenderOnScreenKeyboard() {
    ImGuiIO& io = ImGui::GetIO();
    if (!g_padModeActive) { s_oskUp = false; return; }

    if (!io.WantTextInput) { s_oskUp = false; return; }
    s_oskUp = true;

    // We hold the pads, so the field below keeps focus and stays still.

    if (s_pad.Pressed(SDL_CONTROLLER_BUTTON_DPAD_LEFT))  s_col--;
    if (s_pad.Pressed(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) s_col++;
    if (s_pad.Pressed(SDL_CONTROLLER_BUTTON_DPAD_UP))    s_row--;
    if (s_pad.Pressed(SDL_CONTROLLER_BUTTON_DPAD_DOWN))  s_row++;
    if (s_row < 0) s_row = kRowCount;
    if (s_row > kRowCount) s_row = 0;
    if (s_col < 0) s_col = RowLen(s_row) - 1;
    if (s_col >= RowLen(s_row)) s_col = 0;

    if (s_pad.Pressed(SDL_CONTROLLER_BUTTON_A)) Commit(io);
    if (s_pad.Pressed(SDL_CONTROLLER_BUTTON_X)) {
        io.AddKeyEvent(ImGuiKey_Backspace, true);
        io.AddKeyEvent(ImGuiKey_Backspace, false);
    }
    if (s_pad.Pressed(SDL_CONTROLLER_BUTTON_B)) {   // done, hand the pad back
        io.AddKeyEvent(ImGuiKey_Enter, true);
        io.AddKeyEvent(ImGuiKey_Enter, false);
        s_oskUp = false;
    }

    // Foreground list, not a window: it has to sit above every tool window,
    // and a window would either steal focus or get buried behind one.
    const float fh    = ImGui::GetFontSize();
    const float cell  = fh * 2.0f;
    const float gap   = fh * 0.28f;
    const float actW  = cell * 1.7f;

    // Width off the widest row, or the action keys get clipped.
    float keyW = 10 * (cell + gap) - gap;
    float actRowW = ACT_COUNT * (actW + gap) - gap;
    const float innerW = (actRowW > keyW) ? actRowW : keyW;
    const float padW = fh * 1.2f;
    const float w = innerW + padW * 2.0f;
    const float h = (kRowCount + 1) * (cell + gap) - gap + padW * 2.0f + fh * 1.6f;

    const float ox = (io.DisplaySize.x - w) * 0.5f;
    const float oy = io.DisplaySize.y - h - fh * 3.2f;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // Controller menu palette, so it reads as one system.
    dl->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + w, oy + h),
                      IM_COL32(8, 22, 8, 245), 8.0f);
    dl->AddRect(ImVec2(ox, oy), ImVec2(ox + w, oy + h),
                IM_COL32(90, 190, 85, 220), 8.0f, 0, 2.0f);

    const char* caption = s_sym ? "SYMBOLS" : (s_shift ? "SHIFT" : "KEYBOARD");
    dl->AddText(ImVec2(ox + padW, oy + fh * 0.5f), IM_COL32(140, 235, 130, 255), caption);

    const float gridY = oy + padW + fh * 1.2f;
    for (int row = 0; row <= kRowCount; row++) {
        const int len = RowLen(row);
        const float cw = (row == kRowCount) ? actW : cell;
        const float rowW = len * (cw + gap) - gap;
        float x = ox + (w - rowW) * 0.5f;
        const float y = gridY + row * (cell + gap);

        for (int col = 0; col < len; col++) {
            const bool sel = (row == s_row && col == s_col);
            char buf[16];
            if (row < kRowCount) { buf[0] = RowChars(row)[col]; buf[1] = '\0'; }
            else                 snprintf(buf, sizeof(buf), "%s", kActLabels[col]);

            const bool latched = (row == kRowCount &&
                                  ((col == ACT_SHIFT && s_shift) || (col == ACT_SYM && s_sym)));
            ImU32 bg = sel      ? IM_COL32(60, 170, 60, 255)
                     : latched  ? IM_COL32(28, 80, 30, 255)
                                : IM_COL32(20, 34, 20, 235);
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cw, y + cell), bg, 5.0f);
            dl->AddRect(ImVec2(x, y), ImVec2(x + cw, y + cell),
                        sel ? IM_COL32(190, 255, 185, 255) : IM_COL32(55, 95, 55, 200),
                        5.0f, 0, sel ? 2.0f : 1.0f);

            const ImVec2 ts = ImGui::CalcTextSize(buf);
            dl->AddText(ImVec2(x + (cw - ts.x) * 0.5f, y + (cell - ts.y) * 0.5f),
                        sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(210, 235, 208, 255), buf);
            x += cw + gap;
        }
    }
}
