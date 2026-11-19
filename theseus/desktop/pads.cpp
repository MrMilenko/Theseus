// pads.cpp: every controller SDL sees, opened as it connects and closed as
// it leaves, so pads can come and go mid-session. All of them drive the
// dashboard together. A new press makes a pad the active one; when the
// active one leaves, the one used most recently takes over.

#include "std.h"
#include <string>
#include <vector>
#include <SDL.h>
#include "pads.h"

struct Pad {
    SDL_GameController* gc;
    SDL_JoystickID id;
    Uint32 used;    // when it was last pressed, on s_clock
    Uint32 held;    // its buttons and directions last frame
};

bool g_padRumble = true;
int  g_padStyle = 0;

static std::vector<Pad> s_pads;
static SDL_JoystickID s_active = -1;
static Uint32 s_clock = 0;

static int Find(SDL_JoystickID id)
{
    for (size_t i = 0; i < s_pads.size(); i++)
        if (s_pads[i].id == id) return (int)i;
    return -1;
}

static void Open(int index)
{
    if (!SDL_IsGameController(index)) return;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
    if (id < 0 || Find(id) >= 0) return;
    SDL_GameController* gc = SDL_GameControllerOpen(index);
    if (!gc) {
        fprintf(stderr, "[Input] Couldn't open controller %d: %s\n", index, SDL_GetError());
        return;
    }
    Pad p = { gc, id, 0, 0 };
    s_pads.push_back(p);
    if (s_active < 0) s_active = id;
    const char* name = SDL_GameControllerName(gc);
    fprintf(stdout, "[Input] Controller connected: %s (%d connected)\n", name ? name : "?", (int)s_pads.size());
}

static void Close(int i)
{
    const char* name = SDL_GameControllerName(s_pads[i].gc);
    fprintf(stdout, "[Input] Controller disconnected: %s (%d left)\n", name ? name : "?", (int)s_pads.size() - 1);
    SDL_JoystickID id = s_pads[i].id;
    SDL_GameControllerClose(s_pads[i].gc);
    s_pads.erase(s_pads.begin() + i);
    if (id != s_active) return;
    int best = -1;
    for (size_t k = 0; k < s_pads.size(); k++)
        if (best < 0 || s_pads[k].used >= s_pads[best].used) best = (int)k;
    s_active = best < 0 ? -1 : s_pads[best].id;
}

// Anything connected that isn't open yet: the pads there at startup, and
// any whose added event another loop (the boot animation) ate.
static void OpenNew()
{
    static int s_seen = -1;
    int n = SDL_NumJoysticks();
    if (n == s_seen) return;
    s_seen = n;
    for (int i = 0; i < n; i++) Open(i);
}

void Pads_HandleEvent(const SDL_Event* e)
{
    if (e->type == SDL_CONTROLLERDEVICEADDED) {
        Open(e->cdevice.which);
    } else if (e->type == SDL_CONTROLLERDEVICEREMOVED) {
        int i = Find(e->cdevice.which);
        if (i >= 0) Close(i);
    }
}

// Buttons, triggers and sticks past half, one bit each.
static Uint32 Held(SDL_GameController* gc)
{
    Uint32 bits = 0;
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX && b < 20; b++)
        if (SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)b)) bits |= 1u << b;
    if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000)  bits |= 1u << 20;
    if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000) bits |= 1u << 21;
    for (int a = 0; a < 4; a++) {
        int v = SDL_GameControllerGetAxis(gc, (SDL_GameControllerAxis)a);
        if (v < -16384) bits |= 1u << (22 + a * 2);
        if (v > 16384)  bits |= 1u << (23 + a * 2);
    }
    return bits;
}

static void DevVirtualPads();

void Pads_Update()
{
    DevVirtualPads();
    OpenNew();
    for (int i = (int)s_pads.size() - 1; i >= 0; i--)
        if (!SDL_GameControllerGetAttached(s_pads[i].gc)) Close(i);

    for (size_t i = 0; i < s_pads.size(); i++) {
        Pad& p = s_pads[i];
        Uint32 held = Held(p.gc);
        if (held & ~p.held) {
            p.used = ++s_clock;
            s_active = p.id;
        }
        p.held = held;
    }
}

void Pads_Shutdown()
{
    for (size_t i = 0; i < s_pads.size(); i++) SDL_GameControllerClose(s_pads[i].gc);
    s_pads.clear();
    s_active = -1;
}

int Pads_Count() { return (int)s_pads.size(); }

SDL_GameController* Pads_Active()
{
    int i = Find(s_active);
    return i < 0 ? NULL : s_pads[i].gc;
}

SDL_GameController* Pads_Get(int i)
{
    return i < 0 || i >= (int)s_pads.size() ? NULL : s_pads[i].gc;
}

bool Pads_Button(SDL_GameControllerButton button)
{
    for (size_t i = 0; i < s_pads.size(); i++)
        if (SDL_GameControllerGetButton(s_pads[i].gc, button)) return true;
    return false;
}

int Pads_Style()
{
    if (g_padStyle > 0) return g_padStyle - 1;
    SDL_GameController* gc = Pads_Active();
    if (!gc) return PAD_STYLE_XBOX;
    switch (SDL_GameControllerGetType(gc)) {
    case SDL_CONTROLLER_TYPE_PS3:
    case SDL_CONTROLLER_TYPE_PS4:
#if SDL_VERSION_ATLEAST(2, 0, 14)
    case SDL_CONTROLLER_TYPE_PS5:
#endif
        return PAD_STYLE_PLAYSTATION;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
#if SDL_VERSION_ATLEAST(2, 24, 0)
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
#endif
        return PAD_STYLE_NINTENDO;
    default:
        break;
    }
    // Pads SDL can't name (third party, virtual) go by who made them.
    switch (SDL_GameControllerGetVendor(gc)) {
    case 0x054C: return PAD_STYLE_PLAYSTATION;   // Sony
    case 0x057E: return PAD_STYLE_NINTENDO;      // Nintendo
    }
    return PAD_STYLE_XBOX;
}

void Pads_Rumble(float strength, Uint32 ms)
{
    SDL_GameController* gc = Pads_Active();
    if (!g_padRumble || !gc) return;
    float s = strength < 0.0f ? 0.0f : strength > 1.0f ? 1.0f : strength;
    // The low motor is the heavy one; a touch less of it keeps short ones crisp.
    SDL_GameControllerRumble(gc, (Uint16)(s * 0.6f * 65535.0f), (Uint16)(s * 65535.0f), ms);
}

Sint16 Pads_Axis(SDL_GameControllerAxis axis)
{
    int best = 0;
    for (size_t i = 0; i < s_pads.size(); i++) {
        int v = SDL_GameControllerGetAxis(s_pads[i].gc, axis);
        if (abs(v) > abs(best)) best = v;
    }
    return (Sint16)best;
}

// Development only: UIX_DEV_PAD="5:plug 6:plug=ps 8:a@2 9:unplug@1" plugs
// in virtual pads (xbox, ps or switch), holds their buttons for 150 ms and
// pulls them out again, so hot plug and several pads can be tried without
// a real one. Pads are numbered from 1 in the order they were plugged in.
#if SDL_VERSION_ATLEAST(2, 24, 0)
struct DevPad { SDL_Joystick* joy; SDL_JoystickID id; };
static std::vector<DevPad> s_devPads;

static int SDLCALL DevRumble(void* user, Uint16 low, Uint16 high)
{
    fprintf(stdout, "[DevPad] Pad %d rumble %u %u\n", (int)(intptr_t)user, low, high);
    return 0;
}

static void DevPlug(const std::string& kind)
{
    SDL_VirtualJoystickDesc d;
    SDL_zero(d);
    d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes = SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    d.vendor_id = 0x045E; d.product_id = 0x0B12; d.name = "Virtual Xbox Pad";
    if (kind == "ps")     { d.vendor_id = 0x054C; d.product_id = 0x0CE6; d.name = "Virtual PlayStation Pad"; }
    if (kind == "switch") { d.vendor_id = 0x057E; d.product_id = 0x2009; d.name = "Virtual Switch Pad"; }
    d.userdata = (void*)(intptr_t)(s_devPads.size() + 1);
    d.Rumble = DevRumble;
    int index = SDL_JoystickAttachVirtualEx(&d);
    SDL_Joystick* joy = index < 0 ? NULL : SDL_JoystickOpen(index);
    if (!joy) { fprintf(stderr, "[DevPad] Couldn't plug in a pad: %s\n", SDL_GetError()); return; }
    DevPad p = { joy, SDL_JoystickInstanceID(joy) };
    s_devPads.push_back(p);
}

static void DevUnplug(int n)
{
    if (n < 1 || n > (int)s_devPads.size() || !s_devPads[n - 1].joy) return;
    DevPad& p = s_devPads[n - 1];
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_JoystickGetDeviceInstanceID(i) == p.id) { SDL_JoystickDetachVirtual(i); break; }
    SDL_JoystickClose(p.joy);
    p.joy = NULL;
}

static void DevPress(int n, const std::string& name, bool down)
{
    if (n < 1 || n > (int)s_devPads.size() || !s_devPads[n - 1].joy) return;
    SDL_Joystick* joy = s_devPads[n - 1].joy;
    static const struct { const char* name; int button; } kButtons[] = {
        { "a", SDL_CONTROLLER_BUTTON_A }, { "b", SDL_CONTROLLER_BUTTON_B },
        { "x", SDL_CONTROLLER_BUTTON_X }, { "y", SDL_CONTROLLER_BUTTON_Y },
        { "back", SDL_CONTROLLER_BUTTON_BACK }, { "guide", SDL_CONTROLLER_BUTTON_GUIDE },
        { "start", SDL_CONTROLLER_BUTTON_START },
        { "white", SDL_CONTROLLER_BUTTON_LEFTSHOULDER }, { "black", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
        { "up", SDL_CONTROLLER_BUTTON_DPAD_UP }, { "down", SDL_CONTROLLER_BUTTON_DPAD_DOWN },
        { "left", SDL_CONTROLLER_BUTTON_DPAD_LEFT }, { "right", SDL_CONTROLLER_BUTTON_DPAD_RIGHT },
    };
    if (name == "lt" || name == "rt") {
        SDL_JoystickSetVirtualAxis(joy, name == "lt" ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
                                   down ? 32767 : 0);
        return;
    }
    for (size_t i = 0; i < sizeof(kButtons) / sizeof(kButtons[0]); i++)
        if (name == kButtons[i].name) SDL_JoystickSetVirtualButton(joy, kButtons[i].button, down ? 1 : 0);
}

static void DevVirtualPads()
{
    struct Step { Uint32 at; std::string name, arg; int pad, state; };
    static std::vector<Step> s_steps;
    static bool s_parsed = false;
    if (!s_parsed) {
        s_parsed = true;
        const char* env = getenv("UIX_DEV_PAD");
        while (env && *env) {
            float t = 0;
            char name[16] = "", rest[32] = "";
            int used = 0;
            if (sscanf(env, " %f:%15[a-z]%n", &t, name, &used) != 2) break;
            env += used;
            if (sscanf(env, "%31[^ ]%n", rest, &used) == 1) env += used;
            Step st = { (Uint32)(t * 1000.0f), name, "", 1, 0 };
            if (rest[0] == '=') st.arg = rest + 1;
            else if (rest[0] == '@') st.pad = atoi(rest + 1);
            else if (strcmp(name, "plug") != 0 && strcmp(name, "unplug") != 0) continue;
            if (st.name == "unplug" && rest[0] != '@') st.pad = 0;
            s_steps.push_back(st);
        }
    }
    const Uint32 now = SDL_GetTicks();
    for (size_t i = 0; i < s_steps.size(); i++) {
        Step& st = s_steps[i];
        if (st.state == 2 || now < st.at) continue;
        if (st.name == "plug")        { DevPlug(st.arg); st.state = 2; }
        else if (st.name == "unplug") {
            int n = st.pad;
            for (int k = (int)s_devPads.size(); !n && k > 0; k--) if (s_devPads[k - 1].joy) n = k;
            DevUnplug(n);
            st.state = 2;
        }
        else if (st.state == 0)       { DevPress(st.pad, st.name, true); st.state = 1; }
        else if (now >= st.at + 150)  { DevPress(st.pad, st.name, false); st.state = 2; }
    }
}
#else
static void DevVirtualPads() {}
#endif
