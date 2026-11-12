// joystick.cpp: the desktop Joystick node. The node itself (binding,
// properties, the secret key, the event rules) is rebuilt from the 5960
// retail XBE (0x40620..0x41800); reading an SDL pad and the keyboard in
// place of XInput is Theseus's. Counterpart to xbox/input.cpp.

#include "std.h"
#include <vector>
#include "dashapp.h"
#include "node.h"
#include <SDL.h>

#define REPEAT_DELAY 0.3f
#define REPEAT_INTERVAL 0.12f

#define KS_BEGIN 0x0001
#define KS_VERIFIED 0x0002
#define KS_END 0x0004

float g_xaxis, g_yaxis;


bool g_bInputEnable = true;

// ============================================================================
// JoySnapshot: unified controller + keyboard state
// ============================================================================

struct JoySnapshot {
    bool analogDown[8];   // A,B,X,Y,Black,White,LTrig,RTrig (after threshold)
    bool digitalDown[8];  // DPadU,D,L,R,Start,Back,LThumb,RThumb
    float thumbLX, thumbLY, thumbRX, thumbRY;  // after dead zone
};

class CJoystick : public CNode
{
    DECLARE_NODE(CJoystick, CNode)

public:
    CJoystick();
    ~CJoystick();

    bool m_bNeedsInit;

    bool m_isBound;
    float m_frequency;

    float m_hat;

    float m_xaxis;
    float m_yaxis;

    float m_xaxis2;
    float m_yaxis2;

    float m_a;
    float m_b;
    float m_x;
    float m_y;
    float m_black;
    float m_white;
    float m_left;
    float m_right;
    float m_leftThumb;
    float m_rightThumb;
    float m_start;
    float m_back;

    char *m_secretKey;
    size_t m_secretKeyLength;
    size_t m_secretKeyCurrentIndex;
    bool m_enableSecretKey;
    bool m_eatSecretKey;

    bool m_enableRemote;
    bool m_enableGamepad;

    // 5960's LastActivePort (PRD 0x001747e8) is the Xbox port that last gave
    // input. Desktop folds the pad and keyboard into one player, so it stays 0.
    int m_LastActivePort;

    void Advance(float nSeconds);
    bool OnSetProperty(const PRD *pprd, const void *pvValue);

    void Bind();

    void CheckDevice();

    void EnableGlobalInput(int bEnable);

    void CallFunction(const char *szFunc, bool bRemote = false);

    static SDL_GameController* c_controller;

    static CJoystick *c_pPreviousBoundJoystick;
    static CJoystick *c_pBoundJoystick;

    JoySnapshot m_prevState;
    XTIME m_nextRepeatTime;
    float m_repeatInterval;

    XTIME m_timeNextUpdate;

    uint32_t m_prevMediaKeys; // bitmask for edge detection

    void OnMoveUp();
    void OnMoveDown();
    void OnADown();
    void OnBDown();

    DECLARE_NODE_PROPS()
    DECLARE_NODE_FUNCTIONS()

protected:
    int CheckSecretKeySequence(char key);
    void ProcessSecretKeySequence(int flags);
    void ButtonEvent(bool down, const char *downName, const char *upName, char key);
    void MoveEvent(char key, const char *name);
    static void PollSDL(SDL_GameController* gc, JoySnapshot* out);
    void ProcessMediaKeys();
};

SDL_GameController* CJoystick::c_controller = NULL;

// The keyboard reads the pad itself while up: nav is off for the duration,
// so ImGui's key state isn't fed.
SDL_GameController* Joy_GetController() { return CJoystick::c_controller; }

CJoystick *CJoystick::c_pBoundJoystick = NULL;
CJoystick *CJoystick::c_pPreviousBoundJoystick = NULL;

IMPLEMENT_NODE("Joystick", CJoystick, CNode)

START_NODE_PROPS(CJoystick, CNode)
NODE_PROP(pt_boolean, CJoystick, isBound)
NODE_PROP(pt_number, CJoystick, frequency)
NODE_PROP(pt_number, CJoystick, hat)
NODE_PROP(pt_number, CJoystick, xaxis)
NODE_PROP(pt_number, CJoystick, yaxis)
NODE_PROP(pt_number, CJoystick, xaxis2)
NODE_PROP(pt_number, CJoystick, yaxis2)
NODE_PROP(pt_number, CJoystick, a)
NODE_PROP(pt_number, CJoystick, b)
NODE_PROP(pt_number, CJoystick, x)
NODE_PROP(pt_number, CJoystick, y)
NODE_PROP(pt_number, CJoystick, black)
NODE_PROP(pt_number, CJoystick, white)
NODE_PROP(pt_number, CJoystick, left)
NODE_PROP(pt_number, CJoystick, right)
NODE_PROP(pt_number, CJoystick, leftThumb)
NODE_PROP(pt_number, CJoystick, rightThumb)
NODE_PROP(pt_number, CJoystick, start)
NODE_PROP(pt_number, CJoystick, back)
NODE_PROP(pt_string, CJoystick, secretKey)
NODE_PROP(pt_boolean, CJoystick, enableSecretKey)
NODE_PROP(pt_boolean, CJoystick, enableGamepad)
NODE_PROP(pt_boolean, CJoystick, enableRemote)
NODE_PROP(pt_integer, CJoystick, LastActivePort)
END_NODE_PROPS()

#undef _FND_CLASS
#define _FND_CLASS CJoystick
START_NODE_FUN(CJoystick, CNode)
NODE_FUN_VI(EnableGlobalInput)
NODE_FUN_VV(OnMoveUp)
NODE_FUN_VV(OnMoveDown)
NODE_FUN_VV(OnADown)
NODE_FUN_VV(OnBDown)
END_NODE_FUN()

// CJoystick::CJoystick()
// Reconstructed from 5960 retail XBE @ 0x00040620: polls 20 times a second,
// sticks and buttons at rest, no secret key, remote and gamepad enabled,
// waiting for its first poll. The first Joystick made is bound right away.
// Theseus starts hat at -1 and adds the desktop poll state.
CJoystick::CJoystick()
{
    m_bNeedsInit = true;
    m_isBound = false;
    m_frequency = 20.0f;
    m_hat = -1.0f;
    m_xaxis = m_yaxis = 0.0f;
    m_xaxis2 = m_yaxis2 = 0.0f;
    m_a = m_b = m_x = m_y = 0.0f;
    m_black = m_white = 0.0f;
    m_left = m_right = 0.0f;
    m_leftThumb = m_rightThumb = 0.0f;
    m_start = m_back = 0.0f;
    m_secretKey = NULL;
    m_secretKeyLength = 0;
    m_secretKeyCurrentIndex = 0;
    m_enableSecretKey = false;
    m_eatSecretKey = false;
    m_enableRemote = true;
    m_enableGamepad = true;
    m_LastActivePort = 0;
    m_nextRepeatTime = 0.0f;
    m_repeatInterval = 0.0f;
    m_timeNextUpdate = 0.0f;
    m_prevMediaKeys = 0;
    memset(&m_prevState, 0, sizeof(m_prevState));

    if (c_pBoundJoystick == NULL)
        Bind();
}

// CJoystick::~CJoystick()
// Reconstructed from 5960 retail XBE @ 0x00040750: forgets itself as the
// previous binding, hands the binding back to the previous one if it held
// it, and frees the secret key.
CJoystick::~CJoystick()
{
    if (c_pPreviousBoundJoystick == this)
        c_pPreviousBoundJoystick = NULL;
    if (c_pBoundJoystick == this)
        c_pBoundJoystick = c_pPreviousBoundJoystick;
    delete[] m_secretKey;
}

// Script events go only to the bound joystick. 5960 also drops gamepad
// events while enableGamepad is off; desktop's pad and keyboard always
// count, and only remote events honor enableRemote.
void CJoystick::CallFunction(const char *szFunc, bool bRemote /* = false */)
{
    if (c_pBoundJoystick != this)
        return;
    if (bRemote && !m_enableRemote)
        return;
    ::CallFunction(this, szFunc);
}

// BindJoystick(node)
// Reconstructed from 5960 retail XBE @ 0x00040790: NULL clears both
// bindings; a Joystick node gets bound; anything else is ignored.
void BindJoystick(CNode *pJoystickNode)
{
    if (pJoystickNode == NULL)
    {
        CJoystick::c_pBoundJoystick = NULL;
        CJoystick::c_pPreviousBoundJoystick = NULL;
        return;
    }
    if (pJoystickNode->IsKindOf(NODE_CLASS(CJoystick)))
        ((CJoystick *)pJoystickNode)->Bind();
}

// Theseus: the four nav calls exist so scripts can name them; they do nothing.
void CJoystick::OnMoveUp()
{
}

void CJoystick::OnMoveDown()
{
}

void CJoystick::OnADown()
{
}

void CJoystick::OnBDown()
{
}

// CJoystick::Bind()
// Reconstructed from 5960 retail XBE @ 0x000407f0: the current binding
// becomes the previous one and this takes over; once it has polled before,
// it rereads the device so held buttons don't fire.
void CJoystick::Bind()
{
    c_pPreviousBoundJoystick = c_pBoundJoystick;
    c_pBoundJoystick = this;
    if (m_bNeedsInit)
        return;
    CheckDevice();
    PollSDL(c_controller, &m_prevState);
}

// CJoystick::OnSetProperty(prd, value)
// Reconstructed from 5960 retail XBE @ 0x00040820.
//     isBound: true binds; false, if bound, unbinds and rebinds the previous
//     secretKey: replaces the key (ending a sequence in progress), and a
//         new key turns enableSecretKey on
//     enableSecretKey: stores it; turning it off mid sequence ends it; the
//         position restarts either way
//     eatSecretKey, enableGamepad, enableRemote: stored here, not by the caller
bool CJoystick::OnSetProperty(const PRD *pprd, const void *pvValue)
{
    intptr_t field = (intptr_t)pprd->pbOffset;
    if (field == MEMBER_OFFSET(m_isBound))
    {
        if (*(const bool *)pvValue)
        {
            Bind();
        }
        else if (c_pBoundJoystick == this)
        {
            c_pBoundJoystick = NULL;
            if (c_pPreviousBoundJoystick != NULL)
                c_pPreviousBoundJoystick->Bind();
        }
        return true;
    }

    if (field == MEMBER_OFFSET(m_secretKey))
    {
        delete[] m_secretKey;
        m_secretKey = NULL;
        m_secretKeyLength = 0;
        if (m_secretKeyCurrentIndex != 0)
            ProcessSecretKeySequence(KS_END);
        m_secretKeyCurrentIndex = 0;

        const char *key = *(char *const *)pvValue;
        if (key != NULL)
        {
            m_secretKeyLength = strlen(key);
            m_secretKey = new char[m_secretKeyLength + 1];
            strcpy(m_secretKey, key);
            m_enableSecretKey = true;
        }
        return true;
    }

    if (field == MEMBER_OFFSET(m_enableSecretKey))
    {
        m_enableSecretKey = *(const bool *)pvValue;
        if (!m_enableSecretKey && m_secretKeyCurrentIndex != 0 && m_secretKeyCurrentIndex < m_secretKeyLength)
            ProcessSecretKeySequence(KS_END);
        m_secretKeyCurrentIndex = 0;
        return true;
    }

    bool *flag = NULL;
    if (field == MEMBER_OFFSET(m_eatSecretKey))
        flag = &m_eatSecretKey;
    else if (field == MEMBER_OFFSET(m_enableGamepad))
        flag = &m_enableGamepad;
    else if (field == MEMBER_OFFSET(m_enableRemote))
        flag = &m_enableRemote;
    if (flag == NULL)
        return true;
    *flag = *(const bool *)pvValue;
    return false;
}

// ============================================================================
// Controller Detection
// ============================================================================
void CJoystick::CheckDevice()
{
    // Check if current controller was disconnected
    if (c_controller && !SDL_GameControllerGetAttached(c_controller))
    {
        fprintf(stdout, "[Input] Controller disconnected\n");
        SDL_GameControllerClose(c_controller);
        c_controller = NULL;
    }

    // Try to find a controller if we don't have one
    if (!c_controller)
    {
        for (int i = 0; i < SDL_NumJoysticks(); i++)
        {
            if (SDL_IsGameController(i))
            {
                c_controller = SDL_GameControllerOpen(i);
                if (c_controller)
                {
                    fprintf(stdout, "[Input] SDL controller opened: %s\n",
                            SDL_GameControllerName(c_controller));
                    break;
                }
            }
        }
    }
}

// CJoystick::EnableGlobalInput(on)   [script fn 'EnableGlobalInput', FND 0x00181fb0, sig 14]
// Reconstructed from 5960 retail XBE @ 0x00040b70: sets the global input switch.
void CJoystick::EnableGlobalInput(int bEnable)
{
    g_bInputEnable = bEnable != 0;
}

// ============================================================================
// Input Polling
// ============================================================================
static bool GuideChord(SDL_GameController* gc);
static void DevPadScript(JoySnapshot* out);

void CJoystick::PollSDL(SDL_GameController* gc, JoySnapshot* out)
{
    memset(out, 0, sizeof(*out));

    // Tools have the pad: leave it zeroed and the dashboard sits still.
    extern bool ImGui_OwnsPad();
    if (ImGui_OwnsPad()) return;

    const bool guideHoldsB = GuideChord(gc);

    // --- Physical controller ---
    if (gc)
    {
        // Face buttons -> analogDown[0..3] = A,B,X,Y
        out->analogDown[0] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A);
        out->analogDown[1] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B) && !guideHoldsB;
        out->analogDown[2] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X);
        out->analogDown[3] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y);
        // Black(RB), White(LB) -> analogDown[4,5]
        out->analogDown[4] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        out->analogDown[5] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        // Triggers -> analogDown[6,7]
        Sint16 lt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        Sint16 rt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        out->analogDown[6] = (lt > 8000);
        out->analogDown[7] = (rt > 8000);

        // Digital buttons -> digitalDown[0..7] = DPadU,D,L,R,Start,Back,LThumb,RThumb
        out->digitalDown[0] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP);
        out->digitalDown[1] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        out->digitalDown[2] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        out->digitalDown[3] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        out->digitalDown[4] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START);
        out->digitalDown[5] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK);
        out->digitalDown[6] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSTICK);
        out->digitalDown[7] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSTICK);

        // Thumbsticks (SDL Y is inverted vs Xbox)
        float lx = (float)SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
        float ly = (float)(-SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY)) / 32767.0f;
        float rx = (float)SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
        float ry = (float)(-SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY)) / 32767.0f;

        // Dead zone
        if (lx > -0.9f && lx < 0.9f) lx = 0.0f;
        if (ly > -0.9f && ly < 0.9f) ly = 0.0f;
        if (rx > -0.9f && rx < 0.9f) rx = 0.0f;
        if (ry > -0.9f && ry < 0.9f) ry = 0.0f;

        out->thumbLX = lx;
        out->thumbLY = ly;
        out->thumbRX = rx;
        out->thumbRY = ry;
    }

    // --- Keyboard overlay (OR on top) ---
    extern bool ImGui_WantsKeyboard();
    extern SDL_Window* g_pSDLWindow;
    extern bool g_bUseOnScreenKeyboard;
    extern void* g_pActiveKeyboard_void();  // CKeyboard* erased to dodge fwd-decl wrangling

    bool imguiActive = ImGui_WantsKeyboard();
    SDL_Window* focusedWin = SDL_GetKeyboardFocus();
    bool mainHasFocus = (focusedWin == g_pSDLWindow);
    // Suppress kb->gamepad mapping while an on-screen keyboard popup is taking
    // physical input. Otherwise typing 'q' fires LT, 'e' fires RT, backspace
    // fires both Backspace and B (back/cancel), etc.
    bool dashKbActive = (g_pActiveKeyboard_void() != NULL) && !g_bUseOnScreenKeyboard;

    // Escape always acts as B (back/cancel) when a dashboard keyboard popup is
    // up. Otherwise there's no keyboard-only way out, since Backspace is now
    // routed into the text buffer instead of the gamepad layer.
    if (mainHasFocus && !imguiActive && dashKbActive)
    {
        const Uint8* keys = SDL_GetKeyboardState(NULL);
        if (keys[SDL_SCANCODE_ESCAPE]) out->analogDown[1] = true;  // B
    }

    if (mainHasFocus && !imguiActive && !dashKbActive)
    {
        const Uint8* keys = SDL_GetKeyboardState(NULL);

        // Analog: A,B,X,Y,Black,White,LTrig,RTrig
        if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE]) out->analogDown[0] = true;  // A
        if (keys[SDL_SCANCODE_B] || keys[SDL_SCANCODE_ESCAPE]) out->analogDown[1] = true;  // B
        if (keys[SDL_SCANCODE_X]) out->analogDown[2] = true;  // X
        if (keys[SDL_SCANCODE_Y]) out->analogDown[3] = true;  // Y
        if (keys[SDL_SCANCODE_GRAVE]) out->analogDown[4] = true;  // Black
        if (keys[SDL_SCANCODE_TAB]) out->analogDown[5] = true;  // White
        if (keys[SDL_SCANCODE_Q]) out->analogDown[6] = true;  // LTrig
        if (keys[SDL_SCANCODE_E]) out->analogDown[7] = true;  // RTrig

        // Digital: DPadU,D,L,R,Start,Back,LThumb,RThumb
        if (keys[SDL_SCANCODE_UP]) out->digitalDown[0] = true;
        if (keys[SDL_SCANCODE_DOWN]) out->digitalDown[1] = true;
        if (keys[SDL_SCANCODE_LEFT]) out->digitalDown[2] = true;
        if (keys[SDL_SCANCODE_RIGHT]) out->digitalDown[3] = true;
        // Don't map keyboard to Start. Enter is A only (Start+A = double OnADown).
        // Backspace fires digital Back (which routes to OnBDown). The letter B
        // key fires analog B (also OnBDown). Each key fires OnBDown exactly
        // once per press; sharing one key between both would double-fire and
        // skip a back-nav level.
        if (keys[SDL_SCANCODE_BACKSPACE]) out->digitalDown[5] = true;  // Back
        if (keys[SDL_SCANCODE_Z]) out->digitalDown[6] = true;  // LThumb
        if (keys[SDL_SCANCODE_C]) out->digitalDown[7] = true;  // RThumb

        // WASD -> left stick
        if (out->thumbLX == 0.0f)
        {
            if (keys[SDL_SCANCODE_A]) out->thumbLX = -1.0f;
            else if (keys[SDL_SCANCODE_D]) out->thumbLX = 1.0f;
        }
        if (out->thumbLY == 0.0f)
        {
            if (keys[SDL_SCANCODE_W]) out->thumbLY = 1.0f;
            else if (keys[SDL_SCANCODE_S]) out->thumbLY = -1.0f;
        }
    }

    DevPadScript(out);
}

// Guide button, LT+B or F7 asks for the guide. The B of LT+B never reaches
// the scene, or it would also back out of whatever is under the guide.
static bool GuideChord(SDL_GameController* gc)
{
    extern bool g_guideRequested;
    extern SDL_Window* g_pSDLWindow;
    static bool s_guide = false, s_chord = false, s_f7 = false;
    static bool s_holdB = false;

    bool guide = false, chord = false, b = false;
    if (gc) {
        guide = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_GUIDE) != 0;
        b = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B) != 0;
        chord = b && SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000;
    }
    extern bool ImGui_WantsKeyboard();
    const bool f7 = SDL_GetKeyboardFocus() == g_pSDLWindow && !ImGui_WantsKeyboard() &&
                    SDL_GetKeyboardState(NULL)[SDL_SCANCODE_F7];

    if ((guide && !s_guide) || (chord && !s_chord) || (f7 && !s_f7))
        g_guideRequested = true;
    if (chord && !s_chord) s_holdB = true;
    s_guide = guide; s_chord = chord; s_f7 = f7;

    if (s_holdB && !b) s_holdB = false;
    return s_holdB;
}

// Development only: UIX_DEV_PAD="14:guide 16:down 17.5:a" presses pad buttons
// at those seconds after start, so screens can be captured without a pad.
static void DevPadScript(JoySnapshot* out)
{
    struct Press { Uint32 at; int kind, idx; };
    static std::vector<Press> s_presses;
    static bool s_parsed = false;
    if (!s_parsed) {
        s_parsed = true;
        const char* env = getenv("UIX_DEV_PAD");
        static const struct { const char* name; int kind, idx; } kNames[] = {
            { "a", 0, 0 }, { "b", 0, 1 }, { "x", 0, 2 }, { "y", 0, 3 },
            { "black", 0, 4 }, { "white", 0, 5 }, { "lt", 0, 6 }, { "rt", 0, 7 },
            { "up", 1, 0 }, { "down", 1, 1 }, { "left", 1, 2 }, { "right", 1, 3 },
            { "start", 1, 4 }, { "back", 1, 5 }, { "guide", 2, 0 },
        };
        while (env && *env) {
            float t = 0; char name[16] = "";
            int used = 0;
            if (sscanf(env, " %f:%15[a-z]%n", &t, name, &used) != 2) break;
            env += used;
            for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++)
                if (strcmp(kNames[i].name, name) == 0) {
                    Press p = { (Uint32)(t * 1000.0f), kNames[i].kind, kNames[i].idx };
                    s_presses.push_back(p);
                }
        }
    }
    const Uint32 now = SDL_GetTicks();
    for (size_t i = 0; i < s_presses.size(); i++) {
        const Press& p = s_presses[i];
        if (now < p.at || now > p.at + 150) continue;
        if (p.kind == 0) out->analogDown[p.idx] = true;
        else if (p.kind == 1) out->digitalDown[p.idx] = true;
        else { extern bool g_guideRequested; static Uint32 s_last = 0;
               if (s_last != p.at) { s_last = p.at; g_guideRequested = true; } }
    }
}

// ============================================================================
// Media Keys (DVD player controls mapped to keyboard)
// ============================================================================
void CJoystick::ProcessMediaKeys()
{
    extern bool ImGui_WantsKeyboard();
    extern SDL_Window* g_pSDLWindow;

    if (ImGui_WantsKeyboard()) return;
    SDL_Window* focusedWin = SDL_GetKeyboardFocus();
    if (focusedWin != g_pSDLWindow) return;

    const Uint8* keys = SDL_GetKeyboardState(NULL);

    // Build current bitmask
    uint32_t cur = 0;
    if (keys[SDL_SCANCODE_LEFTBRACKET])  cur |= (1 << 0);  // [ = OnReverse
    if (keys[SDL_SCANCODE_RIGHTBRACKET]) cur |= (1 << 1);  // ] = OnForward
    if (keys[SDL_SCANCODE_I])            cur |= (1 << 2);  // I = OnInfo
    if (keys[SDL_SCANCODE_SEMICOLON])    cur |= (1 << 3);  // ; = OnPlay
    if (keys[SDL_SCANCODE_APOSTROPHE])   cur |= (1 << 4);  // ' = OnPause
    if (keys[SDL_SCANCODE_0])            cur |= (1 << 5);
    if (keys[SDL_SCANCODE_1])            cur |= (1 << 6);
    if (keys[SDL_SCANCODE_2])            cur |= (1 << 7);
    if (keys[SDL_SCANCODE_3])            cur |= (1 << 8);
    if (keys[SDL_SCANCODE_4])            cur |= (1 << 9);
    if (keys[SDL_SCANCODE_5])            cur |= (1 << 10);
    if (keys[SDL_SCANCODE_6])            cur |= (1 << 11);
    if (keys[SDL_SCANCODE_7])            cur |= (1 << 12);
    if (keys[SDL_SCANCODE_8])            cur |= (1 << 13);
    if (keys[SDL_SCANCODE_9])            cur |= (1 << 14);

    // Fire on rising edges only
    uint32_t pressed = cur & ~m_prevMediaKeys;
    m_prevMediaKeys = cur;

    if (pressed & (1 << 0))  CallFunction("OnReverse");
    if (pressed & (1 << 1))  CallFunction("OnForward");
    if (pressed & (1 << 2))  CallFunction("OnInfo");
    if (pressed & (1 << 3))  CallFunction("OnPlay");
    if (pressed & (1 << 4))  CallFunction("OnPause");

    // Digit keys
    static const char* digitFuncs[] = {"On0","On1","On2","On3","On4","On5","On6","On7","On8","On9"};
    for (int i = 0; i < 10; i++)
    {
        if (pressed & (1 << (5 + i)))
            CallFunction(digitFuncs[i]);
    }
}

// ============================================================================
// Advance: main input processing loop
// ============================================================================
static bool EqualSnapshots(const JoySnapshot* a, const JoySnapshot* b)
{
    for (int i = 0; i < 8; i++)
        if (a->analogDown[i] != b->analogDown[i]) return false;
    for (int i = 0; i < 8; i++)
        if (a->digitalDown[i] != b->digitalDown[i]) return false;
    if (a->thumbLX != b->thumbLX || a->thumbLY != b->thumbLY) return false;
    if (a->thumbRX != b->thumbRX || a->thumbRY != b->thumbRY) return false;
    return true;
}

// Event names and secret key letters, in the binary's table order: the
// eight analog buttons (0x00176640 down, 0x00176620 up, letters
// "ABXYbwlr") and the eight digital ones (0x00176600 down, 0x001765e0 up,
// letters at 0x000259a0, none for the d-pad). Start and Back raise the A and
// B events.
static const char *const s_analogDown[8] = { "OnADown", "OnBDown", "OnXDown", "OnYDown", "OnBlackDown", "OnWhiteDown", "OnLeftDown", "OnRightDown" };
static const char *const s_analogUp[8]   = { "OnAUp", "OnBUp", "OnXUp", "OnYUp", "OnBlackUp", "OnWhiteUp", "OnLeftUp", "OnRightUp" };
static const char s_analogKeys[8]        = { 'A', 'B', 'X', 'Y', 'b', 'w', 'l', 'r' };
static const char *const s_digitalDown[8] = { "OnPressUp", "OnPressDown", "OnPressLeft", "OnPressRight", "OnADown", "OnBDown", "OnLeftThumbDown", "OnRightThumbDown" };
static const char *const s_digitalUp[8]   = { "OnReleaseUp", "OnReleaseDown", "OnReleaseLeft", "OnReleaseRight", "OnAUp", "OnBUp", "OnLeftThumbUp", "OnRightThumbUp" };
static const char s_digitalKeys[8]        = { 0, 0, 0, 0, 'S', 'P', 'Q', 'W' };

// Fires down or up for one button, feeding a press to the secret key check.
void CJoystick::ButtonEvent(bool down, const char *downName, const char *upName, char key)
{
    if (!down)
    {
        CallFunction(upName);
        return;
    }
    int flags = CheckSecretKeySequence(key);
    CallFunction(downName);
    ProcessSecretKeySequence(flags);
}

// One step of a held direction: the secret key letter, then the move.
void CJoystick::MoveEvent(char key, const char *name)
{
    int flags = CheckSecretKeySequence(key);
    CallFunction(name);
    ProcessSecretKeySequence(flags);
}

// CJoystick::Advance(dt)
// Reconstructed from 5960 retail XBE @ 0x00041040; see notes on the Xbox
// side in docs/decomp/Input.md. Only the bound joystick with input enabled
// runs, at `frequency` polls a second; the first poll just records state.
// A change while the screen saver is up only wakes it. Then:
//   - the left stick (or the d-pad when the stick rests) sets xaxis/yaxis;
//     a held direction fires OnMoveLeft/Right then OnMoveDown/Up at once,
//     again after 0.3 s, then every 0.12 s
//   - every analog and digital button that changed fires its down or up
//     event, presses also stepping the secret key
//   - Start with A and B fires OnReset
//   - the stick coming back to rest fires OnMoveCenter
// Theseus polls SDL (pad plus keyboard) instead of XInput, adds the media
// keys, has no IR remote or progressive scan combo, and fires OnMoveCenter
// last where 5960 fires it before the buttons.
void CJoystick::Advance(float nSeconds)
{
    CNode::Advance(nSeconds);
    if (!g_bInputEnable || this != c_pBoundJoystick)
        return;

    CheckDevice();
    if (m_bNeedsInit)
    {
        PollSDL(c_controller, &m_prevState);
        m_timeNextUpdate = TheseusGetNow();
        m_bNeedsInit = false;
    }
    if (TheseusGetNow() < m_timeNextUpdate)
        return;
    m_timeNextUpdate = TheseusGetNow() + 1.0f / m_frequency;

    ProcessMediaKeys();

    JoySnapshot now;
    PollSDL(c_controller, &now);
    if (!EqualSnapshots(&now, &m_prevState) && ResetScreenSaver())
    {
        m_prevState = now;
        return;
    }

    bool wasCentered = m_xaxis == 0.0f && m_yaxis == 0.0f;
    m_xaxis = now.thumbLX;
    m_yaxis = now.thumbLY;
    m_xaxis2 = now.thumbRX;
    m_yaxis2 = now.thumbRY;
    if (m_xaxis == 0.0f && m_yaxis == 0.0f)
    {
        if (now.digitalDown[0])
            m_yaxis = 1.0f;
        else if (now.digitalDown[1])
            m_yaxis = -1.0f;
        if (now.digitalDown[2])
            m_xaxis = -1.0f;
        else if (now.digitalDown[3])
            m_xaxis = 1.0f;
    }
    g_xaxis = m_xaxis;
    g_yaxis = m_yaxis;

    bool centered = m_xaxis == 0.0f && m_yaxis == 0.0f;
    if (centered)
    {
        m_nextRepeatTime = 0;
        m_repeatInterval = REPEAT_DELAY;
    }
    else if (TheseusGetNow() >= m_nextRepeatTime)
    {
        if (m_xaxis != 0.0f)
            MoveEvent(m_xaxis < 0.0f ? 'L' : 'R', m_xaxis < 0.0f ? "OnMoveLeft" : "OnMoveRight");
        if (m_yaxis != 0.0f)
            MoveEvent(m_yaxis < 0.0f ? 'D' : 'U', m_yaxis < 0.0f ? "OnMoveDown" : "OnMoveUp");
        m_nextRepeatTime = TheseusGetNow() + m_repeatInterval;
        m_repeatInterval = REPEAT_INTERVAL;
    }

    for (int i = 0; i < 8; i++)
    {
        if (now.analogDown[i] != m_prevState.analogDown[i])
            ButtonEvent(now.analogDown[i], s_analogDown[i], s_analogUp[i], s_analogKeys[i]);
    }
    for (int i = 0; i < 8; i++)
    {
        if (now.digitalDown[i] != m_prevState.digitalDown[i])
            ButtonEvent(now.digitalDown[i], s_digitalDown[i], s_digitalUp[i], s_digitalKeys[i]);
    }

    if (now.digitalDown[4] && now.analogDown[0] && now.analogDown[1])
        CallFunction("OnReset");
    if (!wasCentered && centered)
        CallFunction("OnMoveCenter");

    m_prevState = now;
}

// CJoystick::CheckSecretKeySequence(key)
// Reconstructed from 5960 retail XBE @ 0x00041790. With no key set (or
// checking off) returns 0. A wrong press restarts the sequence (KS_END if
// one was under way); a right one at the start adds KS_BEGIN, and the last
// one adds KS_END | KS_VERIFIED and restarts.
int CJoystick::CheckSecretKeySequence(char key)
{
    if (!m_enableSecretKey || m_secretKeyLength == 0 || m_secretKey == NULL)
        return 0;

    if (key != m_secretKey[m_secretKeyCurrentIndex])
    {
        int flags = m_secretKeyCurrentIndex != 0 ? KS_END : 0;
        m_secretKeyCurrentIndex = 0;
        return flags;
    }

    int flags = m_secretKeyCurrentIndex == 0 ? KS_BEGIN : 0;
    if (++m_secretKeyCurrentIndex == m_secretKeyLength)
    {
        flags |= KS_END | KS_VERIFIED;
        m_secretKeyCurrentIndex = 0;
    }
    return flags;
}

// CJoystick::ProcessSecretKeySequence(flags)
// Reconstructed from 5960 retail XBE @ 0x00041800: OnKeyVerificationEnter,
// OnKeyVerificationExit, OnKeyVerified, in that order, for the flags set.
void CJoystick::ProcessSecretKeySequence(int flags)
{
    if (flags & KS_BEGIN)
        CallFunction("OnKeyVerificationEnter");
    if (flags & KS_END)
        CallFunction("OnKeyVerificationExit");
    if (flags & KS_VERIFIED)
        CallFunction("OnKeyVerified");
}
